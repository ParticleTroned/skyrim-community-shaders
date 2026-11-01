#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Diagnostics/D3DTextureLifetimeTracker.h"

#	include "Api/RuntimeThreadAffinity.h"
#	include "Features/TextureStreaming/Preview.h"
#	include "Globals.h"
#	include "State.h"
#	include "TruePBR/TextureEnumeration.h"
#	include "Utils/Game.h"
#	include "Utils/RendererContextAccess.h"
#	include "Utils/SceneInventory.h"
#	include "RE/A/Actor.h"
#	include "RE/B/BSFaceGenNiNode.h"
#	include "RE/B/BSLightingShaderMaterialFacegen.h"
#	include "RE/N/NiSourceTexture.h"
#	include "REL/Relocation.h"

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <chrono>
#	include <cstdint>
#	include <memory>
#	include <mutex>
#	include <new>
#	include <string>
#	include <unordered_map>
#	include <unordered_set>
#	include <vector>

namespace Diagnostics::D3DTextureLifetimeTracker
{
	namespace
	{
		constexpr std::size_t kCapturedStackDepth = 12;
		constexpr std::size_t kMaxTrackedTextureGroups = 4096;
		constexpr std::size_t kMaxTrackedLiveTextures = 16384;
		constexpr std::size_t kMaxCollectedNiSourceTextureOwners = 32768;
		constexpr std::size_t kMaxSceneTraversal = 16384;
		constexpr std::size_t kMaxTextureNameBytes = 256;
		constexpr std::uintptr_t kMinimumRendererTextureAddress = 0x10000;

		// Private-data GUID used only by this diagnostic build. D3D11 owns one
		// reference to the attached sentinel for exactly the texture lifetime.
		constexpr GUID kLifetimeSentinelGuid{
			0xf43e68a1,
			0xa738,
			0x49e4,
			{ 0xb5, 0x9d, 0x73, 0x8c, 0xe1, 0x86, 0xb6, 0xc4 }
		};

		struct DescriptorKey
		{
			uint32_t width{};
			uint32_t height{};
			uint32_t mipLevels{};
			uint32_t arraySize{};
			uint32_t format{};
			uint32_t sampleCount{};
			uint32_t usage{};
			uint32_t bindFlags{};
			uint32_t miscFlags{};
			uint32_t cohort{};
			uint64_t caller{};
			uint32_t stackDepth{};
			std::array<uint64_t, kCapturedStackDepth> stack{};

			bool operator==(const DescriptorKey&) const = default;
		};

		struct DescriptorHash
		{
			std::size_t operator()(const DescriptorKey& a_key) const noexcept
			{
				std::size_t seed = 0xcbf29ce484222325ull;
				auto mix = [&](uint32_t a_value) {
					seed ^= static_cast<std::size_t>(a_value);
					seed *= 0x100000001b3ull;
				};
				mix(a_key.width);
				mix(a_key.height);
				mix(a_key.mipLevels);
				mix(a_key.arraySize);
				mix(a_key.format);
				mix(a_key.sampleCount);
				mix(a_key.usage);
				mix(a_key.bindFlags);
				mix(a_key.miscFlags);
				mix(a_key.cohort);
				mix(static_cast<uint32_t>(a_key.caller));
				mix(static_cast<uint32_t>(a_key.caller >> 32));
				mix(a_key.stackDepth);
				for (const auto frame : a_key.stack) {
					mix(static_cast<uint32_t>(frame));
					mix(static_cast<uint32_t>(frame >> 32));
				}
				return seed;
			}
		};

		struct GroupStats
		{
			uint64_t createdCount{};
			uint64_t destroyedCount{};
			uint64_t estimatedBytesPerTexture{};
			bool estimateKnown{};
		};

		struct LiveTextureRecord
		{
			DescriptorKey key;
			uint64_t estimatedBytes{};
		};

		struct NiSourceTextureOwner
		{
			uint64_t address{};
			uint32_t refCount{};
			std::string name;
		};

		struct FaceGenTintOwnerObservation
		{
			uint64_t sourceTexture{};
			uint64_t material{};
			uint64_t node{};
			uint64_t npc{};
			uint32_t npcFormID{};
			uint32_t actorHandle{};
		};

		using NiSourceTextureOwners = std::unordered_map<uint64_t, std::vector<NiSourceTextureOwner>>;
		struct NiSourceTextureOwnerSnapshot
		{
			NiSourceTextureOwners owners;
			std::unordered_map<uint64_t, FaceGenTintOwnerObservation> faceGenOwners;
			std::string skippedReason;
			uint64_t nodesVisited{};
			uint64_t observedAtMs{};
			bool invalidSceneInventory{};
			uint64_t droppedOwnerCount{};
			uint64_t invalidRendererTextureCount{};
			uint64_t firstInvalidOwner{};
			uint64_t firstInvalidRendererTexture{};
			bool traversalLimitReached{};
		};

