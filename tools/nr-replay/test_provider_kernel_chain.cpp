#include "ProviderKernelChainProbe.h"

#include <fstream>
#include <iostream>
#include <thread>

namespace NrReplay
{
	struct KernelChainProbeTestAccess
	{
		static std::size_t RuntimeStoredFrames(const ProviderKernelChainProbe& probe)
		{
			return probe.pairOwners_->samples.size() + (probe.modelBatchOwners_ ? probe.modelBatchOwners_->size() : 0);
		}
		static const std::uint8_t* FirstOwnedPacket(const ProviderKernelChainProbe& probe)
		{
			return probe.descriptors_.front().bytes.data();
		}
		static void Configure(ProviderKernelChainProbe& probe, decltype(probe.real_) launch)
		{
			probe.real_ = launch;
			probe.thread_ = GetCurrentThreadId();
			probe.installed_ = true;
		}
		static NvAPI_Status Invoke(ProviderKernelChainProbe& probe, ID3D12GraphicsCommandList* list,
			const NVAPI_CU_KERNEL_LAUNCH_PARAMS* values, NvU32 count)
		{
			ProviderKernelChainProbe::owner_ = &probe;
			const auto status = ProviderKernelChainProbe::Intercept(list, values, count);
			ProviderKernelChainProbe::owner_ = nullptr;
			return status;
		}
		static void MarkRetirementFailure(ProviderKernelChainProbe& probe)
		{
			probe.retirementFailed_ = true;
		}
		static void MarkCacheUncertain(ProviderKernelChainProbe& probe) { probe.caches_[0].uncertain = true; }
		static void ConfigureIdentity(ProviderKernelChainProbe& probe, decltype(probe.realCreateModule_) module, decltype(probe.realCreateFunction_) function)
		{
			probe.PrepareIdentityOutput();
			probe.realCreateModule_ = module;
			probe.realCreateFunction_ = function;
		}
		static NvAPI_Status Module(ProviderKernelChainProbe& probe, ID3D12Device* device, const void* blob, NvU32 size, NVDX_ObjectHandle* output)
		{
			ProviderKernelChainProbe::owner_ = &probe;
			const auto result = ProviderKernelChainProbe::InterceptModule(device, blob, size, output);
			ProviderKernelChainProbe::owner_ = nullptr;
			return result;
		}
		static NvAPI_Status Function(ProviderKernelChainProbe& probe, ID3D12Device* device, NVDX_ObjectHandle module, const char* name, NVDX_ObjectHandle* output)
		{
			ProviderKernelChainProbe::owner_ = &probe;
			const auto result = ProviderKernelChainProbe::InterceptFunction(device, module, name, output);
			ProviderKernelChainProbe::owner_ = nullptr;
			return result;
		}
		static bool CacheRollback(std::array<void*, 3>& values, unsigned installed, bool conflict)
		{
			nlohmann::json report;
			ProviderKernelChainProbe probe(report, "forward");
			for (std::size_t i = 0; i < values.size(); ++i) {
				auto& cache = probe.caches_[i];
				cache.address = &values[i];
				cache.original = reinterpret_cast<void*>(i + 1);
				cache.hook = reinterpret_cast<void*>(i + 10);
				cache.protection = PAGE_READWRITE;
				values[i] = cache.original;
				if (i < installed)
					ProviderKernelChainProbe::Exchange(cache, cache.original, cache.hook);
			}
			if (conflict)
				values[1] = reinterpret_cast<void*>(99);
			return probe.RestoreCaches();
		}
		static bool CacheRollbackAll(std::array<void*, 8>& values, unsigned installed, int conflict)
		{
			nlohmann::json report;
			ProviderKernelChainProbe probe(report, "forward");
			for (std::size_t i = 0; i < values.size(); ++i) {
				auto& cache = probe.caches_[i];
				cache.address = &values[i];
				cache.original = reinterpret_cast<void*>(i + 1);
				cache.hook = reinterpret_cast<void*>(i + 10);
				cache.protection = PAGE_READWRITE;
				values[i] = cache.original;
				if (i < installed)
					ProviderKernelChainProbe::Exchange(cache, cache.original, cache.hook);
			}
			if (conflict >= 0)
				values[conflict] = reinterpret_cast<void*>(99);
			return probe.RestoreCaches();
		}
		static void ValidatePairManifest(const nlohmann::json& value) { ProviderKernelChainProbe::ValidateReplacementManifest(value, true); }
		static void DescriptorFunctions(ProviderKernelChainProbe& probe, const std::array<void*, 5>& values)
		{
			for (std::size_t i = 0; i < values.size(); ++i) probe.caches_[3 + i].real = values[i];
		}
		static NvAPI_Status DescriptorInvoke(ProviderKernelChainProbe& probe, unsigned api, void* params, ID3D12Device* device,
			D3D12_CPU_DESCRIPTOR_HANDLE a, D3D12_CPU_DESCRIPTOR_HANDLE b, NvU32* output, bool foreign = false)
		{
			ProviderKernelChainProbe::owner_ = &probe;
			NvAPI_Status status = NVAPI_ERROR;
			const auto call = [&] {
				switch (api) {
				case 0:
					status = ProviderKernelChainProbe::InterceptMerged(static_cast<NVAPI_D3D12_GET_CUDA_MERGED_TEXTURE_SAMPLER_OBJECT_PARAMS*>(params));
					break;
				case 1:
					status = ProviderKernelChainProbe::InterceptIndependent(static_cast<NVAPI_D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_PARAMS*>(params));
					break;
				case 2:
					status = ProviderKernelChainProbe::InterceptTexture(device, a, b, output);
					break;
				case 3:
					status = ProviderKernelChainProbe::InterceptSurface(device, a, output);
					break;
				case 4:
					status = ProviderKernelChainProbe::InterceptCaptureUav(device, static_cast<NVAPI_UAV_INFO*>(params));
					break;
				}
			};
			if (foreign) {
				std::thread worker(call);
				worker.join();
			} else
				call();
			ProviderKernelChainProbe::owner_ = nullptr;
			return status;
		}
		static KernelPair::Sample& PairSample(ProviderKernelChainProbe& probe) { return *probe.pairSample_; }
		static nlohmann::json PairReceipt(const ProviderKernelChainProbe& probe) { return probe.PairReceipt(); }
		static void ConfigureRepetition(ProviderKernelChainProbe& probe, ID3D12GraphicsCommandList* list,
			KernelPair::Pairing pairs = KernelPair::kSameEyePairs, std::array<unsigned, 4> gridWidths = { 1, 1, 1, 1 })
		{
			probe.realList_ = list;
			probe.pairSample_->pairs = pairs;
			for (std::size_t region = 0; region < KernelPair::kRegions; ++region) {
				auto& value = probe.pairSample_->regions[region];
				value.complete = true;
				value.target = KernelPair::kTargetStage;
				for (std::size_t stage = 0; stage < KernelPair::kStages; ++stage) {
					const auto id = region * KernelPair::kStages + stage;
					const auto model = probe.ModelEnabled();
					const auto index = stage % KernelReplacement::kFunctionCount;
					std::vector<std::uint8_t> bytes(model ? 72 : 8, static_cast<std::uint8_t>(id));
					NVAPI_CU_KERNEL_LAUNCH_PARAMS launch{ reinterpret_cast<NVDX_ObjectHandle>(model ? 0x3000 + index : 0x3000), { gridWidths[region], 1, model ? 4u : 1u }, { 32, 1, 1 }, 0, nullptr, static_cast<NvU32>(bytes.size()) };
					probe.descriptors_.push_back({ launch, bytes, 0, static_cast<unsigned>(region / 2), static_cast<unsigned>(region % 2), model ? index : SIZE_MAX });
					value.descriptors.push_back(id);
					value.commands.push_back({ KernelPair::Kind::Launch, id });
				}
			}
			probe.pairSample_->completed = KernelPair::kRegions;
			probe.pairSample_->order = KernelPair::BuildOrder(*probe.pairSample_, probe.pairMode_);
			if (probe.ComparisonEnabled())
				probe.pairSample_->controlOrder = KernelPair::BuildOrder(*probe.pairSample_, probe.repetitionControl_);
			if (probe.ModelBatchEnabled())
				probe.PrepareModelBatches();
		}
		static void ReplayRepetition(ProviderKernelChainProbe& probe, const ProviderKernelChainProbe::RepetitionCallback& begin,
			const ProviderKernelChainProbe::RepetitionCallback& end) { probe.ReplayPairSchedule(begin, end); }
		static void PairAdmissionPrerequisites(ProviderKernelChainProbe& probe)
		{
			probe.defaultAllocationConfirmed_.fill(true);
			probe.pairSample_->descriptorCacheConfirmed.fill(true);
		}
		static void PairDependencies(ProviderKernelChainProbe& probe) { probe.ValidatePairDependencies(); }
		static void ConfigurePairBatch(ProviderKernelChainProbe& probe, std::span<const std::uint8_t> a, std::span<const std::uint8_t> b,
			std::array<unsigned, 4> widths = { 20, 20, 20, 20 }, std::array<unsigned, 4> heights = { 20, 20, 20, 20 })
		{
			probe.descriptors_.clear();
			probe.descriptors_.resize(KernelPair::kRegions * KernelPair::kStages);
			for (std::size_t region = 0; region < KernelPair::kRegions; ++region) {
				const auto id = region * KernelPair::kStages + KernelPair::kTargetStage;
				const auto bytes = region % 2 == 0 ? a : b;
				NVAPI_CU_KERNEL_LAUNCH_PARAMS value{ reinterpret_cast<NVDX_ObjectHandle>(0x3000), { widths[region], heights[region], 1 }, { 32, 1, 1 }, 0, bytes.data(), 96 };
				probe.descriptors_[id] = { value, { bytes.begin(), bytes.end() }, 0, static_cast<unsigned>(region / 2), static_cast<unsigned>(region % 2) };
				auto& target = probe.pairSample_->regions[region];
				target.descriptors.resize(KernelPair::kTargetStage + 1);
				target.descriptors[KernelPair::kTargetStage] = id;
				target.stageSignature = std::to_string(widths[region]) + "|" + std::to_string(heights[region]);
			}
			probe.SelectCompatiblePairing();
			for (std::size_t group = 0; group < probe.pairSample_->pairs.size(); ++group)
				for (std::size_t region = 0; region < 2; ++region) {
					const auto id = probe.pairSample_->regions[probe.pairSample_->pairs[group][region]].descriptors[KernelPair::kTargetStage];
					const auto& bytes = probe.descriptors_[id].bytes;
					std::copy(bytes.begin(), bytes.end(), probe.pairSample_->batchPackets[group].begin() + region * 96);
				}
		}
		static NVAPI_CU_KERNEL_LAUNCH_PARAMS& PairTarget(ProviderKernelChainProbe& probe, std::size_t region)
		{
			return probe.descriptors_.at(probe.pairSample_->regions.at(region).descriptors.at(KernelPair::kTargetStage)).value;
		}
		static void SubmitPairBatch(ProviderKernelChainProbe& probe, std::size_t group = 0)
		{
			const auto& pair = probe.pairSample_->pairs.at(group);
			probe.SubmitPairBatch(probe.pairSample_->regions[pair[0]].descriptors[KernelPair::kTargetStage],
				probe.pairSample_->regions[pair[1]].descriptors[KernelPair::kTargetStage], group);
		}
		static void PairBatch(ProviderKernelChainProbe& probe, std::span<const std::uint8_t> a, std::span<const std::uint8_t> b)
		{
			ConfigurePairBatch(probe, a, b);
			SubmitPairBatch(probe);
		}
		static void ValidateReplacement(const nlohmann::json& manifest) { ProviderKernelChainProbe::ValidateReplacementManifest(manifest); }
		static void ConfigureReplacement(ProviderKernelChainProbe& probe, ID3D12Device* device,
			decltype(probe.realDestroyFunction_) destroyFunction, decltype(probe.realDestroyModule_) destroyModule, bool created = true)
		{
			auto& state = *probe.replacement_;
			state.bytes = { 9, 8, 7, 6 };
			state.candidateSha256 = KernelChain::kReplacementCandidateSha256;
			probe.realDestroyFunction_ = destroyFunction;
			probe.realDestroyModule_ = destroyModule;
			ProviderKernelChainProbe::ModuleIdentity module;
			module.device = reinterpret_cast<std::uintptr_t>(device);
			module.handle = 0x5000;
			module.called = module.stable = module.fileComplete = true;
			module.status = NVAPI_OK;
			module.sha256 = KernelChain::kReplacementOriginalModuleSha256;
			probe.modules_.push_back(module);
			if (created) {
				state.device = device;
				state.module = reinterpret_cast<NVDX_ObjectHandle>(0x6000);
				state.function = reinterpret_cast<NVDX_ObjectHandle>(0x7000);
				state.moduleValue = 0x6000;
				state.functionValue = 0x7000;
				state.moduleCalled = state.functionCalled = true;
				state.createModule = state.createFunction = NVAPI_OK;
				ProviderKernelChainProbe::FunctionIdentity function;
				function.device = module.device;
				function.module = module.handle;
				function.handle = 0x3000;
				function.name = KernelChain::kReplacementEntry;
				function.called = function.replacement = true;
				function.status = NVAPI_OK;
				probe.functions_.push_back(function);
			}
		}
		static bool RetireReplacement(ProviderKernelChainProbe& probe) { return probe.RetireReplacement(); }
		static void ConfigureModel(ProviderKernelChainProbe& probe, const std::filesystem::path& manifest, const std::string& fingerprint,
			decltype(probe.realCreateModule_) module, decltype(probe.realCreateFunction_) function,
			decltype(probe.realDestroyFunction_) destroyFunction, decltype(probe.realDestroyModule_) destroyModule, unsigned batchCount = 1)
		{
			probe.modelManifest_ = manifest;
			probe.modelReplacement_ = std::make_unique<KernelReplacement::Set<ProviderKernelChainProbe::ModelOperations>>(
				manifest, fingerprint, batchCount, ProviderKernelChainProbe::ModelOperations{ module, function, destroyFunction, destroyModule });
		}
		static void ObserveTestModel(ProviderKernelChainProbe& probe, ID3D12Device* device, const nlohmann::json& manifest, bool prepare = true)
		{
			for (const auto& item : manifest.at("modules")) {
				ProviderKernelChainProbe::ModuleIdentity module;
				module.device = reinterpret_cast<std::uintptr_t>(device);
				module.handle = 0x5000 + probe.modules_.size();
				module.sha256 = item.at("originalSha256");
				module.called = module.stable = module.fileComplete = true;
				module.status = NVAPI_OK;
				if (prepare)
					probe.modelReplacement_->ObserveOriginalModule(module.device, module.handle, module.sha256);
				for (const auto& entry : item.at("functions")) {
					ProviderKernelChainProbe::FunctionIdentity function;
					function.device = module.device;
					function.module = module.handle;
					function.moduleIdentity = probe.modules_.size();
					function.handle = 0x3000 + probe.functions_.size();
					function.name = entry.at("name");
					function.called = true;
					function.status = NVAPI_OK;
					if (prepare) {
						probe.modelReplacement_->ObserveOriginalFunction(function.device, function.module, function.handle, function.name);
						(void)probe.modelReplacement_->Resolve(function.device, function.handle, module.sha256, function.name, 72, 4);
					}
					probe.functions_.push_back(std::move(function));
				}
				probe.modules_.push_back(std::move(module));
			}
		}
		static void RuntimeBindingDescriptors(ProviderKernelChainProbe& probe, unsigned count)
		{
			for (unsigned i = 0; i < count; ++i) {
				const auto& function = probe.functions_.at(i);
				NVAPI_CU_KERNEL_LAUNCH_PARAMS value{ reinterpret_cast<NVDX_ObjectHandle>(function.handle), { 1, 1, 4 }, { 32, 1, 1 }, 0, nullptr, 72 };
				probe.descriptors_.push_back({ value, std::vector<std::uint8_t>(72), 0, 0, 0, i });
			}
		}
		static void PrepareRuntimeBindings(ProviderKernelChainProbe& probe) { probe.PrepareRuntimeBindings(); }
		static std::array<std::string, 2> StageSignatures(ProviderKernelChainProbe& probe, const NVAPI_CU_KERNEL_LAUNCH_PARAMS& launch)
		{
			ProviderKernelChainProbe::Descriptor descriptor{ launch, {}, 0, 0, 0, 0 };
			return { probe.StageSignature(descriptor), probe.StageSignature(descriptor, false) };
		}
		static void ConfigureStageIdentity(ProviderKernelChainProbe& probe,
			std::string moduleHash = KernelChain::kReplacementOriginalModuleSha256, std::string entry = KernelChain::kReplacementEntry)
		{
			probe.modules_.clear();
			probe.functions_.clear();
			ProviderKernelChainProbe::ModuleIdentity module;
			module.sha256 = std::move(moduleHash);
			probe.modules_.push_back(std::move(module));
			ProviderKernelChainProbe::FunctionIdentity function;
			function.moduleIdentity = 0;
			function.name = std::move(entry);
			probe.functions_.push_back(std::move(function));
		}
		static auto& Model(ProviderKernelChainProbe& probe) { return *probe.modelReplacement_; }
		static void ClearFakeReplacement(ProviderKernelChainProbe& probe)
		{
			probe.replacement_->module = probe.replacement_->function = nullptr;
			probe.replacement_->device.Reset();
		}
		static void AddOtherFunction(ProviderKernelChainProbe& probe, std::uintptr_t handle)
		{
			auto function = probe.functions_.empty() ? ProviderKernelChainProbe::FunctionIdentity{} : probe.functions_.front();
			function.handle = handle;
			function.name = "other_provider_kernel";
			function.called = true;
			function.status = NVAPI_OK;
			function.replacement = false;
			probe.functions_.push_back(function);
		}
	};
}

