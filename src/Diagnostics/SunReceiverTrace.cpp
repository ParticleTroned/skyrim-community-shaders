#include "Diagnostics/SunReceiverTrace.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Compatibility.h"
#	include "Diagnostics/BoundedThreadTrace.h"
#	include "EngineFixes/SunReceiverPolicy.h"
#	include "Diagnostics/SunReceiverTraceRequest.h"
#	include "Diagnostics/SunReceiverTraceProbe.h"
#	include "Globals.h"
#	include "State.h"
#	include "Utils/VirtualFunctionHook.h"
#	include "Utils/Game.h"
#	include <RE/B/BSShadowDirectionalLight.h>
#	include <RE/B/BSCullingProcess.h>
#	include <RE/B/BSShaderPropertyLightData.h>
#	include <RE/B/BSVisit.h>
#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <charconv>
#	include <cstring>
#	include <stdexcept>
#	include <vector>
#	include <xbyak/xbyak.h>

namespace Diagnostics::SunReceiverTrace
{
	namespace
	{
		using json = nlohmann::json;
		using namespace Probe;
		namespace Policy = SunShadowReceiver::Policy;
		constexpr std::size_t ThreadCount = 16, EventCapacity = 4096, AncestorCapacity = 32;
		enum class Kind : std::uint32_t
		{
			mask,
			helper,
			cull,
			boundTest,
			append,
			passes
		};
		struct Event
		{
			std::int64_t qpc{}, repairQpc{};
			std::uint64_t call{}, object{}, property{}, lightData{}, evaluatedLightData{}, context{}, camera{}, sun{}, flags{}, primaryAccumulator{};
			std::uint32_t frame{}, graphicsFrame{}, kind{}, after{}, site{}, mask{}, marker{}, operand{}, mode{}, sunMask{};
			std::uint32_t activePlanes{}, customActive{}, cullFlags{}, objectFlags{}, result{}, passCount{}, passOverflow{};
			std::uint32_t maskBefore{}, maskAfterRepair{}, evaluatedMask{}, referenceFormId{};
			Policy::Reason decision = Policy::Reason::unavailable;
			bool receiverEvaluated{}, repairApplied{}, rangeValid{};
			float shadowDistance{};
			double distanceSquared{}, rangeSquared{};
			std::array<char, 128> geometryName{};
			std::array<float, 4> bound{};
			std::array<float, 3> cameraPosition{};
			std::array<float, 9> cameraRotation{};
			std::array<float, 7> frustum{};
			std::array<float, 24> planes{}, customPlanes{};
			std::array<std::uint32_t, 16> passes{};
		};
		using Journal = BoundedThreadTrace<Event, ThreadCount, EventCapacity>;
		Journal journal;
		std::atomic<RE::BSShaderProperty*> selectedProperty{};
		std::atomic<RE::BSGeometry*> selectedGeometry{};
		std::uintptr_t initialLightData{};
		RE::NiAVObject* initialReferenceRoot{};
		std::array<std::atomic<RE::NiAVObject*>, AncestorCapacity> watched{};
		std::vector<RE::NiPointer<RE::NiAVObject>> retainedNodes;
		RE::NiPointer<RE::BSShaderProperty> retainedProperty;
		RE::TESObjectREFRPtr retainedReference;
		std::atomic_bool identityChanged{}, expired{};
		std::uint32_t startFrame{}, maximumFrames{};
		std::uint64_t deadlineMilliseconds{}, generation{};
		std::string traceId, installationError;
		json selection, hooks = json::array();
		bool installed{}, attempted{}, upstreamInstalled{}, allGeometry{}, repairEnabled{};
		std::string upstreamError;
		std::atomic_uint64_t appliedRepairs{}, changedBeforeRepair{};
		std::array<std::atomic_uint64_t, static_cast<std::size_t>(Policy::Reason::count)> decisions{};
		static_assert(Policy::ReceiveShadows == static_cast<std::uint64_t>(RE::BSShaderProperty::EShaderPropertyFlag::kReceiveShadows));
		thread_local std::uint64_t nextCall{};
		thread_local std::array<std::uint64_t, 2> maskCalls{}, maskGenerations{};
		using PassFunction = SunShadowReceiver::PassFunction;
		using CullFunction = void (*)(RE::BSCullingProcess*, RE::NiAVObject*, std::int32_t);
		using AppendFunction = void (*)(RE::BSCullingProcess*, RE::BSGeometry*, std::int32_t);
		using BoundFunction = bool (*)(RE::BSCullingProcess*, const RE::NiBound*);
		using HelperFunction = void (*)(RE::BSShaderProperty*, std::uint32_t);
		CullFunction originalCull{};
		AppendFunction originalAppend{};
		BoundFunction originalBound{};
		HelperFunction originalHelper{};
		SKSE::Trampoline probeTrampoline{ "SunReceiverTrace" };