		struct TrackerState
		{
			std::mutex mutex;
			std::atomic_bool active{ false };
			std::atomic_uint64_t sessionID{ 0 };
			std::atomic_uint32_t cohort{ 0 };
			uint64_t createdCount{};
			uint64_t destroyedCount{};
			uint64_t createdEstimatedBytes{};
			uint64_t destroyedEstimatedBytes{};
			std::atomic_uint64_t attachFailures{ 0 };
			std::atomic_uint64_t sentinelAllocationFailures{ 0 };
			std::atomic_uint64_t sourceObservationFailures{ 0 };
			std::atomic_uint64_t recordingFailures{ 0 };
			std::atomic_uint64_t droppedTextureRecords{ 0 };
			std::unordered_map<DescriptorKey, GroupStats, DescriptorHash> groups;
			std::unordered_map<uint64_t, LiveTextureRecord> liveTextures;
			std::unordered_map<uint64_t, NiSourceTextureOwner> nativeOrigins;
		};

		TrackerState& GetState()
		{
			// Sentinels may be released during process teardown. Intentionally leak
			// this small diagnostic state so their callbacks never observe a
			// destroyed global mutex.
			static auto* state = new TrackerState();
			return *state;
		}

		DescriptorKey MakeKey(
			const D3D11_TEXTURE2D_DESC& a_desc,
			uint32_t a_cohort,
			std::uintptr_t a_caller,
			const std::array<uint64_t, kCapturedStackDepth>& a_stack,
			uint32_t a_stackDepth)
		{
			return DescriptorKey{
				a_desc.Width,
				a_desc.Height,
				a_desc.MipLevels,
				a_desc.ArraySize,
				static_cast<uint32_t>(a_desc.Format),
				a_desc.SampleDesc.Count,
				static_cast<uint32_t>(a_desc.Usage),
				a_desc.BindFlags,
				a_desc.MiscFlags,
				a_cohort,
				static_cast<uint64_t>(a_caller),
				a_stackDepth,
				a_stack,
			};
		}

		nlohmann::json DescribeAddress(uint64_t a_address)
		{
			std::string module;
			uint64_t offset = 0;
			HMODULE moduleHandle = nullptr;
			if (a_address != 0 && GetModuleHandleExA(
									  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
									  reinterpret_cast<LPCSTR>(a_address),
									  &moduleHandle)) {
				char modulePath[MAX_PATH]{};
				if (GetModuleFileNameA(moduleHandle, modulePath, MAX_PATH) != 0) {
					module = modulePath;
					const auto separator = module.find_last_of("\\/");
					if (separator != std::string::npos)
						module.erase(0, separator + 1);
				}
				offset = a_address - reinterpret_cast<uint64_t>(moduleHandle);
			}
			return {
				{ "address", a_address },
				{ "module", module },
				{ "offset", offset },
			};
		}

		uint32_t BytesPerBlock(DXGI_FORMAT a_format, bool& a_blockCompressed)
		{
			a_blockCompressed = false;
			switch (a_format) {
			case DXGI_FORMAT_BC1_TYPELESS:
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC1_UNORM_SRGB:
			case DXGI_FORMAT_BC4_TYPELESS:
			case DXGI_FORMAT_BC4_UNORM:
			case DXGI_FORMAT_BC4_SNORM:
				a_blockCompressed = true;
				return 8;
			case DXGI_FORMAT_BC2_TYPELESS:
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC2_UNORM_SRGB:
			case DXGI_FORMAT_BC3_TYPELESS:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC3_UNORM_SRGB:
			case DXGI_FORMAT_BC5_TYPELESS:
			case DXGI_FORMAT_BC5_UNORM:
			case DXGI_FORMAT_BC5_SNORM:
			case DXGI_FORMAT_BC6H_TYPELESS:
			case DXGI_FORMAT_BC6H_UF16:
			case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_BC7_TYPELESS:
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_UNORM_SRGB:
				a_blockCompressed = true;
				return 16;
			case DXGI_FORMAT_R8_TYPELESS:
			case DXGI_FORMAT_R8_UNORM:
			case DXGI_FORMAT_R8_UINT:
			case DXGI_FORMAT_R8_SNORM:
			case DXGI_FORMAT_R8_SINT:
			case DXGI_FORMAT_A8_UNORM:
				return 1;
			case DXGI_FORMAT_R8G8_TYPELESS:
			case DXGI_FORMAT_R8G8_UNORM:
			case DXGI_FORMAT_R8G8_UINT:
			case DXGI_FORMAT_R8G8_SNORM:
			case DXGI_FORMAT_R8G8_SINT:
			case DXGI_FORMAT_R16_TYPELESS:
			case DXGI_FORMAT_R16_FLOAT:
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_R16_UNORM:
			case DXGI_FORMAT_R16_UINT:
			case DXGI_FORMAT_R16_SNORM:
			case DXGI_FORMAT_R16_SINT:
				return 2;
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_UINT:
			case DXGI_FORMAT_R8G8B8A8_SNORM:
			case DXGI_FORMAT_R8G8B8A8_SINT:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			case DXGI_FORMAT_R16G16_TYPELESS:
			case DXGI_FORMAT_R16G16_FLOAT:
			case DXGI_FORMAT_R16G16_UNORM:
			case DXGI_FORMAT_R16G16_UINT:
			case DXGI_FORMAT_R16G16_SNORM:
			case DXGI_FORMAT_R16G16_SINT:
			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32_FLOAT:
			case DXGI_FORMAT_R32_UINT:
			case DXGI_FORMAT_R32_SINT:
			case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R10G10B10A2_UINT:
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
				return 4;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
			case DXGI_FORMAT_R16G16B16A16_UINT:
			case DXGI_FORMAT_R16G16B16A16_SNORM:
			case DXGI_FORMAT_R16G16B16A16_SINT:
			case DXGI_FORMAT_R32G32_TYPELESS:
			case DXGI_FORMAT_R32G32_FLOAT:
			case DXGI_FORMAT_R32G32_UINT:
			case DXGI_FORMAT_R32G32_SINT:
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
				return 8;
			case DXGI_FORMAT_R32G32B32_TYPELESS:
			case DXGI_FORMAT_R32G32B32_FLOAT:
			case DXGI_FORMAT_R32G32B32_UINT:
			case DXGI_FORMAT_R32G32B32_SINT:
				return 12;
			case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_UINT:
			case DXGI_FORMAT_R32G32B32A32_SINT:
				return 16;
			default:
				return 0;
			}
		}

