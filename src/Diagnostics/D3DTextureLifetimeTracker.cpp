#include "Diagnostics/D3DTextureLifetimeTracker.h"

#include "RE/N/NiSourceTexture.h"
#include "REL/Relocation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Diagnostics::D3DTextureLifetimeTracker
{
	namespace
	{
		constexpr std::size_t kCapturedStackDepth = 12;
		constexpr std::uintptr_t kFaceGenTintTextureOffset = 0xA0;
		constexpr std::size_t kMaxTrackedTextureGroups = 4096;
		constexpr std::size_t kMaxTrackedLiveTextures = 16384;
		constexpr std::size_t kMaxCollectedNiSourceTextureOwners = 32768;
		constexpr std::size_t kMaxNiTextureTraversal = 1'000'000;

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

		struct FaceGenTintAssignment
		{
			uint64_t sourceTexture{};
			uint64_t material{};
			uint64_t node{};
			uint64_t headPart{};
			uint32_t headPartFormID{};
			uint64_t npc{};
			uint32_t npcFormID{};
			uint32_t actorHandle{};
			uint32_t cohort{};
		};

		using NiSourceTextureOwners = std::unordered_map<uint64_t, std::vector<NiSourceTextureOwner>>;
		struct NiSourceTextureOwnerSnapshot
		{
			NiSourceTextureOwners owners;
			uint64_t droppedOwnerCount{};
			bool traversalLimitReached{};
		};

		class CriticalSectionGuard
		{
		public:
			explicit CriticalSectionGuard(CRITICAL_SECTION* a_lock) noexcept : lock(a_lock)
			{
				EnterCriticalSection(lock);
			}

			~CriticalSectionGuard()
			{
				LeaveCriticalSection(lock);
			}

			CriticalSectionGuard(const CriticalSectionGuard&) = delete;
			CriticalSectionGuard& operator=(const CriticalSectionGuard&) = delete;

		private:
			CRITICAL_SECTION* lock;
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
			std::atomic_uint64_t faceGenAssignmentFailures{ 0 };
			std::atomic_uint64_t recordingFailures{ 0 };
			std::atomic_uint64_t droppedTextureRecords{ 0 };
			std::unordered_map<DescriptorKey, GroupStats, DescriptorHash> groups;
			std::unordered_map<uint64_t, LiveTextureRecord> liveTextures;
			std::unordered_map<uint64_t, FaceGenTintAssignment> faceGenTintAssignments;
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

		NiSourceTextureOwnerSnapshot CollectNiSourceTextureOwners()
		{
			// Skyrim maintains all live NiTexture instances in this locked intrusive
			// list. These VR offsets are confirmed both by CommonLib and by the
			// reconstructed NiSourceTexture constructor/destructor.
			static REL::Relocation<RE::NiTexture**> textureListHead{ REL::Offset(0x316BF80) };
			static REL::Relocation<std::uintptr_t> niSourceTextureVtable{ RE::NiSourceTexture::VTABLE[0] };
			auto* textureListLock = reinterpret_cast<CRITICAL_SECTION*>(REL::Offset(0x316C000).address());

			NiSourceTextureOwnerSnapshot snapshot;
			std::size_t capturedOwnerCount = 0;
			CriticalSectionGuard textureListGuard(textureListLock);
			auto* texture = *textureListHead;
			std::size_t visited = 0;
			for (; texture && visited < kMaxNiTextureTraversal; ++visited) {
				const auto vtable = *reinterpret_cast<const std::uintptr_t*>(texture);
				if (vtable == niSourceTextureVtable.address()) {
					auto* sourceTexture = static_cast<RE::NiSourceTexture*>(texture);
					if (sourceTexture->rendererTexture && sourceTexture->rendererTexture->texture) {
						if (capturedOwnerCount < kMaxCollectedNiSourceTextureOwners) {
							const auto resource = reinterpret_cast<uint64_t>(sourceTexture->rendererTexture->texture);
							snapshot.owners[resource].push_back({
								reinterpret_cast<uint64_t>(sourceTexture),
								sourceTexture->GetRefCount(),
								sourceTexture->name.c_str(),
							});
							++capturedOwnerCount;
						} else {
							++snapshot.droppedOwnerCount;
						}
					}
				}
				texture = texture->next;
			}
			snapshot.traversalLimitReached = texture != nullptr;
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
				state.faceGenTintAssignments.erase(a_texture);
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
		state.faceGenAssignmentFailures.store(0, std::memory_order_relaxed);
		state.recordingFailures.store(0, std::memory_order_relaxed);
		state.droppedTextureRecords.store(0, std::memory_order_relaxed);
		state.cohort.store(0, std::memory_order_relaxed);
		state.groups.clear();
		state.liveTextures.clear();
		state.faceGenTintAssignments.clear();
		state.active.store(true, std::memory_order_release);
		return true;
	}

	bool Stop()
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (!state.active.load(std::memory_order_relaxed))
			return false;
		state.active.store(false, std::memory_order_release);
		return true;
	}

	bool Reset()
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		if (state.active.load(std::memory_order_relaxed))
			return false;
		state.createdCount = 0;
		state.destroyedCount = 0;
		state.createdEstimatedBytes = 0;
		state.destroyedEstimatedBytes = 0;
		state.attachFailures.store(0, std::memory_order_relaxed);
		state.sentinelAllocationFailures.store(0, std::memory_order_relaxed);
		state.faceGenAssignmentFailures.store(0, std::memory_order_relaxed);
		state.recordingFailures.store(0, std::memory_order_relaxed);
		state.droppedTextureRecords.store(0, std::memory_order_relaxed);
		state.groups.clear();
		state.liveTextures.clear();
		state.faceGenTintAssignments.clear();
		return true;
	}

	bool Checkpoint()
	{
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
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

	void OnFaceGenTintAssigned(
		void* a_tintTextureSlot,
		RE::NiSourceTexture* a_texture,
		RE::BSFaceGenNiNode* a_node,
		RE::BGSHeadPart* a_headPart,
		RE::TESNPC* a_npc) noexcept
	{
		TrackerState* statePtr = nullptr;
		uint64_t sessionID = 0;
		try {
			statePtr = std::addressof(GetState());
			auto& state = *statePtr;
			if (!a_tintTextureSlot || !a_texture || !state.active.load(std::memory_order_acquire))
				return;

			sessionID = state.sessionID.load(std::memory_order_acquire);
			const uint32_t cohort = state.cohort.load(std::memory_order_acquire);
			if (!a_texture->rendererTexture || !a_texture->rendererTexture->texture)
				return;

			const auto d3dTexture = reinterpret_cast<uint64_t>(a_texture->rendererTexture->texture);
			FaceGenTintAssignment assignment{
				reinterpret_cast<uint64_t>(a_texture),
				reinterpret_cast<uint64_t>(a_tintTextureSlot) - kFaceGenTintTextureOffset,
				reinterpret_cast<uint64_t>(a_node),
				reinterpret_cast<uint64_t>(a_headPart),
				a_headPart ? a_headPart->GetFormID() : 0,
				reinterpret_cast<uint64_t>(a_npc),
				a_npc ? a_npc->GetFormID() : 0,
				a_node ? a_node->GetRuntimeData().unk15C.native_handle() : 0,
				cohort,
			};

			std::scoped_lock lock(state.mutex);
			if (state.active.load(std::memory_order_relaxed) &&
				state.sessionID.load(std::memory_order_relaxed) == sessionID &&
				state.liveTextures.contains(d3dTexture)) {
				if (state.faceGenTintAssignments.contains(d3dTexture) ||
					state.faceGenTintAssignments.size() < kMaxTrackedLiveTextures) {
					state.faceGenTintAssignments.insert_or_assign(d3dTexture, assignment);
				} else {
					state.faceGenAssignmentFailures.fetch_add(1, std::memory_order_relaxed);
				}
			}
		} catch (...) {
			if (statePtr && statePtr->active.load(std::memory_order_relaxed) &&
				(sessionID == 0 || statePtr->sessionID.load(std::memory_order_relaxed) == sessionID)) {
				statePtr->faceGenAssignmentFailures.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	nlohmann::json BuildStatus()
	{
		using json = nlohmann::json;
		if (!REL::Module::IsVR()) {
			return json{
				{ "supported", false },
				{ "error", "D3D11 texture-lifetime capture requires Skyrim VR" },
			};
		}

		// Take Skyrim's texture-list lock before the tracker mutex. A texture
		// destructor can release its D3D sentinel while removing itself from that
		// list; this ordering avoids inverting those two locks.
		const auto niSourceTextureOwnerSnapshot = CollectNiSourceTextureOwners();
		const auto& niSourceTextureOwners = niSourceTextureOwnerSnapshot.owners;
		auto& state = GetState();
		std::scoped_lock lock(state.mutex);
		std::unordered_map<DescriptorKey, uint64_t, DescriptorHash> niSourceMatchesByGroup;
		std::unordered_map<DescriptorKey, uint64_t, DescriptorHash> niSourceMatchBytesByGroup;
		json nonCurrentNiSourceMatches = json::array();
		uint64_t niSourceMatchedCount = 0;
		uint64_t niSourceMatchedEstimatedBytes = 0;
		const auto currentCohort = state.cohort.load(std::memory_order_relaxed);
		for (const auto& [textureAddress, record] : state.liveTextures) {
			const auto ownerIt = niSourceTextureOwners.find(textureAddress);
			if (ownerIt == niSourceTextureOwners.end())
				continue;
			++niSourceMatchedCount;
			niSourceMatchedEstimatedBytes += record.estimatedBytes;
			++niSourceMatchesByGroup[record.key];
			niSourceMatchBytesByGroup[record.key] += record.estimatedBytes;
			if (record.key.cohort != currentCohort) {
				json faceGenAssignment = nullptr;
				if (const auto assignmentIt = state.faceGenTintAssignments.find(textureAddress);
					assignmentIt != state.faceGenTintAssignments.end()) {
					const auto& assignment = assignmentIt->second;
					faceGenAssignment = {
						{ "sourceTexture", assignment.sourceTexture },
						{ "material", assignment.material },
						{ "node", assignment.node },
						{ "headPart", assignment.headPart },
						{ "headPartFormID", assignment.headPartFormID },
						{ "npc", assignment.npc },
						{ "npcFormID", assignment.npcFormID },
						{ "actorHandle", assignment.actorHandle },
						{ "assignmentCohort", assignment.cohort },
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
					{ "faceGenAssignment", std::move(faceGenAssignment) },
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
			{ "faceGenAssignmentFailures", state.faceGenAssignmentFailures.load(std::memory_order_relaxed) },
			{ "recordingFailures", state.recordingFailures.load(std::memory_order_relaxed) },
			{ "droppedTextureRecords", state.droppedTextureRecords.load(std::memory_order_relaxed) },
			{ "maxTrackedTextureGroups", kMaxTrackedTextureGroups },
			{ "maxTrackedLiveTextures", kMaxTrackedLiveTextures },
			{ "liveTextureRecordCount", state.liveTextures.size() },
			{ "faceGenTintAssignmentCount", state.faceGenTintAssignments.size() },
			{ "niSourceTextureResourceCount", niSourceTextureOwners.size() },
			{ "niSourceTextureOwnerRecordsDropped", niSourceTextureOwnerSnapshot.droppedOwnerCount },
			{ "niSourceTextureTraversalLimitReached", niSourceTextureOwnerSnapshot.traversalLimitReached },
			{ "niSourceTextureMatchedCount", niSourceMatchedCount },
			{ "niSourceTextureMatchedEstimatedBytes", niSourceMatchedEstimatedBytes },
			{ "nonCurrentNiSourceTextureMatches", std::move(nonCurrentNiSourceMatches) },
			{ "groupCount", groups.size() },
			{ "groups", std::move(groups) },
		};
	}
}
