#pragma once

#include "KernelChainCommandList.h"
#include "KernelPairSchedule.h"
#include "KernelReplacementSet.h"
#include "ProviderDescriptorGuardContract.h"
#include "ProviderKernelChainContract.h"
#include "Utils/CryptoHash.h"

#include <Windows.h>

#include <Psapi.h>
#include <d3d12.h>
#include <dxgi.h>
#include <nlohmann/json.hpp>
#include <nvapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <vector>

namespace NrReplay
{
	/** Pinned standalone interception with owned arguments and command-boundary flushing. */
	class ProviderKernelChainProbe
	{
		friend struct KernelChainProbeTestAccess;
		using Json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		using Launch = NvAPI_Status(__cdecl*)(ID3D12GraphicsCommandList*, const NVAPI_CU_KERNEL_LAUNCH_PARAMS*, NvU32);
		using CreateModule = NvAPI_Status(__cdecl*)(ID3D12Device*, const void*, NvU32, NVDX_ObjectHandle*);
		using CreateFunction = NvAPI_Status(__cdecl*)(ID3D12Device*, NVDX_ObjectHandle, const char*, NVDX_ObjectHandle*);
		using DestroyObject = NvAPI_Status(__cdecl*)(ID3D12Device*, NVDX_ObjectHandle);
		using QueryInterface = void*(__cdecl*)(unsigned);
		struct ModelOperations
		{
			using DeviceOwner = Microsoft::WRL::ComPtr<ID3D12Device>;
			decltype(&NvAPI_D3D12_CreateCuModule) createModule;
			decltype(&NvAPI_D3D12_CreateCuFunction) createFunction;
			DestroyObject destroyFunction, destroyModule;
			DeviceOwner RetainDevice(std::uintptr_t device) { return DeviceOwner(reinterpret_cast<ID3D12Device*>(device)); }
			std::uintptr_t DeviceIdentity(const DeviceOwner& device) const { return reinterpret_cast<std::uintptr_t>(device.Get()); }
			KernelReplacement::NativeResult CreateModule(std::uintptr_t device, const void* bytes, std::uint32_t size)
			{
				NVDX_ObjectHandle handle = nullptr;
				const auto status = createModule(reinterpret_cast<ID3D12Device*>(device), bytes, size, &handle);
				return { static_cast<int>(status), reinterpret_cast<std::uintptr_t>(handle) };
			}
			KernelReplacement::NativeResult CreateFunction(std::uintptr_t device, std::uintptr_t module, const char* name)
			{
				NVDX_ObjectHandle handle = nullptr;
				const auto status = createFunction(reinterpret_cast<ID3D12Device*>(device), reinterpret_cast<NVDX_ObjectHandle>(module), name, &handle);
				return { static_cast<int>(status), reinterpret_cast<std::uintptr_t>(handle) };
			}
			int DestroyFunction(std::uintptr_t device, std::uintptr_t handle) { return static_cast<int>(destroyFunction(reinterpret_cast<ID3D12Device*>(device), reinterpret_cast<NVDX_ObjectHandle>(handle))); }
			int DestroyModule(std::uintptr_t device, std::uintptr_t handle) { return static_cast<int>(destroyModule(reinterpret_cast<ID3D12Device*>(device), reinterpret_cast<NVDX_ObjectHandle>(handle))); }
		};
		struct CacheSlot
		{
			std::size_t rva = 0;
			unsigned interfaceId = 0;
			void** address = nullptr;
			void* original = nullptr;
			void* real = nullptr;
			void* hook = nullptr;
			HMODULE targetModule = nullptr;
			DWORD protection = 0;
			bool installed = false, uncertain = false;
		};
		struct ModuleIdentity
		{
			std::uintptr_t device = 0, source = 0, handle = 0;
			std::size_t bytes = 0;
			std::string sha256, file;
			NvAPI_Status status = NVAPI_ERROR;
			bool called = false, stable = false, fileComplete = false;
		};
		struct FunctionIdentity
		{
			std::uintptr_t device = 0, module = 0, handle = 0;
			std::size_t moduleIdentity = 0;
			std::string name;
			NvAPI_Status status = NVAPI_ERROR;
			bool called = false;
			bool replacement = false;
		};
		struct Replacement
		{
			Microsoft::WRL::ComPtr<ID3D12Device> device;
			std::vector<std::uint8_t> bytes;
			std::filesystem::path path;
			std::string manifestSha256, candidateSha256;
			NVDX_ObjectHandle module = nullptr, function = nullptr;
			std::uintptr_t moduleValue = 0, functionValue = 0;
			NvAPI_Status createModule = NVAPI_ERROR, createFunction = NVAPI_ERROR, destroyModule = NVAPI_ERROR, destroyFunction = NVAPI_ERROR;
			bool moduleCalled = false, functionCalled = false, destroyModuleCalled = false, destroyFunctionCalled = false;
			bool cleanupFailed = false;
			std::size_t attempted = 0, successful = 0;
		};
		struct Descriptor
		{
			NVAPI_CU_KERNEL_LAUNCH_PARAMS value{};
			std::vector<std::uint8_t> bytes;
			std::uintptr_t originalParams = 0;
			unsigned eye = 0, region = 0;
			std::size_t functionIdentity = SIZE_MAX;
			std::uintptr_t submittedFunction = 0;
			bool replacementApplied = false;
		};
		struct PhysicalLaunch
		{
			NVAPI_CU_KERNEL_LAUNCH_PARAMS value{};
			std::vector<std::uint8_t> bytes;
		};
		struct Event
		{
			std::string kind, reason;
			std::vector<std::size_t> descriptors;
			std::uintptr_t commandList = 0;
			std::size_t pendingBefore = 0;
			std::size_t pendingAfter = 0;
			NvAPI_Status status = NVAPI_OK;
			double apiCpu = 0;
			std::vector<PhysicalLaunch> physical;
		};
		struct ModelBatch
		{
			NVAPI_CU_KERNEL_LAUNCH_PARAMS value{};
			std::vector<std::uint8_t> packet;
			std::size_t first = 0, second = 0, binding = 0;
			std::string candidateSha256;
			NvAPI_Status status = NVAPI_ERROR;
			std::vector<NvAPI_Status> statuses;
			std::vector<unsigned> repetitions;
		};

	public:
		using RepetitionCallback = std::function<void(unsigned)>;
		ProviderKernelChainProbe(Json& receipt, std::string_view mode, std::filesystem::path identityOutput = {}, std::filesystem::path replacementManifest = {}, std::string_view pairMode = {}, std::filesystem::path modelManifest = {}, std::size_t modelBatchStages = KernelPair::kStages, unsigned scheduleRepetitions = 1, std::string_view repetitionControl = {}) :
			receipt_(receipt), group_(KernelChain::GroupMode(mode)), identityOutput_(std::move(identityOutput)), replacementManifest_(std::move(replacementManifest)), modelManifest_(std::move(modelManifest)), pairMode_(pairMode), modelBatchStages_(modelBatchStages), scheduleRepetitions_(scheduleRepetitions), repetitionControl_(repetitionControl)
		{
			KernelPair::ValidateRepetitions(scheduleRepetitions_, pairMode_, modelBatchStages_);
			KernelPair::ValidateComparison(repetitionControl_, scheduleRepetitions_, pairMode_, modelBatchStages_);
			ProviderFloor::Require(scheduleRepetitions_ == 1 || !ModelEnabled() || ModelBatchEnabled(),
				"repeated schedules do not admit a single-item model replacement");
			receipt_ = { { "requested", true }, { "mode", Mode() }, { "providerDiskSha256", ProviderFloor::kProviderSha256 },
				{ "performanceQualified", false }, { "qualityQualified", false }, { "productionPerformanceQualified", false },
				{ "inMemoryState", "not_loaded" }, { "transitions", Json::array() },
				{ "groupingScope", "original_order_between_intercepted_command_boundaries" } };
			if (PairEnabled()) {
				ProviderFloor::Require((pairMode_ == "original" || pairMode_ == "control" || pairMode_ == "layer-control" || pairMode_ == "batch" || pairMode_ == "model-batch") && !group_ && IdentityEnabled(),
					"kernel pair requires forward mode, identity capture and a supported mode");
				ProviderFloor::Require((pairMode_ == "batch") == ReplacementEnabled(), "only kernel pair batch requires a replacement manifest");
				pairOwners_ = std::make_unique<KernelPair::Owners>();
			}
			if (IdentityEnabled())
				receipt_["identityCapture"] = IdentityReceipt();
			ProviderFloor::Require(!ModelEnabled() || (!ReplacementEnabled() && !group_ && IdentityEnabled() && (pairMode_ == "original" || pairMode_ == "layer-control" || ModelBatchEnabled())),
				"model replacement requires an original or layer-control pair schedule without another replacement");
			ProviderFloor::Require(!ModelBatchEnabled() || ModelEnabled(), "model batch requires a pinned model replacement catalog");
			ProviderFloor::Require(modelBatchStages_ >= 1 && modelBatchStages_ <= KernelPair::kStages &&
									   (ModelBatchEnabled() || modelBatchStages_ == KernelPair::kStages),
				"model batch stage limit requires a valid model-batch prefix");
			if (ModelBatchEnabled())
				modelBatchOwners_ = std::make_unique<std::vector<std::vector<ModelBatch>>>();
			if (ReplacementEnabled()) {
				ProviderFloor::Require(!group_ && IdentityEnabled(), "N1 replacement requires forward mode and identity capture");
				replacementStorage_ = std::make_unique<Replacement>();
				replacement_ = replacementStorage_.get();
				receipt_[ReplacementKey()] = ReplacementReceipt();
			}
		}
		ProviderKernelChainProbe(const ProviderKernelChainProbe&) = delete;
		ProviderKernelChainProbe& operator=(const ProviderKernelChainProbe&) = delete;
		~ProviderKernelChainProbe() noexcept { (void)Restore(false); }