		uint64_t EstimateBytes(const D3D11_TEXTURE2D_DESC& a_desc)
		{
			bool blockCompressed = false;
			const uint32_t bytesPerBlock = BytesPerBlock(a_desc.Format, blockCompressed);
			if (bytesPerBlock == 0)
				return 0;

			uint32_t mipLevels = a_desc.MipLevels;
			if (mipLevels == 0) {
				mipLevels = 1;
				for (uint32_t dimension = std::max(a_desc.Width, a_desc.Height); dimension > 1; dimension >>= 1)
					++mipLevels;
			}

			uint64_t bytes = 0;
			uint32_t width = std::max(a_desc.Width, 1u);
			uint32_t height = std::max(a_desc.Height, 1u);
			for (uint32_t mip = 0; mip < mipLevels; ++mip) {
				if (blockCompressed) {
					const uint64_t blocksWide = std::max<uint64_t>(1, (width + 3u) / 4u);
					const uint64_t blocksHigh = std::max<uint64_t>(1, (height + 3u) / 4u);
					bytes += blocksWide * blocksHigh * bytesPerBlock;
				} else {
					bytes += static_cast<uint64_t>(width) * height * bytesPerBlock;
				}
				width = std::max(width >> 1, 1u);
				height = std::max(height >> 1, 1u);
			}
			return bytes * std::max(a_desc.ArraySize, 1u) * std::max(a_desc.SampleDesc.Count, 1u);
		}

