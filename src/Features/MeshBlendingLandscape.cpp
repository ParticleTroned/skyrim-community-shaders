#include "MeshBlendingLandscape.h"

#include "../Globals.h"
#include "../TruePBR/BSLightingShaderMaterialPBRLandscape.h"
#include "../Util.h"
#include "../Utils/Form.h"
#include "../Utils/Game.h"
#include "MeshBlendingLandscapeSnapshot.h"

#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>

namespace
{
	namespace Landscape = CSX::MeshBlendingLandscapePolicy;
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	const RE::BSFixedString& SnapshotName()
	{
		static const RE::BSFixedString name("CSXMeshBlendingLANDSnapshot");
		return name;
	}

	const RE::BSFixedString& PublicationName()
	{
		static const RE::BSFixedString name("CSXMeshBlendingLANDPublication");
		return name;
	}

	std::span<const std::int32_t> ReadMetadata(const RE::NiObjectNET& a_owner, const RE::BSFixedString& a_name)
	{
		const auto* extra = a_owner.GetExtraData(a_name);
		static const REL::Relocation<const RE::NiRTTI*> integerRTTI{ RE::NiIntegersExtraData::Ni_RTTI };
		if (!extra || extra->GetRTTI() != integerRTTI.get())
			return {};
		const auto* integers = static_cast<const RE::NiIntegersExtraData*>(extra);
		if (!integers->value || integers->size > Landscape::MaximumSnapshotBytes / sizeof(std::int32_t))
			return {};
		return { integers->value, integers->size };
	}

	RE::NiPointer<RE::NiIntegersExtraData> CreateMetadata(const RE::BSFixedString& a_name, const std::vector<std::int32_t>& a_words)
	{
		// Use the stock engine type and heaps, checking both allocations before copying.
		auto* raw = RE::calloc<RE::NiIntegersExtraData>(1u);
		if (!raw)
			return {};
		if (!RE::stl::emplace_vtable(raw)) {
			RE::free(raw);
			return {};
		}
		RE::NiPointer<RE::NiIntegersExtraData> result(raw);
		result->name = a_name;
		result->value = RE::NiAlloc<std::int32_t>(a_words.size());
		if (!result->value)
			return {};
		std::memcpy(result->value, a_words.data(), a_words.size() * sizeof(std::int32_t));
		result->size = static_cast<std::uint32_t>(a_words.size());
		return result;
	}

	struct TextureBindings
	{
		std::array<RE::NiSourceTexture*, Landscape::LayerCount> diffuse{};
		Landscape::MaterialLayout layout = Landscape::MaterialLayout::kVanilla;
		std::uint32_t count = 0u;
	};

