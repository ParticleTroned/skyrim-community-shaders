#include "Diagnostics/AssetLoadHooks.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterMonitor.h"
#	include "Diagnostics/D3DTextureLifetimeTracker.h"
#	include "Diagnostics/OptionalAssetRelocation.h"
#	include "Utils/VirtualFunctionHook.h"
#	include "Utils/NativeTextureLoading.h"
#	include <nlohmann/json.hpp>
#	include <atomic>
#	include <mutex>
#	include <limits>
#	include <format>
#	include <stdexcept>

namespace CSX::Diagnostics::Stutters::AssetLoadHooks
{
	namespace
	{
		struct HookStatus
		{
			bool ready = false, resultKnown = false;
			long error = 0, detourError = 0;
			Util::VirtualHookMethod method = Util::VirtualHookMethod::Detour;
			std::string failure;
		};
		std::once_flag installed;
		HookStatus textureCreate, meshDemand, meshPostCreate;
		std::mutex statusMutex;
		std::atomic_bool ddsAvailable{ false }, textureResourceAvailable{ false };
		bool InSegment(std::uintptr_t address, std::size_t bytes, REL::Segment::Name name)
		{
			const auto segment = REL::Module::get().segment(name);
			return address >= segment.address() && address - segment.address() <= segment.size() &&
			       bytes <= segment.size() - (address - segment.address());
		}
		std::uintptr_t ResolveAddress(const REL::Offset2ID& mappings, std::uint64_t id)
		{
			const auto offset = FindAssetOffset(mappings, id);
			if (!offset || *offset > (std::numeric_limits<std::uintptr_t>::max)() - REL::Module::get().base())
				throw std::runtime_error(std::format("Optional asset relocation {} is unavailable", id));
			return REL::Module::get().base() + *offset;
		}
		std::uintptr_t ResolveTable(const REL::Offset2ID& mappings, REL::RelocationID flatTable, std::size_t slot)
		{
			const auto address = ResolveAddress(mappings, flatTable.id());
			if (address % alignof(void*) || !InSegment(address, (slot + 1) * sizeof(void*), REL::Segment::rdata))
				throw std::runtime_error("Optional asset vtable is outside the game's read-only data segment");
			return address;
		}
		using TextureCreate = Util::NativeTextureLoading::Create;
		TextureCreate originalTextureCreate = nullptr;
		using MeshDemand = decltype(&RE::BSModelDB::Demand);
		MeshDemand originalMeshDemand = nullptr;
		using MeshPostCreate = void (*)(RE::BSModelDB::BSModelProcessor*, const RE::BSModelDB::DBTraits::ArgsType&, const char*, RE::NiPointer<RE::NiNode>&, std::uint32_t&);
		MeshPostCreate originalMeshPostCreate = nullptr;