		// Skyrim publishes a source texture before its renderer pointer is ready.
		// Renderer ownership does not cover every asynchronous pointer update.
		bool TryReadRendererResource(const RE::BSGraphics::Texture* a_rendererTexture, uint64_t& a_resource) noexcept
		{
			__try {
				a_resource = reinterpret_cast<uint64_t>(a_rendererTexture->texture);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		uint64_t ReadRendererResource(const RE::NiSourceTexture* a_sourceTexture, uint64_t& a_invalidRendererTexture) noexcept
		{
			a_invalidRendererTexture = 0;
			const auto* rendererTexture = a_sourceTexture->rendererTexture;
			if (!rendererTexture)
				return 0;

			const auto address = reinterpret_cast<uintptr_t>(rendererTexture);
			if (address < kMinimumRendererTextureAddress || (address & (alignof(void*) - 1)) != 0) {
				a_invalidRendererTexture = address;
				return 0;
			}

			uint64_t resource = 0;
			if (!TryReadRendererResource(rendererTexture, resource)) {
				a_invalidRendererTexture = address;
				return 0;
			}
			return resource;
		}

		std::string TextureName(const RE::NiSourceTexture* a_texture)
		{
			const char* name = a_texture->name.c_str();
			std::size_t length = 0;
			while (name && length < kMaxTextureNameBytes && name[length])
				++length;
			return name ? std::string(name, length) : std::string{};
		}

		NiSourceTextureOwnerSnapshot CollectNiSourceTextureOwners()
		{
			NiSourceTextureOwnerSnapshot snapshot;
			if (!CSX::Api::IsRuntimeMainThread()) {
				snapshot.skippedReason = "main_thread_required";
				return snapshot;
			}
			if (!globals::state || globals::state->IsSaveLoadSafeModeActive()) {
				snapshot.skippedReason = "world_transition";
				return snapshot;
			}
			Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
			if (!owner) {
				snapshot.skippedReason = "renderer_unavailable_or_busy";
				return snapshot;
			}
			struct Node
			{
				RE::NiPointer<RE::NiAVObject> object;
				RE::NiPointer<RE::BSFaceGenNiNode> faceGen;
			};
			std::vector<Node> frontier;
			std::unordered_set<RE::NiAVObject*> visited;
			std::unordered_set<RE::NiSourceTexture*> seenTextures;
			const auto addRoot = [&](RE::NiAVObject* root) {
				if (!root)
					return;
				if (!Util::IsLikelyValidPointer(root) || frontier.size() >= kMaxSceneTraversal) {
					snapshot.invalidSceneInventory = true;
					return;
				}
				frontier.push_back({ RE::NiPointer<RE::NiAVObject>(root), {} });
			};
			addRoot(RE::Main::WorldRootNode());
			const auto previews = StreamingTextures::Preview::Roots();
			snapshot.invalidSceneInventory |= !previews.complete;
			for (const auto& root : previews.roots)
				addRoot(root.get());
			if (frontier.empty()) {
				snapshot.skippedReason = "no_scene_roots";
				return snapshot;
			}
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
			while (!frontier.empty() && visited.size() < kMaxSceneTraversal && std::chrono::steady_clock::now() < deadline) {
				auto current = std::move(frontier.back());
				frontier.pop_back();
				if (!current.object || !visited.insert(current.object.get()).second)
					continue;
				if (auto* faceGen = netimmerse_cast<RE::BSFaceGenNiNode*>(current.object.get()))
					current.faceGen.reset(faceGen);
				if (auto* geometry = current.object->AsGeometry()) {
					if (auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get()) {
						struct Visitor final : RE::BSShaderProperty::ForEachVisitor
						{
							NiSourceTextureOwnerSnapshot& snapshot;
							std::unordered_set<RE::NiSourceTexture*>& seen;
							Visitor(NiSourceTextureOwnerSnapshot& a_snapshot, std::unordered_set<RE::NiSourceTexture*>& a_seen) :
								snapshot(a_snapshot), seen(a_seen) {}
							std::uint32_t Accept(RE::NiSourceTexture* texture) override
							{
								if (!texture || seen.contains(texture))
									return 1;
								if (!Util::IsLikelyValidPointer(texture)) {
									snapshot.invalidSceneInventory = true;
									return 0;
								}
								if (seen.size() >= kMaxCollectedNiSourceTextureOwners) {
									++snapshot.droppedOwnerCount;
									return 0;
								}
								seen.insert(texture);
								uint64_t invalidRenderer = 0;
								const auto resource = ReadRendererResource(texture, invalidRenderer);
								if (invalidRenderer) {
									++snapshot.invalidRendererTextureCount;
									if (!snapshot.firstInvalidOwner) {
										snapshot.firstInvalidOwner = reinterpret_cast<uint64_t>(texture);
										snapshot.firstInvalidRendererTexture = invalidRenderer;
									}
								}
								if (resource)
									snapshot.owners[resource].push_back({ reinterpret_cast<uint64_t>(texture), texture->GetRefCount(), TextureName(texture) });
								return 1;
							}
						} visitor(snapshot, seenTextures);
						PBRTextures::ForEach(*property, visitor);
						static REL::Relocation<std::uintptr_t> faceGenVtable{ RE::BSLightingShaderMaterialFacegen::VTABLE[0] };
						if (Util::IsLikelyValidPointer(property->material) && *reinterpret_cast<const std::uintptr_t*>(property->material) == faceGenVtable.address()) {
							const auto* material = static_cast<RE::BSLightingShaderMaterialFacegen*>(property->material);
							if (auto* texture = material->tintTexture.get(); Util::IsLikelyValidPointer(texture)) {
								visitor.Accept(texture);
								uint64_t invalidRenderer = 0;
								const auto resource = ReadRendererResource(texture, invalidRenderer);
								if (resource && snapshot.faceGenOwners.size() < kMaxTrackedLiveTextures) {
									const auto actor = current.faceGen ? current.faceGen->GetRuntimeData().unk15C.get() : RE::NiPointer<RE::Actor>{};
									const auto* npc = actor ? actor->GetActorBase() : nullptr;
									snapshot.faceGenOwners.try_emplace(resource, FaceGenTintOwnerObservation{
																					 reinterpret_cast<uint64_t>(texture),
																					 reinterpret_cast<uint64_t>(material),
																					 reinterpret_cast<uint64_t>(current.faceGen.get()),
																					 reinterpret_cast<uint64_t>(npc),
																					 npc ? npc->GetFormID() : 0,
																					 current.faceGen ? current.faceGen->GetRuntimeData().unk15C.native_handle() : 0,
																				 });
								}
							}
						}
					}
				}
				if (auto* branch = current.object->AsNode()) {
					if (!Util::VisitSceneInventoryChildren(branch->GetChildren(), kMaxSceneTraversal - frontier.size(), [](const void* value) { return Util::IsLikelyValidPointer(value); }, [&](auto* child) { frontier.push_back({ RE::NiPointer<RE::NiAVObject>(child), current.faceGen }); })) {
						snapshot.invalidSceneInventory = true;
						break;
					}
				}
			}
			snapshot.nodesVisited = visited.size();
			snapshot.traversalLimitReached = !frontier.empty();
			snapshot.observedAtMs = GetTickCount64();
			return snapshot;
		}

		void RecordDestroyed(
			uint64_t a_sessionID,
			const DescriptorKey& a_key,
			uint64_t a_bytes,
			uint64_t a_texture) noexcept
		{
			try {
				auto& state = GetState();
				std::scoped_lock lock(state.mutex);
				if (!state.active.load(std::memory_order_relaxed) ||
					state.sessionID.load(std::memory_order_relaxed) != a_sessionID)
					return;
				auto it = state.groups.find(a_key);
				if (it == state.groups.end())
					return;
				++it->second.destroyedCount;
				++state.destroyedCount;
				state.destroyedEstimatedBytes += a_bytes;
				state.liveTextures.erase(a_texture);
				state.nativeOrigins.erase(a_texture);
			} catch (...) {
				// Destruction callbacks must never escape into the D3D11 runtime.
			}
		}

		class LifetimeSentinel final : public IUnknown
		{
		public:
			LifetimeSentinel(uint64_t a_sessionID, DescriptorKey a_key, uint64_t a_bytes, uint64_t a_texture) :
				sessionID(a_sessionID), key(a_key), bytes(a_bytes), texture(a_texture)
			{}

			void Arm() noexcept { armed.store(true, std::memory_order_release); }

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_riid, void** a_object) override
			{
				if (!a_object)
					return E_POINTER;
				if (a_riid == __uuidof(IUnknown)) {
					*a_object = static_cast<IUnknown*>(this);
					AddRef();
					return S_OK;
				}
				*a_object = nullptr;
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return references.fetch_add(1, std::memory_order_relaxed) + 1;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remaining = references.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (remaining == 0) {
					if (armed.load(std::memory_order_acquire))
						RecordDestroyed(sessionID, key, bytes, texture);
					delete this;
				}
				return remaining;
			}

		private:
			std::atomic_ulong references{ 1 };
			std::atomic_bool armed{ false };
			uint64_t sessionID{};
			DescriptorKey key{};
			uint64_t bytes{};
			uint64_t texture{};
		};
	}