namespace
{
	using NrReplay::KernelChainProbeTestAccess;
	using NrReplay::ProviderKernelChainProbe;
	using Json = nlohmann::json;
	unsigned checks = 0;
	void Check(bool value)
	{
		++checks;
		if (!value)
			throw std::runtime_error("kernel-chain assertion failed at check " + std::to_string(checks));
	}
	template <class Function>
	void Reject(Function&& function)
	{
		try {
			function();
		} catch (const std::exception&) {
			++checks;
			return;
		}
		throw std::runtime_error("kernel-chain invalid input was admitted");
	}
	struct Submission
	{
		ID3D12GraphicsCommandList* list;
		std::vector<std::vector<std::uint8_t>> bytes;
		std::vector<const void*> pointers;
		std::vector<NVAPI_CU_KERNEL_LAUNCH_PARAMS> descriptors;
	};
	std::vector<Submission> submissions;
	const ProviderKernelChainProbe* inspectLaunchProbe = nullptr;
	std::vector<std::size_t> privateAttemptsAtCall;
	std::vector<std::string> retirementCalls;
	bool failPrivateModule = false, failPrivateFunction = false, failDestroyFunction = false, failDestroyModule = false;
	NVDX_ObjectHandle privateFunctionHandle = reinterpret_cast<NVDX_ObjectHandle>(0x7000);
	NvAPI_Status __cdecl PrivateModule(ID3D12Device*, const void*, NvU32, NVDX_ObjectHandle* output)
	{
		if (failPrivateModule)
			return NVAPI_ERROR;
		*output = reinterpret_cast<NVDX_ObjectHandle>(0x6000);
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl PrivateFunction(ID3D12Device*, NVDX_ObjectHandle module, const char*, NVDX_ObjectHandle* output)
	{
		if (module == reinterpret_cast<NVDX_ObjectHandle>(0x5000)) {
			*output = reinterpret_cast<NVDX_ObjectHandle>(0x3000);
			return NVAPI_OK;
		}
		if (failPrivateFunction)
			return NVAPI_ERROR;
		*output = privateFunctionHandle;
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl AliasPrivateFunction(ID3D12Device*, NVDX_ObjectHandle, const char*, NVDX_ObjectHandle* output)
	{
		*output = reinterpret_cast<NVDX_ObjectHandle>(0x7000);
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl DestroyPrivateFunction(ID3D12Device*, NVDX_ObjectHandle handle)
	{
		Check(handle == reinterpret_cast<NVDX_ObjectHandle>(0x7000));
		retirementCalls.push_back("function");
		return failDestroyFunction ? NVAPI_ERROR : NVAPI_OK;
	}
	NvAPI_Status __cdecl DestroyPrivateModule(ID3D12Device*, NVDX_ObjectHandle handle)
	{
		Check(handle == reinterpret_cast<NVDX_ObjectHandle>(0x6000));
		retirementCalls.push_back("module");
		return failDestroyModule ? NVAPI_ERROR : NVAPI_OK;
	}
	struct FakeDevice final : IUnknown
	{
		ULONG references = 1;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
		ULONG STDMETHODCALLTYPE Release() override { return --references; }
		ID3D12Device* AsDevice() { return reinterpret_cast<ID3D12Device*>(this); }
	};
	struct FakeScheduleList
	{
		void** vtable = methods.data();
		std::array<void*, 29> methods{};
		std::size_t barriers = 0;
		FakeScheduleList() { methods[26] = reinterpret_cast<void*>(&Barrier); }
		static void STDMETHODCALLTYPE Barrier(FakeScheduleList* self, UINT count, const D3D12_RESOURCE_BARRIER* values)
		{
			Check(count == 1 && values && values[0].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV && values[0].UAV.pResource == nullptr);
			++self->barriers;
		}
		ID3D12GraphicsCommandList* AsList() { return reinterpret_cast<ID3D12GraphicsCommandList*>(this); }
	};
	NvAPI_Status submitStatus = NVAPI_OK;
	unsigned moduleCalls = 0, functionCalls = 0;
	NvAPI_Status creationStatus = NVAPI_OK;
	bool mutateBlob = false, mutateName = false;
	const void* forwardedBlob = nullptr;
	const char* forwardedName = nullptr;
	auto* const identityTestDevice = reinterpret_cast<ID3D12Device*>(0x4000);
	NvAPI_Status __cdecl FakeModule(ID3D12Device* inputDevice, const void* blob, NvU32 size, NVDX_ObjectHandle* output)
	{
		Check(inputDevice == identityTestDevice && size > 0);
		++moduleCalls;
		forwardedBlob = blob;
		if (creationStatus == NVAPI_OK)
			*output = reinterpret_cast<NVDX_ObjectHandle>(0x5000);
		if (mutateBlob)
			++*static_cast<std::uint8_t*>(const_cast<void*>(blob));
		return creationStatus;
	}
	NvAPI_Status __cdecl FakeFunction(ID3D12Device* inputDevice, NVDX_ObjectHandle module, const char* name, NVDX_ObjectHandle* output)
	{
		Check(inputDevice == identityTestDevice && module == reinterpret_cast<NVDX_ObjectHandle>(0x5000));
		++functionCalls;
		forwardedName = name;
		if (creationStatus == NVAPI_OK)
			*output = reinterpret_cast<NVDX_ObjectHandle>(0x3000);
		if (mutateName)
			++*const_cast<char*>(name);
		return creationStatus;
	}
	std::filesystem::path UniqueIdentityPath()
	{
		static unsigned ordinal = 0;
		return std::filesystem::temp_directory_path() / ("nr-kernel-identity-cpu-" + std::to_string(GetCurrentProcessId()) + "-" +
															std::to_string(GetTickCount64()) + "-" + std::to_string(ordinal++));
	}
	NvAPI_Status __cdecl FakeLaunch(ID3D12GraphicsCommandList* list, const NVAPI_CU_KERNEL_LAUNCH_PARAMS* values, NvU32 count)
	{
		if (inspectLaunchProbe)
			privateAttemptsAtCall.push_back(inspectLaunchProbe->GetRuntimeCounters().privateAttempts);
		Submission value{ list, {}, {}, {} };
		for (NvU32 i = 0; i < count; ++i) {
			const auto* first = static_cast<const std::uint8_t*>(values[i].pParams);
			value.bytes.emplace_back(first, first + values[i].paramSize);
			value.pointers.push_back(first);
			value.descriptors.push_back(values[i]);
		}
		submissions.push_back(std::move(value));
		return submitStatus;
	}
	auto* const proxy = reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000);
	auto* const real = reinterpret_cast<ID3D12GraphicsCommandList*>(0x2000);
	NVAPI_CU_KERNEL_LAUNCH_PARAMS Packet(std::span<const std::uint8_t> bytes)
	{
		return { reinterpret_cast<NVDX_ObjectHandle>(0x3000), { 4, 8, 1 }, { 32, 1, 1 }, 0, bytes.data(), static_cast<NvU32>(bytes.size()) };
	}
	void Feed(ProviderKernelChainProbe& probe, unsigned region, std::span<const std::uint8_t> bytes)
	{
		probe.BeginEvaluation(proxy, real, 0, region);
		const auto packet = Packet(bytes);
		Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &packet, 1) == NVAPI_OK);
		probe.EndEvaluation();
	}
	void TestOwnedPacketsAndBoundaries()
	{
		submissions.clear();
		Json report;
		ProviderKernelChainProbe probe(report, "group");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		probe.BeginSample(16, false);
		std::array<std::uint8_t, 4> bytes{ 1, 2, 3, 4 };
		Feed(probe, 0, bytes);
		bytes[0] = 5;
		Feed(probe, 1, bytes);
		bytes.fill(99);
		Check(submissions.empty());
		Check(probe.BeforeCommand("ResourceBarrier"));
		Check(submissions.size() == 1 && submissions[0].bytes.size() == 2 && submissions[0].list == real);
		Check(submissions[0].bytes[0] == std::vector<std::uint8_t>({ 1, 2, 3, 4 }));
		Check(submissions[0].bytes[1] == std::vector<std::uint8_t>({ 5, 2, 3, 4 }));
		Check(submissions[0].pointers[0] != bytes.data() && submissions[0].pointers[1] != bytes.data());
		Feed(probe, 2, bytes);
		probe.FinishCommandList();
		Check(submissions.size() == 2 && submissions[1].bytes.size() == 1);
		const auto receipt = probe.SampleReceipt();
		Check(receipt["observedDescriptors"] == 3 && receipt["submittedDescriptors"] == 3);
		Check(receipt["multiDescriptorCalls"] == 1 && receipt["crossRegionCalls"] == 1 && receipt["maxRegionsPerSubmission"] == 2);
		Check(receipt["pendingDescriptors"] == 0 && receipt["failed"] == false);
		std::vector<Json> observed, submitted;
		for (const auto& event : receipt["events"]) {
			if (event["kind"] == "boundary")
				Check(event["pendingDescriptorsAfter"] == 0);
			for (const auto& d : event["descriptors"])
				(event["kind"] == "observed" ? observed : submitted).push_back(d);
		}
		Check(observed == submitted);
		Check(report["qualityQualified"] == false && report["productionPerformanceQualified"] == false);
	}
	void TestGrowthAndCapacity()
	{
		submissions.clear();
		Json report;
		ProviderKernelChainProbe probe(report, "group");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		probe.BeginSample(16, false);
		for (unsigned i = 0; i < 130; ++i) {
			const std::array<std::uint8_t, 4> bytes{ static_cast<std::uint8_t>(i), 9, 8, 7 };
			Feed(probe, i, bytes);
		}
		probe.FinishCommandList();
		Check(submissions.size() == 3 && submissions[0].bytes.size() == 64 && submissions[1].bytes.size() == 64 && submissions[2].bytes.size() == 2);
		unsigned index = 0;
		for (const auto& submission : submissions)
			for (const auto& bytes : submission.bytes)
				Check(bytes == std::vector<std::uint8_t>({ static_cast<std::uint8_t>(index++), 9, 8, 7 }));
		Check(probe.SampleReceipt()["maxSubmittedDescriptors"] == 64);
	}
	void TestForwardWarmupAndFailure()
	{
		for (const auto mode : { "forward", "group" }) {
			submissions.clear();
			Json report;
			ProviderKernelChainProbe probe(report, mode);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			probe.BeginSample(0, true);
			const std::array<std::uint8_t, 1> bytes{ 7 };
			Feed(probe, 0, bytes);
			Feed(probe, 1, bytes);
			Check(submissions.size() == 2);
			probe.FinishCommandList();
			Check(probe.SampleReceipt()["multiDescriptorCalls"] == 0);
		}
		submissions.clear();
		Json report;
		ProviderKernelChainProbe probe(report, "group");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		probe.BeginSample(1, false);
		const std::array<std::uint8_t, 1> bytes{ 8 };
		Feed(probe, 0, bytes);
		submitStatus = NVAPI_ERROR;
		Check(!probe.BeforeCommand("ResourceBarrier"));
		Check(!probe.BeforeCommand("CopyResource"));
		Check(submissions.size() == 1 && !probe.Healthy());
		Reject([&] { probe.FinishCommandList(); });
		Check(probe.SampleReceipt()["failed"] == true);
		submitStatus = NVAPI_OK;
	}
	void TestAdmissionAndAbiFailure()
	{
		using namespace NrReplay::KernelChain;
		Reject([] { (void)GroupMode("unknown"); });
		Reject([] { ValidateDescriptorBudget(65, 0, 1, 0); });
		Reject([] { ValidateDescriptorBudget(1, kMaximumSampleDescriptors, 1, 0); });
		Reject([] { ValidateDescriptorBudget(1, 0, kMaximumParameterBytes + 1, 0); });
		Reject([] { ValidateDescriptorBudget(1, 0, 1, kMaximumSampleBytes); });
		Json report;
		ProviderKernelChainProbe probe(report, "forward");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		probe.BeginSample(0, false);
		probe.BeginEvaluation(proxy, real, 0, 0);
		const auto packet = Packet(std::span<const std::uint8_t>());
		Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &packet, 1) == NVAPI_ERROR);
		Check(!probe.Healthy());
		Reject([&] { probe.EndEvaluation(); });
		KernelChainProbeTestAccess::MarkRetirementFailure(probe);
		Check(!probe.Restore(true));
		Check(!probe.Restore(false));
	}
	void TestForeignThreadBoundary()
	{
		submissions.clear();
		Json report;
		ProviderKernelChainProbe probe(report, "group");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		probe.BeginSample(16, false);
		const std::array<std::uint8_t, 1> bytes{ 7 };
		Feed(probe, 0, bytes);
		bool admitted = true;
		std::thread worker([&] { admitted = probe.BeforeCommand("ResourceBarrier"); });
		worker.join();
		Check(!admitted && !probe.Healthy() && submissions.empty());
		const auto receipt = probe.SampleReceipt();
		Check(receipt["pendingDescriptors"] == 1 && receipt["submittedDescriptors"] == 0);
		Check(receipt["reason"] == "kernel-chain command escaped the owned recording thread");
	}
	void TestRestorationContract()
	{
		struct Operations
		{
			void* value = reinterpret_cast<void*>(1);
			bool writable = true, protection = true, failRollback = false;
			unsigned failures = 0, restores = 0, exchanges = 0;
			bool MakeWritable() { return writable; }
			void* Read() { return value; }
			bool ProtectionMatches() { return protection; }
			bool RestoreProtection() { return protection = ++restores > failures; }
			void* CompareExchange(void* next, void* expected)
			{
				const auto previous = value;
				if (value == expected && !(failRollback && exchanges != 0))
					value = next;
				++exchanges;
				return previous;
			}
		};
		void* original = reinterpret_cast<void*>(1);
		void* hook = reinterpret_cast<void*>(2);
		Operations successful;
		Check(NrReplay::KernelChain::ExchangePointer(successful, original, hook).succeeded);
		Check(NrReplay::KernelChain::ExchangePointer(successful, hook, original).succeeded);
		Operations rollback;
		rollback.failures = 1;
		const auto restored = NrReplay::KernelChain::ExchangePointer(rollback, original, hook);
		Check(!restored.succeeded && restored.rollbackProven && restored.observed == original);
		Operations uncertain;
		uncertain.failures = 2;
		const auto protectionFailure = NrReplay::KernelChain::ExchangePointer(uncertain, original, hook);
		Check(!protectionFailure.succeeded && !protectionFailure.rollbackProven && !protectionFailure.protectionRestored);
		Operations badRollback;
		badRollback.failures = 1;
		badRollback.failRollback = true;
		const auto stillHooked = NrReplay::KernelChain::ExchangePointer(badRollback, original, hook);
		Check(!stillHooked.succeeded && !stillHooked.rollbackProven && stillHooked.observed == hook);
		Operations conflict;
		conflict.value = reinterpret_cast<void*>(3);
		const auto foreign = NrReplay::KernelChain::ExchangePointer(conflict, original, hook);
		Check(!foreign.succeeded && !foreign.rollbackProven && foreign.observed == conflict.value);
	}
	void TestRuntimeFrameOwnership()
	{
		const auto admittedCatalog = [](std::string_view hash, bool pair, bool runtime) {
			const auto catalogs = NrReplay::KernelChain::ModelCatalogs(pair, runtime);
			return std::ranges::find(catalogs, hash) != catalogs.end();
		};
		for (const bool runtime : { false, true }) {
			Check(admittedCatalog("2b4b945d3ac65ebc5c7d9f2ab72405d892412d59d875864c51edb611f7bba6c8", false, runtime));
			Check(admittedCatalog("094ecf56151133f64e9c57553d5aabf62fd4bea615d4e75b071301005b804597", true, runtime));
			Check(!admittedCatalog("7f59c83db564ada63d50cd1d8e869aa1f66c898954c98ff11ca81b6a86e64315", false, runtime));
			Check(!admittedCatalog("d55fde8e1ea12ab25f43c14a671b079216ac877e0a4ca019a8c6efa9fb52dd06", true, runtime));
			Check(admittedCatalog("2f07438e084824246931f8f2712d395cd6fc15cab267e69b4e99db3af9238b60", false, runtime) == !runtime);
			Check(!admittedCatalog(NrReplay::KernelChain::kSharedModelControlCatalogSha256, true, runtime));
			Check(!admittedCatalog(NrReplay::KernelChain::kSharedModelCatalogSha256, false, runtime));
		}
		Check(NrReplay::KernelChain::ReplacementCandidateAdmitted(NrReplay::KernelChain::kCorrectedIndexedCandidateSha256, false));
		Check(NrReplay::KernelChain::ReplacementCandidateAdmitted(NrReplay::KernelChain::kCorrectedIndexedPairCandidateSha256, true));
		Check(!NrReplay::KernelChain::ReplacementCandidateAdmitted(NrReplay::KernelChain::kCorrectedIndexedPairCandidateSha256, false));
		Check(!NrReplay::KernelChain::ReplacementCandidateAdmitted(NrReplay::KernelChain::kCorrectedIndexedCandidateSha256, true));
		Json report;
		ProviderKernelChainProbe probe(report, "forward", {}, {}, "original", {}, NrReplay::KernelPair::kStages, 1, {}, true);
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		KernelChainProbeTestAccess::ConfigureIdentity(probe, FakeModule, FakeFunction);
		std::array<std::shared_ptr<void>, 3> retained;
		std::array<std::uint8_t, 4> source{ 1, 2, 3, 4 };
		const auto* stable = static_cast<const std::uint8_t*>(nullptr);
		for (unsigned frame = 0; frame < 160; ++frame) {
			probe.BeginSample(frame, true);
			Reject([&] { (void)probe.TakeRuntimeFrame(); });
			probe.BeginEvaluation(proxy, real, 0, 0);
			if (frame == 0) {
				NVDX_ObjectHandle module = nullptr, function = nullptr;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, source.data(), 4, &module) == NVAPI_OK);
				Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, "test_kernel", &function) == NVAPI_OK);
			}
			auto launch = Packet(source);
			Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &launch, 1) == NVAPI_OK);
			probe.EndEvaluation();
			probe.FinishCommandList();
			const auto count = probe.GetRuntimeCounters();
			Check(count.logical == 1 && count.physical == 1 && count.privateLaunches == 0 && count.privateAttempts == 0);
			if (!frame)
				stable = KernelChainProbeTestAccess::FirstOwnedPacket(probe);
			auto owner = probe.TakeRuntimeFrame();
			Check(owner && KernelChainProbeTestAccess::RuntimeStoredFrames(probe) == 0);
			if (frame < 3)
				Check(std::memcmp(stable, source.data(), source.size()) == 0);
			std::weak_ptr<void> previous = retained[frame % 3];
			retained[frame % 3] = std::move(owner);
			Check(previous.expired());
		}
		Check(probe.SampleReceipt()["events"].empty());
		Check(probe.GetRuntimeGraph().qualified == false);
		auto launch = Packet(source);
		submitStatus = NVAPI_ERROR;
		Check(KernelChainProbeTestAccess::Invoke(probe, real, &launch, 1) == NVAPI_ERROR);
		submitStatus = NVAPI_OK;
		Check(probe.RuntimeEpochStale() && probe.Healthy());
		Reject([&] { probe.BeginSample(161, true); });
		NVDX_ObjectHandle module = nullptr, function = nullptr;
		Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, source.data(), 4, &module) == NVAPI_OK);
		Check(forwardedBlob == source.data());
		const char name[] = "outside_frame";
		Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name, &function) == NVAPI_OK);
		Check(forwardedName == name && probe.Healthy());
		Json rejected;
		Reject([&] { ProviderKernelChainProbe invalid(rejected, "group", {}, {}, "original", {}, NrReplay::KernelPair::kStages, 1, {}, true); });
		Reject([&] { ProviderKernelChainProbe invalid(rejected, "forward", {}, {}, "original", {}, NrReplay::KernelPair::kStages, 2, {}, true); });
	}
	void TestRuntimeFallbackClassification()
	{
		for (unsigned scenario = 0; scenario < 7; ++scenario) {
			Json report;
			ProviderKernelChainProbe probe(report, "forward", {}, {}, "original", {}, NrReplay::KernelPair::kStages, 1, {}, true);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			probe.BeginSample(3, false);
			if (scenario != 6)
				probe.BeginEvaluation(proxy, real, 0, 0);
			Check(!probe.CanRetryOriginalBeforeSubmission());
			if (scenario == 1)
				Check(!probe.BeforeCommand("Dispatch"));
			if (scenario == 2) {
				bool result = true;
				std::thread worker([&] { result = probe.BeforeCommand("Dispatch"); });
				worker.join();
				Check(!result);
			}
			if (scenario == 3)
				KernelChainProbeTestAccess::MarkRetirementFailure(probe);
			if (scenario == 4)
				KernelChainProbeTestAccess::MarkCacheUncertain(probe);
			FakeScheduleList list;
			if (scenario == 5) {
				KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
				submitStatus = NVAPI_ERROR;
				Reject([&] { KernelChainProbeTestAccess::ReplayRepetition(probe, {}, {}); });
				submitStatus = NVAPI_OK;
				Check(probe.GetRuntimeCounters().physical == 1 && probe.GetRuntimeCounters().privateAttempts == 0);
			}
			if (scenario == 6) {
				KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
				Reject([&] { probe.FinishCommandList(); });
				Check(probe.GetRuntimeCounters().physical == 0 && probe.GetRuntimeCounters().privateAttempts == 0);
			} else
				Reject([&] { probe.EndEvaluation(); });
			Check(probe.CanRetryOriginalBeforeSubmission() == (scenario < 2 || scenario == 6));
			probe.AbortRuntimeFrame();
			Check(!probe.CanRetryOriginalBeforeSubmission());
		}
	}
	void TestIdentityCapture()
	{
		const auto path = UniqueIdentityPath();
		Json report;
		ProviderKernelChainProbe probe(report, "forward", path);
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		KernelChainProbeTestAccess::ConfigureIdentity(probe, FakeModule, FakeFunction);
		std::array<std::uint8_t, 4> bytes{ 1, 2, 3, 4 };
		NVDX_ObjectHandle module = nullptr, function = nullptr;
		Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_OK);
		Check(forwardedBlob == bytes.data() && module == reinterpret_cast<NVDX_ObjectHandle>(0x5000));
		const char name[] = "test_kernel";
		Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name, &function) == NVAPI_OK);
		Check(forwardedName == name && function == reinterpret_cast<NVDX_ObjectHandle>(0x3000));
		Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_OK);
		probe.BeginSample(0, false);
		Feed(probe, 0, bytes);
		probe.FinishCommandList();
		const auto receipt = probe.SampleReceipt();
		Check(receipt["mappingComplete"] == true && receipt["failed"] == false);
		const auto& capture = receipt["identityCapture"];
		Check(capture["modules"].size() == 2 && capture["functions"].size() == 1 && capture["capturedBytes"] == 8);
		Check(capture["modules"][0]["stableAtObservedBoundaries"] == true && capture["functions"][0]["name"] == name);
		const auto blobPath = path / capture["modules"][0]["file"].get<std::string>();
		std::ifstream input(blobPath, std::ios::binary);
		const std::vector<std::uint8_t> saved(std::istreambuf_iterator<char>(input), {});
		Check(saved == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
		input.close();
		for (const auto& event : receipt["events"])
			for (const auto& packet : event["descriptors"]) {
				Check(packet["functionIdentity"] == 0 && packet["functionName"] == name);
				Check(packet["moduleBlobSha256"] == capture["modules"][0]["sha256"]);
			}
		Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name, &function) == NVAPI_OK);
		Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, "different_kernel", &function) == NVAPI_ERROR);
		Check(!probe.Healthy());
		Json duplicateReport;
		ProviderKernelChainProbe duplicate(duplicateReport, "forward", path);
		Reject([&] { KernelChainProbeTestAccess::ConfigureIdentity(duplicate, FakeModule, FakeFunction); });
		std::filesystem::remove_all(path);
	}
	struct ModelFixture
	{
		std::filesystem::path directory, manifest;
		std::array<std::uint8_t, 4> original{};
		std::string originalSha256, fingerprint;
		ModelFixture(unsigned batchCount = 1)
		{
			directory = UniqueIdentityPath();
			Check(std::filesystem::create_directories(directory));
			manifest = directory / "model.json";
			std::array<std::uint8_t, 64> candidate{};
			candidate[0] = 0x7f;
			candidate[1] = 'E';
			candidate[2] = 'L';
			candidate[3] = 'F';
			const auto candidatePath = directory / "candidate.cubin";
			{
				std::ofstream out(candidatePath, std::ios::binary);
				out.write(reinterpret_cast<const char*>(candidate.data()), candidate.size());
			}
			const auto hash = [](const auto& bytes) { return Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(std::span(bytes)))); };
			std::vector<std::pair<std::string, std::array<std::uint8_t, 4>>> originals;
			for (std::uint8_t i = 0; i < 9; ++i) {
				const std::array<std::uint8_t, 4> bytes{ i, 9, 8, 7 };
				originals.emplace_back(hash(bytes), bytes);
			}
			std::ranges::sort(originals);
			originalSha256 = originals.front().first;
			original = originals.front().second;
			Json value{ { "schema", "nr-model-kernel-replacement-v1" }, { "batchCount", batchCount }, { "modules", Json::array() } };
			for (std::size_t m = 0; m < originals.size(); ++m) {
				Json functions = Json::array();
				for (unsigned i = 0; i < (m == 8 ? 4u : 5u); ++i)
					functions.push_back({ { "name", "entry" + std::to_string(i) }, { "paramSize", 72 }, { "gridZ", 4 } });
				value["modules"].push_back({ { "originalSha256", originals[m].first }, { "sha256", hash(candidate) },
					{ "path", candidatePath.string() }, { "functions", functions } });
			}
			Json semantic = value;
			semantic.erase("schema");
			for (auto& module : semantic["modules"]) module.erase("path");
			fingerprint = Util::CryptoHash::Sha256Hex(semantic.dump());
			std::ofstream(manifest) << value.dump();
		}
		~ModelFixture() { std::filesystem::remove_all(directory); }
	};
	NvAPI_Status __cdecl ModelOriginalModule(ID3D12Device* inputDevice, const void* blob, NvU32 size, NVDX_ObjectHandle* output)
	{
		Check(inputDevice && blob && size == 4);
		++moduleCalls;
		forwardedBlob = blob;
		*output = reinterpret_cast<NVDX_ObjectHandle>(0x5000);
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl ModelOriginalFunction(ID3D12Device* inputDevice, NVDX_ObjectHandle module, const char* name, NVDX_ObjectHandle* output)
	{
		Check(inputDevice && module == reinterpret_cast<NVDX_ObjectHandle>(0x5000));
		++functionCalls;
		forwardedName = name;
		*output = reinterpret_cast<NVDX_ObjectHandle>(0x3000);
		return NVAPI_OK;
	}
	unsigned comparisonModules = 0, comparisonFunctions = 0, comparisonRetired = 0;
	NvAPI_Status __cdecl ComparisonModule(ID3D12Device*, const void*, NvU32, NVDX_ObjectHandle* output)
	{
		*output = reinterpret_cast<NVDX_ObjectHandle>(std::uintptr_t{ 0x9000 } + comparisonModules++);
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl ComparisonFunction(ID3D12Device*, NVDX_ObjectHandle, const char*, NVDX_ObjectHandle* output)
	{
		*output = reinterpret_cast<NVDX_ObjectHandle>(std::uintptr_t{ 0xa000 } + comparisonFunctions++);
		return NVAPI_OK;
	}
	NvAPI_Status __cdecl ComparisonDestroy(ID3D12Device*, NVDX_ObjectHandle)
	{
		++comparisonRetired;
		return NVAPI_OK;
	}
	void TestDeferredRuntimePreparation()
	{
		for (unsigned scenario = 0; scenario < 3; ++scenario) {
			ModelFixture fixture(2);
			FakeDevice device;
			Json receipt;
			ProviderKernelChainProbe probe(receipt, "forward", {}, {}, "model-batch", fixture.manifest, NrReplay::KernelPair::kStages, 1, {}, true);
			KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 2);
			comparisonModules = comparisonFunctions = comparisonRetired = 0;
			Json manifest = Json::parse(std::ifstream(fixture.manifest));
			KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), manifest, false);
			KernelChainProbeTestAccess::RuntimeBindingDescriptors(probe, scenario == 1 ? 43 : 44);
			Check(comparisonModules == 0 && comparisonFunctions == 0);
			if (scenario == 2)
				KernelChainProbeTestAccess::AddOtherFunction(probe, 0xa000);
			if (scenario == 0) {
				KernelChainProbeTestAccess::PrepareRuntimeBindings(probe);
				Check(comparisonModules == 9 && comparisonFunctions == 44 && probe.Healthy());
				Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) && comparisonRetired == 53 && device.references == 1);
			} else {
				Reject([&] { KernelChainProbeTestAccess::PrepareRuntimeBindings(probe); });
				Check(!probe.Healthy());
				Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) == (scenario == 1));
				Check(scenario == 1 ? comparisonRetired == 52 && device.references == 1 : comparisonRetired == 0 && device.references == 2);
			}
		}
	}
	void TestModelN1StagePrefix()
	{
		using namespace NrReplay::KernelPair;
		for (unsigned scenario = 0; scenario < 6; ++scenario) {
			const unsigned limit = scenario == 0 ? 1 : scenario == 1 ? 25 :
			                                       scenario == 2     ? 157 :
			                                       scenario == 3     ? 158 :
			                                                           1;
			ModelFixture fixture(1);
			FakeDevice device;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", {}, "original", fixture.manifest, kStages, 1, {}, false, limit);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			comparisonModules = comparisonFunctions = comparisonRetired = 0;
			KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 1);
			KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), Json::parse(std::ifstream(fixture.manifest)));
			Check(comparisonModules == 9 && comparisonFunctions == 44);
			probe.BeginSample(0, true);
			submissions.clear();
			submitStatus = NVAPI_OK;
			for (unsigned region = 0; region < kRegions; ++region) {
				probe.BeginEvaluation(proxy, real, region / 2, region % 2);
				for (unsigned stage = 0; stage < kStages - (scenario == 4 && region == 3 ? 1 : 0); ++stage) {
					std::array<std::uint8_t, 72> bytes{};
					bytes[0] = static_cast<std::uint8_t>(stage);
					const auto index = stage % NrReplay::KernelReplacement::kFunctionCount;
					NVAPI_CU_KERNEL_LAUNCH_PARAMS value{ reinterpret_cast<NVDX_ObjectHandle>(std::uintptr_t{ 0x3000 } + index), { 1, 1, 4 }, { 32, 1, 1 }, 0, bytes.data(), 72 };
					Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &value, 1) == NVAPI_OK);
					const auto& submitted = submissions.back();
					Check(submitted.descriptors[0].hFunction == reinterpret_cast<NVDX_ObjectHandle>((stage < limit ? std::uintptr_t{ 0xa000 } : std::uintptr_t{ 0x3000 }) + index));
					Check(submitted.bytes[0] == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
				}
				probe.EndEvaluation();
			}
			if (scenario == 5)
				KernelChainProbeTestAccess::Model(probe).RecordSubmission(0, 0);
			if (scenario >= 4)
				Reject([&] { probe.FinishCommandList(); });
			else {
				probe.FinishCommandList();
				Check(probe.SampleReceipt()["modelN1StageLimit"] == limit);
				Check(probe.GetRuntimeCounters().privateAttempts == limit * kRegions);
			}
			Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) && comparisonRetired == 53 && device.references == 1);
		}
		Json report;
		for (const auto count : { 0u, 159u, UINT_MAX })
			Reject([&] { ProviderKernelChainProbe probe(report, "forward", "identity", {}, "original", "manifest", kStages, 1, {}, false, count); });
		Reject([&] { ProviderKernelChainProbe probe(report, "forward", {}, {}, "original", "manifest", kStages, 1, {}, true, 1); });
		Reject([&] { ProviderKernelChainProbe probe(report, "forward", "identity", {}, "model-batch", "manifest", kStages, 1, {}, false, 1); });
	}
	void TestModelObserverAndFailedSubmission()
	{
		for (unsigned scenario = 0; scenario < 4; ++scenario) {
			ModelFixture fixture;
			FakeDevice ownedDevice, otherDevice;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", fixture.directory / "capture");
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			KernelChainProbeTestAccess::ConfigureIdentity(probe, ModelOriginalModule, ModelOriginalFunction);
			KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, PrivateModule, PrivateFunction, DestroyPrivateFunction, DestroyPrivateModule);
			auto& model = KernelChainProbeTestAccess::Model(probe);
			failPrivateModule = failPrivateFunction = failDestroyFunction = failDestroyModule = false;
			privateFunctionHandle = reinterpret_cast<NVDX_ObjectHandle>(0x7000);
			retirementCalls.clear();
			submissions.clear();
			submitStatus = NVAPI_OK;
			if (scenario == 0)
				model.ObserveOriginalModule(reinterpret_cast<std::uintptr_t>(otherDevice.AsDevice()), 0x9000, fixture.originalSha256);
			NVDX_ObjectHandle module = nullptr, function = nullptr;
			const auto callsBefore = moduleCalls;
			Check(KernelChainProbeTestAccess::Module(probe, ownedDevice.AsDevice(), fixture.original.data(), 4, &module) == NVAPI_OK);
			Check(moduleCalls == callsBefore + 1 && forwardedBlob == fixture.original.data() && module == reinterpret_cast<NVDX_ObjectHandle>(0x5000));
			if (scenario != 0) {
				if (scenario == 1)
					model.ObserveOriginalFunction(reinterpret_cast<std::uintptr_t>(ownedDevice.AsDevice()), 0x5000, 0x3000, "conflicting_entry");
				const char name[] = "entry0";
				const auto functionsBefore = functionCalls;
				Check(KernelChainProbeTestAccess::Function(probe, ownedDevice.AsDevice(), module, name, &function) == NVAPI_OK);
				Check(functionCalls == functionsBefore + 1 && forwardedName == name && function == reinterpret_cast<NVDX_ObjectHandle>(0x3000));
			}
			if (scenario >= 2) {
				probe.BeginSample(0, true);
				probe.BeginEvaluation(proxy, real, 0, 0);
				const std::array<std::uint8_t, 72> bytes{};
				auto packet = Packet(bytes);
				packet.gridDim.z = 4;
				submitStatus = scenario == 3 ? NVAPI_ERROR : NVAPI_OK;
				Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &packet, 1) == submitStatus);
				if (scenario == 2)
					probe.EndEvaluation();
				else
					Reject([&] { probe.EndEvaluation(); });
				Reject([&] { probe.FinishCommandList(); });
				const auto receipt = probe.SampleReceipt();
				Check(receipt["submittedCalls"] == 1 && receipt["submittedDescriptors"] == 1 && receipt["pendingDescriptors"] == 0);
				unsigned submitted = 0;
				for (const auto& event : receipt["events"])
					if (event["kind"] == "submitted") {
						++submitted;
						Check(event["status"] == static_cast<int>(submitStatus) && event["descriptors"].size() == 1);
						const auto& d = event["descriptors"][0];
						Check(d["originalFunction"] == 0x3000 && d["submittedFunction"] == 0x7000 && d["replacementApplied"] == true);
					}
				Check(submitted == 1 && submissions.size() == 1);
				const auto& entry = receipt["modelReplacement"]["functions"][0];
				Check(entry["attempted"] == 1 && entry["successful"] == (scenario == 2 ? 1 : 0));
			}
			Check(!probe.Healthy() && !model.Healthy() && model.Receipt()["failed"] == true);
			Check(model.Retire(true, true) && ownedDevice.references == 1 && otherDevice.references == 1);
			submitStatus = NVAPI_OK;
		}
	}
	void TestIdentityRejections()
	{
		using namespace NrReplay::KernelChain;
		Reject([] { ValidateModuleBudget(0, 0, 0); });
		Reject([] { ValidateModuleBudget(kMaximumModuleBytes + 1, 0, 0); });
		Reject([] { ValidateModuleBudget(1, kMaximumModules, 0); });
		Reject([] { ValidateModuleBudget(1, 0, kMaximumCapturedModuleBytes); });
		for (unsigned scenario = 0; scenario < 17; ++scenario) {
			const auto path = UniqueIdentityPath();
			Json report;
			ProviderKernelChainProbe probe(report, "forward", path);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			KernelChainProbeTestAccess::ConfigureIdentity(probe, FakeModule, FakeFunction);
			std::array<std::uint8_t, 4> bytes{ 1, 2, 3, 4 };
			NVDX_ObjectHandle module = nullptr, function = nullptr;
			if (scenario == 14) {
				auto* guard = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
				Check(guard != nullptr);
				const auto calls = moduleCalls;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, guard, 4, &module) == NVAPI_ERROR);
				Check(moduleCalls == calls && !probe.Healthy());
				Check(VirtualFree(guard, 0, MEM_RELEASE) != 0);
			} else if (scenario == 15) {
				auto* readOnly = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
				Check(readOnly != nullptr);
				const auto calls = moduleCalls;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, static_cast<NVDX_ObjectHandle*>(readOnly)) == NVAPI_ERROR);
				Check(moduleCalls == calls && !probe.Healthy());
				Check(VirtualFree(readOnly, 0, MEM_RELEASE) != 0);
			} else if (scenario == 16) {
				std::filesystem::remove(path);
				const auto calls = moduleCalls;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_ERROR);
				const auto capture = probe.SampleReceipt()["identityCapture"];
				Check(moduleCalls == calls && !probe.Healthy());
				Check(capture["modules"].size() == 1 && capture["modules"][0]["fileComplete"] == false && capture["modules"][0]["called"] == false);
			} else if (scenario <= 2) {
				const auto calls = moduleCalls;
				const void* blob = scenario == 0 ? nullptr : bytes.data();
				const auto size = scenario == 1 ? 0 : 4;
				auto* output = scenario == 2 ? nullptr : &module;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, blob, size, output) == NVAPI_ERROR);
				Check(moduleCalls == calls && !probe.Healthy());
			} else if (scenario == 3) {
				mutateBlob = true;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_ERROR);
				mutateBlob = false;
				Check(!probe.Healthy() && probe.SampleReceipt()["identityCapture"]["modules"][0]["stableAtObservedBoundaries"] == false);
			} else if (scenario == 4) {
				creationStatus = NVAPI_ERROR;
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_ERROR);
				creationStatus = NVAPI_OK;
				Check(probe.SampleReceipt()["identityCapture"]["modules"][0]["status"] == NVAPI_ERROR);
				Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, reinterpret_cast<NVDX_ObjectHandle>(0x5000), "test", &function) == NVAPI_ERROR);
			} else {
				Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_OK);
				const auto calls = functionCalls;
				std::string name = scenario == 5 ? "" : scenario == 6 ? std::string(kMaximumFunctionNameBytes, 'x') :
				                                    scenario == 7     ? "bad\nname" :
				                                                        "test";
				if (scenario == 8)
					mutateName = true;
				if (scenario == 10) {
					++bytes[0];
					Check(KernelChainProbeTestAccess::Module(probe, identityTestDevice, bytes.data(), 4, &module) == NVAPI_ERROR);
					Check(!probe.Healthy());
				} else if (scenario == 11) {
					creationStatus = NVAPI_ERROR;
					Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name.data(), &function) == NVAPI_ERROR);
					creationStatus = NVAPI_OK;
					const auto capture = probe.SampleReceipt()["identityCapture"];
					Check(capture["functions"][0]["status"] == NVAPI_ERROR && capture["functions"][0]["handleRead"] == false);
					Check(probe.Healthy());
				} else if (scenario == 12) {
					Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, reinterpret_cast<NVDX_ObjectHandle>(0x9876), name.data(), &function) == NVAPI_ERROR);
					Check(functionCalls == calls && !probe.Healthy());
				} else if (scenario == 13) {
					NvAPI_Status status = NVAPI_OK;
					std::thread worker([&] { status = KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name.data(), &function); });
					worker.join();
					Check(status == NVAPI_ERROR && functionCalls == calls && !probe.Healthy());
				} else if (scenario == 9) {
					probe.BeginSample(0, false);
					probe.BeginEvaluation(proxy, real, 0, 0);
					const auto packet = Packet(bytes);
					Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &packet, 1) == NVAPI_ERROR);
					Check(!probe.Healthy() && probe.SampleReceipt()["mappingComplete"] == false);
				} else {
					Check(KernelChainProbeTestAccess::Function(probe, identityTestDevice, module, name.data(), &function) == NVAPI_ERROR);
					Check(!probe.Healthy() && functionCalls == calls + (scenario == 8 ? 1 : 0));
				}
				mutateName = false;
			}
			std::filesystem::remove_all(path);
		}
	}
	void TestMultipleCacheRollback()
	{
		for (unsigned installed = 0; installed <= 3; ++installed) {
			std::array<void*, 3> values{};
			Check(KernelChainProbeTestAccess::CacheRollback(values, installed, false));
			for (std::size_t i = 0; i < values.size(); ++i)
				Check(values[i] == reinterpret_cast<void*>(i + 1));
		}
		std::array<void*, 3> conflict{};
		Check(!KernelChainProbeTestAccess::CacheRollback(conflict, 3, true));
		Check(conflict[0] == reinterpret_cast<void*>(1) && conflict[1] == reinterpret_cast<void*>(99) && conflict[2] == reinterpret_cast<void*>(3));
	}
	Json ReplacementManifest()
	{
		using namespace NrReplay::KernelChain;
		return { { "schema", kReplacementSchema }, { "batchCount", 1 },
			{ "original", { { "moduleSha256", kReplacementOriginalModuleSha256 }, { "entry", kReplacementEntry }, { "paramSize", 96 } } },
			{ "candidate", { { "path", "C:/test/n1.cubin" }, { "sha256", kReplacementCandidateSha256 }, { "entry", kReplacementEntry } } } };
	}
	void TestReplacementAdmission()
	{
		const auto admitted = ReplacementManifest();
		KernelChainProbeTestAccess::ValidateReplacement(admitted);
		Check(true);
		auto indexed = admitted;
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedCandidateSha256;
		KernelChainProbeTestAccess::ValidateReplacement(indexed);
		Check(true);
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedScheduledCandidateSha256;
		Reject([&] { KernelChainProbeTestAccess::ValidateReplacement(indexed); });
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedPairCandidateSha256;
		Reject([&] { KernelChainProbeTestAccess::ValidateReplacement(indexed); });
		for (unsigned mutation = 0; mutation < 10; ++mutation) {
			auto value = admitted;
			switch (mutation) {
			case 0:
				value["batchCount"] = 2;
				break;
			case 1:
				value["batchCount"] = true;
				break;
			case 2:
				value["original"]["paramSize"] = 95;
				break;
			case 3:
				value["original"]["moduleSha256"] = std::string(64, '0');
				break;
			case 4:
				value["original"]["entry"] = "cc_tinlayout_fused_swin_1h_32_1_inpview_fp8";
				break;
			case 5:
				value["candidate"]["sha256"] = "cf7e99045119504d8aa5451a70f50a50e4da588b22dc3e2f3b9cfb35ec7489e4";
				break;
			case 6:
				value["candidate"]["entry"] = "other";
				break;
			case 7:
				value["candidate"]["path"] = "relative.cubin";
				break;
			case 8:
				value["extra"] = true;
				break;
			case 9:
				value["schema"] = "unknown";
				break;
			}
			Reject([&] { KernelChainProbeTestAccess::ValidateReplacement(value); });
		}
		Json report;
		Reject([&] { ProviderKernelChainProbe probe(report, "group", "identity", "manifest"); });
		Reject([&] { ProviderKernelChainProbe probe(report, "forward", {}, "manifest"); });
		ProviderKernelChainProbe ordinary(report, "forward");
		Check(!report.contains("n1Replacement"));
	}
	void TestReplacementSubmissionAndRetirement()
	{
		FakeDevice ownedDevice;
		Json report;
		ProviderKernelChainProbe probe(report, "forward", "identity", "manifest");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
		KernelChainProbeTestAccess::AddOtherFunction(probe, 0x3100);
		Check(ownedDevice.references == 2);
		submissions.clear();
		probe.BeginSample(16, false);
		std::array<std::uint8_t, 96> bytes{};
		std::iota(bytes.begin(), bytes.end(), std::uint8_t{ 0 });
		Feed(probe, 0, bytes);
		Feed(probe, 1, bytes);
		probe.BeginEvaluation(proxy, real, 0, 2);
		auto other = Packet(bytes);
		other.hFunction = reinterpret_cast<NVDX_ObjectHandle>(0x3100);
		Check(KernelChainProbeTestAccess::Invoke(probe, proxy, &other, 1) == NVAPI_OK);
		probe.EndEvaluation();
		probe.FinishCommandList();
		Check(submissions.size() == 3);
		for (std::size_t i = 0; i < submissions.size(); ++i) {
			const auto& descriptor = submissions[i].descriptors[0];
			Check(descriptor.hFunction == reinterpret_cast<NVDX_ObjectHandle>(static_cast<std::uintptr_t>(i < 2 ? 0x7000 : 0x3100)));
			Check(descriptor.gridDim.x == 4 && descriptor.gridDim.y == 8 && descriptor.gridDim.z == 1);
			Check(descriptor.blockDim.x == 32 && descriptor.blockDim.y == 1 && descriptor.blockDim.z == 1 && descriptor.dynSharedMemBytes == 0);
			Check(descriptor.paramSize == 96 && submissions[i].bytes[0] == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
		}
		const auto receipt = probe.SampleReceipt();
		Check(receipt["replacementAttemptedDescriptors"] == 2 && receipt["replacementSuccessfulDescriptors"] == 2 && receipt["replacementTargetExecuted"] == true);
		for (const auto& event : receipt["events"])
			for (const auto& descriptor : event["descriptors"])
				if (descriptor["originalFunction"] == 0x3000)
					Check(descriptor["function"] == 0x3000 && descriptor["submittedFunction"] == 0x7000 && descriptor["replacementApplied"] == true);
		retirementCalls.clear();
		Check(KernelChainProbeTestAccess::RetireReplacement(probe));
		Check(retirementCalls == std::vector<std::string>({ "function", "module" }) && ownedDevice.references == 1);
		Check(KernelChainProbeTestAccess::RetireReplacement(probe) && retirementCalls.size() == 2);
	}
	void TestReplacementSubmissionRejections()
	{
		for (unsigned scenario = 0; scenario < 4; ++scenario) {
			FakeDevice ownedDevice;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", "manifest");
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
			KernelChainProbeTestAccess::AddOtherFunction(probe, 0x3100);
			probe.BeginSample(16, false);
			probe.BeginEvaluation(proxy, real, 0, 0);
			std::array<std::uint8_t, 96> bytes{};
			auto packet = Packet(bytes);
			if (scenario == 0)
				packet.paramSize = 95;
			if (scenario == 1)
				packet.gridDim.z = 2;
			if (scenario == 2)
				packet.hFunction = reinterpret_cast<NVDX_ObjectHandle>(0x3100);
			if (scenario == 3)
				submitStatus = NVAPI_ERROR;
			submissions.clear();
			const auto status = KernelChainProbeTestAccess::Invoke(probe, proxy, &packet, 1);
			submitStatus = NVAPI_OK;
			if (scenario == 2) {
				Check(status == NVAPI_OK);
				probe.EndEvaluation();
				Reject([&] { probe.FinishCommandList(); });
			} else {
				Check(status == NVAPI_ERROR);
				Reject([&] { probe.EndEvaluation(); });
			}
			const auto receipt = probe.SampleReceipt();
			Check(!probe.Healthy() && receipt["replacementTargetExecuted"] == false && receipt["replacementSuccessfulDescriptors"] == 0);
			if (scenario < 2)
				Check(submissions.empty());
			if (scenario == 3)
				Check(receipt["replacementAttemptedDescriptors"] == 1);
			Check(KernelChainProbeTestAccess::RetireReplacement(probe));
			Check(ownedDevice.references == 1);
		}
	}
	void TestReplacementCreationFailures()
	{
		for (unsigned scenario = 0; scenario < 4; ++scenario) {
			FakeDevice ownedDevice;
			Json report;
			const auto path = UniqueIdentityPath();
			ProviderKernelChainProbe probe(report, "forward", path, "manifest");
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			KernelChainProbeTestAccess::ConfigureIdentity(probe, PrivateModule, PrivateFunction);
			KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule, false);
			KernelChainProbeTestAccess::AddOtherFunction(probe, 0x8000);
			failPrivateModule = scenario == 0;
			failPrivateFunction = scenario == 1;
			privateFunctionHandle = reinterpret_cast<NVDX_ObjectHandle>(static_cast<std::uintptr_t>(scenario == 2 ? 0x8000 : 0x7000));
			NVDX_ObjectHandle output = nullptr;
			Check(KernelChainProbeTestAccess::Function(probe, ownedDevice.AsDevice(), reinterpret_cast<NVDX_ObjectHandle>(0x5000),
					  NrReplay::KernelChain::kReplacementEntry, &output) == NVAPI_OK);
			Check(output == reinterpret_cast<NVDX_ObjectHandle>(0x3000) && !probe.Healthy());
			Check(ownedDevice.references == 2);
			failPrivateModule = failPrivateFunction = false;
			privateFunctionHandle = reinterpret_cast<NVDX_ObjectHandle>(0x7000);
			retirementCalls.clear();
			Check(KernelChainProbeTestAccess::RetireReplacement(probe) == (scenario != 2));
			if (scenario == 0)
				Check(retirementCalls.empty());
			if (scenario == 1)
				Check(retirementCalls == std::vector<std::string>({ "module" }));
			if (scenario == 2)
				Check(retirementCalls.empty() && ownedDevice.references == 2);
			if (scenario == 3)
				Check(retirementCalls == std::vector<std::string>({ "function", "module" }));
			KernelChainProbeTestAccess::ClearFakeReplacement(probe);
			Check(ownedDevice.references == 1);
			std::filesystem::remove_all(path);
		}
	}
	void TestReplacementRetirementFailures()
	{
		for (unsigned scenario = 0; scenario < 2; ++scenario) {
			FakeDevice ownedDevice;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", "manifest");
			KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
			failDestroyFunction = scenario == 0;
			failDestroyModule = scenario == 1;
			retirementCalls.clear();
			Check(!KernelChainProbeTestAccess::RetireReplacement(probe) && ownedDevice.references == 2);
			Check(retirementCalls.size() == scenario + 1);
			Check(!KernelChainProbeTestAccess::RetireReplacement(probe) && retirementCalls.size() == scenario + 1);
			Check(probe.SampleReceipt()["n1Replacement"]["cleanupFailed"] == true);
			failDestroyFunction = failDestroyModule = false;
			KernelChainProbeTestAccess::ClearFakeReplacement(probe);
			Check(ownedDevice.references == 1);
		}
	}
	void TestReplacementFutureAliases()
	{
		for (unsigned scenario = 0; scenario < 2; ++scenario) {
			FakeDevice ownedDevice;
			Json report;
			const auto path = UniqueIdentityPath();
			ProviderKernelChainProbe probe(report, "forward", path, "manifest");
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			KernelChainProbeTestAccess::ConfigureIdentity(probe, PrivateModule, AliasPrivateFunction);
			KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
			NVDX_ObjectHandle output = nullptr;
			const std::array<std::uint8_t, 4> bytes{ 1, 2, 3, 4 };
			if (scenario == 0)
				Check(KernelChainProbeTestAccess::Module(probe, ownedDevice.AsDevice(), bytes.data(), 4, &output) == NVAPI_OK);
			else
				Check(KernelChainProbeTestAccess::Function(probe, ownedDevice.AsDevice(), reinterpret_cast<NVDX_ObjectHandle>(0x5000), "later_kernel", &output) == NVAPI_OK);
			Check(output == reinterpret_cast<NVDX_ObjectHandle>(static_cast<std::uintptr_t>(scenario == 0 ? 0x6000 : 0x7000)));
			retirementCalls.clear();
			Check(!probe.Healthy() && !KernelChainProbeTestAccess::RetireReplacement(probe));
			Check(retirementCalls.empty() && ownedDevice.references == 2);
			KernelChainProbeTestAccess::ClearFakeReplacement(probe);
			Check(ownedDevice.references == 1);
			std::filesystem::remove_all(path);
		}
	}
	void TestPairPlans()
	{
		using namespace NrReplay::KernelPair;
		Sample sample;
		sample.completed = 4;
		for (std::size_t r = 0; r < 4; ++r) {
			auto& region = sample.regions[r];
			region.complete = true;
			for (std::size_t stage = 0; stage < kStages; ++stage) {
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				region.commands.push_back({ Kind::Barrier, SIZE_MAX, { barrier } });
				if (stage == 3)
					region.target = region.commands.size();
				region.descriptors.push_back(r * kStages + stage);
				region.commands.push_back({ Kind::Launch, r * kStages + stage });
			}
		}
		const auto original = BuildOrder(sample, "original");
		const auto control = BuildOrder(sample, "control");
		const auto batch = BuildOrder(sample, "batch");
		Check(original.size() == 1264 && control.size() == 1266 && batch.size() == 1264);
		for (std::size_t r = 0; r < 4; ++r)
			for (std::size_t i = 0; i < sample.regions[r].commands.size(); ++i)
				Check(original[r * 316 + i].region == r && original[r * 316 + i].command == i);
		Check(control[7].region == 1 && control[14].region == 0 && control[14].command == 7);
		Check(control[15].region == 1 && control[16].kind == Kind::Join);
		Check(batch[14].kind == Kind::Batch && batch[14].region == 0 && batch[14].secondRegion == 1 && batch[15].kind == Kind::Join);
		auto broken = sample;
		broken.completed = 3;
		Reject([&] { BuildOrder(broken, "batch"); });
		broken = sample;
		broken.regions[3].complete = false;
		Reject([&] { BuildOrder(broken, "batch"); });
		broken = sample;
		broken.regions[0].target = 6;
		Reject([&] { BuildOrder(broken, "batch"); });
		broken = sample;
		broken.regions[0].commands[11].descriptor = 0;
		Reject([&] { BuildOrder(broken, "batch"); });
		Reject([&] { BuildOrder(sample, "unknown"); });
		Owners owners;
		owners.resources.resize(2);
		owners.resources[0].base = 100;
		owners.resources[0].end = 200;
		owners.resources[1].base = 200;
		owners.resources[1].end = 300;
		Check(FindBuffer(owners, 100, 100) == 0 && FindBuffer(owners, 200, 100) == 1);
		Reject([&] { FindBuffer(owners, 150, 100); });
		Reject([&] { FindBuffer(owners, 0, 1); });
		Reject([&] { FindBuffer(owners, UINT64_MAX, 2); });
		owners.resources[1].base = 100;
		Reject([&] { FindBuffer(owners, 100, 1); });
	}
	void TestLayerControlPlan()
	{
		using namespace NrReplay::KernelPair;
		Sample sample;
		sample.completed = kRegions;
		const auto addHeap = [](Region& region) {
			Command command;
			command.kind = Kind::Heaps;
			command.heaps = { reinterpret_cast<ID3D12DescriptorHeap*>(0x1000), reinterpret_cast<ID3D12DescriptorHeap*>(0x2000) };
			region.commands.push_back(std::move(command));
		};
		const auto addBarrier = [](Region& region, std::size_t identity) {
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = reinterpret_cast<ID3D12Resource*>(identity);
			region.commands.push_back({ Kind::Barrier, SIZE_MAX, { barrier } });
		};
		for (std::size_t r = 0; r < kRegions; ++r) {
			auto& region = sample.regions[r];
			region.complete = true;
			addHeap(region);
			for (std::size_t stage = 0; stage < kStages; ++stage) {
				if (stage % 7 == 0)
					addBarrier(region, 0x3000 + r * kStages + stage);
				if (stage == 5 && r % 2 == 1)
					addBarrier(region, 0x7000 + r);
				if (stage % 19 == 0)
					addHeap(region);
				if (stage == kTargetStage)
					region.target = region.commands.size();
				const auto id = r * kStages + stage;
				region.descriptors.push_back(id);
				region.commands.push_back({ Kind::Launch, id });
				if (stage % 11 == 0)
					addBarrier(region, 0x4000 + r * kStages + stage);
			}
			addHeap(region);
			addBarrier(region, 0x5000 + r);
		}
		for (const auto& [mode, prefix] : std::array<std::pair<std::string_view, std::size_t>, 5>{ { { "layer-control", kStages }, { "model-batch", 1 }, { "model-batch", 7 }, { "model-batch", kStages - 1 }, { "model-batch", kStages } } }) {
			const bool batching = mode == "model-batch";
			const auto order = BuildOrder(sample, mode, prefix);
			std::array<std::size_t, kRegions> nextCommand{};
			std::vector<std::size_t> launches;
			std::size_t joins = 0, pairLaunches = 0, barriers = 0, heaps = 0, batches = 0, physical = 0;
			for (const auto& operation : order) {
				if (operation.kind == Kind::Join) {
					Check(pairLaunches == 2 && operation.region == (joins / kStages) * 2 && operation.secondRegion == SIZE_MAX && operation.secondCommand == SIZE_MAX);
					++joins;
					pairLaunches = 0;
					continue;
				}
				Check(operation.command == nextCommand[operation.region]++);
				const auto& command = sample.regions[operation.region].commands[operation.command];
				Check(command.kind == (operation.kind == Kind::Batch ? Kind::Launch : operation.kind));
				if (operation.kind == Kind::Launch || operation.kind == Kind::Batch) {
					launches.push_back(command.descriptor);
					++pairLaunches;
					++physical;
					if (operation.kind == Kind::Batch) {
						Check(batching && command.descriptor % kStages < prefix && operation.secondRegion == operation.region + 1 && operation.secondCommand == nextCommand[operation.secondRegion]++);
						const auto& second = sample.regions[operation.secondRegion].commands[operation.secondCommand];
						Check(second.kind == Kind::Launch && second.descriptor % kStages == command.descriptor % kStages);
						launches.push_back(second.descriptor);
						++pairLaunches;
						++batches;
					} else
						Check((!batching || command.descriptor % kStages >= prefix) && operation.secondRegion == SIZE_MAX && operation.secondCommand == SIZE_MAX);
				} else {
					Check(operation.secondRegion == SIZE_MAX && operation.secondCommand == SIZE_MAX);
					if (operation.kind == Kind::Barrier) {
						Check(command.barriers.size() == 1 && command.barriers.front().UAV.pResource != nullptr);
						++barriers;
					} else {
						Check(command.heaps.size() == 2);
						++heaps;
					}
				}
			}
			const auto expectedBatches = batching ? 2 * prefix : 0;
			Check(launches.size() == 632 && joins == 316 && pairLaunches == 0 && physical == 632 - expectedBatches && batches == expectedBatches);
			for (std::size_t eye = 0; eye < 2; ++eye)
				for (std::size_t stage = 0; stage < kStages; ++stage)
					for (std::size_t region = 0; region < 2; ++region)
						Check(launches[eye * kStages * 2 + stage * 2 + region] == (eye * 2 + region) * kStages + stage);
			std::size_t originalCommands = 0, originalBarriers = 0, originalHeaps = 0;
			for (std::size_t r = 0; r < kRegions; ++r) {
				Check(nextCommand[r] == sample.regions[r].commands.size());
				originalCommands += sample.regions[r].commands.size();
				for (const auto& command : sample.regions[r].commands) {
					originalBarriers += command.kind == Kind::Barrier;
					originalHeaps += command.kind == Kind::Heaps;
				}
			}
			Check(order.size() == originalCommands + joins - batches && barriers == originalBarriers && heaps == originalHeaps);
		}
		const auto defaultOrder = BuildOrder(sample, "model-batch");
		const auto fullOrder = BuildOrder(sample, "model-batch", kStages);
		Check(defaultOrder.size() == fullOrder.size());
		for (std::size_t i = 0; i < defaultOrder.size(); ++i)
			Check(defaultOrder[i].kind == fullOrder[i].kind && defaultOrder[i].region == fullOrder[i].region &&
				  defaultOrder[i].command == fullOrder[i].command && defaultOrder[i].secondRegion == fullOrder[i].secondRegion &&
				  defaultOrder[i].secondCommand == fullOrder[i].secondCommand);
		for (const auto invalid : { std::size_t{ 0 }, kStages + 1, SIZE_MAX })
			Reject([&] { BuildOrder(sample, "model-batch", invalid); });
		for (const auto mode : { "original", "control", "batch", "layer-control" })
			Reject([&] { BuildOrder(sample, mode, 1); });
		for (const auto mode : { "original", "control", "batch", "layer-control", "model-batch" }) {
			for (unsigned invalid = 0; invalid < 12; ++invalid) {
				auto broken = sample;
				auto& region = broken.regions[0];
				const auto target = region.target;
				switch (invalid) {
				case 0:
					region.commands[target].kind = Kind::Batch;
					break;
				case 1:
					region.commands[target].kind = Kind::Join;
					break;
				case 2:
					region.commands[target].kind = static_cast<Kind>(99);
					break;
				case 3:
					std::swap(region.descriptors[5], region.descriptors[6]);
					break;
				case 4:
					region.commands[target].descriptor = region.descriptors[kTargetStage + 1];
					break;
				case 5:
					region.commands.erase(region.commands.begin() + target + 1);
					break;
				case 6:
					region.commands.push_back({ Kind::Launch, region.descriptors.back() });
					break;
				case 7:
					region.commands[target].heaps = { reinterpret_cast<ID3D12DescriptorHeap*>(0x1000) };
					break;
				case 8:
					region.commands.front().heaps[0] = nullptr;
					break;
				case 9:
					region.commands.back().descriptor = 0;
					break;
				case 10:
					region.commands.back().barriers.clear();
					break;
				case 11:
					while (region.commands.size() <= kMaximumRegionCommands)
						addBarrier(region, 0x6000);
					break;
				}
				Reject([&] { BuildOrder(broken, mode); });
			}
		}
	}
	void TestCompatiblePairing()
	{
		using namespace NrReplay::KernelPair;
		const Pairing aligned{ std::array<std::size_t, 2>{ 0, 2 }, std::array<std::size_t, 2>{ 1, 3 } };
		const Pairing reversed{ std::array<std::size_t, 2>{ 0, 3 }, std::array<std::size_t, 2>{ 1, 2 } };
		Check(CompatiblePairing({ "same", "same", "same", "same" }) == kSameEyePairs);
		Check(CompatiblePairing({ "large", "large", "small", "small" }) == kSameEyePairs);
		Check(CompatiblePairing({ "large", "small", "large", "small" }) == aligned);
		Check(CompatiblePairing({ "large", "small", "small", "large" }) == reversed);
		Check(!CompatiblePairing({ "", "", "", "" }));
		Check(!CompatiblePairing({ "large", "small", "large", "different" }));
		Check(!CompatiblePairing({ "large", "small", "different", "large" }));
		Check(!CompatiblePairing({ "large", "small", "tiny", "different" }));
		Sample sample;
		sample.completed = kRegions;
		for (std::size_t region = 0; region < kRegions; ++region) {
			auto& commands = sample.regions[region];
			commands.complete = true;
			commands.target = kTargetStage;
			for (std::size_t stage = 0; stage < kStages; ++stage) {
				commands.descriptors.push_back(region * kStages + stage);
				commands.commands.push_back({ Kind::Launch, region * kStages + stage });
			}
		}
		for (const auto& pairs : { kSameEyePairs, aligned, reversed }) {
			Check(ValidPairing(pairs));
			sample.pairs = pairs;
			const auto original = BuildOrder(sample, "original");
			for (std::size_t index = 0; index < original.size(); ++index)
				Check(sample.regions[original[index].region].commands[original[index].command].descriptor == index);
			const auto batched = BuildOrder(sample, "model-batch");
			const auto layer = BuildOrder(sample, "layer-control");
			Check(batched.size() == 632 && layer.size() == 948);
			for (std::size_t group = 0; group < pairs.size(); ++group)
				for (std::size_t stage = 0; stage < kStages; ++stage) {
					const auto& batch = batched[group * kStages * 2 + stage * 2];
					Check(batch.kind == Kind::Batch && batch.region == pairs[group][0] && batch.secondRegion == pairs[group][1] &&
						  batch.command == stage && batch.secondCommand == stage);
					Check(batched[group * kStages * 2 + stage * 2 + 1].kind == Kind::Join);
					Check(layer[group * kStages * 3 + stage * 3].region == pairs[group][0] &&
						  layer[group * kStages * 3 + stage * 3 + 1].region == pairs[group][1]);
				}
		}
		for (const auto& invalid : { Pairing{ std::array<std::size_t, 2>{ 0, 2 }, std::array<std::size_t, 2>{ 1, 2 } },
				 Pairing{ std::array<std::size_t, 2>{ 0, 4 }, std::array<std::size_t, 2>{ 1, 3 } },
				 Pairing{ std::array<std::size_t, 2>{ 2, 0 }, std::array<std::size_t, 2>{ 1, 3 } },
				 Pairing{ std::array<std::size_t, 2>{ 0, 0 }, std::array<std::size_t, 2>{ 1, 3 } } }) {
			Check(!ValidPairing(invalid));
			sample.pairs = invalid;
			Reject([&] { BuildOrder(sample, "model-batch"); });
		}
		Check(TensorByteSpan(160, 160) == 819200 && TensorByteSpan(704, 576) == 12976128);
		Check(TensorByteSpan(16384, 16384) == 8589934592ull);
		Check(CompletionByteSpan(0x5b00) == 0x16c00 && CompletionByteSpan(UINT32_MAX) == 17179869180ull);
		for (const auto& extent : { std::array<std::uint32_t, 2>{ 0, 160 }, { 160, 0 }, { 16385, 160 }, { 160, 16385 }, { UINT32_MAX, UINT32_MAX } })
			Reject([&] { TensorByteSpan(extent[0], extent[1]); });
		Reject([] { CompletionByteSpan(0); });
		Owners owners;
		owners.resources.resize(1);
		owners.resources[0].base = 100;
		owners.resources[0].end = 100 + TensorByteSpan(704, 576);
		Check(FindBuffer(owners, 100, TensorByteSpan(704, 576)) == 0);
		Reject([&] { FindBuffer(owners, 101, TensorByteSpan(704, 576)); });
	}
	void TestStructuralStageIdentity()
	{
		Json report;
		ProviderKernelChainProbe probe(report, "forward");
		KernelChainProbeTestAccess::ConfigureStageIdentity(probe);
		NVAPI_CU_KERNEL_LAUNCH_PARAMS launch{ reinterpret_cast<NVDX_ObjectHandle>(0x3000), { 20, 20, 1 }, { 32, 1, 1 }, 0, nullptr, 96 };
		const auto baseline = KernelChainProbeTestAccess::StageSignatures(probe, launch);
		launch.gridDim.x = 88;
		launch.gridDim.y = 72;
		const auto resized = KernelChainProbeTestAccess::StageSignatures(probe, launch);
		Check(baseline[0] != resized[0] && baseline[1] == resized[1]);
		for (unsigned field = 0; field < 6; ++field) {
			auto changed = launch;
			switch (field) {
			case 0:
				changed.gridDim.z = 2;
				break;
			case 1:
				changed.blockDim.x = 64;
				break;
			case 2:
				changed.blockDim.y = 2;
				break;
			case 3:
				changed.blockDim.z = 2;
				break;
			case 4:
				changed.dynSharedMemBytes = 8;
				break;
			case 5:
				changed.paramSize = 104;
				break;
			}
			Check(KernelChainProbeTestAccess::StageSignatures(probe, changed)[1] != baseline[1]);
		}
		KernelChainProbeTestAccess::ConfigureStageIdentity(probe, std::string(64, '0'));
		Check(KernelChainProbeTestAccess::StageSignatures(probe, launch)[1] != baseline[1]);
		KernelChainProbeTestAccess::ConfigureStageIdentity(probe, NrReplay::KernelChain::kReplacementOriginalModuleSha256, "unqualified_entry");
		Check(KernelChainProbeTestAccess::StageSignatures(probe, launch)[1] != baseline[1]);
	}
	void TestCrossEyeModelPackets()
	{
		using namespace NrReplay::KernelPair;
		const Pairing aligned{ std::array<std::size_t, 2>{ 0, 2 }, std::array<std::size_t, 2>{ 1, 3 } };
		const Pairing reversed{ std::array<std::size_t, 2>{ 0, 3 }, std::array<std::size_t, 2>{ 1, 2 } };
		for (const auto& pairs : { aligned, reversed }) {
			ModelFixture fixture(2);
			FakeDevice device;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", {}, "model-batch", fixture.manifest);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			comparisonModules = comparisonFunctions = comparisonRetired = 0;
			KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 2);
			KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), Json::parse(std::ifstream(fixture.manifest)));
			probe.BeginSample(3, false);
			FakeScheduleList list;
			std::array<unsigned, 4> widths{};
			for (std::size_t group = 0; group < pairs.size(); ++group)
				for (const auto region : pairs[group])
					widths[region] = group == 0 ? 88 : 20;
			KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList(), pairs, widths);
			submissions.clear();
			submitStatus = NVAPI_OK;
			KernelChainProbeTestAccess::ReplayRepetition(probe, {}, {});
			Check(submissions.size() == 316);
			for (std::size_t group = 0; group < pairs.size(); ++group)
				for (std::size_t stage = 0; stage < kStages; ++stage) {
					const auto& launch = submissions[group * kStages + stage];
					const auto& packet = launch.bytes.at(0);
					const auto first = static_cast<std::uint8_t>(pairs[group][0] * kStages + stage);
					const auto second = static_cast<std::uint8_t>(pairs[group][1] * kStages + stage);
					Check(packet.size() == 160 && std::all_of(packet.begin(), packet.begin() + 72, [first](auto value) { return value == first; }) &&
						  std::all_of(packet.begin() + 80, packet.begin() + 152, [second](auto value) { return value == second; }));
					Check(std::all_of(packet.begin() + 72, packet.begin() + 80, [](auto value) { return value == 0; }) &&
						  std::all_of(packet.begin() + 152, packet.end(), [](auto value) { return value == 0; }));
					Check(launch.descriptors.at(0).gridDim.x == widths[pairs[group][0]] && launch.descriptors.at(0).gridDim.y == 1 && launch.descriptors.at(0).gridDim.z == 8);
				}
			Check(probe.SampleReceipt()["kernelPair"]["regionPairs"] == Json(pairs));
			Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) && comparisonRetired == 53 && device.references == 1);
		}
		ModelFixture fixture(2);
		FakeDevice device;
		Json report;
		ProviderKernelChainProbe probe(report, "forward", "identity", {}, "model-batch", fixture.manifest);
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		comparisonModules = comparisonFunctions = comparisonRetired = 0;
		KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 2);
		KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), Json::parse(std::ifstream(fixture.manifest)));
		probe.BeginSample(3, false);
		FakeScheduleList list;
		submissions.clear();
		Reject([&] { KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList(), aligned, { 88, 20, 87, 20 }); });
		Check(submissions.empty() && probe.GetRuntimeCounters().privateAttempts == 0);
		Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) && comparisonRetired == 53 && device.references == 1);
	}
	void TestPairHeapIdentityAndManifest()
	{
		using namespace NrReplay::KernelPair;
		Sample sample;
		for (auto& region : sample.regions) {
			Command command;
			command.kind = Kind::Heaps;
			command.heaps = { reinterpret_cast<ID3D12DescriptorHeap*>(0x1000), reinterpret_cast<ID3D12DescriptorHeap*>(0x2000) };
			region.commands.push_back(command);
		}
		ValidateHeapBindings(sample);
		++checks;
		auto broken = sample;
		broken.regions[1].commands[0].heaps[0] = reinterpret_cast<ID3D12DescriptorHeap*>(0x3000);
		Reject([&] { ValidateHeapBindings(broken); });
		broken = sample;
		std::reverse(broken.regions[2].commands[0].heaps.begin(), broken.regions[2].commands[0].heaps.end());
		Reject([&] { ValidateHeapBindings(broken); });
		broken = sample;
		broken.regions[3].commands.clear();
		Reject([&] { ValidateHeapBindings(broken); });
		auto manifest = ReplacementManifest();
		manifest["schema"] = NrReplay::KernelChain::kPairReplacementSchema;
		manifest["batchCount"] = 2;
		manifest["candidate"]["sha256"] = NrReplay::KernelChain::kPairCandidateSha256;
		KernelChainProbeTestAccess::ValidatePairManifest(manifest);
		++checks;
		auto indexed = manifest;
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedScheduledPairCandidateSha256;
		Reject([&] { KernelChainProbeTestAccess::ValidatePairManifest(indexed); });
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedPairCandidateSha256;
		Reject([&] { KernelChainProbeTestAccess::ValidatePairManifest(indexed); });
		indexed["candidate"]["sha256"] = NrReplay::KernelChain::kIndexedCandidateSha256;
		Reject([&] { KernelChainProbeTestAccess::ValidatePairManifest(indexed); });
		Reject([&] { KernelChainProbeTestAccess::ValidateReplacement(manifest); });
		for (unsigned i = 0; i < 6; ++i) {
			auto invalid = manifest;
			switch (i) {
			case 0:
				invalid["batchCount"] = 1;
				break;
			case 1:
				invalid["candidate"]["sha256"] = NrReplay::KernelChain::kReplacementCandidateSha256;
				break;
			case 2:
				invalid["original"]["paramSize"] = 192;
				break;
			case 3:
				invalid["original"]["entry"] = "cc_tinlayout_fused_swin_1h_32_1_inpview_fp8";
				break;
			case 4:
				invalid["candidate"]["path"] = "relative.cubin";
				break;
			case 5:
				invalid["extra"] = true;
				break;
			}
			Reject([&] { KernelChainProbeTestAccess::ValidatePairManifest(invalid); });
		}
	}
	void TestPairAdmissionAndEightSlotRollback()
	{
		for (unsigned count = 0; count <= 8; ++count) {
			std::array<void*, 8> slots{};
			Check(KernelChainProbeTestAccess::CacheRollbackAll(slots, count, -1));
			for (std::size_t i = 0; i < 8; ++i) Check(slots[i] == reinterpret_cast<void*>(i + 1));
		}
		for (int conflict = 0; conflict < 8; ++conflict) {
			std::array<void*, 8> slots{};
			Check(!KernelChainProbeTestAccess::CacheRollbackAll(slots, 8, conflict));
			for (std::size_t i = 0; i < 8; ++i) Check(slots[i] == reinterpret_cast<void*>(i == conflict ? 99 : i + 1));
		}
		Json report;
		Reject([&] { ProviderKernelChainProbe p(report, "group", "identity", {}, "control"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", {}, {}, "original"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "batch"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", "manifest", "control"); });
		ProviderKernelChainProbe probe(report, "forward", "identity", {}, "control");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		Reject([&] { probe.ConfirmDefaultAllocationParameters(); });
		probe.BeginSample(1, false);
		Reject([&] { probe.BeginEvaluation(proxy, real, 0, 1); });
		probe.BeginEvaluation(proxy, real, 0, 0);
		Check(!probe.BeforeCommand("CopyBufferRegion"));
		Check(!probe.Healthy());
		Reject([&] { probe.FinishCommandList(); });
	}
	NvAPI_Status descriptorStatus = NVAPI_OK;
	std::vector<std::uintptr_t> descriptorArguments;
	NvAPI_Status __cdecl MockMerged(NVAPI_D3D12_GET_CUDA_MERGED_TEXTURE_SAMPLER_OBJECT_PARAMS* p)
	{
		descriptorArguments = { reinterpret_cast<std::uintptr_t>(p) };
		return descriptorStatus;
	}
	NvAPI_Status __cdecl MockIndependent(NVAPI_D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_PARAMS* p)
	{
		descriptorArguments = { reinterpret_cast<std::uintptr_t>(p) };
		return descriptorStatus;
	}
	NvAPI_Status __cdecl MockTexture(ID3D12Device* d, D3D12_CPU_DESCRIPTOR_HANDLE a, D3D12_CPU_DESCRIPTOR_HANDLE b, NvU32* o)
	{
		descriptorArguments = { reinterpret_cast<std::uintptr_t>(d), a.ptr, b.ptr, reinterpret_cast<std::uintptr_t>(o) };
		*o = 42;
		return descriptorStatus;
	}
	NvAPI_Status __cdecl MockSurface(ID3D12Device* d, D3D12_CPU_DESCRIPTOR_HANDLE a, NvU32* o)
	{
		descriptorArguments = { reinterpret_cast<std::uintptr_t>(d), a.ptr, reinterpret_cast<std::uintptr_t>(o) };
		*o = 43;
		return descriptorStatus;
	}
	NvAPI_Status __cdecl MockCapture(ID3D12Device* d, NVAPI_UAV_INFO* p)
	{
		descriptorArguments = { reinterpret_cast<std::uintptr_t>(d), reinterpret_cast<std::uintptr_t>(p) };
		return descriptorStatus;
	}
	void TestPairDescriptorGuards()
	{
		for (unsigned api = 0; api < 5; ++api)
			for (unsigned mode = 0; mode < 4; ++mode) {
				Json report;
				ProviderKernelChainProbe probe(report, "forward", "identity", {}, "original");
				KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
				KernelChainProbeTestAccess::DescriptorFunctions(probe, { reinterpret_cast<void*>(&MockMerged), reinterpret_cast<void*>(&MockIndependent),
																		   reinterpret_cast<void*>(&MockTexture), reinterpret_cast<void*>(&MockSurface), reinterpret_cast<void*>(&MockCapture) });
				probe.BeginSample(0, mode != 2);
				probe.BeginEvaluation(proxy, real, 0, 0);
				descriptorStatus = mode == 1 ? NVAPI_ERROR : NVAPI_OK;
				std::array<std::uint8_t, 1024> params{};
				NvU32 output = 0;
				Check(KernelChainProbeTestAccess::DescriptorInvoke(probe, api, params.data(), identityTestDevice, { 123 }, { 456 }, &output, mode == 3) == descriptorStatus);
				const auto pointer = reinterpret_cast<std::uintptr_t>(params.data());
				if (api < 2)
					Check(descriptorArguments == std::vector<std::uintptr_t>{ pointer });
				if (api == 2)
					Check(descriptorArguments == std::vector<std::uintptr_t>{ 0x4000, 123, 456, reinterpret_cast<std::uintptr_t>(&output) } && output == 42);
				if (api == 3)
					Check(descriptorArguments == std::vector<std::uintptr_t>{ 0x4000, 123, reinterpret_cast<std::uintptr_t>(&output) } && output == 43);
				if (api == 4)
					Check(descriptorArguments == std::vector<std::uintptr_t>{ 0x4000, pointer });
				Check(probe.Healthy() == (mode < 2));
				if (mode >= 2) {
					Reject([&] { probe.EndEvaluation(); });
					Check(probe.SampleReceipt()["submittedDescriptors"] == 0);
				}
			}
	}
	void TestPairPackedSubmission()
	{
		FakeDevice ownedDevice;
		Json report;
		ProviderKernelChainProbe probe(report, "forward", "identity", "manifest", "batch");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
		probe.BeginSample(1, false);
		probe.BeginEvaluation(proxy, real, 0, 0);
		std::array<std::uint8_t, 96> a{}, b{};
		for (std::size_t i = 0; i < 96; ++i) {
			a[i] = static_cast<std::uint8_t>(i);
			b[i] = static_cast<std::uint8_t>(255 - i);
		}
		submissions.clear();
		submitStatus = NVAPI_OK;
		KernelChainProbeTestAccess::PairBatch(probe, a, b);
		Check(submissions.size() == 1 && submissions[0].descriptors.size() == 1);
		const auto& d = submissions[0].descriptors[0];
		Check(d.hFunction == reinterpret_cast<NVDX_ObjectHandle>(0x7000) && d.gridDim.z == 2 && d.gridDim.x == 20 && d.gridDim.y == 20 && d.paramSize == 192);
		Check(std::equal(a.begin(), a.end(), submissions[0].bytes[0].begin()) && std::equal(b.begin(), b.end(), submissions[0].bytes[0].begin() + 96));
		const auto& pair = KernelChainProbeTestAccess::PairSample(probe);
		Check(pair.submittedLogicalDescriptors == std::vector<std::size_t>{ 3, 161 } && pair.batchedCalls == 1);
		Check(KernelChainProbeTestAccess::RetireReplacement(probe));
	}
	void TestSelectedStageDynamicPairs()
	{
		using namespace NrReplay::KernelPair;
		FakeDevice ownedDevice;
		Json report;
		ProviderKernelChainProbe probe(report, "forward", "identity", "manifest", "batch");
		KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
		KernelChainProbeTestAccess::ConfigureReplacement(probe, ownedDevice.AsDevice(), DestroyPrivateFunction, DestroyPrivateModule);
		probe.BeginSample(1, false);
		probe.BeginEvaluation(proxy, real, 0, 0);
		std::array<std::uint8_t, 96> a{}, b{};
		a.fill(11);
		b.fill(29);
		submissions.clear();
		Reject([&] { KernelChainProbeTestAccess::ConfigurePairBatch(probe, a, b, { 88, 20, 87, 20 }, { 72, 20, 72, 20 }); });
		Check(submissions.empty() && probe.GetRuntimeCounters().privateAttempts == 0);
		KernelChainProbeTestAccess::ConfigurePairBatch(probe, a, b, { 88, 20, 88, 20 }, { 72, 20, 72, 20 });
		const Pairing aligned{ std::array<std::size_t, 2>{ 0, 2 }, std::array<std::size_t, 2>{ 1, 3 } };
		Check(KernelChainProbeTestAccess::PairSample(probe).pairs == aligned);
		auto& second = KernelChainProbeTestAccess::PairTarget(probe, 2);
		const auto original = second;
		for (unsigned field = 0; field < 8; ++field) {
			second = original;
			switch (field) {
			case 0:
				++second.gridDim.x;
				break;
			case 1:
				++second.gridDim.y;
				break;
			case 2:
				++second.gridDim.z;
				break;
			case 3:
				++second.blockDim.x;
				break;
			case 4:
				++second.blockDim.y;
				break;
			case 5:
				++second.blockDim.z;
				break;
			case 6:
				++second.dynSharedMemBytes;
				break;
			case 7:
				++second.paramSize;
				break;
			}
			Reject([&] { KernelChainProbeTestAccess::SubmitPairBatch(probe); });
			Check(submissions.empty() && probe.GetRuntimeCounters().privateAttempts == 0);
		}
		second = original;
		submitStatus = NVAPI_OK;
		KernelChainProbeTestAccess::SubmitPairBatch(probe);
		KernelChainProbeTestAccess::SubmitPairBatch(probe, 1);
		Check(submissions.size() == 2 && submissions[0].descriptors[0].gridDim.x == 88 && submissions[0].descriptors[0].gridDim.y == 72 &&
			  submissions[1].descriptors[0].gridDim.x == 20 && submissions[1].descriptors[0].gridDim.y == 20);
		Check(std::equal(a.begin(), a.end(), submissions[0].bytes[0].begin()) &&
			  std::equal(a.begin(), a.end(), submissions[0].bytes[0].begin() + 96) &&
			  std::equal(b.begin(), b.end(), submissions[1].bytes[0].begin()) &&
			  std::equal(b.begin(), b.end(), submissions[1].bytes[0].begin() + 96));
		const auto receipt = KernelChainProbeTestAccess::PairReceipt(probe);
		Check(receipt["regionPairs"] == Json(aligned) && receipt["physicalBatches"].size() == 2);
		Check(receipt["physicalBatches"][0]["grid"] == Json({ 88, 72, 2 }) && receipt["physicalBatches"][0]["sourceDescriptorIds"] == Json({ 3, 319 }));
		Check(receipt["physicalBatches"][1]["grid"] == Json({ 20, 20, 2 }) && receipt["physicalBatches"][1]["sourceDescriptorIds"] == Json({ 161, 477 }));
		Check(KernelChainProbeTestAccess::RetireReplacement(probe));
	}
	void TestRepeatedSchedules()
	{
		using namespace NrReplay::KernelPair;
		for (const auto mode : { "original", "layer-control" })
			for (const auto count : { 1u, 2u, 4u }) {
				Json report;
				ProviderKernelChainProbe probe(report, "forward", "identity", {}, mode, {}, kStages, count);
				KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
				probe.BeginSample(3, false);
				FakeScheduleList list;
				KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
				submissions.clear();
				submitStatus = NVAPI_OK;
				const auto joins = std::string_view(mode) == "layer-control" ? kStages * 2 : 0;
				std::vector<unsigned> callbacks;
				KernelChainProbeTestAccess::ReplayRepetition(probe, [&](unsigned ordinal) {
					Check(submissions.size() == ordinal * kStages * kRegions);
					Check(list.barriers == ordinal * (joins + 1));
					callbacks.push_back(ordinal * 2); }, [&](unsigned ordinal) {
					Check(submissions.size() == (ordinal + 1) * kStages * kRegions);
					Check(list.barriers == (ordinal + 1) * joins + ordinal);
					callbacks.push_back(ordinal * 2 + 1); });
				const auto& sample = KernelChainProbeTestAccess::PairSample(probe);
				Check(sample.submitted && sample.repetitions.size() == count && sample.repetitionBoundaries == count - 1);
				Check(sample.submittedLogicalDescriptors.size() == count * kStages * kRegions);
				for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
					const auto& repetition = sample.repetitions[ordinal];
					Check(repetition.ordinal == ordinal && repetition.completed && repetition.beginCompleted && repetition.endCompleted);
					Check(repetition.physicalLaunches == kStages * kRegions && repetition.commands == sample.order.size() && repetition.joins == joins);
					Check(repetition.eventBegin == ordinal * kStages * kRegions && repetition.eventEnd == (ordinal + 1) * kStages * kRegions);
					ValidateLogicalPermutation(repetition.logicalDescriptors, kStages * kRegions);
					Check(callbacks[ordinal * 2] == ordinal * 2 && callbacks[ordinal * 2 + 1] == ordinal * 2 + 1);
					for (std::size_t index = 0; index < kStages * kRegions; ++index) {
						const auto& value = submissions[ordinal * kStages * kRegions + index];
						Check(value.list == list.AsList() && value.bytes == submissions[index].bytes && value.pointers == submissions[index].pointers);
					}
				}
				const auto receipt = KernelChainProbeTestAccess::PairReceipt(probe);
				Check(receipt["scheduleRepetitions"] == count && receipt["executedScheduleRepetitions"] == count && receipt["logicalKernelLaunches"] == kStages * kRegions);
				Check(receipt["executedLogicalKernelLaunches"] == count * kStages * kRegions && receipt["physicalKernelLaunches"] == count * kStages * kRegions);
				Check(receipt["physicalBatches"].empty() && receipt["repetitions"].size() == count);
				Reject([&] { KernelChainProbeTestAccess::ReplayRepetition(probe, {}, {}); });
				Check(submissions.size() == count * kStages * kRegions);
			}
	}
	void TestRepetitionFailures()
	{
		using namespace NrReplay::KernelPair;
		for (unsigned scenario = 0; scenario < 3; ++scenario) {
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", {}, "original", {}, kStages, 4);
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			probe.BeginSample(3, false);
			FakeScheduleList list;
			KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
			submissions.clear();
			submitStatus = NVAPI_OK;
			std::vector<unsigned> ended;
			Reject([&] {
				KernelChainProbeTestAccess::ReplayRepetition(probe, [&](unsigned ordinal) {
					if (ordinal == 1 && scenario == 0) throw std::runtime_error("begin callback failure");
					if (ordinal == 1 && scenario == 2) submitStatus = NVAPI_ERROR; }, [&](unsigned ordinal) {
					if (ordinal == 1 && scenario == 1) throw std::runtime_error("end callback failure");
					ended.push_back(ordinal); });
			});
			submitStatus = NVAPI_OK;
			const auto& sample = KernelChainProbeTestAccess::PairSample(probe);
			Check(!sample.submitted && sample.repetitions.size() == 2 && sample.repetitionBoundaries == 1 && ended == std::vector<unsigned>{ 0 });
			Check(sample.repetitions[0].completed && !sample.repetitions[1].completed && !sample.repetitions[1].endCompleted);
			Check(sample.repetitions[1].beginCompleted == (scenario != 0));
			Check(sample.repetitions[1].physicalLaunches == (scenario == 0 ? 0 : scenario == 1 ? kStages * kRegions :
																								 1));
			Check(sample.repetitions[1].logicalDescriptors.size() == (scenario == 1 ? kStages * kRegions : 0));
			const auto before = submissions.size();
			Reject([&] { KernelChainProbeTestAccess::ReplayRepetition(probe, {}, {}); });
			Check(submissions.size() == before);
		}
		Json report;
		for (const auto count : { 0u, 5u, UINT_MAX })
			Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "original", {}, kStages, count); });
		for (const auto mode : { "", "control", "batch" })
			Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, mode, {}, kStages, 2); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "model-batch", "manifest", 157, 2); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "original", "manifest", kStages, 2); });
		for (unsigned scenario = 0; scenario < 3; ++scenario) {
			ProviderKernelChainProbe p(report, "forward", "identity", {}, "original", {}, kStages, scenario == 0 ? 1 : 2);
			KernelChainProbeTestAccess::Configure(p, FakeLaunch);
			p.BeginSample(3, false);
			const ProviderKernelChainProbe::RepetitionCallback callback = [](unsigned) {};
			Reject([&] { p.FinishCommandList(scenario == 2 ? ProviderKernelChainProbe::RepetitionCallback{} : callback, scenario == 0 ? callback : ProviderKernelChainProbe::RepetitionCallback{}); });
			Check(!p.Healthy());
		}
		const std::vector<std::size_t> good{ 0, 1, 2 };
		ValidateLogicalPermutation(good, 3);
		for (const auto& values : { std::vector<std::size_t>{ 0, 1 }, std::vector<std::size_t>{ 0, 1, 1 }, std::vector<std::size_t>{ 1, 2, 3 } })
			Reject([&] { ValidateLogicalPermutation(values, 3); });
	}
	void TestBalancedComparisons()
	{
		using namespace NrReplay::KernelPair;
		for (const auto control : { "original", "layer-control" })
			for (unsigned iteration : { 3u, 4u }) {
				ModelFixture fixture(2);
				FakeDevice device;
				Json report;
				ProviderKernelChainProbe probe(report, "forward", "identity", {}, "model-batch", fixture.manifest, kStages, 4, control);
				KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
				comparisonModules = comparisonFunctions = comparisonRetired = 0;
				KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 2);
				KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), Json::parse(std::ifstream(fixture.manifest)));
				Check(comparisonModules == 9 && comparisonFunctions == 44);
				probe.BeginSample(iteration, false);
				FakeScheduleList list;
				KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
				submissions.clear();
				submitStatus = NVAPI_OK;
				std::vector<unsigned> callbacks;
				KernelChainProbeTestAccess::ReplayRepetition(probe, [&](unsigned i) { callbacks.push_back(i * 2); }, [&](unsigned i) { callbacks.push_back(i * 2 + 1); });
				Check(submissions.size() == 1896 && callbacks == std::vector<unsigned>{ 0, 1, 2, 3, 4, 5, 6, 7 });
				const auto receipt = probe.SampleReceipt();
				const auto& pair = receipt.at("kernelPair");
				Check(pair["comparison"]["order"] == ((iteration & 1u) ? "BAAB" : "ABBA") && pair["comparison"]["plannedPrivatePasses"] == 2);
				Check(pair["physicalBatches"].size() == 632 && pair["physicalKernelLaunches"] == 1896 && pair["executedLogicalKernelLaunches"] == 2528);
				Check(pair["batchedKernelLaunches"] == 632 && pair["comparison"]["controlOrder"].size() == (std::string_view(control) == "original" ? 632 : 948));
				std::size_t physical = 0, batchRecords = 0;
				for (unsigned i = 0; i < 4; ++i) {
					const bool batch = ComparisonBatch(iteration, i);
					const auto& repeat = pair["repetitions"][i];
					Check(repeat["mode"] == (batch ? "model-batch" : control) && repeat["physicalLaunches"] == (batch ? 316 : 632));
					for (std::size_t index = repeat["eventBegin"]; index < repeat["eventEnd"]; ++index) {
						const auto& event = receipt["events"][index];
						Check(event["physicalLaunches"].size() == 1 && event["descriptorIds"].size() == (batch ? 2 : 1));
						const auto id = event["descriptorIds"][0].get<std::size_t>();
						const auto binding = id % kStages % NrReplay::KernelReplacement::kFunctionCount;
						const auto& actual = event["physicalLaunches"][0];
						Check(actual["function"] == (batch ? 0xa000 : 0x3000) + binding && actual["grid"] == Json({ 1, 1, batch ? 8 : 4 }) && actual["status"] == 0);
						Check(actual["paramSize"] == (batch ? 160 : 72));
						const auto& packet = submissions[physical].bytes[0];
						Check(submissions[physical].descriptors[0].hFunction == reinterpret_cast<NVDX_ObjectHandle>((batch ? 0xa000 : 0x3000) + binding));
						Check(actual["paramsSha256"] == Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(std::span(packet)))));
						Check(packet.size() == (batch ? 160 : 72) && std::all_of(packet.begin(), packet.begin() + 72, [id](auto byte) { return byte == static_cast<std::uint8_t>(id); }));
						if (batch) {
							Check(pair["physicalBatches"][batchRecords++]["repetition"] == i);
							const auto second = event["descriptorIds"][1].get<std::size_t>();
							Check(std::all_of(packet.begin() + 72, packet.begin() + 80, [](auto byte) { return byte == 0; }) &&
								  std::all_of(packet.begin() + 80, packet.begin() + 152, [second](auto byte) { return byte == static_cast<std::uint8_t>(second); }) &&
								  std::all_of(packet.begin() + 152, packet.end(), [](auto byte) { return byte == 0; }));
						}
						++physical;
					}
				}
				Check(physical == submissions.size() && batchRecords == 632);
				std::array<std::size_t, NrReplay::KernelReplacement::kFunctionCount> expected{};
				for (unsigned stage = 0; stage < kStages; ++stage) expected[stage % expected.size()] += 4;
				auto& model = KernelChainProbeTestAccess::Model(probe);
				model.RequireSubmissionCounts(expected);
				Check(model.Healthy() && model.Retire(true, true) && comparisonRetired == 53 && device.references == 1);
			}
		Json report;
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "original", {}, kStages, 4, "original"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "model-batch", "manifest", kStages, 3, "original"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "model-batch", "manifest", kStages, 4, "batch"); });
		Reject([&] { ProviderKernelChainProbe p(report, "forward", "identity", {}, "model-batch", "manifest", 157, 4, "layer-control"); });
		Reject([] { (void)ComparisonBatch(3, 4); });
	}
	void TestComparisonFailedPhysicalReceipt()
	{
		using namespace NrReplay::KernelPair;
		for (unsigned failureRepeat : { 0u, 1u }) {
			ModelFixture fixture(2);
			FakeDevice device;
			Json report;
			ProviderKernelChainProbe probe(report, "forward", "identity", {}, "model-batch", fixture.manifest, kStages, 4, "original");
			KernelChainProbeTestAccess::Configure(probe, FakeLaunch);
			comparisonModules = comparisonFunctions = comparisonRetired = 0;
			KernelChainProbeTestAccess::ConfigureModel(probe, fixture.manifest, fixture.fingerprint, ComparisonModule, ComparisonFunction, ComparisonDestroy, ComparisonDestroy, 2);
			KernelChainProbeTestAccess::ObserveTestModel(probe, device.AsDevice(), Json::parse(std::ifstream(fixture.manifest)));
			probe.BeginSample(3, false);
			FakeScheduleList list;
			KernelChainProbeTestAccess::ConfigureRepetition(probe, list.AsList());
			submissions.clear();
			submitStatus = NVAPI_OK;
			unsigned completed = 0;
			inspectLaunchProbe = &probe;
			privateAttemptsAtCall.clear();
			Reject([&] {
				KernelChainProbeTestAccess::ReplayRepetition(probe, [&](unsigned repetition) {
					if (repetition == failureRepeat) submitStatus = NVAPI_ERROR; }, [&](unsigned) { ++completed; });
			});
			submitStatus = NVAPI_OK;
			inspectLaunchProbe = nullptr;
			Check(privateAttemptsAtCall.front() == 1);
			Check(probe.GetRuntimeCounters().privateAttempts == (failureRepeat == 0 ? 1 : 316));
			const auto receipt = probe.SampleReceipt();
			const auto& physical = receipt["events"].back()["physicalLaunches"][0];
			Check(completed == failureRepeat && receipt["kernelPair"]["submittedComplete"] == false);
			Check(physical["function"] == (failureRepeat == 0 ? 0xa000 : 0x3000) && physical["paramSize"] == (failureRepeat == 0 ? 160 : 72));
			Check(physical["status"] == NVAPI_ERROR && receipt["events"].back()["status"] == NVAPI_ERROR);
			Check(submissions.size() == (failureRepeat == 0 ? 1 : 317));
			const auto& repeat = receipt["kernelPair"]["repetitions"].back();
			Check(repeat["beginCompleted"] == true && repeat["completed"] == false && repeat["endCompleted"] == false && repeat["physicalLaunches"] == 1);
			Check(KernelChainProbeTestAccess::Model(probe).Retire(true, true) && comparisonRetired == 53 && device.references == 1);
		}
	}

}