	bool ReadBindings(const RE::BSShaderMaterial* a_material, TextureBindings& a_bindings)
	{
		if (!a_material)
			return false;
		std::uintptr_t vtable = 0u;
		std::memcpy(&vtable, a_material, sizeof(vtable));
		static const auto vanillaVtable = RE::BSLightingShaderMaterialLandscape::VTABLE[0].address();
		if (vtable == vanillaVtable) {
			const auto* material = static_cast<const RE::BSLightingShaderMaterialLandscape*>(a_material);
			a_bindings.layout = Landscape::MaterialLayout::kVanilla;
			a_bindings.count = material->numLandscapeTextures;
			a_bindings.diffuse[0] = material->diffuseTexture.get();
			for (std::size_t layer = 1u; layer < Landscape::LayerCount; ++layer)
				a_bindings.diffuse[layer] = material->landscapeDiffuseTexture[layer - 1u].get();
		} else if (const auto* material = BSLightingShaderMaterialPBRLandscape::GetRegistered(a_material)) {
			a_bindings.layout = Landscape::MaterialLayout::kPBR;
			a_bindings.count = material->numLandscapeTextures;
			for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer)
				a_bindings.diffuse[layer] = material->landscapeBaseColorTextures[layer].get();
		} else {
			return false;
		}
		return a_bindings.count > 0u && a_bindings.count <= Landscape::LayerCount;
	}

	bool BuildDrawIdentity(RE::BSGeometry* a_geometry, RE::BSShaderProperty* a_property, std::uint32_t a_childIndex,
		Landscape::DrawIdentity& a_identity, TextureBindings& a_bindings)
	{
		if (!a_geometry || !a_property || !a_geometry->parent ||
			a_geometry->GetGeometryRuntimeData().shaderProperty.get() != a_property ||
			a_property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get())
			return false;
		const auto* property = static_cast<const RE::BSLightingShaderProperty*>(a_property);
		const auto& children = a_geometry->parent->GetChildren();
		if (!property->flags.any(ShaderFlag::kMultiTextureLandscape) || a_childIndex >= children.capacity() ||
			children.capacity() > Landscape::MaximumQuadrantChildren || children[static_cast<std::uint16_t>(a_childIndex)].get() != a_geometry ||
			!ReadBindings(property->material, a_bindings))
			return false;
		a_identity.root = reinterpret_cast<std::uintptr_t>(a_geometry->parent);
		a_identity.geometry = reinterpret_cast<std::uintptr_t>(a_geometry);
		a_identity.shaderProperty = reinterpret_cast<std::uintptr_t>(a_property);
		a_identity.material = reinterpret_cast<std::uintptr_t>(property->material);
		a_identity.layout = a_bindings.layout;
		a_identity.textureCount = a_bindings.count;
		a_identity.childSlots = children.capacity();
		a_identity.childIndex = a_childIndex;
		for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer)
			a_identity.textures[layer] = reinterpret_cast<std::uintptr_t>(a_bindings.diffuse[layer]);
		return true;
	}

	std::string DiffusePath(const RE::TESLandTexture& a_texture)
	{
		auto* textureSet = Util::GetSeasonalSwap(a_texture.textureSet);
		const auto* path = textureSet ? textureSet->GetTexturePath(RE::BSTextureSet::Textures::kDiffuse) : nullptr;
		return path ? Landscape::NormalizeTexturePath(path) : std::string{};
	}

	bool CaptureTexture(const RE::TESLandTexture& a_texture, Landscape::CapturedTexture& a_identity)
	{
		a_identity.present = true;
		a_identity.form = Landscape::CanonicalFormKey(Util::GetFormFileKey(&a_texture));
		const auto* editorID = a_texture.GetFormEditorID();
		a_identity.editorID = editorID ? Landscape::NormalizeIdentity(editorID) : std::string{};
		a_identity.diffuse = DiffusePath(a_texture);
		std::array<const RE::BGSMaterialType*, Landscape::MaximumMaterialDepth> parents{};
		for (const auto* material = a_texture.materialType; material; material = material->parentType) {
			if (a_identity.materialCount == parents.size() ||
				std::find(parents.begin(), parents.begin() + a_identity.materialCount, material) != parents.begin() + a_identity.materialCount) {
				a_identity.materialChainValid = false;
				break;
			}
			parents[a_identity.materialCount] = material;
			auto& captured = a_identity.materials[a_identity.materialCount++];
			captured.form = Landscape::CanonicalFormKey(Util::GetFormFileKey(material));
			const auto* materialEditorID = material->GetFormEditorID();
			captured.editorID = materialEditorID ? Landscape::NormalizeIdentity(materialEditorID) : std::string{};
			captured.name = Landscape::NormalizeIdentity(material->materialName.c_str());
		}
		return !a_identity.diffuse.empty();
	}

	void WarnRejectedCapture(std::size_t a_quadrant)
	{
		static std::atomic_uint32_t warnings = 0u;
		if (warnings.fetch_add(1u, std::memory_order_relaxed) < 4u)
			logger::warn("[Mesh Blending] LAND quadrant {} has an unsupported layout or texture identity; leaving it unblended", a_quadrant);
	}
}

MeshBlendingLandscape::MeshBlendingLandscape() :
	policy(Landscape::CompilePolicy({}))
{
	// Runtime snapshots carry a process-local stamp as well as exact current owners.
	session = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ reinterpret_cast<std::uintptr_t>(this);
	if (session == 0u)
		session = 1u;
}

void MeshBlendingLandscape::PublishPolicy(std::shared_ptr<const Policy> a_policy) noexcept
{
	if (a_policy)
		policy.store(std::move(a_policy), std::memory_order_release);
}

std::size_t MeshBlendingLandscape::GetBlendPairCount() const noexcept
{
	return policy.load(std::memory_order_acquire)->blendPairCount;
}

