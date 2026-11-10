#include "Interactors.h"

#include "Globals.h"
#include "Utils/ActorUtils.h"
#include "Utils/Finite.h"
#include "Utils/SkinnedMesh.h"

#ifdef TRACY_SUPPORT
#	include <Tracy/Tracy.hpp>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string_view>

namespace FurInteractors
{
	namespace
	{
		constexpr float MaxDentRadius = 4.0f;
		constexpr float MinDentPress = 0.15f;
		constexpr float MinDentDepth = 0.04f;
		constexpr float DentRecoverBase = 0.4f;
		constexpr float DentRecoverPerDepth = 2.0f;
		constexpr float MaxFrameSeconds = 0.1f;
		constexpr size_t MaxPressSamples = 384;
		constexpr uint32_t MemoryIdleFrames = 240;
	}

	void Memory::Advance()
	{
		const float deltaTime = globals::game::deltaTime ? *globals::game::deltaTime : 0.0f;
		frameSeconds = Util::ClampFinite(deltaTime, 0.0f, MaxFrameSeconds, 0.0f);
		DecayDents();
		++frame;
	}

	void Memory::Clear()
	{
		memory.clear();
		frameSeconds = 0.0f;
	}

	size_t Memory::GetDentCount() const
	{
		size_t count = 0;
		for (const auto& [geometry, mesh] : memory)
			count += mesh.dents.size();
		return count;
	}

	void Memory::DecayDents()
	{
		for (auto it = memory.begin(); it != memory.end();) {
			auto& dents = it->second.dents;
			for (Dent& dent : dents) {
				if (dent.refreshedFrame != frame)
					dent.depth *= std::exp(-frameSeconds / std::max(dent.recoverSeconds, 0.05f));
			}
			std::erase_if(dents, [](const Dent& a_dent) { return a_dent.depth < MinDentDepth; });
			if (dents.empty() && frame - it->second.usedFrame > MemoryIdleFrames)
				it = memory.erase(it);
			else
				++it;
		}
	}

	void Memory::BuildPressSamples(const RE::BSGeometry* a_geometry, MeshMemory& a_memory) const
	{
		a_memory.samples.clear();
		a_memory.bones.clear();
		std::vector<Util::SkinnedVertex> vertices;
		if (!Util::ReadSkinnedVertices(const_cast<RE::BSGeometry*>(a_geometry), vertices, MaxPressSamples))
			return;

		const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		std::vector<int> boneSides(skin->numMatrices, -1);
		for (uint32_t bone = 0; bone < skin->numMatrices; ++bone) {
			a_memory.bones.emplace_back(skin->bones[bone]);
			const std::string_view name = skin->bones[bone] && skin->bones[bone]->name.c_str() ? skin->bones[bone]->name.c_str() : "";
			boneSides[bone] = Util::BodyCapsules::HandBoneSide(name);
		}

		for (const Util::SkinnedVertex& vertex : vertices) {
			const auto dominant = Util::DominantBone(vertex);
			if (!dominant || !a_memory.bones[*dominant])
				continue;
			const uint16_t bone = *dominant;
			const auto local = skin->skinData->GetBoneDataSkinToBone(bone) * vertex.position;
			if (std::isfinite(local.SqrLength()))
				a_memory.samples.push_back({ bone, boneSides[bone], local });
		}
	}

	Memory::MeshMemory& Memory::Prepare(const RE::BSGeometry* a_geometry, const void* a_skin)
	{
		if (!memory.contains(a_geometry) && memory.size() >= 512) {
			const auto oldest = std::ranges::min_element(memory, {}, [](const auto& entry) { return entry.second.usedFrame; });
			memory.erase(oldest);
		}
		MeshMemory& mesh = memory[a_geometry];
		mesh.usedFrame = frame;
		const auto* skin = static_cast<const RE::NiSkinInstance*>(a_skin);
		if (mesh.skin != a_skin || mesh.partition != skin->skinPartition.get() || mesh.root != skin->rootParent ||
			(!mesh.bones.empty() && mesh.bones.size() != skin->numMatrices) ||
			mesh.center.GetDistance(a_geometry->worldBound.center) > 300.0f ||
			(!mesh.bones.empty() && mesh.bones.front().get() != skin->bones[0])) {
			mesh.skin = a_skin;
			mesh.partition = skin->skinPartition.get();
			mesh.root = skin->rootParent;
			mesh.dents.clear();
			mesh.measuredFrame = UINT32_MAX;
			BuildPressSamples(a_geometry, mesh);
		}
		mesh.center = a_geometry->worldBound.center;
		return mesh;
	}