int main()
{
	try {
		TestOwnedPacketsAndBoundaries();
		TestGrowthAndCapacity();
		TestForwardWarmupAndFailure();
		TestAdmissionAndAbiFailure();
		TestForeignThreadBoundary();
		TestRestorationContract();
		TestIdentityCapture();
		TestRuntimeFrameOwnership();
		TestRuntimeFallbackClassification();
		TestDeferredRuntimePreparation();
		TestModelN1StagePrefix();
		TestIdentityRejections();
		TestModelObserverAndFailedSubmission();
		TestMultipleCacheRollback();
		TestReplacementAdmission();
		TestReplacementSubmissionAndRetirement();
		TestReplacementSubmissionRejections();
		TestReplacementCreationFailures();
		TestReplacementRetirementFailures();
		TestReplacementFutureAliases();
		TestPairPlans();
		TestLayerControlPlan();
		TestCompatiblePairing();
		TestStructuralStageIdentity();
		TestCrossEyeModelPackets();
		TestPairHeapIdentityAndManifest();
		TestPairAdmissionAndEightSlotRollback();
		TestPairDescriptorGuards();
		TestPairPackedSubmission();
		TestSelectedStageDynamicPairs();
		TestRepeatedSchedules();
		TestRepetitionFailures();
		TestBalancedComparisons();
		TestComparisonFailedPhysicalReceipt();
		std::cout << checks << " kernel-chain CPU checks passed; no GPU or provider hook installed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