void MeshBlendingLandscape::Capture(RE::TESObjectLAND* a_land, bool a_materialSetupSucceeded, DiscoverySink a_discovery)
{
	if (!a_land || !a_land->loadedData)
		return;
	const std::unique_lock lock(metadataMutex);
	// Revoke every previous quadrant before any allocation or layout validation can fail.
	for (auto* root : a_land->loadedData->mesh) {
		if (root)
			root->RemoveExtraData(PublicationName());
	}
	if (!a_materialSetupSucceeded)
		return;
	for (std::size_t quadrant = 0u; quadrant < 4u; ++quadrant) {
		try {
			if (!CaptureQuadrant(a_land, quadrant, a_discovery))
				WarnRejectedCapture(quadrant);
		} catch (const std::exception& error) {
			logger::warn("[Mesh Blending] Could not capture LAND quadrant {}: {}", quadrant, error.what());
		}
	}
}

bool MeshBlendingLandscape::CaptureQuadrant(RE::TESObjectLAND* a_land, std::size_t a_quadrant, const DiscoverySink& a_discovery)
{
	auto* root = a_land->loadedData->mesh[a_quadrant];
	if (!root)
		return true;
	const auto& children = root->GetChildren();
	if (children.empty() || children.capacity() > Landscape::MaximumQuadrantChildren)
		return false;
	RE::BSGeometry* geometry = nullptr;
	std::uint32_t childIndex = 0u;
	for (std::uint16_t child = 0u; child < children.capacity(); ++child) {
		auto* candidate = children[child] ? children[child]->AsGeometry() : nullptr;
		auto* property = candidate ? candidate->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
		if (!property || !property->flags.any(ShaderFlag::kMultiTextureLandscape))
			continue;
		if (geometry)
			return false;
		geometry = candidate;
		childIndex = child;
	}
	if (!geometry || geometry->parent != root)
		return false;
	Landscape::SnapshotHeader header{ session, nextPublication++, {} };
	TextureBindings bindings;
	if (!BuildDrawIdentity(geometry, geometry->GetGeometryRuntimeData().shaderProperty.get(), childIndex, header.draw, bindings))
		return false;
	std::array<Landscape::CapturedTexture, Landscape::LayerCount> textures{};
	for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer) {
		auto* texture = layer == 0u ? a_land->loadedData->defQuadTextures[a_quadrant] : a_land->loadedData->quadTextures[a_quadrant][layer - 1u];
		if ((layer == 0u && !texture) || (texture && texture->GetFormID() == 0u && bindings.layout == Landscape::MaterialLayout::kVanilla))
			texture = Util::GetDefaultLandTexture();
		if (!texture)
			continue;
		if (layer >= bindings.count || !bindings.diffuse[layer] || !CaptureTexture(*texture, textures[layer]) ||
			textures[layer].diffuse != Landscape::NormalizeTexturePath(bindings.diffuse[layer]->name.c_str()))
			return false;
	}
	const auto words = Landscape::EncodeSnapshot(header, textures);
	if (!words)
		return false;
	auto snapshot = CreateMetadata(SnapshotName(), *words);
	auto publication = CreateMetadata(PublicationName(), Landscape::EncodePublication(header));
	if (!snapshot || !publication)
		return false;
	if (a_discovery.capture) {
		const auto currentPolicy = policy.load(std::memory_order_acquire);
		for (const auto& texture : textures) {
			if (texture.present)
				a_discovery.capture(a_discovery.context, texture, Landscape::ClassifyTexture(texture, *currentPolicy));
		}
	}
	geometry->RemoveExtraData(SnapshotName());
	if (!geometry->AddExtraData(snapshot.get()))
		return false;
	return root->AddExtraData(publication.get());
}

bool MeshBlendingLandscape::TryGetClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_classes)
{
	a_classes = 0u;
	if (!a_pass || !a_pass->geometry)
		return false;
	const std::shared_lock lock(metadataMutex);
	const auto words = ReadMetadata(*a_pass->geometry, SnapshotName());
	Landscape::SnapshotHeader header;
	if (!Landscape::ReadSnapshotHeader(words, header))
		return false;
	Landscape::DrawIdentity current;
	TextureBindings bindings;
	if (!BuildDrawIdentity(a_pass->geometry, a_pass->shaderProperty, header.draw.childIndex, current, bindings) ||
		!Landscape::MatchesPublication(ReadMetadata(*a_pass->geometry->parent, PublicationName()), header, current, session))
		return false;
	const auto currentPolicy = policy.load(std::memory_order_acquire);
	static thread_local Landscape::ClassificationCache<> cache;
	return cache.Resolve(words, header, *currentPolicy, a_classes);
}