		std::string_view Path(const char* path) noexcept
		{
			if (!path)
				return {};
			std::size_t length = 0;
			while (length < kAssetPathBytes && path[length])
				++length;
			return { path, length };
		}
		void CreateTexture(void* manager, RE::NiSourceTexture* source)
		{
			if (!AssetRecordingEnabled()) {
				originalTextureCreate(manager, source);
				::Diagnostics::D3DTextureLifetimeTracker::OnSourceTextureCreated(source);
				return;
			}
			AssetScope scope(AssetKind::Texture, source ? Path(source->name.c_str()) : std::string_view{}, "Texture::Create");
			originalTextureCreate(manager, source);
			::Diagnostics::D3DTextureLifetimeTracker::OnSourceTextureCreated(source);
			// This void engine method has no documented success result.
		}
		RE::BSResource::ErrorCode DemandMesh(const char* path, RE::NiPointer<RE::NiNode>& output, const RE::BSModelDB::DBTraits::ArgsType& args)
		{
			if (!AssetRecordingEnabled())
				return originalMeshDemand(path, output, args);
			AssetScope scope(AssetKind::Mesh, Path(path), "Mesh::Demand");
			const auto result = originalMeshDemand(path, output, args);
			scope.Result(result == RE::BSResource::ErrorCode::kNone && output != nullptr, static_cast<std::int64_t>(result));
			return result;
		}
		void PostCreateMesh(RE::BSModelDB::BSModelProcessor* self, const RE::BSModelDB::DBTraits::ArgsType& args, const char* path, RE::NiPointer<RE::NiNode>& root, std::uint32_t& type)
		{
			if (!AssetRecordingEnabled()) {
				originalMeshPostCreate(self, args, path, root, type);
				return;
			}
			AssetScope scope(AssetKind::Mesh, Path(path), "Mesh::PostCreate");
			originalMeshPostCreate(self, args, path, root, type);
		}
		template <class InstallHook>
		void TryInstall(HookStatus& status, std::string_view name, InstallHook install)
		{
			try {
				const auto result = install();
				status.resultKnown = true;
				status.error = result.error;
				status.detourError = result.detourError;
				status.method = result.method;
				status.ready = status.error == NO_ERROR;
				if (!status.ready)
					logger::error("Asset diagnostics {} hook unavailable ({})", name, status.error);
			} catch (const std::exception& error) {
				status.failure = error.what();
				logger::error("Asset diagnostics {} hook unavailable: {}", name, error.what());
			}
		}
		nlohmann::json Describe(const HookStatus& status)
		{
			const auto method = status.method == Util::VirtualHookMethod::Detour ? "detour" : status.method == Util::VirtualHookMethod::VTable ? "vtable" :
			                                                                                                                                     "clone";
			return { { "available", status.ready }, { "win32Error", status.resultKnown ? nlohmann::json(status.error) : nlohmann::json(nullptr) }, { "detourError", status.resultKnown ? nlohmann::json(status.detourError) : nlohmann::json(nullptr) }, { "method", status.resultKnown ? nlohmann::json(method) : nlohmann::json(nullptr) }, { "failure", status.failure.empty() ? nlohmann::json(nullptr) : nlohmann::json(status.failure) } };
		}
	}
	void Install()
	{
		std::call_once(installed, [] {
			std::lock_guard lock(statusMutex);
			std::optional<REL::Offset2ID> mappings;
			try {
				mappings.emplace();
			} catch (const std::exception& error) {
				textureCreate.failure = meshDemand.failure = meshPostCreate.failure = error.what();
				logger::error("Asset diagnostic relocation inventory unavailable: {}", error.what());
				return;
			}
			TryInstall(textureCreate, "texture creation", [&] {
				// The established shader-resource-manager slot is shared by SE and AE.
				const auto table = ResolveTable(*mappings, REL::RelocationID(305902, 255472), Util::NativeTextureLoading::kCreateSlot);
				return Util::InstallNativeVirtualFunctionHook(table, Util::NativeTextureLoading::kCreateSlot,
					reinterpret_cast<void**>(&originalTextureCreate), reinterpret_cast<void*>(&CreateTexture));
			});
			TryInstall(meshDemand, "mesh demand", [&] {
				// Match CommonLib's native Demand ABI and runtime relocation.
				const auto address = ResolveAddress(*mappings, REL::RelocationID(74040, 75782).id());
				if (!InSegment(address, 1, REL::Segment::textx))
					throw std::runtime_error("Optional mesh demand is outside the game's executable segment");
				originalMeshDemand = reinterpret_cast<MeshDemand>(address);
				const auto result = Util::AttachDetour(reinterpret_cast<void**>(&originalMeshDemand), reinterpret_cast<void*>(&DemandMesh));
				return Util::VirtualHookResult{ result.error, result.error, Util::VirtualHookMethod::Detour };
			});
			TryInstall(meshPostCreate, "mesh post-create", [&] {
				const auto table = ResolveTable(*mappings, REL::RelocationID(231089, 187648), 1);
				return Util::InstallNativeVirtualFunctionHook(table, 1,
					reinterpret_cast<void**>(&originalMeshPostCreate), reinterpret_cast<void*>(&PostCreateMesh));
			});
		});
	}
	void SetDDSAvailable(bool available) noexcept { ddsAvailable.store(available, std::memory_order_release); }
	void SetTextureResourceAvailable(bool available) noexcept { textureResourceAvailable.store(available, std::memory_order_release); }
	nlohmann::json Status()
	{
		std::lock_guard lock(statusMutex);
		return { { "textureCreate", Describe(textureCreate) }, { "meshDemand", Describe(meshDemand) }, { "meshPostCreate", Describe(meshPostCreate) },
			{ "nativeDDSStage", ddsAvailable.load(std::memory_order_acquire) }, { "textureResourceCreation", textureResourceAvailable.load(std::memory_order_acquire) },
			{ "hookCoverageSemantics", "successful installation, not complete coverage of foreign overrides or all engine load routes" },
			{ "meshPostCreateCoverage", "base model processor virtual implementation" },
			{ "unobservedStages", { "native queue delay", "native cache hit", "archive member read/decompression split", "GPU completion", "scene attachment outside model post-create" } },
			{ "semantics", "CPU wall durations include waits; overlapping stages are not additive or proof of frame-blocking work" } };
	}
}
#endif