	size_t Memory::Nearest(const RE::BSGeometry* a_geometry, RE::FormID a_wearer, std::span<const Capsule> a_candidates, std::span<Capsule> a_out)
	{
#ifdef TRACY_SUPPORT
		ZoneScopedN("FurShells::Nearest");
#endif
		const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		if (a_out.empty())
			return 0;
		a_candidates = a_candidates.first(std::min(a_candidates.size(), Util::BodyCapsules::MaxSelectedCapsules));
		if (a_candidates.size() <= a_out.size() || !Util::HasUsableSkin(skin)) {
			const size_t keep = std::min(a_out.size(), a_candidates.size());
			std::copy_n(a_candidates.begin(), keep, a_out.begin());
			return keep;
		}

		MeshMemory& mesh = Prepare(a_geometry, skin);
		const size_t keep = std::min(a_out.size(), a_candidates.size());
		if (mesh.samples.empty()) {
			std::copy_n(a_candidates.begin(), keep, a_out.begin());
			return keep;
		}

		std::array<float3, MaxPressSamples> positions{};
		for (size_t index = 0; index < mesh.samples.size(); ++index) {
			const auto& sample = mesh.samples[index];
			positions[index] = Util::ToFloat3(mesh.bones[sample.bone]->world * sample.local);
		}

		std::array<std::pair<float, size_t>, Util::BodyCapsules::MaxSelectedCapsules> storage;
		auto gaps = std::span(storage).first(a_candidates.size());
		for (size_t index = 0; index < a_candidates.size(); ++index) {
			const Capsule& capsule = a_candidates[index];
			float nearest = FLT_MAX;
			for (size_t sample = 0; sample < mesh.samples.size(); ++sample) {
				if (a_wearer != 0 && capsule.owner == a_wearer && capsule.side >= 0 && mesh.samples[sample].side == capsule.side)
					continue;
				nearest = std::min(nearest, (positions[sample] - capsule.ClosestPointOnAxis(positions[sample])).LengthSquared());
			}
			gaps[index] = { std::sqrt(nearest) - capsule.radius, index };
		}
		std::ranges::partial_sort(gaps, gaps.begin() + static_cast<std::ptrdiff_t>(keep));
		for (size_t index = 0; index < keep; ++index)
			a_out[index] = a_candidates[gaps[index].second];
		return keep;
	}

	size_t Memory::Remember(const RE::BSGeometry* a_geometry, RE::FormID a_wearer, float a_furLength, size_t a_count, std::array<Capsule, MaxCapsulesPerPass>& a_capsules)
	{
#ifdef TRACY_SUPPORT
		ZoneScopedN("FurShells::Remember");
#endif
		const auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		a_count = std::min(a_count, a_capsules.size());
		if (!Util::HasUsableSkin(skin) || !std::isfinite(a_furLength) || a_furLength <= 0.0f)
			return a_count;
		const auto live = std::span(a_capsules).first(a_count);
		const bool canDent = std::ranges::any_of(live, [](const Capsule& capsule) {
			return capsule.appendage && capsule.radius <= MaxDentRadius;
		});
		const auto existing = memory.find(a_geometry);
		if (!canDent && (existing == memory.end() || existing->second.dents.empty()))
			return a_count;

		MeshMemory& mesh = Prepare(a_geometry, skin);

		if (mesh.measuredFrame != frame && frameSeconds > 0.0f) {
			mesh.measuredFrame = frame;
			for (size_t index = 0; index < a_count && !mesh.samples.empty(); ++index) {
				const Capsule& capsule = a_capsules[index];
				if (!capsule.appendage || capsule.radius > MaxDentRadius)
					continue;

				float nearest = FLT_MAX;
				const PressSample* nearestSample = nullptr;
				float3 pressPoint;
				for (const PressSample& sample : mesh.samples) {
					if (a_wearer != 0 && capsule.owner == a_wearer && capsule.side >= 0 && sample.side == capsule.side)
						continue;
					const float3 position = Util::ToFloat3(mesh.bones[sample.bone]->world * sample.local);
					const float3 closest = capsule.ClosestPointOnAxis(position);
					const float gap = (position - closest).Length() - capsule.radius;
					if (gap < nearest) {
						nearest = gap;
						nearestSample = &sample;
						pressPoint = closest;
					}
				}

				const float press = std::clamp((a_furLength - nearest) / a_furLength, 0.0f, 1.0f);
				if (!nearestSample || press < MinDentPress)
					continue;

				const RE::NiAVObject* bone = mesh.bones[nearestSample->bone].get();
				const float scale = std::abs(bone->world.scale);
				if (!std::isfinite(scale) || scale < 1e-4f)
					continue;
				const RE::NiPoint3 local = bone->world.Invert() * RE::NiPoint3{ pressPoint.x, pressPoint.y, pressPoint.z };
				if (!std::isfinite(local.SqrLength()))
					continue;
				const float localRadius = capsule.radius / scale;
				const float recover = DentRecoverBase + DentRecoverPerDepth * press;
				Dent* dent = nullptr;
				for (Dent& existingDent : mesh.dents) {
					if (existingDent.bone.get() == bone && existingDent.local.GetDistance(local) < std::max(localRadius, 0.5f * existingDent.radius))
						dent = &existingDent;
				}
				if (!dent && mesh.dents.size() < MaxDentsPerMesh)
					dent = &mesh.dents.emplace_back();
				if (!dent) {
					dent = &*std::ranges::min_element(mesh.dents, [](const Dent& a, const Dent& b) { return a.depth < b.depth; });
					if (dent->depth >= press)
						continue;
					*dent = {};
				}
				dent->bone.reset(const_cast<RE::NiAVObject*>(bone));
				dent->local = local;
				dent->radius = localRadius;
				dent->depth = press;
				dent->recoverSeconds = std::max(dent->recoverSeconds, recover);
				dent->refreshedFrame = frame;
			}
		}

		size_t count = a_count;
		for (const Dent& dent : mesh.dents) {
			if (dent.refreshedFrame == frame || count >= MaxCapsulesPerPass)
				continue;
			const RE::NiPoint3 position = dent.bone->world * dent.local;
			const float radius = dent.radius * std::abs(dent.bone->world.scale);
			if (!std::isfinite(position.SqrLength()) || !std::isfinite(radius) || radius <= 0.0f)
				continue;
			Capsule ghost;
			ghost.pointA = ghost.pointB = Util::ToFloat3(position);
			ghost.radius = radius;
			ghost.strength = std::clamp(dent.depth, 0.0f, 1.0f);
			ghost.remembered = true;
			a_capsules[count++] = ghost;
		}
		return count;
	}
}