		template <class T, class U>
		T Read(const U* base, std::size_t offset) noexcept
		{
			T value{};
			std::memcpy(&value, reinterpret_cast<const std::byte*>(base) + offset, sizeof(T));
			return value;
		}
		std::uint32_t Frame() noexcept
		{
			return globals::state ? globals::state->frameCountAtomic.load(std::memory_order_relaxed) : 0;
		}
		bool Running(const Journal::Lease& lease) noexcept
		{
			// Keep admission, limits and native state bound to one capture generation.
			if (!lease || !journal.IsOpen())
				return false;
			if (Frame() - startFrame >= maximumFrames || GetTickCount64() >= deadlineMilliseconds) {
				expired.store(true, std::memory_order_relaxed);
				journal.Close();
				return false;
			}
			return true;
		}
		bool Watched(const RE::NiAVObject* object) noexcept
		{
			if (!journal.IsOpen())
				return false;
			for (const auto& node : watched)
				if (node.load(std::memory_order_relaxed) == object)
					return object != nullptr;
			return false;
		}
		RE::NiAVObject* BoundOwner(const RE::NiBound* bound) noexcept
		{
			for (const auto& slot : watched) {
				auto* node = slot.load(std::memory_order_relaxed);
				if (node && reinterpret_cast<const std::byte*>(node) + 0xE4 == reinterpret_cast<const std::byte*>(bound))
					return node;
			}
			return nullptr;
		}
		Event Base(Kind kind, bool after, std::uint64_t call, RE::NiAVObject* object = nullptr,
			RE::BSShaderProperty* observedProperty = nullptr) noexcept
		{
			Event event{};
			LARGE_INTEGER now{};
			QueryPerformanceCounter(&now);
			event.qpc = now.QuadPart;
			event.frame = Frame();
			event.graphicsFrame = globals::game::graphicsState ? globals::game::graphicsState->GetFrameCount() : 0;
			event.kind = static_cast<std::uint32_t>(kind);
			event.after = after;
			event.call = call;
			event.object = reinterpret_cast<std::uintptr_t>(object);
			auto* property = observedProperty ? observedProperty : selectedProperty.load(std::memory_order_relaxed);
			event.property = reinterpret_cast<std::uintptr_t>(property);
			if (property) {
				event.flags = property->flags.underlying();
				auto* lightData = property->lightData;
				event.lightData = reinterpret_cast<std::uintptr_t>(lightData);
				if (!allGeometry && event.lightData != initialLightData)
					identityChanged.store(true, std::memory_order_relaxed);
				if (lightData)
					event.mask = Policy::ReadMask(lightData->activeLightMask);
			}
			if (object) {
				event.bound = Read<std::array<float, 4>>(object, 0xE4);
				event.objectFlags = object->GetFlags().underlying();
			}
			return event;
		}
		void Emit(const Event& event) noexcept { journal.Append(GetCurrentThreadId(), event); }
		std::array<float, 7> Frustum(const RE::NiFrustum& value) noexcept
		{
			return { value.fLeft, value.fRight, value.fTop, value.fBottom,
				value.fNear, value.fFar, value.bOrtho ? 1.0f : 0.0f };
		}
		void CameraState(Event& event, const RE::NiCamera* camera) noexcept
		{
			event.camera = reinterpret_cast<std::uintptr_t>(camera);
			if (!camera)
				return;
			const auto& position = camera->world.translate;
			event.cameraPosition = { position.x, position.y, position.z };
			event.cameraRotation = Read<std::array<float, 9>>(&camera->world.rotate, 0);
			event.frustum = Frustum(camera->GetRuntimeData2().viewFrustum);
		}
		void CullState(Event& event, RE::BSCullingProcess* culler) noexcept
		{
			event.context = reinterpret_cast<std::uintptr_t>(culler);
			CameraState(event, culler->camera);
			event.frustum = Frustum(culler->frustum);
			event.planes = Read<std::array<float, 24>>(culler, 0x3C);
			event.customPlanes = Read<std::array<float, 24>>(culler, 0xAC);
			event.activePlanes = Read<std::uint32_t>(culler, 0x9C);
			event.customActive = Read<std::uint32_t>(culler, 0x10C);
			event.cullFlags = Read<std::uint32_t>(culler, 0x11C);
			event.mode = Read<std::uint32_t>(culler, 0x30198);
			event.marker = Read<std::uint32_t>(culler, 0x301D4);
		}
		void MaskProbe(RE::BSShaderProperty* property, void* accumulator, std::uint32_t site, std::uint32_t after) noexcept
		{
			if (property != selectedProperty.load(std::memory_order_relaxed))
				return;
			Journal::Lease lease(journal);
			if (!Running(lease) || property != selectedProperty.load(std::memory_order_relaxed))
				return;
			const auto slot = site == 0x13086B7 ? 0u : 1u;
			if (!after) {
				maskCalls[slot] = ++nextCall;
				maskGenerations[slot] = generation;
			}
			const auto call = maskGenerations[slot] == generation ? maskCalls[slot] : 0;
			auto event = Base(Kind::mask, after != 0, call, selectedGeometry.load(std::memory_order_relaxed));
			if (after)
				maskCalls[slot] = 0;
			event.site = site;
			event.context = reinterpret_cast<std::uintptr_t>(accumulator);
			event.marker = Read<std::uint32_t>(accumulator, 0x188);
			event.operand = Read<std::uint32_t>(accumulator, 0x18C);
			event.mode = Read<std::uint32_t>(accumulator, 0x178);
			Emit(event);
		}
		void Helper(RE::BSShaderProperty* property, std::uint32_t index)
		{
			if (property != selectedProperty.load(std::memory_order_relaxed)) {
				originalHelper(property, index);
				return;
			}
			Journal::Lease lease(journal);
			if (!Running(lease) || property != selectedProperty.load(std::memory_order_relaxed)) {
				originalHelper(property, index);
				return;
			}
			const auto call = ++nextCall;
			auto before = Base(Kind::helper, false, call);
			before.site = 0x12C9E00;
			before.marker = index;
			before.operand = index == 0xFFFF ? 0 : 1u << ((index - 1) & 31);
			Emit(before);
			originalHelper(property, index);
			auto after = Base(Kind::helper, true, call);
			after.site = before.site;
			after.marker = index;
			after.operand = before.operand;
			Emit(after);
		}
		void Cull(RE::BSCullingProcess* culler, RE::NiAVObject* object, std::int32_t group)
		{
			if (!Watched(object)) {
				originalCull(culler, object, group);
				return;
			}
			Journal::Lease lease(journal);
			if (!Running(lease) || !Watched(object)) {
				originalCull(culler, object, group);
				return;
			}
			const auto call = ++nextCall;
			auto before = Base(Kind::cull, false, call, object);
			CullState(before, culler);
			Emit(before);
			originalCull(culler, object, group);
			auto after = Base(Kind::cull, true, call, object);
			CullState(after, culler);
			Emit(after);
		}
		bool Bound(RE::BSCullingProcess* culler, const RE::NiBound* bound)
		{
			if (!journal.IsOpen())
				return originalBound(culler, bound);
			auto* object = BoundOwner(bound);
			if (!object)
				return originalBound(culler, bound);
			Journal::Lease lease(journal);
			if (!Running(lease) || BoundOwner(bound) != object)
				return originalBound(culler, bound);
			const auto call = ++nextCall;
			auto before = Base(Kind::boundTest, false, call, object);
			CullState(before, culler);
			Emit(before);
			const bool result = originalBound(culler, bound);
			auto after = Base(Kind::boundTest, true, call, object);
			CullState(after, culler);
			after.result = result;
			Emit(after);
			return result;
		}
		void Append(RE::BSCullingProcess* culler, RE::BSGeometry* geometry, std::int32_t group)
		{
			if (geometry == selectedGeometry.load(std::memory_order_relaxed)) {
				Journal::Lease lease(journal);
				if (Running(lease) && geometry == selectedGeometry.load(std::memory_order_relaxed)) {
					auto event = Base(Kind::append, false, ++nextCall, geometry);
					CullState(event, culler);
					Emit(event);
				}
			}
			originalAppend(culler, geometry, group);
		}
		Policy::Input ReceiverInput(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
			std::uint32_t mode, RE::BSShaderAccumulator* accumulator, std::uintptr_t& sun) noexcept
		{
			auto input = SunShadowReceiver::ReadInputs(property, geometry, mode, accumulator, sun);
			input.identityStable = allGeometry || (!identityChanged.load(std::memory_order_relaxed) &&
													  geometry == selectedGeometry.load(std::memory_order_relaxed) &&
													  property == selectedProperty.load(std::memory_order_relaxed) && input.lightData == initialLightData);
			return input;
		}