		/** Hooks only the pinned provider's data cache in this process, before evaluation. */
		void Apply(const std::filesystem::path& provider, std::string_view diskHash)
		{
			using namespace ProviderFloor;
			std::lock_guard lock(ownerMutex_);
			Require(!module_ && !owner_ && !abandoned_, "kernel-chain hook ownership unavailable");
			caches_ = {};
			const auto canonicalHash = CanonicalSha256(diskHash);
			Require(canonicalHash == kProviderSha256, "kernel-chain provider disk hash mismatch");
			try {
				Require(GetModuleHandleExW(0, provider.c_str(), &module_) != 0, "kernel-chain provider is not loaded");
				std::vector<wchar_t> path(32768);
				const auto size = GetModuleFileNameW(module_, path.data(), static_cast<DWORD>(path.size()));
				Require(size && size < path.size() && std::filesystem::equivalent(provider, std::filesystem::path(path.data())),
					"kernel-chain provider path mismatch");
				MODULEINFO info{};
				Require(GetModuleInformation(GetCurrentProcess(), module_, &info, sizeof(info)) && info.SizeOfImage == kModuleBytes,
					"kernel-chain provider extent mismatch");
				auto* base = static_cast<std::uint8_t*>(info.lpBaseOfDll);
				base_ = base;
				ValidateReadable(base + kCodeRva, kCodeBytes, base, true);
				const auto codeHash = Hash({ base + kCodeRva, kCodeBytes });
				Require(codeHash == kCodeSha256, "kernel-chain provider code hash mismatch");
				Require(GetModuleHandleExW(0, L"nvapi64.dll", &nvapiModule_) != 0, "kernel-chain NvAPI is not already loaded");
				const auto query = reinterpret_cast<QueryInterface>(GetProcAddress(nvapiModule_, "nvapi_QueryInterface"));
				Require(query != nullptr, "kernel-chain NvAPI query interface unavailable");
				PrepareSlot(caches_[0], KernelChain::kCacheRva, KernelChain::kInterfaceId, reinterpret_cast<void*>(&Intercept), query);
				real_ = reinterpret_cast<Launch>(caches_[0].real);
				if (IdentityEnabled()) {
					PrepareIdentityOutput();
					PrepareSlot(caches_[1], KernelChain::kCreateModuleCacheRva, KernelChain::kCreateModuleInterfaceId,
						reinterpret_cast<void*>(&InterceptModule), query);
					PrepareSlot(caches_[2], KernelChain::kCreateFunctionCacheRva, KernelChain::kCreateFunctionInterfaceId,
						reinterpret_cast<void*>(&InterceptFunction), query);
					realCreateModule_ = reinterpret_cast<CreateModule>(caches_[1].real);
					realCreateFunction_ = reinterpret_cast<CreateFunction>(caches_[2].real);
				}
				if (ReplacementEnabled() || ModelEnabled()) {
					if (ReplacementEnabled())
						PrepareReplacement();
					PrepareTarget(destroyTargets_[0], KernelChain::kDestroyFunctionInterfaceId, query);
					PrepareTarget(destroyTargets_[1], KernelChain::kDestroyModuleInterfaceId, query);
					realDestroyFunction_ = reinterpret_cast<DestroyObject>(destroyTargets_[0].real);
					realDestroyModule_ = reinterpret_cast<DestroyObject>(destroyTargets_[1].real);
					if (ModelEnabled()) {
						constexpr std::array<std::string_view, 2> singleCatalogs{
							"c914eda8a1c46d87d91c4413df8b0be1bf3706ec64076a7921184aabd8c4a3ee",
							"a8d6d4ebddadfa2ddfef5fa960a7a0713403ceceb7324fde3045eb52871bf281"
						};
						constexpr std::array<std::string_view, 1> pairCatalogs{
							"2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e"
						};
						modelReplacement_ = std::make_unique<KernelReplacement::Set<ModelOperations>>(modelManifest_,
							ModelBatchEnabled() ? std::span<const std::string_view>(pairCatalogs) : std::span<const std::string_view>(singleCatalogs), ModelBatchEnabled() ? 2 : 1,
							ModelOperations{ realCreateModule_, realCreateFunction_, realDestroyFunction_, realDestroyModule_ });
					}
				}
				if (PairEnabled()) {
					const std::array<void*, 5> hooks{ reinterpret_cast<void*>(&InterceptMerged), reinterpret_cast<void*>(&InterceptIndependent),
						reinterpret_cast<void*>(&InterceptTexture), reinterpret_cast<void*>(&InterceptSurface), reinterpret_cast<void*>(&InterceptCaptureUav) };
					for (std::size_t i = 0; i < hooks.size(); ++i)
						PrepareSlot(caches_[3 + i], DescriptorGuard::kSlots[i].cacheRva, DescriptorGuard::kSlots[i].interfaceId, hooks[i], query);
				}
				thread_ = GetCurrentThreadId();
				owner_ = this;
				for (auto& cache : caches_)
					if (cache.address)
						Exchange(cache, cache.original, cache.hook);
				installed_ = true;
				receipt_["inMemoryState"] = "kernel_chain_hook_installed";
				receipt_["transitions"].push_back({ { "state", "applied_own_replay_process_only" }, { "providerDiskSha256", canonicalHash },
					{ "codeSha256", codeHash }, { "cacheRva", KernelChain::kCacheRva }, { "originalCachePointer", Pointer(caches_[0].original) },
					{ "realLaunchPointer", Pointer(reinterpret_cast<void*>(real_)) }, { "mode", Mode() }, { "cacheSlots", CacheReceipt() } });
			} catch (...) {
				if (RestoreCaches()) {
					owner_ = nullptr;
					(void)ReleaseModules();
				}
				throw;
			}
		}