	bool Start()
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (state.active.load(std::memory_order_relaxed))
			return false;
		uint64_t sessionID = state.sessionID.load(std::memory_order_relaxed) + 1;
		if (sessionID == 0)
			sessionID = 1;
		state.sessionID.store(sessionID, std::memory_order_relaxed);
		state.createdCount = 0;
		state.destroyedCount = 0;
		state.createdEstimatedBytes = 0;
		state.destroyedEstimatedBytes = 0;
		state.attachFailures.store(0, std::memory_order_relaxed);
		state.sentinelAllocationFailures.store(0, std::memory_order_relaxed);
		state.sourceObservationFailures.store(0, std::memory_order_relaxed);
		state.recordingFailures.store(0, std::memory_order_relaxed);
		state.droppedTextureRecords.store(0, std::memory_order_relaxed);
		state.cohort.store(0, std::memory_order_relaxed);
		state.groups.clear();
		state.liveTextures.clear();
		state.nativeOrigins.clear();
		state.active.store(true, std::memory_order_release);
		return true;
	}

	bool Stop(uint64_t a_expectedSessionID)
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (a_expectedSessionID && state.sessionID.load(std::memory_order_relaxed) != a_expectedSessionID)
			return false;
		if (!state.active.load(std::memory_order_relaxed))
			return false;
		state.active.store(false, std::memory_order_release);
		return true;
	}

	bool Reset(uint64_t a_expectedSessionID)
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (a_expectedSessionID && state.sessionID.load(std::memory_order_relaxed) != a_expectedSessionID)
			return false;
		if (state.active.load(std::memory_order_relaxed))
			return false;
		state.createdCount = 0;
		state.destroyedCount = 0;
		state.createdEstimatedBytes = 0;
		state.destroyedEstimatedBytes = 0;
		state.attachFailures.store(0, std::memory_order_relaxed);
		state.sentinelAllocationFailures.store(0, std::memory_order_relaxed);
		state.sourceObservationFailures.store(0, std::memory_order_relaxed);
		state.recordingFailures.store(0, std::memory_order_relaxed);
		state.droppedTextureRecords.store(0, std::memory_order_relaxed);
		state.groups.clear();
		state.liveTextures.clear();
		state.nativeOrigins.clear();
		return true;
	}

	bool Checkpoint(uint64_t a_expectedSessionID)
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (a_expectedSessionID && state.sessionID.load(std::memory_order_relaxed) != a_expectedSessionID)
			return false;
		if (!state.active.load(std::memory_order_relaxed))
			return false;
		state.cohort.fetch_add(1, std::memory_order_release);
		return true;
	}

	bool IsActive()
	{
		return GetState().active.load(std::memory_order_acquire);
	}

	void OnTextureCreated(
		ID3D11Texture2D* a_texture,
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::uintptr_t a_caller) noexcept
	{
		TrackerState* statePtr = nullptr;
		uint64_t sessionID = 0;
		try {
			statePtr = std::addressof(GetState());
			auto& state = *statePtr;
			if (!a_texture || !state.active.load(std::memory_order_acquire))
				return;

			sessionID = state.sessionID.load(std::memory_order_acquire);
			const uint32_t cohort = state.cohort.load(std::memory_order_acquire);
			std::array<void*, kCapturedStackDepth> rawStack{};
			const auto rawStackDepth = CaptureStackBackTrace(
				0,
				static_cast<DWORD>(rawStack.size()),
				rawStack.data(),
				nullptr);
			std::array<uint64_t, kCapturedStackDepth> stack{};
			for (uint32_t index = 0; index < rawStackDepth; ++index)
				stack[index] = reinterpret_cast<uint64_t>(rawStack[index]);
			const DescriptorKey key = MakeKey(a_desc, cohort, a_caller, stack, rawStackDepth);
			const uint64_t bytes = EstimateBytes(a_desc);
			const auto textureAddress = reinterpret_cast<uint64_t>(a_texture);
			auto* sentinel = new (std::nothrow) LifetimeSentinel(sessionID, key, bytes, textureAddress);
			if (!sentinel) {
				if (state.active.load(std::memory_order_relaxed) &&
					state.sessionID.load(std::memory_order_relaxed) == sessionID) {
					state.sentinelAllocationFailures.fetch_add(1, std::memory_order_relaxed);
					state.droppedTextureRecords.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
			struct SentinelReference
			{
				LifetimeSentinel* value;
				~SentinelReference() { value->Release(); }
			} sentinelReference{ sentinel };

			std::scoped_lock lock(state.mutex);
			if (!state.active.load(std::memory_order_relaxed) ||
				state.sessionID.load(std::memory_order_relaxed) != sessionID) {
				return;
			}

			auto groupIt = state.groups.find(key);
			bool insertedGroup = false;
			if (groupIt == state.groups.end()) {
				if (state.groups.size() >= kMaxTrackedTextureGroups) {
					state.droppedTextureRecords.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				auto inserted = state.groups.try_emplace(key);
				groupIt = inserted.first;
				insertedGroup = inserted.second;
			}

			if (state.liveTextures.contains(textureAddress) ||
				state.liveTextures.size() >= kMaxTrackedLiveTextures) {
				if (insertedGroup)
					state.groups.erase(groupIt);
				state.droppedTextureRecords.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			try {
				state.liveTextures.try_emplace(textureAddress, LiveTextureRecord{ key, bytes });
			} catch (...) {
				if (insertedGroup)
					state.groups.erase(groupIt);
				throw;
			}

			HRESULT attached = E_FAIL;
			try {
				attached = a_texture->SetPrivateDataInterface(kLifetimeSentinelGuid, sentinel);
			} catch (...) {
				state.liveTextures.erase(textureAddress);
				if (insertedGroup)
					state.groups.erase(groupIt);
				throw;
			}
			if (FAILED(attached)) {
				state.liveTextures.erase(textureAddress);
				if (insertedGroup)
					state.groups.erase(groupIt);
				state.attachFailures.fetch_add(1, std::memory_order_relaxed);
				state.droppedTextureRecords.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			auto& group = groupIt->second;
			group.estimatedBytesPerTexture = bytes;
			group.estimateKnown = bytes != 0;
			++group.createdCount;
			++state.createdCount;
			state.createdEstimatedBytes += bytes;
			sentinel->Arm();
		} catch (...) {
			if (statePtr && statePtr->active.load(std::memory_order_relaxed) &&
				(sessionID == 0 || statePtr->sessionID.load(std::memory_order_relaxed) == sessionID)) {
				statePtr->recordingFailures.fetch_add(1, std::memory_order_relaxed);
				statePtr->droppedTextureRecords.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	void OnSourceTextureCreated(RE::NiSourceTexture* a_texture) noexcept
	{
		TrackerState* statePtr = nullptr;
		uint64_t sessionID = 0;
		try {
			statePtr = std::addressof(GetState());
			auto& state = *statePtr;
			if (!a_texture || !state.active.load(std::memory_order_acquire))
				return;
			sessionID = state.sessionID.load(std::memory_order_acquire);
			uint64_t invalidRenderer = 0;
			const auto resource = ReadRendererResource(a_texture, invalidRenderer);
			if (!resource) {
				if (invalidRenderer)
					state.sourceObservationFailures.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			NiSourceTextureOwner origin{ reinterpret_cast<uint64_t>(a_texture), a_texture->GetRefCount(), TextureName(a_texture) };
			std::scoped_lock lock(state.mutex);
			if (state.active.load(std::memory_order_relaxed) && state.sessionID.load(std::memory_order_relaxed) == sessionID &&
				state.liveTextures.contains(resource)) {
				state.nativeOrigins.insert_or_assign(resource, std::move(origin));
			}
		} catch (...) {
			if (statePtr && statePtr->active.load(std::memory_order_relaxed) &&
				(sessionID == 0 || statePtr->sessionID.load(std::memory_order_relaxed) == sessionID)) {
				statePtr->sourceObservationFailures.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	nlohmann::json BuildStatus()
	{
		using json = nlohmann::json;

		auto& state = GetState();
		std::unique_lock lock(state.mutex);
		const bool hasTrackedTextures = state.active.load(std::memory_order_relaxed) && !state.liveTextures.empty();
		const auto snapshotSession = state.sessionID.load(std::memory_order_relaxed);
		// Native traversal owns the renderer; sentinel destruction can take the
		// tracker mutex from inside a renderer transaction.
		if (hasTrackedTextures)
			lock.unlock();
		NiSourceTextureOwnerSnapshot niSourceTextureOwnerSnapshot;
		if (hasTrackedTextures) {
			try {
				niSourceTextureOwnerSnapshot = CollectNiSourceTextureOwners();
			} catch (...) {
				niSourceTextureOwnerSnapshot = {};
				niSourceTextureOwnerSnapshot.skippedReason = "scene_inventory_failed";
			}
			lock.lock();
			if (state.sessionID.load(std::memory_order_relaxed) != snapshotSession || !state.active.load(std::memory_order_relaxed)) {
				niSourceTextureOwnerSnapshot = {};
				niSourceTextureOwnerSnapshot.skippedReason = "capture_changed";
			}
		}
		const auto& niSourceTextureOwners = niSourceTextureOwnerSnapshot.owners;
		json firstInvalidRendererTexture = nullptr;
		if (niSourceTextureOwnerSnapshot.firstInvalidOwner) {
			firstInvalidRendererTexture = {
				{ "owner", niSourceTextureOwnerSnapshot.firstInvalidOwner },
				{ "rendererTexture", niSourceTextureOwnerSnapshot.firstInvalidRendererTexture },
			};
		}
		std::unordered_map<DescriptorKey, uint64_t, DescriptorHash> niSourceMatchesByGroup;
		std::unordered_map<DescriptorKey, uint64_t, DescriptorHash> niSourceMatchBytesByGroup;
		json nonCurrentNiSourceMatches = json::array();
		json nativeSourceOrigins = json::array();
		uint64_t niSourceMatchedCount = 0;
		uint64_t niSourceMatchedEstimatedBytes = 0;
		const auto currentCohort = state.cohort.load(std::memory_order_relaxed);
		for (const auto& [textureAddress, record] : state.liveTextures) {
			if (const auto origin = state.nativeOrigins.find(textureAddress); origin != state.nativeOrigins.end() && nativeSourceOrigins.size() < 256) {
				nativeSourceOrigins.push_back({ { "texture", textureAddress }, { "sourceTexture", origin->second.address },
					{ "refCountAtCreation", origin->second.refCount }, { "name", origin->second.name }, { "cohort", record.key.cohort } });
			}
			const auto ownerIt = niSourceTextureOwners.find(textureAddress);
			if (ownerIt == niSourceTextureOwners.end())
				continue;
			++niSourceMatchedCount;
			niSourceMatchedEstimatedBytes += record.estimatedBytes;
			++niSourceMatchesByGroup[record.key];
			niSourceMatchBytesByGroup[record.key] += record.estimatedBytes;
			if (record.key.cohort != currentCohort) {
				json faceGenOwner = nullptr;
				if (const auto assignmentIt = niSourceTextureOwnerSnapshot.faceGenOwners.find(textureAddress);
					assignmentIt != niSourceTextureOwnerSnapshot.faceGenOwners.end()) {
					const auto& assignment = assignmentIt->second;
					faceGenOwner = {
						{ "sourceTexture", assignment.sourceTexture },
						{ "material", assignment.material },
						{ "node", assignment.node },
						{ "headPart", nullptr },
						{ "headPartFormID", nullptr },
						{ "npc", assignment.npc },
						{ "npcFormID", assignment.npcFormID },
						{ "actorHandle", assignment.actorHandle },
						{ "observedAtMs", niSourceTextureOwnerSnapshot.observedAtMs },
					};
				}
				json owners = json::array();
				for (const auto& owner : ownerIt->second) {
					owners.push_back({
						{ "address", owner.address },
						{ "refCount", owner.refCount },
						{ "name", owner.name },
					});
				}
				nonCurrentNiSourceMatches.push_back({
					{ "texture", textureAddress },
					{ "cohort", record.key.cohort },
					{ "caller", record.key.caller },
					{ "estimatedBytes", record.estimatedBytes },
					{ "faceGenOwnerObservation", std::move(faceGenOwner) },
					{ "owners", std::move(owners) },
				});
			}
		}
		json groups = json::array();
		uint64_t outstandingUnknownEstimateCount = 0;
		for (const auto& [key, stats] : state.groups) {
			const uint64_t outstandingCount = stats.createdCount >= stats.destroyedCount ?
			                                      stats.createdCount - stats.destroyedCount :
			                                      0;
			const auto caller = DescribeAddress(key.caller);
			if (!stats.estimateKnown)
				outstandingUnknownEstimateCount += outstandingCount;
			json callerStack = json::array();
			for (uint32_t index = 0; index < key.stackDepth && index < key.stack.size(); ++index)
				callerStack.push_back(DescribeAddress(key.stack[index]));
			groups.push_back({
				{ "width", key.width },
				{ "height", key.height },
				{ "mipLevels", key.mipLevels },
				{ "arraySize", key.arraySize },
				{ "format", key.format },
				{ "sampleCount", key.sampleCount },
				{ "usage", key.usage },
				{ "bindFlags", key.bindFlags },
				{ "miscFlags", key.miscFlags },
				{ "cohort", key.cohort },
				{ "caller", key.caller },
				{ "callerModule", caller["module"] },
				{ "callerOffset", caller["offset"] },
				{ "callerStack", std::move(callerStack) },
				{ "createdCount", stats.createdCount },
				{ "destroyedCount", stats.destroyedCount },
				{ "outstandingCount", outstandingCount },
				{ "estimatedBytesPerTexture", stats.estimatedBytesPerTexture },
				{ "estimateKnown", stats.estimateKnown },
				{ "outstandingEstimatedBytes", outstandingCount * stats.estimatedBytesPerTexture },
				{ "niSourceTextureMatchedCount", niSourceMatchesByGroup[key] },
				{ "niSourceTextureMatchedEstimatedBytes", niSourceMatchBytesByGroup[key] },
			});
		}
		std::sort(groups.begin(), groups.end(), [](const json& a_left, const json& a_right) {
			return a_left["outstandingEstimatedBytes"].get<uint64_t>() >
			       a_right["outstandingEstimatedBytes"].get<uint64_t>();
		});

		const uint64_t outstandingCount = state.createdCount >= state.destroyedCount ?
		                                      state.createdCount - state.destroyedCount :
		                                      0;
		const uint64_t outstandingEstimatedBytes =
			state.createdEstimatedBytes >= state.destroyedEstimatedBytes ?
				state.createdEstimatedBytes - state.destroyedEstimatedBytes :
				0;
		return json{
			{ "supported", true },
			{ "active", state.active.load(std::memory_order_relaxed) },
			{ "sessionID", state.sessionID.load(std::memory_order_relaxed) },
			{ "currentCohort", state.cohort.load(std::memory_order_relaxed) },
			{ "createdCount", state.createdCount },
			{ "destroyedCount", state.destroyedCount },
			{ "outstandingCount", outstandingCount },
			{ "createdEstimatedBytes", state.createdEstimatedBytes },
			{ "destroyedEstimatedBytes", state.destroyedEstimatedBytes },
			{ "outstandingEstimatedBytes", outstandingEstimatedBytes },
			{ "outstandingUnknownEstimateCount", outstandingUnknownEstimateCount },
			{ "attachFailures", state.attachFailures.load(std::memory_order_relaxed) },
			{ "sentinelAllocationFailures", state.sentinelAllocationFailures.load(std::memory_order_relaxed) },
			{ "sourceObservationFailures", state.sourceObservationFailures.load(std::memory_order_relaxed) },
			{ "recordingFailures", state.recordingFailures.load(std::memory_order_relaxed) },
			{ "droppedTextureRecords", state.droppedTextureRecords.load(std::memory_order_relaxed) },
			{ "maxTrackedTextureGroups", kMaxTrackedTextureGroups },
			{ "maxTrackedLiveTextures", kMaxTrackedLiveTextures },
			{ "liveTextureRecordCount", state.liveTextures.size() },
			{ "nativeSourceCreationObservationCount", state.nativeOrigins.size() },
			{ "faceGenTintOwnerObservationCount", niSourceTextureOwnerSnapshot.faceGenOwners.size() },
			{ "niSourceTextureCoverage", "bounded reachable world and preview materials under renderer ownership; unmatched resources have unknown ownership" },
			{ "nativeSourceCreationCoverage", "historical observations from the hooked native texture-creation path; no native or COM references retained" },
			{ "statisticsFrozen", !state.active.load(std::memory_order_relaxed) },
			{ "niSourceTextureOwnerScanReason", hasTrackedTextures ? niSourceTextureOwnerSnapshot.skippedReason : "capture_inactive_or_empty" },
			{ "niSourceTextureObservedAtMs", niSourceTextureOwnerSnapshot.observedAtMs },
			{ "niSourceTextureSceneNodesVisited", niSourceTextureOwnerSnapshot.nodesVisited },
			{ "niSourceTextureInvalidSceneInventory", niSourceTextureOwnerSnapshot.invalidSceneInventory },
			{ "faceGenCoverage", "typed native FaceGen materials observed in the reachable scene; head-part assignment callsites are not instrumented" },
			{ "niSourceTextureResourceCount", niSourceTextureOwners.size() },
			{ "niSourceTextureOwnerScanSkipped", !hasTrackedTextures || !niSourceTextureOwnerSnapshot.skippedReason.empty() },
			{ "niSourceTextureOwnerRecordsDropped", niSourceTextureOwnerSnapshot.droppedOwnerCount },
			{ "niSourceTextureInvalidRendererTextureCount", niSourceTextureOwnerSnapshot.invalidRendererTextureCount },
			{ "niSourceTextureFirstInvalidRendererTexture", std::move(firstInvalidRendererTexture) },
			{ "niSourceTextureTraversalLimitReached", niSourceTextureOwnerSnapshot.traversalLimitReached },
			{ "niSourceTextureMatchedCount", niSourceMatchedCount },
			{ "niSourceTextureMatchedEstimatedBytes", niSourceMatchedEstimatedBytes },
			{ "nonCurrentNiSourceTextureMatches", std::move(nonCurrentNiSourceMatches) },
			{ "nativeSourceCreationObservationsTruncated", state.nativeOrigins.size() > nativeSourceOrigins.size() },
			{ "nativeSourceCreationObservations", std::move(nativeSourceOrigins) },
			{ "groupCount", groups.size() },
			{ "groups", std::move(groups) },
		};
	}
}

#endif