		void Identify(Event& event, RE::BSGeometry* geometry) noexcept
		{
			if (!geometry)
				return;
			if (const auto* name = geometry->name.c_str())
				for (std::size_t i = 0; i + 1 < event.geometryName.size() && name[i]; ++i)
					event.geometryName[i] = name[i];
			const RE::NiAVObject* node = geometry;
			for (std::size_t i = 0; node && i < AncestorCapacity; ++i, node = node->parent)
				if (const auto* reference = Util::GetOwnUserData(node)) {
					event.referenceFormId = reference->GetFormID();
					break;
				}
		}
		RE::BSShaderProperty::RenderPassArray* TracePasses(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry,
			std::uint32_t mode, RE::BSShaderAccumulator* accumulator, PassFunction originalPasses)
		{
			Journal::Lease lease(journal);
			if (!Running(lease))
				return SunShadowReceiver::ProcessPasses(property, geometry, mode, accumulator, originalPasses);
			if (!allGeometry && geometry != selectedGeometry.load(std::memory_order_relaxed))
				return originalPasses(property, geometry, mode, accumulator);
			std::uintptr_t sun{};
			const auto input = ReceiverInput(property, geometry, mode, accumulator, sun);
			const auto decision = Policy::Evaluate(input);
			decisions[static_cast<std::size_t>(decision.reason)].fetch_add(1, std::memory_order_relaxed);
			if (decision.reason == Policy::Reason::identityChanged)
				identityChanged.store(true, std::memory_order_relaxed);
			if (allGeometry && decision.reason != Policy::Reason::candidate)
				return originalPasses(property, geometry, mode, accumulator);
			const auto call = ++nextCall;
			auto before = Base(Kind::passes, false, call, geometry, property);
			before.context = reinterpret_cast<std::uintptr_t>(accumulator);
			before.mode = mode;
			CameraState(before, accumulator ? accumulator->camera : nullptr);
			before.primaryAccumulator = reinterpret_cast<std::uintptr_t>(SunShadowReceiver::PrimaryAccumulator());
			before.sun = sun;
			before.sunMask = input.sunMask;
			before.receiverEvaluated = true;
			before.decision = decision.reason;
			before.shadowDistance = input.shadowDistance;
			before.rangeValid = decision.rangeValid;
			before.distanceSquared = decision.distanceSquared;
			before.rangeSquared = decision.rangeSquared;
			before.evaluatedLightData = input.lightData;
			before.evaluatedMask = input.activeMask;
			before.maskBefore = before.maskAfterRepair = before.mask;
			Identify(before, geometry);
			Emit(before);
			if (decision.reason == Policy::Reason::candidate && repairEnabled) {
				before.repairApplied = SunShadowReceiver::Repair(property, geometry, input, decision);
				before.maskAfterRepair = property->lightData ? Policy::ReadMask(property->lightData->activeLightMask) : 0;
				if (before.repairApplied) {
					LARGE_INTEGER now{};
					QueryPerformanceCounter(&now);
					before.repairQpc = now.QuadPart;
					appliedRepairs.fetch_add(1, std::memory_order_relaxed);
				} else
					changedBeforeRepair.fetch_add(1, std::memory_order_relaxed);
			}
			auto* result = originalPasses(property, geometry, mode, accumulator);
			auto after = before;
			after.after = true;
			LARGE_INTEGER now{};
			QueryPerformanceCounter(&now);
			after.qpc = now.QuadPart;
			const auto* lightData = property ? property->lightData : nullptr;
			after.lightData = reinterpret_cast<std::uintptr_t>(lightData);
			after.mask = lightData ? Policy::ReadMask(lightData->activeLightMask) : 0;
			if (!allGeometry && after.lightData != initialLightData)
				identityChanged.store(true, std::memory_order_relaxed);
			if (result) {
				auto* pass = result->head;
				for (; pass && after.passCount < after.passes.size(); pass = pass->next)
					after.passes[after.passCount++] = pass->passEnum;
				after.passOverflow = pass != nullptr;
			}
			Emit(after);
			return result;
		}