		void BeginSample(unsigned iteration, bool warmup)
		{
			ProviderFloor::Require(installed_ && pending_.empty() && !evaluating_, "kernel-chain sample started with unfinished work");
			ThrowIfFailed();
			if (modelReplacement_ && (!ModelBatchEnabled() || !warmup)) {
				ObserveModelBoundary([&] { modelReplacement_->BeginSample(); });
				ThrowIfFailed();
			}
			modelBatches_ = nullptr;
			if (ModelBatchEnabled() && !warmup) {
				ProviderFloor::Require(modelBatchOwners_->size() < KernelPair::kMaximumRetainedSamples, "model batch retained sample bound exceeded");
				modelBatches_ = &modelBatchOwners_->emplace_back();
			}
			if (PairEnabled()) {
				ProviderFloor::Require(pairOwners_->samples.size() < KernelPair::kMaximumRetainedSamples, "kernel pair retained sample bound exceeded");
				descriptorGuard_.BeginSample();
				pairOwners_->samples.push_back(std::make_unique<KernelPair::Sample>());
				pairSample_ = pairOwners_->samples.back().get();
			}
			iteration_ = iteration;
			warmup_ = warmup;
			descriptors_.clear();
			events_.clear();
			parameterBytes_ = observedCalls_ = submittedCalls_ = submittedDescriptors_ = multiCalls_ = maxSubmitted_ = crossRegionCalls_ = maxRegions_ = 0;
			apiCpu_ = hookCpu_ = 0;
			replacementAttempts_ = replacementSuccesses_ = 0;
			proxy_ = realList_ = nullptr;
			sample_ = true;
		}
		void BeginEvaluation(ID3D12GraphicsCommandList* proxy, ID3D12GraphicsCommandList* real, unsigned eye, unsigned region)
		{
			ProviderFloor::Require(sample_ && !evaluating_ && proxy && real, "kernel-chain evaluation boundary invalid");
			if (realList_ && realList_ != real)
				ProviderFloor::Require(BeforeCommand("command_list_change"), "kernel-chain command-list change flush failed");
			ThrowIfFailed();
			proxy_ = proxy;
			realList_ = real;
			eye_ = eye;
			region_ = region;
			if (PairRecording())
				ProviderFloor::Require(eye < 2 && region < 2 && eye * 2 + region == pairSample_->completed,
					"kernel pair evaluation order must be eye0/ROI0, eye0/ROI1, eye1/ROI0, eye1/ROI1");
			evaluating_ = true;
		}
		void EndEvaluation()
		{
			try {
				ThrowIfFailed();
				ProviderFloor::Require(evaluating_, "kernel-chain evaluation ended without a matching beginning");
				if (PairRecording()) {
					auto& region = PairRegion();
					ProviderFloor::Require(region.descriptors.size() == KernelPair::kStages &&
											   HashText(region.stageSignature) == KernelPair::kStageSha256 && HashText(region.commandSignature) == KernelPair::kCommandSha256,
						"kernel pair stage or command schedule differs from the pinned live trace");
					ProviderFloor::Require(descriptorGuard_.Healthy() && descriptorGuard_.WarmupObserved(),
						"kernel pair descriptor cache miss or missing observer warmup proof");
					pairSample_->descriptorCacheConfirmed.at(eye_ * 2 + region_) = true;
					region.complete = true;
					++pairSample_->completed;
				}
				evaluating_ = false;
			} catch (const std::exception& error) {
				RecordFailure(error.what());
				throw;
			}
		}
		/** The proxy must suppress the command when this returns false. */
		bool BeforeCommand(std::string_view reason) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				const auto start = Clock::now();
				const auto previousApi = apiCpu_;
				try {
					ProviderFloor::Require(GetCurrentThreadId() == thread_, "kernel-chain command escaped the owned recording thread");
					ThrowIfFailed();
					const auto pending = pending_.size();
					if (PairRecording()) {
						ProviderFloor::Require((evaluating_ && (reason == "ResourceBarrier" || reason == "SetDescriptorHeaps")) ||
												   (!evaluating_ && reason == "command_list_end"),
							"kernel pair encountered an unmodeled deferred command");
					} else
						Flush(reason);
					AddEvent({ "boundary", std::string(reason), {}, Pointer(realList_), pending });
				} catch (const std::exception& error) {
					RecordFailure(error.what());
				} catch (...) {
					RecordFailure("unknown kernel-chain command boundary failure");
				}
				hookCpu_ += Elapsed(start) - (apiCpu_ - previousApi);
				return Healthy();
			} catch (...) {
				return false;
			}
		}
		/** Repeated steady schedules retain their packets and run callbacks on the real recording list. */
		void FinishCommandList(const RepetitionCallback& begin = {}, const RepetitionCallback& end = {})
		{
			ProviderFloor::Require(!evaluating_, "kernel-chain command list ended during evaluation");
			try {
				ProviderFloor::Require(static_cast<bool>(begin) == static_cast<bool>(end) &&
										   (!(begin || end) || scheduleRepetitions_ > 1) &&
										   (warmup_ || scheduleRepetitions_ == 1 || (begin && end)),
					"repeated steady schedule requires paired callbacks and count greater than one");
			} catch (const std::exception& error) {
				RecordFailure(error.what());
				throw;
			}
			(void)BeforeCommand("command_list_end");
			ThrowIfFailed();
			if (PairRecording()) {
				try {
					SubmitPair(begin, end);
				} catch (const std::exception& error) {
					RecordFailure(error.what());
					throw;
				} catch (...) {
					RecordFailure("unknown kernel pair schedule failure");
					throw;
				}
			}
			ProviderFloor::Require(!descriptors_.empty(), "kernel-chain hook observed no native descriptors");
			if (modelReplacement_) {
				ObserveModelBoundary([&] {
					if (ModelBatchEnabled() && warmup_)
						modelReplacement_->RequirePrepared();
					else if (ModelBatchEnabled()) {
						std::array<std::size_t, KernelReplacement::kFunctionCount> expected{};
						for (const auto& batch : *modelBatches_)
							expected.at(batch.binding) += ComparisonEnabled() ? 2 : scheduleRepetitions_;
						modelReplacement_->RequireSubmissionCounts(expected);
					} else
						modelReplacement_->RequireCompleteCoverage();
				});
				ThrowIfFailed();
			}
			if (ReplacementEnabled() && !warmup_ && replacementSuccesses_ == 0) {
				RecordFailure("N1 replacement target was not successfully submitted in the steady sample");
				ThrowIfFailed();
			}
			sample_ = false;
		}
		[[nodiscard]] bool Healthy() const noexcept { return error_[0] == '\0'; }
		void RecordFailure(std::string_view reason) noexcept
		{
			if (!Healthy())
				return;
			const auto length = std::min(reason.size(), error_.size() - 1);
			std::memcpy(error_.data(), reason.data(), length);
			error_[length] = '\0';
		}
		void ThrowIfFailed() const { ProviderFloor::Require(Healthy(), error_.data()); }

		/** Records the generated replay runtime's exact null allocation-callback check. */
		void ConfirmDefaultAllocationParameters()
		{
			ProviderFloor::Require(PairEnabled() && warmup_ && evaluating_ && eye_ < 2 && region_ < 2 &&
									   GetCurrentThreadId() == thread_,
				"kernel pair allocation confirmation is outside feature creation");
			defaultAllocationConfirmed_[eye_ * 2 + region_] = true;
		}
		using CommandDisposition = KernelChainCommandList::CommandDisposition;
		CommandDisposition BarrierDisposition(UINT count, const D3D12_RESOURCE_BARRIER* values) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				ThrowIfFailed();
				ProviderFloor::Require(PairEnabled() && evaluating_ && GetCurrentThreadId() == thread_ && count && count <= 4096,
					"kernel pair barrier capture boundary invalid");
				ValidateReadable(values, sizeof(*values) * count);
				KernelPair::Command command;
				command.kind = KernelPair::Kind::Barrier;
				command.barriers.assign(values, values + count);
				std::string signature = "B";
				for (const auto& barrier : command.barriers) {
					ProviderFloor::Require(barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE, "kernel pair rejects split barriers");
					ID3D12Resource* resource = nullptr;
					if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
						resource = barrier.UAV.pResource;
					else {
						ProviderFloor::Require(warmup_ && barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
							"kernel pair steady capture requires UAV barriers only");
						resource = barrier.Transition.pResource;
					}
					RetainPairResource(resource);
					if (signature.size() > 1)
						signature += ',';
					signature += std::to_string(barrier.Type) + ":" + std::to_string(barrier.Flags) + ":" + (resource ? "1" : "0");
				}
				if (!PairRecording())
					return CommandDisposition::Forward;
				RecordPairCommand(std::move(command), signature + '\n');
				return CommandDisposition::Recorded;
			} catch (const std::exception& error) {
				RecordFailure(error.what());
			} catch (...) {
				RecordFailure("unknown kernel pair barrier capture failure");
			}
			return CommandDisposition::Reject;
		}
		CommandDisposition HeapDisposition(UINT count, ID3D12DescriptorHeap* const* values) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				ThrowIfFailed();
				ProviderFloor::Require(PairEnabled() && evaluating_ && GetCurrentThreadId() == thread_ && count && count <= 2,
					"kernel pair heap capture boundary invalid");
				ValidateReadable(values, sizeof(*values) * count);
				KernelPair::Command command;
				command.kind = KernelPair::Kind::Heaps;
				std::string signature = "H";
				for (UINT i = 0; i < count; ++i) {
					ProviderFloor::Require(values[i] != nullptr, "kernel pair descriptor heap is null");
					const auto desc = values[i]->GetDesc();
					ProviderFloor::Require(desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE && desc.NumDescriptors <= 4096 &&
											   (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
						"kernel pair descriptor heap outside bounded contract");
					if (std::none_of(pairOwners_->heaps.begin(), pairOwners_->heaps.end(), [&](const auto& heap) { return heap.Get() == values[i]; })) {
						ProviderFloor::Require(pairOwners_->heaps.size() < 16, "kernel pair retained heap bound exceeded");
						pairOwners_->heaps.emplace_back(values[i]);
					}
					command.heaps.push_back(values[i]);
					if (i)
						signature += ',';
					signature += std::to_string(desc.Type) + ":" + std::to_string(desc.NumDescriptors) + ":" +
					             std::to_string(desc.Flags) + ":" + std::to_string(desc.NodeMask);
				}
				if (!PairRecording())
					return CommandDisposition::Forward;
				RecordPairCommand(std::move(command), signature + '\n');
				return CommandDisposition::Recorded;
			} catch (const std::exception& error) {
				RecordFailure(error.what());
			} catch (...) {
				RecordFailure("unknown kernel pair heap capture failure");
			}
			return CommandDisposition::Reject;
		}

		/** Serializes after submission timing; parameter copies remain owned until the next sample. */
		Json SampleReceipt() const
		{
			Json values = Json::array(), reasons = Json::object();
			for (const auto& event : events_) {
				Json packets = Json::array();
				for (const auto id : event.descriptors) {
					const auto& captured = descriptors_.at(id);
					const auto& d = captured.value;
					packets.push_back({ { "descriptorId", id }, { "eye", captured.eye }, { "region", captured.region },
						{ "function", Pointer(d.hFunction) }, { "grid", { d.gridDim.x, d.gridDim.y, d.gridDim.z } },
						{ "block", { d.blockDim.x, d.blockDim.y, d.blockDim.z } }, { "dynamicSharedMemoryBytes", d.dynSharedMemBytes },
						{ "originalParamsPointer", captured.originalParams }, { "paramSize", d.paramSize },
						{ "paramsHex", Hex(captured.bytes) }, { "paramsSha256", Hash(captured.bytes) } });
					if (IdentityEnabled()) {
						const auto& function = functions_.at(captured.functionIdentity);
						packets.back()["functionIdentity"] = captured.functionIdentity;
						packets.back()["functionName"] = function.name;
						packets.back()["moduleIdentity"] = function.moduleIdentity;
						packets.back()["moduleBlobSha256"] = modules_.at(function.moduleIdentity).sha256;
					}
					if (ReplacementEnabled() || ModelEnabled()) {
						packets.back()["originalFunction"] = Pointer(d.hFunction);
						packets.back()["submittedFunction"] = captured.submittedFunction;
						packets.back()["replacementApplied"] = captured.replacementApplied;
					}
				}
				values.push_back({ { "ordinal", values.size() }, { "kind", event.kind }, { "reason", event.reason },
					{ "commandList", event.commandList }, { "descriptorIds", event.descriptors }, { "descriptors", packets },
					{ "pendingDescriptorsBefore", event.pendingBefore }, { "pendingDescriptorsAfter", event.pendingAfter },
					{ "status", static_cast<int>(event.status) }, { "apiCpuMicroseconds", event.apiCpu } });
				if (ComparisonEnabled() && event.kind == "submitted") {
					Json physical = Json::array();
					for (const auto& submitted : event.physical) {
						const auto& d = submitted.value;
						physical.push_back({ { "function", Pointer(d.hFunction) }, { "grid", { d.gridDim.x, d.gridDim.y, d.gridDim.z } },
							{ "block", { d.blockDim.x, d.blockDim.y, d.blockDim.z } }, { "dynamicSharedMemoryBytes", d.dynSharedMemBytes },
							{ "paramSize", d.paramSize }, { "paramsHex", Hex(submitted.bytes) }, { "paramsSha256", Hash(submitted.bytes) },
							{ "status", static_cast<int>(event.status) } });
					}
					values.back()["physicalLaunches"] = std::move(physical);
				}
				if (event.kind == "submitted")
					reasons[event.reason] = reasons.value(event.reason, 0u) + 1;
			}
			Json result{ { "mode", Mode() }, { "iteration", iteration_ }, { "warmup", warmup_ }, { "failed", !Healthy() },
				{ "reason", error_.data() }, { "observedCalls", observedCalls_ }, { "observedDescriptors", descriptors_.size() },
				{ "submittedCalls", submittedCalls_ }, { "submittedDescriptors", submittedDescriptors_ },
				{ "multiDescriptorCalls", multiCalls_ }, { "maxSubmittedDescriptors", maxSubmitted_ },
				{ "crossRegionCalls", crossRegionCalls_ }, { "maxRegionsPerSubmission", maxRegions_ },
				{ "pendingDescriptors", pending_.size() }, { "parameterBytes", parameterBytes_ }, { "flushReasons", reasons },
				{ "apiCpuMicroseconds", apiCpu_ }, { "hookCpuMicroseconds", hookCpu_ }, { "events", values } };
			if (IdentityEnabled()) {
				result["identityCapture"] = IdentityReceipt();
				result["mappingComplete"] = !descriptors_.empty() && std::all_of(descriptors_.begin(), descriptors_.end(),
																		 [](const auto& descriptor) { return descriptor.functionIdentity != SIZE_MAX; });
				receipt_["identityCapture"] = result["identityCapture"];
			}
			if (ReplacementEnabled()) {
				result[ReplacementKey()] = ReplacementReceipt();
				result["replacementAttemptedDescriptors"] = replacementAttempts_;
				result["replacementSuccessfulDescriptors"] = replacementSuccesses_;
				result["replacementTargetExecuted"] = replacementSuccesses_ != 0;
				receipt_[ReplacementKey()] = result[ReplacementKey()];
			}
			if (modelReplacement_) {
				result["modelReplacement"] = modelReplacement_->Receipt();
				receipt_["modelReplacement"] = result["modelReplacement"];
			}
			if (PairEnabled()) {
				result["kernelPair"] = PairReceipt();
				receipt_["kernelPair"] = result["kernelPair"];
			}
			return result;
		}

		/** Restores only after retirement; failed cleanup retains all hook target modules. */
		bool Restore(bool gpuIdle) noexcept
		{
			std::lock_guard lock(ownerMutex_);
			if (!module_)
				return !retirementFailed_;
			try {
				ProviderFloor::Require(gpuIdle && pending_.empty(), "kernel-chain restoration lacks retirement or pending-work proof");
				ProviderFloor::Require(RestoreCaches(), "kernel-chain cache restoration not proven");
				ValidateReadable(base_ + ProviderFloor::kCodeRva, ProviderFloor::kCodeBytes, base_, true);
				const auto codeHash = Hash({ base_ + ProviderFloor::kCodeRva, ProviderFloor::kCodeBytes });
				ProviderFloor::Require(codeHash == ProviderFloor::kCodeSha256, "kernel-chain restored provider code hash mismatch");
				ProviderFloor::Require(RetireReplacement(), "N1 replacement private-handle retirement failed");
				if (modelReplacement_)
					ProviderFloor::Require(modelReplacement_->Retire(true, true), "model replacement private-handle retirement failed");
				installed_ = false;
				owner_ = nullptr;
				if (IdentityEnabled())
					receipt_["identityCapture"] = IdentityReceipt();
				if (ReplacementEnabled())
					receipt_[ReplacementKey()] = ReplacementReceipt();
				if (modelReplacement_)
					receipt_["modelReplacement"] = modelReplacement_->Receipt();
				ProviderFloor::Require(ReleaseModules(), "kernel-chain module reference release failed");
				receipt_["inMemoryState"] = "original_provider_cache";
				receipt_["transitions"].push_back({ { "state", "restored" }, { "gpuIdleProven", true }, { "originalProtectionRestored", true },
					{ "restoredCodeSha256", codeHash }, { "restoredCachePointer", Pointer(caches_[0].original) }, { "cacheSlots", CacheReceipt() } });
				if (ReplacementEnabled() && replacement_->successful == 0) {
					RecordFailure("N1 replacement target was never successfully submitted");
					receipt_[ReplacementKey()]["experimentFailed"] = true;
					receipt_[ReplacementKey()]["reason"] = error_.data();
				}
				return true;
			} catch (const std::exception& error) {
				RecordFailure(error.what());
				try {
					if (ReplacementEnabled())
						receipt_[ReplacementKey()] = ReplacementReceipt();
					if (modelReplacement_) {
						(void)modelReplacement_->Retire(false, false);
						receipt_["modelReplacement"] = modelReplacement_->Receipt();
					}
					receipt_["inMemoryState"] = "restoration_unproven_modules_retained_until_exit";
					receipt_["transitions"].push_back({ { "state", "restore_failed" }, { "reason", error.what() }, { "gpuIdleProven", gpuIdle } });
				} catch (...) {
					std::fputs("Kernel-chain restoration receipt failed\n", stderr);
				}
				owner_ = nullptr;
				abandoned_ = true;
				retirementFailed_ = true;
				(void)descriptorStorage_.release();
				(void)replacementStorage_.release();
				(void)pairOwners_.release();
				(void)modelBatchOwners_.release();
				module_ = nvapiModule_ = nullptr;
				for (auto& cache : caches_)
					cache.targetModule = nullptr;
				for (auto& target : destroyTargets_)
					target.targetModule = nullptr;
				return false;
			}
		}

	private:
		Json& receipt_;
		bool group_, installed_ = false, evaluating_ = false, sample_ = false, warmup_ = false, retirementFailed_ = false;
		HMODULE module_ = nullptr, nvapiModule_ = nullptr;
		std::uint8_t* base_ = nullptr;
		std::array<CacheSlot, 3 + DescriptorGuard::kSlots.size()> caches_{};
		std::array<CacheSlot, 2> destroyTargets_{};
		Launch real_ = nullptr;
		CreateModule realCreateModule_ = nullptr;
		CreateFunction realCreateFunction_ = nullptr;
		DestroyObject realDestroyFunction_ = nullptr, realDestroyModule_ = nullptr;
		std::filesystem::path identityOutput_;
		std::filesystem::path replacementManifest_, modelManifest_;
		std::unique_ptr<KernelReplacement::Set<ModelOperations>> modelReplacement_;
		std::unique_ptr<std::vector<std::vector<ModelBatch>>> modelBatchOwners_;
		std::vector<ModelBatch>* modelBatches_ = nullptr;
		std::string pairMode_;
		std::size_t modelBatchStages_ = KernelPair::kStages;
		unsigned scheduleRepetitions_ = 1;
		std::string repetitionControl_;
		std::unique_ptr<KernelPair::Owners> pairOwners_;
		KernelPair::Sample* pairSample_ = nullptr;
		std::array<bool, 4> defaultAllocationConfirmed_{};
		DescriptorGuard::Counters descriptorGuard_;
		std::array<NvAPI_Status, DescriptorGuard::kSlots.size()> descriptorStatuses_{};
		bool PairEnabled() const noexcept { return !pairMode_.empty(); }
		bool ModelEnabled() const noexcept { return !modelManifest_.empty(); }
		bool ModelBatchEnabled() const noexcept { return pairMode_ == "model-batch"; }
		bool ComparisonEnabled() const noexcept { return !repetitionControl_.empty(); }
		template <class Callback>
		void ObserveModelBoundary(Callback&& callback) noexcept
		{
			try {
				callback();
			} catch (const std::exception& error) {
				RecordFailure(error.what());
			} catch (...) {
				RecordFailure("unknown model replacement observation failure");
			}
		}
		bool PairRecording() const noexcept { return PairEnabled() && sample_ && !warmup_; }
		const char* ReplacementKey() const noexcept { return PairEnabled() ? "n2Replacement" : "n1Replacement"; }
		bool CandidateAdmitted(std::string_view hash) const noexcept { return KernelChain::ReplacementCandidateAdmitted(hash, PairEnabled()); }
		static std::string HashText(std::string_view value) { return Hash({ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() }); }
		KernelPair::Region& PairRegion() { return pairSample_->regions.at(eye_ * 2 + region_); }
		void RetainPairResource(ID3D12Resource* resource)
		{
			if (!resource || std::any_of(pairOwners_->resources.begin(), pairOwners_->resources.end(),
								 [&](const auto& item) { return item.owner.Get() == resource; }))
				return;
			ProviderFloor::Require(pairOwners_->resources.size() < 128, "kernel pair retained resource bound exceeded");
			KernelPair::Resource item;
			item.owner = resource;
			item.desc = resource->GetDesc();
			if (item.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				item.base = resource->GetGPUVirtualAddress();
				ProviderFloor::Require(item.base && item.desc.Width && item.desc.Width <= UINT64_MAX - item.base &&
										   SUCCEEDED(resource->GetHeapProperties(&item.properties, &item.flags)) && item.properties.Type == D3D12_HEAP_TYPE_DEFAULT &&
										   item.flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
					"kernel pair buffer allocation is outside default committed-resource contract");
				item.end = item.base + item.desc.Width;
			}
			pairOwners_->resources.push_back(std::move(item));
		}
		void RecordPairCommand(KernelPair::Command command, std::string_view signature)
		{
			auto& region = PairRegion();
			ProviderFloor::Require(region.commands.size() < 256 && !region.complete, "kernel pair command budget or boundary invalid");
			region.commandSignature += signature;
			region.commands.push_back(std::move(command));
		}
		void RecordPairLaunch(std::size_t id)
		{
			auto& region = PairRegion();
			ProviderFloor::Require(region.descriptors.size() < KernelPair::kStages, "kernel pair has too many stages");
			const auto& captured = descriptors_.at(id);
			const auto& d = captured.value;
			const auto& function = functions_.at(captured.functionIdentity);
			std::string signature = modules_.at(function.moduleIdentity).sha256 + "|" + function.name;
			for (const auto value : { d.gridDim.x, d.gridDim.y, d.gridDim.z, d.blockDim.x, d.blockDim.y, d.blockDim.z, d.dynSharedMemBytes, d.paramSize })
				signature += '|' + std::to_string(value);
			region.stageSignature += signature + '\n';
			if (region.descriptors.size() == KernelPair::kTargetStage)
				region.target = region.commands.size();
			region.descriptors.push_back(id);
			KernelPair::Command command;
			command.kind = KernelPair::Kind::Launch;
			command.descriptor = id;
			RecordPairCommand(std::move(command), "L\n");
		}
		void ValidatePairDependencies()
		{
			using ProviderFloor::Require;
			Require(std::all_of(defaultAllocationConfirmed_.begin(), defaultAllocationConfirmed_.end(), [](bool value) { return value; }),
				"kernel pair lacks exact default allocation parameter proof for every native feature");
			Require(std::all_of(pairSample_->descriptorCacheConfirmed.begin(), pairSample_->descriptorCacheConfirmed.end(), [](bool value) { return value; }),
				"kernel pair lacks descriptor-cache immutability proof for every deferred evaluation");
			std::array<std::size_t, 4> scratch{}, weights{};
			std::uintptr_t device = 0;
			KernelPair::ValidateHeapBindings(*pairSample_);
			for (std::size_t i = 0; i < KernelPair::kRegions; ++i) {
				const auto& region = pairSample_->regions[i];
				const auto& target = descriptors_.at(region.descriptors.at(KernelPair::kTargetStage));
				const auto& d = target.value;
				const auto& function = functions_.at(target.functionIdentity);
				Require(function.name == KernelChain::kReplacementEntry && modules_.at(function.moduleIdentity).sha256 == KernelChain::kReplacementOriginalModuleSha256 &&
							d.paramSize == 96 && d.gridDim.x == 20 && d.gridDim.y == 20 && d.gridDim.z == 1 &&
							d.blockDim.x == 32 && d.blockDim.y == 1 && d.blockDim.z == 1 && d.dynSharedMemBytes == 0,
					"kernel pair original target launch is outside the pinned N2 ABI");
				if (!device)
					device = function.device;
				Require(function.device == device && device, "kernel pair target native devices differ");
				const auto& packet = target.bytes;
				const auto read64 = [&](std::size_t offset) { return KernelPair::Read<std::uint64_t>(packet, offset); };
				Require(KernelPair::Read<std::uint32_t>(packet, 0x18) == 160 && KernelPair::Read<std::uint32_t>(packet, 0x1c) == 160 &&
							read64(0x20) == 0 && read64(0x28) == 0 && read64(0x30) == 0 && read64(0x40) == 0 &&
							read64(0x48) == 0 && read64(0x50) == 0 && read64(0x58) == 0,
					"kernel pair target packet has unsupported extents or optional context");
				const auto& pre = descriptors_.at(region.descriptors[2]).bytes;
				const auto& clear = descriptors_.at(region.descriptors[1]).bytes;
				const auto& next = descriptors_.at(region.descriptors[4]).bytes;
				Require(KernelPair::Read<std::uint32_t>(pre, 0xc8) == 0 && KernelPair::Read<std::uint64_t>(pre, 0xf8) == read64(0) &&
							KernelPair::Read<std::uint64_t>(clear, 0) == read64(0x38) && KernelPair::Read<std::uint32_t>(clear, 8) == 0x5b00 &&
							KernelPair::Read<std::uint64_t>(next, 0) == read64(8) && KernelPair::Read<std::uint64_t>(next, 0x28) == read64(0x38),
					"kernel pair producer, completion clear or consumer dependencies differ");
				scratch[i] = KernelPair::FindBuffer(*pairOwners_, read64(0), 160 * 160 * 32);
				Require(KernelPair::FindBuffer(*pairOwners_, read64(8), 160 * 160 * 32) == scratch[i] &&
							KernelPair::FindBuffer(*pairOwners_, read64(0x38), 0x16c00) == scratch[i],
					"kernel pair tensor and completion ownership is not one private scratch allocation");
				weights[i] = KernelPair::FindBuffer(*pairOwners_, read64(0x10), 1);
				Require(weights[i] != scratch[i], "kernel pair weights alias writable scratch");
				for (const auto index : { scratch[i], weights[i] }) {
					Microsoft::WRL::ComPtr<ID3D12Device> ownerDevice;
					Require(SUCCEEDED(pairOwners_->resources[index].owner->GetDevice(IID_PPV_ARGS(&ownerDevice))) && Pointer(ownerDevice.Get()) == device,
						"kernel pair packet resource device differs from native function device");
				}
			}
			for (std::size_t i = 0; i < scratch.size(); ++i)
				for (std::size_t j = 0; j < scratch.size(); ++j) {
					const auto& a = pairOwners_->resources[scratch[i]];
					const auto& b = pairOwners_->resources[j == i ? weights[j] : scratch[j]];
					Require(a.end <= b.base || b.end <= a.base, "kernel pair private scratch allocations overlap");
					const auto& weight = pairOwners_->resources[weights[j]];
					Require(a.end <= weight.base || weight.end <= a.base, "kernel pair writable scratch overlaps another weight allocation");
				}
			if (pairMode_ == "batch")
				Require(replacement_ && replacement_->function && !replacement_->cleanupFailed && CandidateAdmitted(replacement_->candidateSha256),
					"kernel pair N2 function ownership is unavailable");
			if (ModelBatchEnabled())
				Require(modelReplacement_ && modelReplacement_->Healthy(), "model batch replacement ownership is unavailable");
		}
		void PrepareModelBatches()
		{
			using ProviderFloor::Require;
			Require(modelBatches_ && modelBatches_->empty() && modelReplacement_, "model batch preparation ownership invalid");
			modelReplacement_->RequirePrepared();
			modelBatches_->reserve(modelBatchStages_ * 2);
			for (const auto& operation : pairSample_->order) {
				if (operation.kind != KernelPair::Kind::Batch)
					continue;
				const auto first = pairSample_->regions.at(operation.region).commands.at(operation.command).descriptor;
				const auto second = pairSample_->regions.at(operation.secondRegion).commands.at(operation.secondCommand).descriptor;
				const auto& a = descriptors_.at(first);
				const auto& b = descriptors_.at(second);
				Require(a.value.gridDim.x == b.value.gridDim.x && a.value.gridDim.y == b.value.gridDim.y && a.value.gridDim.z == b.value.gridDim.z &&
							a.value.blockDim.x == b.value.blockDim.x && a.value.blockDim.y == b.value.blockDim.y && a.value.blockDim.z == b.value.blockDim.z &&
							a.value.dynSharedMemBytes == b.value.dynSharedMemBytes && a.value.paramSize == b.value.paramSize,
					"model batch original launch geometry differs");
				const auto resolve = [&](const Descriptor& descriptor) {
					const auto& function = functions_.at(descriptor.functionIdentity);
					return modelReplacement_->Resolve(function.device, function.handle, modules_.at(function.moduleIdentity).sha256,
						function.name, descriptor.value.paramSize, descriptor.value.gridDim.z);
				};
				const auto binding = resolve(a);
				const auto other = resolve(b);
				Require(binding.batchCount == 2 && binding.index == other.index && binding.function == other.function &&
							a.bytes.size() == binding.paramSize && b.bytes.size() == binding.paramSize && binding.packetStride >= binding.paramSize &&
							binding.candidateParameterBytes == 2 * binding.packetStride && binding.gridZ <= 65535 / 2,
					"model batch private entry or parameter layout differs");
				ModelBatch batch;
				batch.value = a.value;
				batch.value.hFunction = reinterpret_cast<NVDX_ObjectHandle>(binding.function);
				batch.value.gridDim.z *= 2;
				batch.value.paramSize = binding.candidateParameterBytes;
				batch.packet.resize(binding.candidateParameterBytes, 0);
				std::copy(a.bytes.begin(), a.bytes.end(), batch.packet.begin());
				std::copy(b.bytes.begin(), b.bytes.end(), batch.packet.begin() + binding.packetStride);
				batch.first = first;
				batch.second = second;
				batch.binding = binding.index;
				batch.candidateSha256 = binding.candidateSha256;
				batch.statuses.reserve(scheduleRepetitions_);
				batch.repetitions.reserve(scheduleRepetitions_);
				modelBatches_->push_back(std::move(batch));
			}
			Require(modelBatches_->size() == modelBatchStages_ * 2, "model batch plan does not cover its complete stage prefix");
		}
		void SubmitModelBatch(std::size_t index, std::size_t first, std::size_t second, unsigned repetition, unsigned batchExecution)
		{
			auto& batch = modelBatches_->at(index);
			ProviderFloor::Require(repetition < scheduleRepetitions_ && batch.statuses.size() == batchExecution &&
									   batch.repetitions.size() == batchExecution && (batch.repetitions.empty() || batch.repetitions.back() < repetition) &&
									   (!ComparisonEnabled() || KernelPair::ComparisonBatch(iteration_, repetition)) &&
									   batch.first == first && batch.second == second,
				"model batch submission order differs from admission");
			batch.value.pParams = batch.packet.data();
			for (const auto id : { first, second }) {
				descriptors_[id].replacementApplied = true;
				descriptors_[id].submittedFunction = Pointer(batch.value.hFunction);
			}
			auto physical = PhysicalCopies({ &batch.value, 1 });
			const auto start = Clock::now();
			batch.status = real_(realList_, &batch.value, 1);
			batch.statuses.push_back(batch.status);
			batch.repetitions.push_back(repetition);
			const auto duration = Elapsed(start);
			apiCpu_ += duration;
			++submittedCalls_;
			++submittedDescriptors_;
			maxSubmitted_ = std::max(maxSubmitted_, std::size_t{ 1 });
			maxRegions_ = std::max(maxRegions_, std::size_t{ 2 });
			++crossRegionCalls_;
			AddEvent({ "submitted", "model_N2_launch", { first, second }, Pointer(realList_), 2, 0, batch.status, duration, std::move(physical) });
			modelReplacement_->RecordSubmission(batch.binding, static_cast<int>(batch.status));
			ProviderFloor::Require(batch.status == NVAPI_OK, "model batch native launch failed");
			++pairSample_->batchedCalls;
			pairSample_->submittedLogicalDescriptors.push_back(first);
			pairSample_->submittedLogicalDescriptors.push_back(second);
		}
		void SubmitPairOrder(std::span<const KernelPair::Operation> order, unsigned repetition, unsigned batchExecution, bool originalControl)
		{
			using Kind = KernelPair::Kind;
			std::size_t modelBatchIndex = 0;
			for (const auto& operation : order) {
				if (operation.kind == Kind::Join) {
					D3D12_RESOURCE_BARRIER barrier{};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					realList_->ResourceBarrier(1, &barrier);
					++pairSample_->joins;
				} else {
					const auto& command = pairSample_->regions[operation.region].commands[operation.command];
					if (operation.kind == Kind::Barrier)
						realList_->ResourceBarrier(static_cast<UINT>(command.barriers.size()), command.barriers.data());
					else if (operation.kind == Kind::Heaps)
						realList_->SetDescriptorHeaps(static_cast<UINT>(command.heaps.size()), command.heaps.data());
					else if (operation.kind == Kind::Batch) {
						ProviderFloor::Require(!originalControl, "control schedule contains a private batch launch");
						const auto second = pairSample_->regions.at(operation.secondRegion).commands.at(operation.secondCommand).descriptor;
						if (ModelBatchEnabled())
							SubmitModelBatch(modelBatchIndex++, command.descriptor, second, repetition, batchExecution);
						else
							SubmitPairBatch(command.descriptor, second, operation.region / 2);
					} else {
						pending_ = { command.descriptor };
						Flush("pair_original_launch", originalControl);
						pairSample_->submittedLogicalDescriptors.push_back(command.descriptor);
					}
				}
				++pairSample_->commandsSubmitted;
			}
			ProviderFloor::Require(!ModelBatchEnabled() || (originalControl ? modelBatchIndex == 0 : modelBatchIndex == modelBatches_->size()), "model batch submission omitted paired stages");
		}
		void SubmitPair(const RepetitionCallback& begin, const RepetitionCallback& end)
		{
			ProviderFloor::Require(pairSample_ && !pairSample_->submitted, "kernel pair sample was already submitted");
			pairSample_->order = KernelPair::BuildOrder(*pairSample_, pairMode_, modelBatchStages_);
			if (ComparisonEnabled())
				pairSample_->controlOrder = KernelPair::BuildOrder(*pairSample_, repetitionControl_);
			ValidatePairDependencies();
			if (ModelBatchEnabled())
				PrepareModelBatches();
			for (std::size_t eye = 0; eye < 2; ++eye) {
				auto& packet = pairSample_->batchPackets[eye];
				for (std::size_t region = 0; region < 2; ++region) {
					const auto id = pairSample_->regions[eye * 2 + region].descriptors[KernelPair::kTargetStage];
					std::copy(descriptors_[id].bytes.begin(), descriptors_[id].bytes.end(), packet.begin() + region * 96);
				}
			}
			ReplayPairSchedule(begin, end);
		}
		void ReplayPairSchedule(const RepetitionCallback& begin, const RepetitionCallback& end)
		{
			ProviderFloor::Require(pairSample_ && !pairSample_->submitted && pairSample_->repetitions.empty(), "kernel pair repetition plan was already recorded");
			pairSample_->repetitions.reserve(scheduleRepetitions_);
			ProviderFloor::Require(!ComparisonEnabled() || !pairSample_->controlOrder.empty(), "kernel comparison lacks its validated control order");
			unsigned batchExecution = 0;
			for (unsigned repetition = 0; repetition < scheduleRepetitions_; ++repetition) {
				const bool originalControl = ComparisonEnabled() && !KernelPair::ComparisonBatch(iteration_, repetition);
				const auto& order = originalControl ? pairSample_->controlOrder : pairSample_->order;
				const auto physicalPerPass = static_cast<std::size_t>(std::count_if(order.begin(), order.end(), [](const auto& operation) {
					return operation.kind == KernelPair::Kind::Launch || operation.kind == KernelPair::Kind::Batch;
				}));
				if (repetition) {
					D3D12_RESOURCE_BARRIER barrier{};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					realList_->ResourceBarrier(1, &barrier);
					++pairSample_->repetitionBoundaries;
				}
				auto& record = pairSample_->repetitions.emplace_back();
				record.ordinal = repetition;
				record.mode = originalControl ? repetitionControl_ : pairMode_;
				record.eventBegin = events_.size();
				const auto firstLogical = pairSample_->submittedLogicalDescriptors.size();
				const auto firstPhysical = submittedDescriptors_;
				const auto firstCommand = pairSample_->commandsSubmitted;
				const auto firstJoin = pairSample_->joins;
				const auto update = [&] {
					record.eventEnd = events_.size();
					record.physicalLaunches = submittedDescriptors_ - firstPhysical;
					record.commands = pairSample_->commandsSubmitted - firstCommand;
					record.joins = pairSample_->joins - firstJoin;
					record.logicalDescriptors.assign(pairSample_->submittedLogicalDescriptors.begin() + firstLogical, pairSample_->submittedLogicalDescriptors.end());
				};
				try {
					if (begin)
						begin(repetition);
					record.beginCompleted = true;
					SubmitPairOrder(order, repetition, batchExecution, originalControl);
					if (ModelBatchEnabled() && !originalControl)
						++batchExecution;
					update();
					KernelPair::ValidateLogicalPermutation(record.logicalDescriptors, descriptors_.size());
					ProviderFloor::Require(record.physicalLaunches == physicalPerPass && record.commands == order.size(),
						"schedule repetition physical or command coverage differs");
					if (end)
						end(repetition);
					record.endCompleted = record.completed = true;
				} catch (...) {
					update();
					throw;
				}
			}
			ProviderFloor::Require(!ModelBatchEnabled() || batchExecution == (ComparisonEnabled() ? 2 : scheduleRepetitions_),
				"kernel comparison did not execute its complete private schedule count");
			pairSample_->submitted = true;
		}
		void SubmitPairBatch(std::size_t first, std::size_t second, std::size_t eye)
		{
			auto value = descriptors_.at(first).value;
			value.hFunction = replacement_->function;
			value.pParams = pairSample_->batchPackets.at(eye).data();
			value.paramSize = 192;
			value.gridDim.z = 2;
			for (const auto id : { first, second }) {
				descriptors_[id].replacementApplied = true;
				descriptors_[id].submittedFunction = Pointer(value.hFunction);
			}
			const auto started = Clock::now();
			pairSample_->batchAttempted.at(eye) = true;
			const auto status = real_(realList_, &value, 1);
			pairSample_->batchStatuses.at(eye) = status;
			const auto duration = Elapsed(started);
			apiCpu_ += duration;
			++submittedCalls_;
			++submittedDescriptors_;
			maxSubmitted_ = std::max(maxSubmitted_, std::size_t{ 1 });
			maxRegions_ = std::max(maxRegions_, std::size_t{ 2 });
			++crossRegionCalls_;
			++replacementAttempts_;
			++replacement_->attempted;
			AddEvent({ "submitted", "pair_N2_launch", { first, second }, Pointer(realList_), 2, 0, status, duration });
			ProviderFloor::Require(status == NVAPI_OK, "kernel pair N2 native submission failed");
			++replacementSuccesses_;
			++replacement_->successful;
			++pairSample_->batchedCalls;
			pairSample_->submittedLogicalDescriptors.push_back(first);
			pairSample_->submittedLogicalDescriptors.push_back(second);
		}
		Json PairReceipt() const
		{
			Json order = Json::array(), regions = Json::array(), physicalBatches = Json::array(), repetitions = Json::array();
			const auto serializeOrder = [](std::span<const KernelPair::Operation> operations) {
				Json result = Json::array();
				for (const auto& operation : operations)
					result.push_back({ { "kind", static_cast<int>(operation.kind) }, { "region", operation.region }, { "command", operation.command },
						{ "secondRegion", operation.secondRegion == SIZE_MAX ? Json(nullptr) : Json(operation.secondRegion) } });
				return result;
			};
			if (pairSample_) {
				order = serializeOrder(pairSample_->order);
				for (const auto& region : pairSample_->regions)
					regions.push_back({ { "complete", region.complete }, { "stageSignatureSha256", HashText(region.stageSignature) },
						{ "commandSignatureSha256", HashText(region.commandSignature) }, { "logicalDescriptors", region.descriptors } });
				for (std::size_t eye = 0; eye < 2; ++eye) {
					if (!pairSample_->batchAttempted[eye])
						continue;
					physicalBatches.push_back({ { "eye", eye }, { "function", replacement_->functionValue },
						{ "candidateSha256", replacement_->candidateSha256 }, { "grid", { 20, 20, 2 } }, { "block", { 32, 1, 1 } },
						{ "paramSize", 192 }, { "dynamicSharedMemoryBytes", 0 }, { "paramsHex", Hex(pairSample_->batchPackets[eye]) },
						{ "paramsSha256", Hash(pairSample_->batchPackets[eye]) }, { "status", pairSample_->batchStatuses[eye] },
						{ "sourceDescriptorIds", { pairSample_->regions[eye * 2].descriptors[KernelPair::kTargetStage], pairSample_->regions[eye * 2 + 1].descriptors[KernelPair::kTargetStage] } } });
				}
				for (const auto& repetition : pairSample_->repetitions) {
					repetitions.push_back({ { "ordinal", repetition.ordinal }, { "eventBegin", repetition.eventBegin }, { "eventEnd", repetition.eventEnd },
						{ "physicalLaunches", repetition.physicalLaunches }, { "commands", repetition.commands }, { "addedGlobalUavBarriers", repetition.joins },
						{ "logicalDescriptorIds", repetition.logicalDescriptors }, { "beginCompleted", repetition.beginCompleted },
						{ "endCompleted", repetition.endCompleted }, { "completed", repetition.completed } });
					if (ComparisonEnabled())
						repetitions.back()["mode"] = repetition.mode;
				}
				if (modelBatches_)
					for (unsigned repetition = 0; repetition < scheduleRepetitions_; ++repetition)
						for (const auto& batch : *modelBatches_) {
							const auto found = std::find(batch.repetitions.begin(), batch.repetitions.end(), repetition);
							if (found == batch.repetitions.end())
								continue;
							const auto statusIndex = static_cast<std::size_t>(found - batch.repetitions.begin());
							const auto& value = batch.value;
							physicalBatches.push_back({ { "eye", descriptors_.at(batch.first).eye }, { "stage", batch.first % KernelPair::kStages },
								{ "function", Pointer(value.hFunction) }, { "candidateSha256", batch.candidateSha256 }, { "binding", batch.binding },
								{ "grid", { value.gridDim.x, value.gridDim.y, value.gridDim.z } }, { "block", { value.blockDim.x, value.blockDim.y, value.blockDim.z } },
								{ "paramSize", value.paramSize }, { "dynamicSharedMemoryBytes", value.dynSharedMemBytes },
								{ "paramsHex", Hex(batch.packet) }, { "paramsSha256", Hash(batch.packet) }, { "status", batch.statuses.at(statusIndex) },
								{ "sourceDescriptorIds", { batch.first, batch.second } } });
							if (scheduleRepetitions_ > 1)
								physicalBatches.back()["repetition"] = repetition;
						}
			}
			Json result{ { "requested", true }, { "mode", pairMode_ }, { "warmupForwardedUnchanged", warmup_ && (!ModelEnabled() || ModelBatchEnabled()) },
				{ "performanceQualified", false }, { "qualityQualified", false }, { "regions", regions }, { "order", order },
				{ "physicalBatches", physicalBatches },
				{ "scheduleRepetitions", scheduleRepetitions_ }, { "repetitions", repetitions },
				{ "executedScheduleRepetitions", warmup_ ? std::size_t{ 1 } : pairSample_ ? static_cast<std::size_t>(std::count_if(pairSample_->repetitions.begin(), pairSample_->repetitions.end(), [](const auto& value) { return value.completed; })) :
																							0 },
				{ "executedLogicalKernelLaunches", warmup_ ? descriptors_.size() : pairSample_ ? pairSample_->submittedLogicalDescriptors.size() :
																								 0 },
				{ "repetitionBoundaryUavBarriers", pairSample_ ? pairSample_->repetitionBoundaries : 0 },
				{ "modelBatchStageLimit", ModelBatchEnabled() ? Json(modelBatchStages_) : Json(nullptr) },
				{ "defaultAllocationParametersConfirmed", defaultAllocationConfirmed_ },
				{ "descriptorGuardSampleCalls", descriptorGuard_.SampleCounts() }, { "descriptorGuardTotalCalls", descriptorGuard_.TotalCounts() },
				{ "descriptorGuardLastStatus", descriptorStatuses_ }, { "descriptorGuardWarmupObserved", descriptorGuard_.WarmupObserved() },
				{ "descriptorGuardSteadyCalls", descriptorGuard_.SteadyCalls() }, { "descriptorGuardHealthy", descriptorGuard_.Healthy() },
				{ "descriptorCacheConfirmed", pairSample_ ? Json(pairSample_->descriptorCacheConfirmed) : Json(nullptr) },
				{ "submittedLogicalDescriptors", pairSample_ ? Json(pairSample_->submittedLogicalDescriptors) : Json::array() },
				{ "physicalKernelLaunches", submittedDescriptors_ }, { "logicalKernelLaunches", descriptors_.size() },
				{ "addedGlobalUavBarriers", pairSample_ ? pairSample_->joins : 0 }, { "batchedKernelLaunches", pairSample_ ? pairSample_->batchedCalls : 0 },
				{ "retainedResourceCount", pairOwners_->resources.size() }, { "retainedHeapCount", pairOwners_->heaps.size() },
				{ "submittedComplete", pairSample_ && pairSample_->submitted } };
			if (ComparisonEnabled()) {
				result["comparison"] = { { "requested", true }, { "control", repetitionControl_ }, { "batch", "model-batch" },
					{ "iteration", iteration_ }, { "parity", iteration_ & 1u }, { "order", (iteration_ & 1u) ? "BAAB" : "ABBA" },
					{ "parityScope", "absolute_sample_iteration_even_ABBA_odd_BAAB" }, { "A", repetitionControl_ }, { "B", "model-batch" },
					{ "plannedPrivatePasses", warmup_ ? 0 : 2 }, { "descriptorSubmissionStateScope", "final_submission_only_use_event_physicalLaunches" },
					{ "controlOrder", pairSample_ ? serializeOrder(pairSample_->controlOrder) : Json::array() } };
			}
			return result;
		}
		std::unique_ptr<Replacement> replacementStorage_;
		Replacement* replacement_ = nullptr;
		std::size_t replacementAttempts_ = 0, replacementSuccesses_ = 0;
		bool identityOutputReady_ = false;
		std::vector<ModuleIdentity> modules_;
		std::vector<FunctionIdentity> functions_;
		std::size_t moduleBytes_ = 0;
		DWORD thread_ = 0;
		ID3D12GraphicsCommandList* proxy_ = nullptr;
		ID3D12GraphicsCommandList* realList_ = nullptr;
		unsigned iteration_ = 0, eye_ = 0, region_ = 0;
		std::array<char, 512> error_{};
		std::unique_ptr<std::vector<Descriptor>> descriptorStorage_ = std::make_unique<std::vector<Descriptor>>();
		std::vector<Descriptor>& descriptors_ = *descriptorStorage_;
		std::vector<std::size_t> pending_;
		std::vector<Event> events_;
		std::size_t parameterBytes_ = 0, observedCalls_ = 0, submittedCalls_ = 0, submittedDescriptors_ = 0;
		std::size_t multiCalls_ = 0, maxSubmitted_ = 0, crossRegionCalls_ = 0, maxRegions_ = 0;
		double apiCpu_ = 0, hookCpu_ = 0;
		inline static std::recursive_mutex ownerMutex_;
		inline static ProviderKernelChainProbe* owner_ = nullptr;
		inline static bool abandoned_ = false;
		const char* Mode() const noexcept { return group_ ? "group" : "forward"; }
		static std::uintptr_t Pointer(const void* value) noexcept { return reinterpret_cast<std::uintptr_t>(value); }
		static double Elapsed(Clock::time_point start) { return std::chrono::duration<double, std::micro>(Clock::now() - start).count(); }
		static std::string Hash(std::span<const std::uint8_t> bytes)
		{
			return ProviderFloor::CanonicalSha256(Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(bytes))));
		}
		static std::string Hex(std::span<const std::uint8_t> bytes)
		{
			constexpr char digits[] = "0123456789abcdef";
			std::string result(bytes.size() * 2, '0');
			for (std::size_t i = 0; i < bytes.size(); ++i) {
				result[i * 2] = digits[bytes[i] >> 4];
				result[i * 2 + 1] = digits[bytes[i] & 15];
			}
			return result;
		}
		static void ValidateReadable(const void* address, std::size_t length, void* allocation = nullptr, bool executable = false)
		{
			using ProviderFloor::Require;
			const auto start = Pointer(address);
			Require(start && length && length <= UINTPTR_MAX - start, "kernel-chain memory span invalid");
			for (auto current = start; current < start + length;) {
				MEMORY_BASIC_INFORMATION memory{};
				Require(VirtualQuery(reinterpret_cast<void*>(current), &memory, sizeof(memory)) == sizeof(memory), "kernel-chain memory query failed");
				const auto protection = memory.Protect & 0xff;
				Require(memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
							(protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
								protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY) &&
							(!allocation || (memory.AllocationBase == allocation && memory.Type == MEM_IMAGE)) &&
							(!executable || (protection == PAGE_EXECUTE_READ && memory.Type == MEM_IMAGE)),
					"kernel-chain memory guard rejected span");
				const auto end = Pointer(memory.BaseAddress) + memory.RegionSize;
				Require(end > current, "kernel-chain memory query did not advance");
				current = end;
			}
		}
		bool IdentityEnabled() const noexcept { return !identityOutput_.empty(); }
		bool ReplacementEnabled() const noexcept { return !replacementManifest_.empty(); }
		static std::vector<std::uint8_t> ReadBoundedFile(const std::filesystem::path& path, std::size_t maximum)
		{
			struct File
			{
				HANDLE handle = INVALID_HANDLE_VALUE;
				~File()
				{
					if (handle != INVALID_HANDLE_VALUE)
						CloseHandle(handle);
				}
			} file;
			file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			ProviderFloor::Require(file.handle != INVALID_HANDLE_VALUE, "N1 replacement file open failed");
			LARGE_INTEGER size{};
			ProviderFloor::Require(GetFileSizeEx(file.handle, &size) && size.QuadPart > 0 && static_cast<std::uint64_t>(size.QuadPart) <= maximum,
				"N1 replacement file size invalid");
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
			DWORD read = 0;
			ProviderFloor::Require(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size(),
				"N1 replacement file read failed");
			return bytes;
		}
		static void ValidateReplacementManifest(const Json& manifest, bool pair = false)
		{
			using ProviderFloor::Require;
			Require(manifest.is_object() && manifest.size() == 4 && manifest.at("schema") == (pair ? KernelChain::kPairReplacementSchema : KernelChain::kReplacementSchema) &&
						manifest.at("batchCount").is_number_integer() && manifest.at("batchCount") == (pair ? 2 : 1),
				"N1 replacement manifest schema or batch count invalid");
			const auto& original = manifest.at("original");
			const auto& candidate = manifest.at("candidate");
			Require(original.is_object() && original.size() == 3 && original.at("moduleSha256") == KernelChain::kReplacementOriginalModuleSha256 &&
						original.at("entry") == KernelChain::kReplacementEntry && original.at("paramSize").is_number_integer() &&
						original.at("paramSize") == KernelChain::kReplacementParameterBytes,
				"N1 replacement original identity mismatch");
			Require(candidate.is_object() && candidate.size() == 3 && candidate.at("sha256").is_string() &&
						KernelChain::ReplacementCandidateAdmitted(candidate.at("sha256").get<std::string>(), pair) &&
						candidate.at("entry") == KernelChain::kReplacementEntry && candidate.at("path").is_string(),
				"N1 replacement candidate identity mismatch");
			const auto path = std::filesystem::path(candidate.at("path").get<std::string>());
			Require(path.is_absolute(), "N1 replacement candidate path must be absolute");
		}
		void PrepareReplacement()
		{
			ProviderFloor::Require(replacement_ && !replacement_->module && !replacement_->function && !replacement_->device,
				"N1 replacement previous ownership was not retired");
			const auto bytes = ReadBoundedFile(replacementManifest_, 64 * 1024);
			const auto manifest = Json::parse(bytes);
			ValidateReplacementManifest(manifest, PairEnabled());
			replacement_->manifestSha256 = Hash(bytes);
			replacement_->path = manifest.at("candidate").at("path").get<std::string>();
			replacement_->bytes = ReadBoundedFile(replacement_->path, KernelChain::kMaximumModuleBytes);
			replacement_->candidateSha256 = Hash(replacement_->bytes);
			ProviderFloor::Require(replacement_->candidateSha256 == manifest.at("candidate").at("sha256").get<std::string>(),
				"N1 replacement candidate byte hash mismatch");
			const auto& blob = replacement_->bytes;
			ProviderFloor::Require(blob.size() >= 64 && blob[0] == 0x7f && blob[1] == 'E' && blob[2] == 'L' && blob[3] == 'F',
				"N1 replacement candidate is not an ELF cubin");
		}
		void EnsureReplacement(ID3D12Device* device, const FunctionIdentity& original)
		{
			using ProviderFloor::Require;
			auto& state = *replacement_;
			Require(original.device == Pointer(device), "N1 replacement original device mismatch");
			if (state.function) {
				Require(state.functionCalled && state.createFunction == NVAPI_OK && state.device.Get() == device && !state.cleanupFailed,
					"N1 replacement device or ownership mismatch");
				return;
			}
			Require(!state.moduleCalled && !state.functionCalled && realCreateModule_ && realCreateFunction_ && realDestroyFunction_ && realDestroyModule_ &&
						!state.bytes.empty() && CandidateAdmitted(state.candidateSha256),
				"N1 replacement creation not admitted");
			state.device = device;
			state.moduleCalled = true;
			state.createModule = realCreateModule_(device, state.bytes.data(), static_cast<NvU32>(state.bytes.size()), &state.module);
			state.moduleValue = Pointer(state.module);
			Require(state.createModule == NVAPI_OK && state.module, "N1 replacement private module creation failed");
			if (std::any_of(modules_.begin(), modules_.end(), [&](const auto& item) { return item.handle && item.handle == state.moduleValue; })) {
				state.cleanupFailed = true;
				throw std::runtime_error("N1 replacement module aliases provider ownership");
			}
			state.functionCalled = true;
			state.createFunction = realCreateFunction_(device, state.module, KernelChain::kReplacementEntry, &state.function);
			state.functionValue = Pointer(state.function);
			Require(state.createFunction == NVAPI_OK && state.function, "N1 replacement private function creation failed");
			if (std::any_of(functions_.begin(), functions_.end(), [&](const auto& item) { return item.handle && item.handle == state.functionValue; })) {
				state.cleanupFailed = true;
				throw std::runtime_error("N1 replacement function aliases provider ownership");
			}
			Require(Hash(state.bytes) == state.candidateSha256, "N1 replacement owned blob changed during creation");
		}
		bool RetireReplacement() noexcept
		{
			if (!replacement_)
				return true;
			auto& state = *replacement_;
			if (state.cleanupFailed)
				return false;
			const auto fail = [&]() noexcept { state.cleanupFailed = true; return false; };
			if (state.function) {
				if (!realDestroyFunction_ || !state.functionCalled || state.createFunction != NVAPI_OK)
					return fail();
				state.destroyFunctionCalled = true;
				state.destroyFunction = realDestroyFunction_(state.device.Get(), state.function);
				if (state.destroyFunction != NVAPI_OK)
					return fail();
				state.function = nullptr;
			}
			if (state.module) {
				if (!realDestroyModule_ || !state.moduleCalled || state.createModule != NVAPI_OK)
					return fail();
				state.destroyModuleCalled = true;
				state.destroyModule = realDestroyModule_(state.device.Get(), state.module);
				if (state.destroyModule != NVAPI_OK)
					return fail();
				state.module = nullptr;
			}
			state.device.Reset();
			return true;
		}
		Json ReplacementReceipt() const
		{
			const auto& state = *replacement_;
			return { { "requested", true }, { "scope", PairEnabled() ? "one_pinned_kernel_N2_pair_gate" : "one_pinned_kernel_N1_equivalence_gate" }, { "batchCount", PairEnabled() ? 2 : 1 },
				{ "qualityQualified", false }, { "performanceQualified", false }, { "manifest", replacementManifest_.string() },
				{ "manifestSha256", state.manifestSha256 }, { "originalModuleSha256", KernelChain::kReplacementOriginalModuleSha256 },
				{ "entry", KernelChain::kReplacementEntry }, { "paramSize", KernelChain::kReplacementParameterBytes },
				{ "candidate", state.path.string() }, { "candidateSha256", state.candidateSha256 },
				{ "moduleHandle", state.moduleValue }, { "functionHandle", state.functionValue },
				{ "createModuleStatus", state.moduleCalled ? Json(static_cast<int>(state.createModule)) : Json(nullptr) },
				{ "createFunctionStatus", state.functionCalled ? Json(static_cast<int>(state.createFunction)) : Json(nullptr) },
				{ "destroyFunctionStatus", state.destroyFunctionCalled ? Json(static_cast<int>(state.destroyFunction)) : Json(nullptr) },
				{ "destroyModuleStatus", state.destroyModuleCalled ? Json(static_cast<int>(state.destroyModule)) : Json(nullptr) },
				{ "cleanupFailed", state.cleanupFailed }, { "attemptedDescriptors", state.attempted }, { "successfulDescriptors", state.successful },
				{ "targetExecuted", state.successful != 0 }, { "experimentFailed", !Healthy() } };
		}
		void PrepareIdentityOutput()
		{
			if (identityOutputReady_)
				return;
			identityOutput_ = std::filesystem::absolute(identityOutput_).lexically_normal();
			ProviderFloor::Require(std::filesystem::create_directories(identityOutput_), "kernel-chain identity directory must be new");
			modules_.reserve(KernelChain::kMaximumModules);
			functions_.reserve(KernelChain::kMaximumFunctions);
			identityOutputReady_ = true;
		}
		static void ReadOwned(const void* source, std::span<std::uint8_t> destination)
		{
			ValidateReadable(source, destination.size());
			SIZE_T copied = 0;
			ProviderFloor::Require(ReadProcessMemory(GetCurrentProcess(), source, destination.data(), destination.size(), &copied) &&
									   copied == destination.size(),
				"kernel-chain identity memory copy failed");
		}
		static std::string ReadName(const char* source)
		{
			std::string name;
			for (std::size_t i = 0; i < KernelChain::kMaximumFunctionNameBytes; ++i) {
				std::array<std::uint8_t, 1> byte{};
				ProviderFloor::Require(Pointer(source) && i <= UINTPTR_MAX - Pointer(source), "kernel-chain function name pointer invalid");
				ReadOwned(reinterpret_cast<const void*>(Pointer(source) + i), byte);
				if (!byte[0]) {
					ProviderFloor::Require(!name.empty(), "kernel-chain function name is empty");
					return name;
				}
				ProviderFloor::Require(byte[0] >= 0x21 && byte[0] <= 0x7e, "kernel-chain function name contains invalid bytes");
				name.push_back(static_cast<char>(byte[0]));
			}
			throw std::runtime_error("kernel-chain function name exceeds capture bound");
		}
		static void ValidateOutput(NVDX_ObjectHandle* output)
		{
			ValidateReadable(output, sizeof(*output));
			for (auto current = Pointer(output); current < Pointer(output) + sizeof(*output);) {
				MEMORY_BASIC_INFORMATION memory{};
				ProviderFloor::Require(VirtualQuery(reinterpret_cast<void*>(current), &memory, sizeof(memory)) == sizeof(memory),
					"kernel-chain handle output query failed");
				const auto protection = memory.Protect & 0xff;
				ProviderFloor::Require(protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
										   protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY,
					"kernel-chain handle output is not writable");
				current = Pointer(memory.BaseAddress) + memory.RegionSize;
			}
		}
		static std::uintptr_t ReadHandle(NVDX_ObjectHandle* output)
		{
			NVDX_ObjectHandle handle = nullptr;
			ReadOwned(output, { reinterpret_cast<std::uint8_t*>(&handle), sizeof(handle) });
			return Pointer(handle);
		}
		void WriteBlob(const std::string& name, std::span<const std::uint8_t> bytes)
		{
			const auto path = identityOutput_ / name;
			const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			ProviderFloor::Require(file != INVALID_HANDLE_VALUE, "kernel-chain blob file creation failed");
			DWORD written = 0;
			const bool writtenAll = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
			const bool flushed = writtenAll && FlushFileBuffers(file);
			const bool closed = CloseHandle(file) != 0;
			ProviderFloor::Require(writtenAll && flushed && closed, "kernel-chain blob file write/flush/close failed");
		}
		std::size_t FindModule(std::uintptr_t device, std::uintptr_t handle) const
		{
			for (std::size_t i = modules_.size(); i > 0; --i)
				if (modules_[i - 1].called && modules_[i - 1].status == NVAPI_OK && modules_[i - 1].stable &&
					modules_[i - 1].device == device && modules_[i - 1].handle == handle)
					return i - 1;
			throw std::runtime_error("kernel-chain function module lacks captured provenance");
		}
		std::size_t FindFunction(std::uintptr_t handle) const
		{
			for (std::size_t i = functions_.size(); i > 0; --i)
				if (functions_[i - 1].called && functions_[i - 1].status == NVAPI_OK && functions_[i - 1].handle == handle)
					return i - 1;
			throw std::runtime_error("kernel-chain launch function lacks captured provenance");
		}
		void ValidateIdentityCall(ID3D12Device* device, NVDX_ObjectHandle* output) const
		{
			ThrowIfFailed();
			ProviderFloor::Require(IdentityEnabled() && identityOutputReady_ && GetCurrentThreadId() == thread_ && device,
				"kernel-chain creation escaped the owned capture boundary");
			ValidateOutput(output);
		}
		NvAPI_Status CaptureModule(ID3D12Device* device, const void* blob, NvU32 size, NVDX_ObjectHandle* output)
		{
			ValidateIdentityCall(device, output);
			KernelChain::ValidateModuleBudget(size, modules_.size(), moduleBytes_);
			std::vector<std::uint8_t> bytes(size);
			ReadOwned(blob, bytes);
			ModuleIdentity captured;
			captured.device = Pointer(device);
			captured.source = Pointer(blob);
			captured.bytes = size;
			captured.sha256 = Hash(bytes);
			captured.file = "module-" + std::to_string(modules_.size()) + "-" + captured.sha256 + ".bin";
			moduleBytes_ += size;
			modules_.push_back(std::move(captured));
			auto& record = modules_.back();
			WriteBlob(record.file, bytes);
			record.fileComplete = true;
			record.status = realCreateModule_(device, blob, size, output);
			record.called = true;
			if (record.status == NVAPI_OK)
				record.handle = ReadHandle(output);
			ReadOwned(blob, bytes);
			record.stable = Hash(bytes) == record.sha256;
			ProviderFloor::Require(record.stable, "kernel-chain module input changed during creation");
			if (record.status == NVAPI_OK) {
				ProviderFloor::Require(record.handle != 0, "kernel-chain module creation returned a null handle");
				if (modelReplacement_)
					ObserveModelBoundary([&] { modelReplacement_->ObserveOriginalModule(record.device, record.handle, record.sha256); });
				if (ReplacementEnabled() && replacement_->module && record.handle == Pointer(replacement_->module)) {
					replacement_->cleanupFailed = true;
					RecordFailure("provider module aliases N1 private ownership");
				}
				for (std::size_t i = 0; i + 1 < modules_.size(); ++i) {
					const auto& previous = modules_[i];
					ProviderFloor::Require(previous.status != NVAPI_OK || previous.handle != record.handle ||
											   (previous.device == record.device && previous.sha256 == record.sha256),
						"kernel-chain module handle has conflicting provenance");
				}
			}
			return record.status;
		}
		NvAPI_Status CaptureFunction(ID3D12Device* device, NVDX_ObjectHandle module, const char* name, NVDX_ObjectHandle* output)
		{
			ValidateIdentityCall(device, output);
			ProviderFloor::Require(functions_.size() < KernelChain::kMaximumFunctions, "kernel-chain function capture budget exceeded");
			const auto moduleIndex = FindModule(Pointer(device), Pointer(module));
			FunctionIdentity captured;
			captured.device = Pointer(device);
			captured.module = Pointer(module);
			captured.moduleIdentity = moduleIndex;
			captured.name = ReadName(name);
			functions_.push_back(std::move(captured));
			auto& record = functions_.back();
			record.status = realCreateFunction_(device, module, name, output);
			record.called = true;
			if (record.status == NVAPI_OK)
				record.handle = ReadHandle(output);
			ProviderFloor::Require(ReadName(name) == record.name, "kernel-chain function name changed during creation");
			if (record.status == NVAPI_OK) {
				ProviderFloor::Require(record.handle != 0, "kernel-chain function creation returned a null handle");
				if (modelReplacement_)
					ObserveModelBoundary([&] { modelReplacement_->ObserveOriginalFunction(record.device, record.module, record.handle, record.name); });
				if (ReplacementEnabled() && replacement_->function && record.handle == Pointer(replacement_->function)) {
					replacement_->cleanupFailed = true;
					RecordFailure("provider function aliases N1 private ownership");
				}
				for (std::size_t i = 0; i + 1 < functions_.size(); ++i) {
					const auto& previous = functions_[i];
					ProviderFloor::Require(previous.status != NVAPI_OK || previous.handle != record.handle ||
											   (previous.device == record.device && previous.module == record.module && previous.name == record.name &&
												   modules_[previous.moduleIdentity].sha256 == modules_[record.moduleIdentity].sha256),
						"kernel-chain function handle has conflicting provenance");
				}
				if (ReplacementEnabled() && record.name == KernelChain::kReplacementEntry &&
					modules_[record.moduleIdentity].sha256 == KernelChain::kReplacementOriginalModuleSha256) {
					try {
						EnsureReplacement(device, record);
						record.replacement = true;
					} catch (const std::exception& error) {
						RecordFailure(error.what());
					} catch (...) {
						RecordFailure("unknown N1 private creation failure");
					}
				}
			}
			return record.status;
		}
		Json IdentityReceipt() const
		{
			Json modules = Json::array(), functions = Json::array();
			for (const auto& record : modules_)
				modules.push_back({ { "identity", modules.size() }, { "device", record.device }, { "sourcePointer", record.source },
					{ "size", record.bytes }, { "sha256", record.sha256 }, { "file", record.file }, { "fileComplete", record.fileComplete }, { "handle", record.handle },
					{ "handleRead", record.called && record.status == NVAPI_OK }, { "called", record.called },
					{ "status", static_cast<int>(record.status) }, { "stableAtObservedBoundaries", record.stable } });
			for (const auto& record : functions_)
				functions.push_back({ { "identity", functions.size() }, { "device", record.device }, { "module", record.module },
					{ "moduleIdentity", record.moduleIdentity }, { "moduleBlobSha256", modules_.at(record.moduleIdentity).sha256 },
					{ "name", record.name }, { "handle", record.handle }, { "handleRead", record.called && record.status == NVAPI_OK },
					{ "called", record.called }, { "status", static_cast<int>(record.status) } });
			return { { "requested", IdentityEnabled() }, { "enabled", IdentityEnabled() }, { "directory", identityOutput_.string() }, { "modules", modules }, { "functions", functions },
				{ "capturedBytes", moduleBytes_ }, { "boundaryProof", "non_atomic_original_arguments_forwarded_unchanged" },
				{ "timingQualified", false } };
		}
		template <class Function>
		static NvAPI_Status InterceptIdentity(Function&& function) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				if (!owner_)
					return NVAPI_ERROR;
				try {
					return function(*owner_);
				} catch (const std::exception& error) {
					owner_->RecordFailure(error.what());
				} catch (...) {
					owner_->RecordFailure("unknown kernel-chain identity interception failure");
				}
			} catch (...) {
				std::fputs("Kernel-chain identity callback ownership failed\n", stderr);
			}
			return NVAPI_ERROR;
		}
		static NvAPI_Status __cdecl InterceptModule(ID3D12Device* device, const void* blob, NvU32 size, NVDX_ObjectHandle* output) noexcept
		{
			return InterceptIdentity([&](auto& probe) { return probe.CaptureModule(device, blob, size, output); });
		}
		static NvAPI_Status __cdecl InterceptFunction(ID3D12Device* device, NVDX_ObjectHandle module, const char* name, NVDX_ObjectHandle* output) noexcept
		{
			return InterceptIdentity([&](auto& probe) { return probe.CaptureFunction(device, module, name, output); });
		}
		static bool CacheProtectionMatches(const CacheSlot& cache) noexcept
		{
			MEMORY_BASIC_INFORMATION memory{};
			return VirtualQuery(cache.address, &memory, sizeof(memory)) == sizeof(memory) && memory.Protect == cache.protection;
		}
		void PrepareSlot(CacheSlot& cache, std::size_t rva, unsigned interfaceId, void* hook, QueryInterface query)
		{
			using ProviderFloor::Require;
			cache.rva = rva;
			cache.interfaceId = interfaceId;
			cache.hook = hook;
			auto** address = reinterpret_cast<void**>(base_ + rva);
			ValidateReadable(address, sizeof(void*), base_);
			Require(Pointer(address) % alignof(void*) == 0, "kernel-chain cache is unaligned");
			PrepareTarget(cache, interfaceId, query);
			const auto original = *address;
			Require(!original || original == cache.real, "kernel-chain cache has an unexpected prior hook");
			MEMORY_BASIC_INFORMATION memory{};
			Require(VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory), "kernel-chain cache protection query failed");
			cache.original = original;
			cache.protection = memory.Protect;
			cache.address = address;
		}
		static void PrepareTarget(CacheSlot& target, unsigned interfaceId, QueryInterface query)
		{
			target.interfaceId = interfaceId;
			target.real = query(interfaceId);
			ProviderFloor::Require(target.real && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
													  reinterpret_cast<LPCWSTR>(target.real), &target.targetModule),
				"kernel-chain target ownership unavailable");
			ValidateReadable(target.real, 1, nullptr, true);
		}
		static void Exchange(CacheSlot& cache, void* expected, void* replacement)
		{
			struct Operations
			{
				CacheSlot& cache;
				bool MakeWritable()
				{
					DWORD previous = 0;
					return VirtualProtect(cache.address, sizeof(void*), PAGE_READWRITE, &previous) != 0;
				}
				void* Read() const { return *cache.address; }
				void* CompareExchange(void* value, void* expected)
				{
					return InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(cache.address), value, expected);
				}
				bool ProtectionMatches() const { return CacheProtectionMatches(cache); }
				bool RestoreProtection()
				{
					DWORD ignored = 0;
					return VirtualProtect(cache.address, sizeof(void*), cache.protection, &ignored) && ProtectionMatches();
				}
			} operations{ cache };
			const auto result = KernelChain::ExchangePointer(operations, expected, replacement);
			cache.installed = result.observed == cache.hook;
			cache.uncertain = !result.succeeded && !result.rollbackProven;
			ProviderFloor::Require(result.succeeded, "kernel-chain cache exchange/protection failed; rollback " + std::string(result.rollbackProven ? "proven" : "unproven"));
		}
		bool RestoreCaches() noexcept
		{
			bool restored = true;
			for (auto iterator = caches_.rbegin(); iterator != caches_.rend(); ++iterator) {
				auto& cache = *iterator;
				if (!cache.address)
					continue;
				try {
					if (cache.installed)
						Exchange(cache, cache.hook, cache.original);
					ProviderFloor::Require(!cache.uncertain && *cache.address == cache.original && CacheProtectionMatches(cache),
						"kernel-chain cache restoration not proven");
				} catch (const std::exception& error) {
					RecordFailure(error.what());
					restored = false;
				} catch (...) {
					RecordFailure("unknown kernel-chain cache restoration failure");
					restored = false;
				}
			}
			installed_ = caches_[0].installed;
			return restored;
		}
		Json CacheReceipt() const
		{
			Json result = Json::array();
			for (const auto& cache : caches_)
				if (cache.address)
					result.push_back({ { "rva", cache.rva }, { "interfaceId", cache.interfaceId }, { "original", Pointer(cache.original) },
						{ "real", Pointer(cache.real) }, { "installed", cache.installed }, { "uncertain", cache.uncertain },
						{ "originalProtection", cache.protection } });
			return result;
		}
		bool ReleaseModules() noexcept
		{
			const auto release = [this](HMODULE& module) {
				if (module) {
					if (!FreeLibrary(module)) {
						RecordFailure("kernel-chain module reference release failed");
						return false;
					}
					module = nullptr;
				}
				return true;
			};
			for (auto target = destroyTargets_.rbegin(); target != destroyTargets_.rend(); ++target)
				if (!release(target->targetModule))
					return false;
			for (auto cache = caches_.rbegin(); cache != caches_.rend(); ++cache)
				if (!release(cache->targetModule))
					return false;
			return release(nvapiModule_) && release(module_);
		}
		void AddEvent(Event event)
		{
			ProviderFloor::Require(events_.size() < KernelChain::kMaximumSampleEvents, "kernel-chain event capture budget exceeded");
			events_.push_back(std::move(event));
		}
		std::vector<PhysicalLaunch> PhysicalCopies(std::span<const NVAPI_CU_KERNEL_LAUNCH_PARAMS> values) const
		{
			std::vector<PhysicalLaunch> result;
			if (!ComparisonEnabled())
				return result;
			result.reserve(values.size());
			for (const auto& value : values) {
				const auto* bytes = static_cast<const std::uint8_t*>(value.pParams);
				ProviderFloor::Require(bytes && value.paramSize && value.paramSize <= KernelChain::kMaximumParameterBytes,
					"comparison physical packet is out of bounds");
				result.push_back({ value, { bytes, bytes + value.paramSize } });
			}
			return result;
		}
		void Flush(std::string_view reason, bool originalControl = false)
		{
			ProviderFloor::Require(!originalControl || (ComparisonEnabled() && ModelBatchEnabled() && PairRecording()),
				"comparison original submission escaped its admitted steady schedule");
			if (pending_.empty())
				return;
			std::vector<NVAPI_CU_KERNEL_LAUNCH_PARAMS> values;
			std::set<std::pair<unsigned, unsigned>> regions;
			std::size_t replacements = 0;
			std::vector<std::size_t> modelBindings;
			for (const auto id : pending_) {
				auto& captured = descriptors_.at(id);
				auto value = captured.value;
				value.pParams = captured.bytes.data();
				if (ReplacementEnabled() && !PairEnabled() && functions_.at(captured.functionIdentity).replacement) {
					ProviderFloor::Require(replacement_->function && !replacement_->cleanupFailed &&
											   value.paramSize == KernelChain::kReplacementParameterBytes && value.gridDim.z == 1,
						"N1 replacement launch contract mismatch");
					value.hFunction = replacement_->function;
					captured.replacementApplied = true;
					++replacements;
				}
				if (modelReplacement_) {
					const auto& original = functions_.at(captured.functionIdentity);
					const auto binding = modelReplacement_->Resolve(original.device, original.handle,
						modules_.at(original.moduleIdentity).sha256, original.name, value.paramSize, value.gridDim.z);
					ProviderFloor::Require(binding.batchCount == (ModelBatchEnabled() ? 2u : 1u), "model replacement batch ABI mismatch");
					if (ModelBatchEnabled())
						ProviderFloor::Require(warmup_ || (PairRecording() && (originalControl || id % KernelPair::kStages >= modelBatchStages_)),
							"model batch reached an unpaired launch inside its selected prefix");
					else {
						value.hFunction = reinterpret_cast<NVDX_ObjectHandle>(binding.function);
						captured.replacementApplied = true;
						modelBindings.push_back(binding.index);
					}
				}
				if (originalControl)
					captured.replacementApplied = false;
				captured.submittedFunction = Pointer(value.hFunction);
				values.push_back(value);
				regions.emplace(captured.eye, captured.region);
			}
			auto physical = PhysicalCopies(values);
			const auto start = Clock::now();
			const auto status = real_(realList_, values.data(), static_cast<NvU32>(values.size()));
			const auto duration = Elapsed(start);
			apiCpu_ += duration;
			if (ReplacementEnabled()) {
				replacementAttempts_ += replacements;
				replacement_->attempted += replacements;
				if (status == NVAPI_OK) {
					replacementSuccesses_ += replacements;
					replacement_->successful += replacements;
				}
			}
			++submittedCalls_;
			submittedDescriptors_ += values.size();
			multiCalls_ += values.size() > 1;
			crossRegionCalls_ += regions.size() > 1;
			maxSubmitted_ = std::max(maxSubmitted_, values.size());
			maxRegions_ = std::max(maxRegions_, regions.size());
			AddEvent({ "submitted", std::string(reason), pending_, Pointer(realList_), pending_.size(), 0, status, duration, std::move(physical) });
			pending_.clear();
			for (const auto index : modelBindings)
				modelReplacement_->RecordSubmission(index, static_cast<int>(status));
			ProviderFloor::Require(status == NVAPI_OK, "kernel-chain native submission failed with status " + std::to_string(status));
		}
		NvAPI_Status Capture(ID3D12GraphicsCommandList* list, const NVAPI_CU_KERNEL_LAUNCH_PARAMS* values, NvU32 count)
		{
			using ProviderFloor::Require;
			ThrowIfFailed();
			Require(sample_ && evaluating_ && GetCurrentThreadId() == thread_ && (list == proxy_ || list == realList_),
				"kernel-chain call escaped the owned evaluation boundary");
			Require(count && count <= KernelChain::kMaximumDescriptors, "kernel-chain input descriptor count out of bounds");
			ValidateReadable(values, sizeof(*values) * count);
			if (pending_.size() + count > KernelChain::kMaximumDescriptors)
				Flush("descriptor_capacity");
			std::vector<std::size_t> ids;
			for (NvU32 i = 0; i < count; ++i) {
				const auto& d = values[i];
				KernelChain::ValidateDescriptorBudget(1, descriptors_.size(), d.paramSize, parameterBytes_);
				Require(d.hFunction && d.gridDim.x && d.gridDim.y && d.gridDim.z && d.blockDim.x && d.blockDim.y && d.blockDim.z,
					"kernel-chain descriptor lacks function or dimensions");
				ValidateReadable(d.pParams, d.paramSize);
				Descriptor captured{ d, std::vector<std::uint8_t>(d.paramSize), Pointer(d.pParams), eye_, region_ };
				if (IdentityEnabled())
					captured.functionIdentity = FindFunction(Pointer(d.hFunction));
				std::memcpy(captured.bytes.data(), d.pParams, d.paramSize);
				parameterBytes_ += d.paramSize;
				ids.push_back(descriptors_.size());
				descriptors_.push_back(std::move(captured));
			}
			++observedCalls_;
			AddEvent({ "observed", "provider_launch", ids, Pointer(realList_), pending_.size(), pending_.size() + ids.size() });
			if (PairRecording()) {
				Require(ids.size() == 1, "kernel pair requires one original descriptor per launch call");
				RecordPairLaunch(ids.front());
				return NVAPI_OK;
			}
			pending_.insert(pending_.end(), ids.begin(), ids.end());
			if (!group_ || warmup_)
				Flush(group_ ? "warmup_forward" : "forward");
			return NVAPI_OK;
		}
		template <DescriptorGuard::Api Api, class Function, class... Arguments>
		static NvAPI_Status DescriptorCall(Arguments... arguments) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				if (!owner_)
					return NVAPI_ERROR;
				auto& probe = *owner_;
				constexpr auto index = static_cast<std::size_t>(Api);
				const auto nativeFunction = reinterpret_cast<Function>(probe.caches_[3 + index].real);
				if (!nativeFunction) {
					probe.RecordFailure("kernel pair descriptor real target missing");
					return NVAPI_ERROR;
				}
				const bool admitted = probe.PairEnabled() && probe.sample_ && probe.evaluating_ && GetCurrentThreadId() == probe.thread_;
				const bool stable = probe.descriptorGuard_.Observe(Api, admitted && probe.warmup_);
				const auto status = nativeFunction(arguments...);
				probe.descriptorStatuses_[index] = status;
				if (!admitted || !stable)
					probe.RecordFailure("kernel pair descriptor cache mutation invalidates deferred replay");
				return status;
			} catch (...) {
				if (owner_)
					owner_->RecordFailure("kernel pair descriptor callback failed");
				return NVAPI_ERROR;
			}
		}
		static NvAPI_Status __cdecl InterceptMerged(NVAPI_D3D12_GET_CUDA_MERGED_TEXTURE_SAMPLER_OBJECT_PARAMS* params) noexcept
		{
			return DescriptorCall<DescriptorGuard::Api::Merged, decltype(&NvAPI_D3D12_GetCudaMergedTextureSamplerObject)>(params);
		}
		static NvAPI_Status __cdecl InterceptIndependent(NVAPI_D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_PARAMS* params) noexcept
		{
			return DescriptorCall<DescriptorGuard::Api::Independent, decltype(&NvAPI_D3D12_GetCudaIndependentDescriptorObject)>(params);
		}
		static NvAPI_Status __cdecl InterceptTexture(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE texture, D3D12_CPU_DESCRIPTOR_HANDLE sampler, NvU32* output) noexcept
		{
			return DescriptorCall<DescriptorGuard::Api::Texture, decltype(&NvAPI_D3D12_GetCudaTextureObject)>(device, texture, sampler, output);
		}
		static NvAPI_Status __cdecl InterceptSurface(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE surface, NvU32* output) noexcept
		{
			return DescriptorCall<DescriptorGuard::Api::Surface, decltype(&NvAPI_D3D12_GetCudaSurfaceObject)>(device, surface, output);
		}
		static NvAPI_Status __cdecl InterceptCaptureUav(ID3D12Device* device, NVAPI_UAV_INFO* info) noexcept
		{
			return DescriptorCall<DescriptorGuard::Api::CaptureUav, decltype(&NvAPI_D3D12_CaptureUAVInfo)>(device, info);
		}
		static NvAPI_Status __cdecl Intercept(ID3D12GraphicsCommandList* list, const NVAPI_CU_KERNEL_LAUNCH_PARAMS* values, NvU32 count) noexcept
		{
			try {
				std::lock_guard lock(ownerMutex_);
				if (!owner_)
					return NVAPI_ERROR;
				const auto start = Clock::now();
				const auto previousApi = owner_->apiCpu_;
				NvAPI_Status result = NVAPI_ERROR;
				try {
					result = owner_->Capture(list, values, count);
				} catch (const std::exception& error) {
					owner_->RecordFailure(error.what());
				} catch (...) {
					owner_->RecordFailure("unknown kernel-chain interception failure");
				}
				owner_->hookCpu_ += Elapsed(start) - (owner_->apiCpu_ - previousApi);
				return result;
			} catch (...) {
				return NVAPI_ERROR;
			}
		}
	};
}