		bool Matches(std::uintptr_t address, const std::vector<std::uint8_t>& expected)
		{
			std::vector<std::uint8_t> actual(expected.size());
			SIZE_T read{};
			return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), actual.data(), actual.size(), &read) &&
			       read == actual.size() && actual == expected;
		}
		void InstallBlock(std::uint32_t site, bool deferred, const std::vector<std::uint8_t>& bytes)
		{
			MaskBridge bridge(deferred, site, bytes, reinterpret_cast<std::uintptr_t>(&MaskProbe), REL::Module::get().base() + site + bytes.size());
			auto* memory = probeTrampoline.allocate(bridge.getSize());
			std::memcpy(memory, bridge.getCode(), bridge.getSize());
			probeTrampoline.write_branch<6>(REL::Module::get().base() + site, reinterpret_cast<std::uintptr_t>(memory));
			FlushInstructionCache(GetCurrentProcess(), memory, bridge.getSize());
			hooks.push_back({ { "kind", "mask_block" }, { "rva", site }, { "verifiedBytes", bytes.size() } });
		}
		template <class F>
		void Virtual(std::uintptr_t table, std::size_t slot, F& original, F hook)
		{
			const auto target = Read<std::uintptr_t>(reinterpret_cast<void*>(table), slot * 8);
			const auto result = Util::InstallNativeVirtualFunctionHook(table, slot, reinterpret_cast<void**>(&original), reinterpret_cast<void*>(hook));
			hooks.push_back({ { "kind", "virtual" }, { "slot", slot }, { "previousTarget", target }, { "error", result.error }, { "detourError", result.detourError }, { "method", static_cast<std::uint32_t>(result.method) } });
			if (result.error != NO_ERROR)
				throw std::runtime_error("Native virtual observer installation failed");
		}

		void ResetCounters() noexcept
		{
			identityChanged = expired = false;
			appliedRepairs = changedBeforeRepair = 0;
			for (auto& count : decisions)
				count = 0;
		}

		json Status()
		{
			if (retainedReference && retainedReference->Get3D() != initialReferenceRoot)
				identityChanged.store(true, std::memory_order_relaxed);
			{
				Journal::Lease lease(journal);
				(void)Running(lease);
			}
			json lanes = json::array();
			json counts = json::object();
			for (std::size_t i = 0; i < decisions.size(); ++i)
				counts[std::string(Policy::ReasonNames[i])] = decisions[i].load();
			const auto repaired = appliedRepairs.load();
			std::size_t dropped = journal.unassigned.load(), count = 0;
			if (journal.lanes)
				for (std::size_t i = 0; i < ThreadCount; ++i) {
					const auto& lane = journal.lanes[i];
					count += lane.count.load();
					dropped += lane.dropped.load();
					lanes.push_back({ { "lane", i }, { "thread", lane.owner.load() }, { "count", lane.count.load() }, { "dropped", lane.dropped.load() } });
				}
			return {
				{ "schemaRevision", 4 }, { "available", installed }, { "installationError", installationError }, { "hooks", hooks },
				{ "traceId", traceId }, { "generation", generation }, { "active", journal.IsOpen() }, { "quiescent", journal.Quiescent() },
				{ "startFrame", startFrame }, { "maximumFrames", maximumFrames }, { "expired", expired.load() },
				{ "identityChanged", identityChanged.load() }, { "selection", selection }, { "eventCount", count }, { "dropped", dropped }, { "lanes", lanes },
				{ "scope", allGeometry ? "all" : "selected" }, { "mode", repairEnabled ? "repair" : "observe" },
				{ "observationOnly", journal.IsOpen() && !repairEnabled }, { "ordinaryFix", SunShadowReceiver::Control(json::object()) }, { "nativeWritesChanged", repaired != 0 },
				{ "appliedRepairs", repaired }, { "decisions", counts }, { "changedBeforeRepair", changedBeforeRepair.load() },
				{ "countsAreUniqueObjects", false }, { "recordsMayBeDropped", true },
				{ "allWriterCoverageVerified", false }, { "upstreamObserversAvailable", upstreamInstalled }, { "upstreamObserverError", upstreamError },
				{ "coverage", upstreamInstalled ? "SE/AE/VR lighting GetRenderPasses; selected VR 1.4.15 also records two accumulator blocks, property helper and BSCullingProcess traversal/append/bound. Inline plane loops and other cullers are not intercepted." : "Only lighting GetRenderPasses coverage is guaranteed; upstream hooks may be absent or partial. Consult hooks and upstreamObserverError." },
				{ "repairContract", "A bounded trace replaces ordinary repair globally; configured ordinary repair resumes after capture. Current primary-camera state; no prior-frame cache. Same receiver/exterior/LOD/range gates; invalid bounds fail closed. VR selects the first sun entry; SE/AE collect matching entries. Counters count calls, not unique meshes. Overflow does not stop repairs; missing detail cannot establish absence." },
				{ "rootCauseVerdict", "not_evaluated" }, { "boundaryPairsMayBeIncomplete", true }, { "frameDefinition", "CSX render frame; graphicsFrame is the native rendered-frame counter; neither is implicitly the DevBench update counter" }, { "eyeAttribution", "unavailable" }
			};
		}
		std::uint32_t FormId(const json& args)
		{
			auto text = args.at("formId").get<std::string>();
			if (text.starts_with("0x") || text.starts_with("0X"))
				text.erase(0, 2);
			std::uint32_t value{};
			const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
			if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
				throw std::invalid_argument("formId must be a nonzero hexadecimal reference ID");
			return value;
		}
		std::vector<RE::BSGeometry*> Geometries(RE::TESObjectREFR& reference)
		{
			std::vector<RE::BSGeometry*> values;
			auto* scene = reference.Get3D();
			if (!scene)
				throw std::invalid_argument("Reference has no loaded 3D");
			RE::BSVisit::TraverseScenegraphGeometries(scene, [&](RE::BSGeometry* geometry) {
				if (values.size() == 256)
					return RE::BSVisit::BSVisitControl::kStop;
				auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
				if (property && property->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting)
					values.push_back(geometry);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return values;
		}
		json Describe(RE::BSGeometry* geometry, std::size_t index)
		{
			auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
			return { { "geometryIndex", index }, { "name", (geometry->name.c_str() ? geometry->name.c_str() : "") }, { "geometry", reinterpret_cast<std::uintptr_t>(geometry) },
				{ "property", reinterpret_cast<std::uintptr_t>(property) }, { "lightData", reinterpret_cast<std::uintptr_t>(property->lightData) },
				{ "flags", property->flags.underlying() }, { "bound", Read<std::array<float, 4>>(geometry, 0xE4) } };
		}
		void Begin(const json& args)
		{
			if (!installed)
				throw std::runtime_error(installationError);
			if (!journal.Quiescent() || !traceId.empty())
				throw std::invalid_argument("Finish and release the retained trace before starting another");
			if (Compatibility::IsLoaded(Compatibility::shadowFixes))
				throw std::invalid_argument("ShadowFixes.dll masks native failures; disable it before tracing");
			const auto request = ParseStart(args);
			RE::TESObjectREFR* reference = nullptr;
			RE::BSGeometry* geometry = nullptr;
			RE::BSShaderProperty* property = nullptr;
			std::vector<RE::NiPointer<RE::NiAVObject>> nodes;
			json description = nullptr;
			if (!request.allGeometry) {
				reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(FormId(args));
				if (!reference)
					throw std::invalid_argument("formId is not a loaded reference");
				const auto geometries = Geometries(*reference);
				const auto index = request.geometryIndex;
				if (index >= geometries.size())
					throw std::invalid_argument("geometryIndex is not in the current mesh list");
				geometry = geometries[index];
				property = geometry->GetGeometryRuntimeData().shaderProperty.get();
				if (request.expectedGeometry != reinterpret_cast<std::uintptr_t>(geometry) ||
					request.expectedProperty != reinterpret_cast<std::uintptr_t>(property))
					throw std::invalid_argument("Selected mesh identity changed; list meshes again before capture");
				if (!property->lightData)
					throw std::invalid_argument("Selected geometry has no light data");
				for (auto* node = static_cast<RE::NiAVObject*>(geometry); node; node = node->parent) {
					if (nodes.size() == AncestorCapacity)
						throw std::invalid_argument("Ancestor chain exceeds trace capacity");
					nodes.emplace_back(node);
				}
				description = Describe(geometry, index);
				description["formId"] = reference->GetFormID();
				description["ancestors"] = json::array();
				for (const auto& node : nodes)
					description["ancestors"].push_back({ { "address", reinterpret_cast<std::uintptr_t>(node.get()) }, { "name", node->name.c_str() ? node->name.c_str() : "" } });
			}
			journal.Reset();
			retainedNodes = std::move(nodes);
			retainedProperty.reset(property);
			retainedReference = RE::TESObjectREFRPtr(reference);
			initialLightData = property ? reinterpret_cast<std::uintptr_t>(property->lightData) : 0;
			initialReferenceRoot = reference ? reference->Get3D() : nullptr;
			selection = std::move(description);
			for (std::size_t i = 0; i < AncestorCapacity; ++i)
				watched[i].store(i < retainedNodes.size() ? retainedNodes[i].get() : nullptr, std::memory_order_relaxed);
			traceId = request.traceId;
			++generation;
			startFrame = Frame();
			maximumFrames = request.maximumFrames;
			deadlineMilliseconds = GetTickCount64() + MaximumCaptureMilliseconds;
			ResetCounters();
			allGeometry = request.allGeometry;
			repairEnabled = request.repair;
			selectedProperty.store(property, std::memory_order_relaxed);
			selectedGeometry.store(geometry, std::memory_order_relaxed);
			journal.Open();
		}

		json EventJson(const Event& e)
		{
			return {
				{ "qpc", e.qpc }, { "call", e.call }, { "frame", e.frame }, { "graphicsFrame", e.graphicsFrame }, { "kind", e.kind }, { "after", e.after },
				{ "siteRva", e.site }, { "object", e.object }, { "property", e.property }, { "lightData", e.lightData },
				{ "context", e.context }, { "camera", e.camera }, { "cameraPosition", e.cameraPosition }, { "frustum", e.frustum }, { "primaryAccumulator", e.primaryAccumulator }, { "sun", e.sun }, { "flags", e.flags },
				{ "mask", e.mask }, { "marker", e.marker }, { "operand", e.operand }, { "mode", e.mode }, { "sunMask", e.sunMask },
				{ "activePlanes", e.activePlanes }, { "customActive", e.customActive }, { "cullFlags", e.cullFlags },
				{ "objectFlags", e.objectFlags }, { "result", e.result }, { "bound", e.bound }, { "planes", e.planes },
				{ "customPlanes", e.customPlanes }, { "passCount", e.passCount }, { "passOverflow", e.passOverflow }, { "passes", e.passes },
				{ "receiverEvaluated", e.receiverEvaluated }, { "decision", e.receiverEvaluated ? json(Policy::ReasonNames[static_cast<std::size_t>(e.decision)]) : json(nullptr) },
				{ "evaluatedLightData", e.evaluatedLightData }, { "evaluatedMask", e.evaluatedMask },
				{ "repairQpc", e.repairQpc }, { "cameraRotation", e.cameraRotation }, { "maskBefore", e.maskBefore }, { "maskAfterRepair", e.maskAfterRepair }, { "repairApplied", e.repairApplied },
				{ "shadowDistance", e.shadowDistance }, { "distanceSquared", e.rangeValid ? json(e.distanceSquared) : json(nullptr) },
				{ "rangeSquared", e.rangeValid ? json(e.rangeSquared) : json(nullptr) }, { "geometryName", e.geometryName.data() },
				{ "referenceFormId", e.referenceFormId }, { "referenceIdentified", e.referenceFormId != 0 }
			};
		}
	}

	bool Active() noexcept { return journal.IsOpen(); }

	RE::BSShaderProperty::RenderPassArray* ProcessPasses(RE::BSLightingShaderProperty* property,
		RE::BSGeometry* geometry, std::uint32_t mode, RE::BSShaderAccumulator* accumulator, SunShadowReceiver::PassFunction original)
	{
		return TracePasses(property, geometry, mode, accumulator, original);
	}

	void Install()
	{
		if (attempted)
			return;
		attempted = true;
		try {
			const bool vr = REL::Module::IsVR();
			if (vr && REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15)
				throw std::runtime_error("VR receiver diagnostics require verified Skyrim VR 1.4.15");
			if (Compatibility::IsLoaded(Compatibility::shadowFixes))
				throw std::runtime_error("ShadowFixes.dll owns receiver repair; disable it before using receiver diagnostics");
			if (!SunShadowReceiver::Available())
				throw std::runtime_error(SunShadowReceiver::InstallationError());
			hooks.push_back({ { "kind", "shared_receiver" }, { "slot", 0x2A }, { "owner", "SunShadowReceiverFix" } });
			installed = true;
			if (!vr) {
				upstreamError = "Detailed culling and mask-writer probes are verified only for VR 1.4.15";
				return;
			}
			try {
				const auto base = REL::Module::get().base();
				if (!Matches(base + 0x13086B7, ImmediateBytes) || !Matches(base + 0x13088AC, DeferredBytes) ||
					!Matches(base + 0x12C9E00, { 0x4C, 0x8B, 0x41, 0x70, 0x4D, 0x85, 0xC0, 0x74, 0x32, 0x81, 0xFA, 0xFF, 0xFF, 0, 0, 0x75 }))
					throw std::runtime_error("Native mask instructions differ; upstream probes refused");
				const auto culler = RE::VTABLE_BSCullingProcess[0].address();
				if (Read<std::uintptr_t>(reinterpret_cast<void*>(culler), 0x1D * 8) != base + 0xD9A7B0)
					throw std::runtime_error("Native bound-test virtual differs; upstream probes refused");
				probeTrampoline.create(4096);
				InstallBlock(0x13086B7, false, ImmediateBytes);
				InstallBlock(0x13088AC, true, DeferredBytes);
				originalHelper = reinterpret_cast<HelperFunction>(base + 0x12C9E00);
				const auto helper = Util::AttachDetour(reinterpret_cast<void**>(&originalHelper), reinterpret_cast<void*>(&Helper));
				hooks.push_back({ { "kind", "mask_helper" }, { "rva", 0x12C9E00 }, { "error", helper.error } });
				if (helper.error != NO_ERROR)
					throw std::runtime_error("Native mask helper detour failed");
				Virtual(culler, 0x16, originalCull, &Cull);
				Virtual(culler, 0x18, originalAppend, &Append);
				Virtual(culler, 0x1D, originalBound, &Bound);
				upstreamInstalled = true;
			} catch (const std::exception& error) {
				upstreamError = error.what();
				logger::warn("[Sun receiver trace] Upstream coverage incomplete: {}", upstreamError);
			}
		} catch (const std::exception& error) {
			installationError = error.what();
			logger::warn("[Sun receiver trace] {}", installationError);
		}
	}

	json Handle(const json& args)
	{
		const auto operation = args.value("operation", std::string("status"));
		if (operation != "start" && (args.contains("mode") || args.contains("scope")))
			throw std::invalid_argument("mode and scope are accepted only when starting a capture");
		if (operation == "status")
			return Status();
		if (operation == "start") {
			Begin(args);
			return Status();
		}
		if (operation == "meshes") {
			if (!installed)
				throw std::invalid_argument(installationError);
			auto* reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(FormId(args));
			if (!reference)
				throw std::invalid_argument("formId is not a loaded reference");
			auto values = Geometries(*reference);
			json meshes = json::array();
			for (std::size_t i = 0; i < values.size(); ++i)
				meshes.push_back(Describe(values[i], i));
			return { { "formId", reference->GetFormID() }, { "meshes", meshes }, { "boundedAt", 256 } };
		}
		if (traceId.empty() || args.value("traceId", std::string{}) != traceId)
			throw std::invalid_argument("traceId does not match the retained capture");
		if (operation == "stop") {
			journal.Close();
			return Status();
		}
		{
			Journal::Lease lease(journal);
			(void)Running(lease);
		}
		if (!journal.Quiescent())
			throw std::invalid_argument("Stop capture and wait for quiescent before reading or releasing");
		if (operation == "release") {
			for (auto& node : watched)
				node.store(nullptr, std::memory_order_relaxed);
			selectedProperty = nullptr;
			selectedGeometry = nullptr;
			retainedNodes.clear();
			retainedProperty.reset();
			retainedReference.reset();
			traceId.clear();
			selection = nullptr;
			allGeometry = repairEnabled = false;
			initialLightData = 0;
			initialReferenceRoot = nullptr;
			startFrame = maximumFrames = 0;
			deadlineMilliseconds = 0;
			ResetCounters();
			journal.Reset();
			return Status();
		}
		if (operation == "read") {
			const auto laneIndex = UnsignedArgument(args, "lane", 0, ThreadCount - 1);
			const auto offset = args.contains("offset") ? UnsignedArgument(args, "offset", 0, EventCapacity) : 0;
			const auto limit = args.contains("limit") ? UnsignedArgument(args, "limit", 1, 256) : 128;
			if (!journal.lanes)
				throw std::invalid_argument("No capture storage is available");
			const auto& lane = journal.lanes[laneIndex];
			const auto count = lane.count.load();
			if (offset > count)
				throw std::invalid_argument("offset exceeds lane record count");
			json events = json::array();
			for (std::size_t i = offset; i < std::min(count, offset + limit); ++i)
				events.push_back(EventJson(lane.events[i]));
			LARGE_INTEGER frequency{};
			QueryPerformanceFrequency(&frequency);
			return { { "traceId", traceId }, { "generation", generation }, { "lane", laneIndex }, { "thread", lane.owner.load() },
				{ "qpcFrequency", frequency.QuadPart }, { "offset", offset }, { "nextOffset", offset + events.size() },
				{ "count", count }, { "events", events }, { "kinds", { "mask", "helper", "cull", "boundTest", "append", "passes" } } };
		}
		throw std::invalid_argument("Unknown sun_receiver_trace operation");
	}
}
#endif
