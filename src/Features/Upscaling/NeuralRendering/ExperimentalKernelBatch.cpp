#include "ExperimentalKernelBatch.h"

#include "DevBench/KernelFrameFence.h"
#include "DevBench/ProviderKernelChainProbe.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <dxgi1_4.h>
#include <nvsdk_ngx.h>
#include <utility>

namespace NeuralRendering
{
	struct ExperimentalKernelBatch::State
	{
		using Probe = NrReplay::ProviderKernelChainProbe;
		using Proxy = NrReplay::KernelChainCommandList;
		struct Arena
		{
			LifetimeFenceSnapshot before{};
			std::uint64_t completion = 0;
			std::shared_ptr<void> packets;
			std::unique_ptr<Proxy, Proxy::Deleter> proxy;
			Probe::RuntimeCounters counters{};
			bool warmup = true, finished = false;
		};
		nlohmann::json receipt;
		std::unique_ptr<Probe> probe;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		std::array<std::unique_ptr<Arena>, 3> pending;
		std::unique_ptr<Arena> current;
		Status status;
		Frame frame{};
		std::array<char, 256> reason{};
		bool retained = false;
		unsigned completedEvaluations = 0;
		unsigned pendingCreation = UINT_MAX;

		void Note(std::string_view message) noexcept
		{
			const auto size = std::min(message.size(), reason.size() - 1);
			std::memcpy(reason.data(), message.data(), size);
			reason[size] = '\0';
		}
		void Fail(std::string_view message) noexcept
		{
			if (!status.failed)
				Note(message);
			status.failed = true;
		}
		void Reclaim(const LifetimeFenceSnapshot& observed)
		{
			NrReplay::ProviderFloor::Require(KernelFrameFence::Valid(observed), "experimental kernel completed fence is unavailable");
			for (auto& arena : pending) {
				if (!arena)
					continue;
				NrReplay::ProviderFloor::Require(KernelFrameFence::Same(arena->before, observed), "experimental kernel fence owner changed before retirement");
				if (observed.completed >= arena->completion)
					arena.reset();
			}
		}
	};

	ExperimentalKernelBatch::ExperimentalKernelBatch() : state_(std::make_unique<State>()) {}
	ExperimentalKernelBatch::~ExperimentalKernelBatch()
	{
		if (state_ && state_->probe && !state_->retained)
			(void)Shutdown(false);
		if (state_ && state_->retained)
			(void)state_.release();
	}
	std::string_view ExperimentalKernelBatch::ModeName(Mode mode) noexcept
	{
		switch (mode) {
		case Mode::Original:
			return "original";
		case Mode::LayerControl:
			return "layer-control";
		case Mode::ClonedN2:
			return "clonedN2";
		case Mode::SharedN2:
			return "sharedN2";
		}
		return "invalid";
	}
	std::string_view ExperimentalKernelBatch::AdmissionViolation(const Frame& frame) noexcept
	{
		if (frame.style > 3 || frame.singleSubrectScale != 1.0f)
			return "kernel experiment requires a supported model and explicit unchanged native rectangles";
		for (std::size_t i = 0; i < frame.regions.size(); ++i) {
			const auto& region = frame.regions[i];
			const auto& layout = region.layout;
			if (region.eye != i / 2 || region.region != i % 2 || region.slot >= Runtime::kFeatureSlotCount)
				return "kernel experiment requires four ordered private regions";
			for (std::size_t j = 0; j < i; ++j)
				if (region.slot == frame.regions[j].slot)
					return "kernel experiment cannot share native feature slots";
			if (const auto violation = GetNativeEvaluationLayoutViolation(layout, region.controlMask); !violation.empty())
				return violation;
			for (const auto* image : { &layout.color, &layout.depth, &layout.motion, &layout.output })
				if (image->backing.width > 16384 || image->backing.height > 16384)
					return "kernel experiment resource dimensions exceed the bounded native contract";
		}
		return {};
	}
	bool ExperimentalKernelBatch::Initialize(ID3D12Device* device, const std::filesystem::path& provider,
		const std::filesystem::path& manifest, Mode mode) noexcept
	{
		auto& s = *state_;
		try {
			using NrReplay::ProviderFloor::Require;
			Require(!s.probe && !s.retained && device, "experimental kernel session is already owned or lacks its device");
			Require(ModeName(mode) != "invalid", "unknown experimental kernel mode");
			const bool batch = mode == Mode::ClonedN2 || mode == Mode::SharedN2;
			Require(batch == !manifest.empty(), "only batched modes require a kernel catalog");
			auto& runtime = Runtime::Instance();
			Require(runtime.Status() == RuntimeStatus::Initialized && runtime.GetResidentFeatureMask() == 0 &&
						std::filesystem::equivalent(provider, runtime.Path()),
				"kernel hooks require an initialized provider before feature creation");
			const auto hash = NrReplay::ProviderFloor::CanonicalSha256(runtime.Hash());
			Require(hash == NrReplay::ProviderFloor::kProviderSha256, "kernel runtime identity differs from the qualified provider");
			Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			DXGI_ADAPTER_DESC1 desc{};
			Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
						SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) &&
						SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x10de && desc.DeviceId == 0x2f58,
				"kernel experiment requires the qualified SM120 RTX5070Ti Laptop adapter");
			s.status = {};
			s.reason = {};
			s.status.mode = mode;
			s.device = device;
			s.probe = std::make_unique<State::Probe>(s.receipt, "forward", std::filesystem::path{}, std::filesystem::path{},
				batch ? "model-batch" : mode == Mode::Original ? "original" :
																 "layer-control",
				manifest,
				NrReplay::KernelPair::kStages, 1, std::string_view{}, true);
			s.probe->Apply(provider, hash);
			if (batch) {
				const auto expected = mode == Mode::ClonedN2 ? NrReplay::KernelChain::kClonedModelCatalogSha256 : NrReplay::KernelChain::kSharedModelCatalogSha256;
				Require(s.probe->ModelCatalogIdentity() == expected, "selected kernel algorithm does not match the pinned catalog");
			}
			s.status.initialized = true;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel initialization failure");
		}
		if (s.probe && !s.probe->Restore(true)) {
			s.retained = true;
			s.status.retirementProven = false;
		}
		return false;
	}
	bool ExperimentalKernelBatch::BeginFrame(ID3D12GraphicsCommandList* commandList, const LifetimeFenceSnapshot& before,
		const Frame& frame) noexcept
	{
		auto& s = *state_;
		try {
			using NrReplay::ProviderFloor::Require;
			Require(s.status.initialized && !s.status.failed && !s.current && s.probe && commandList && !s.retained,
				"experimental kernel frame has no healthy session or overlaps its predecessor");
			Require(!frame.inspectUnqualifiedPipeline || s.status.mode == Mode::Original,
				"unqualified pipeline inspection only permits original native execution");
			if (const auto violation = AdmissionViolation(frame); !violation.empty()) {
				s.Note(violation);
				return false;
			}
			Require(before.recording && before.device == reinterpret_cast<std::uintptr_t>(s.device.Get()), "kernel frame recording device differs");
			s.Reclaim(before);
			Require(std::ranges::any_of(s.pending, [](const auto& item) { return !item; }), "kernel frame arena capacity is still owned by the GPU");
			Require(!s.probe->RuntimeEpochStale(), "kernel feature epoch requires retirement after original pass-through");
			s.current = std::make_unique<State::Arena>();
			s.current->before = before;
			s.current->warmup = frame.inspectUnqualifiedPipeline || s.status.warmupFrames < 3 || !s.status.graphMatchesQualified;
			s.current->proxy.reset(new State::Proxy(commandList, [&s](std::string_view name) { return s.probe->BeforeCommand(name); }, [&s](std::string_view reason) { s.probe->RecordFailure(reason); }, {}, {}, [&s](UINT count, const D3D12_RESOURCE_BARRIER* barriers) { return s.probe->BarrierDisposition(count, barriers); }, [&s](UINT count, ID3D12DescriptorHeap* const* heaps) { return s.probe->HeapDisposition(count, heaps); }));
			s.probe->BeginSample(static_cast<unsigned>(frame.sequence), s.current->warmup);
			s.Note({});
			s.frame = frame;
			s.completedEvaluations = 0;
			s.pendingCreation = UINT_MAX;
			s.status.recording = true;
			s.status.warmup = s.current->warmup;
			s.status.inspection = frame.inspectUnqualifiedPipeline;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel frame admission failure");
		}
		return false;
	}
	ID3D12GraphicsCommandList* ExperimentalKernelBatch::BeginEvaluation(unsigned eye, unsigned region) noexcept
	{
		auto& s = *state_;
		try {
			NrReplay::ProviderFloor::Require(s.status.recording && s.current && !s.status.failed &&
												 eye < 2 && region < 2 && eye * 2 + region == s.completedEvaluations,
				"kernel evaluation order differs from the admitted frame");
			s.probe->BeginEvaluation(s.current->proxy.get(), s.current->proxy->Real(), eye, region);
			return s.current->proxy.get();
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel evaluation admission failure");
		}
		return nullptr;
	}
	bool ExperimentalKernelBatch::ValidateFeatureCreation(const NVSDK_NGX_Parameter* parameters) noexcept
	{
		auto& s = *state_;
		try {
			using NrReplay::ProviderFloor::Require;
			Require(s.status.recording && s.current && s.current->warmup && !s.status.failed && parameters && s.pendingCreation == UINT_MAX,
				"kernel feature creation occurred outside original warmup");
			const auto& layout = s.frame.regions.at(s.completedEvaluations).layout;
			for (const auto* key : { "Width", "OutWidth", "DLSSNR.Width", "DLSSNR.OutputWidth", "DLSSNR.Output.Width" }) {
				unsigned value = 0;
				Require(parameters->Get(key, &value) == NVSDK_NGX_Result_Success && value == layout.creation.output.width, "kernel feature creation width differs");
			}
			for (const auto* key : { "Height", "OutHeight", "DLSSNR.Height", "DLSSNR.OutputHeight", "DLSSNR.Output.Height" }) {
				unsigned value = 0;
				Require(parameters->Get(key, &value) == NVSDK_NGX_Result_Success && value == layout.creation.output.height, "kernel feature creation height differs");
			}
			for (const auto& [key, expected] : { std::pair{ "DLSSNR.InputWidth", layout.creation.input.width },
					 std::pair{ "DLSSNR.InputHeight", layout.creation.input.height }, std::pair{ "DLSSNR.Upscaling", layout.featureUpscaling ? 1u : 0u },
					 std::pair{ "DLSSNR.Hint.Render.Preset", 0u } }) {
				unsigned value = UINT_MAX;
				Require(parameters->Get(key, &value) == NVSDK_NGX_Result_Success && value == expected, "kernel feature creation input extent or preset differs");
			}
			for (const auto* key : { "DLSSNR.Scale", "DLSSNR.ScalingRatio" }) {
				float value = 0;
				Require(parameters->Get(key, &value) == NVSDK_NGX_Result_Success &&
							value == static_cast<float>(layout.creation.output.width) / layout.creation.input.width,
					"kernel feature creation scale differs");
			}
			for (const auto* key : { NVSDK_NGX_Parameter_ResourceAllocCallback, NVSDK_NGX_Parameter_ResourceReleaseCallback,
					 NVSDK_NGX_EParameter_ResourceAllocCallback, NVSDK_NGX_EParameter_ResourceReleaseCallback }) {
				void* callback = nullptr;
				const auto result = parameters->Get(key, &callback);
				Require(!callback && (result == NVSDK_NGX_Result_Success || result == NVSDK_NGX_Result_FAIL_UnsupportedParameter),
					"kernel feature allocation callback is present or its state is unknown");
			}
			s.pendingCreation = s.completedEvaluations;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel creation admission failure");
		}
		return false;
	}
	bool ExperimentalKernelBatch::CommitFeatureCreation() noexcept
	{
		auto& s = *state_;
		try {
			NrReplay::ProviderFloor::Require(s.status.recording && s.current && s.current->warmup && !s.status.failed &&
												 s.pendingCreation == s.completedEvaluations && s.pendingCreation < s.frame.regions.size(),
				"kernel creation success lacks its matching parameter proof");
			s.probe->ConfirmDefaultAllocationParameters();
			s.pendingCreation = UINT_MAX;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel creation confirmation failure");
		}
		return false;
	}
	bool ExperimentalKernelBatch::EndEvaluation() noexcept
	{
		auto& s = *state_;
		try {
			NrReplay::ProviderFloor::Require(s.current && s.status.recording && !s.status.failed && s.current->proxy->Healthy() &&
												 s.pendingCreation == UINT_MAX,
				"kernel proxy, evaluation or creation confirmation failed");
			s.probe->EndEvaluation();
			++s.completedEvaluations;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel evaluation completion failure");
		}
		return false;
	}
	bool ExperimentalKernelBatch::FinishFrame() noexcept
	{
		auto& s = *state_;
		try {
			NrReplay::ProviderFloor::Require(s.current && s.status.recording && !s.status.failed && s.completedEvaluations == 4 &&
												 s.current->proxy->Healthy() && !s.current->finished,
				"kernel frame is incomplete or its proxy failed");
			s.probe->FinishCommandList();
			s.current->warmup = s.probe->RuntimeWarmup();
			s.status.warmup = s.current->warmup;
			if (s.probe->RuntimeDescriptorRefresh())
				s.Note("original frame: native descriptor metadata refreshed before kernel recording");
			s.current->counters = s.probe->GetRuntimeCounters();
			const auto graph = s.probe->GetRuntimeGraph();
			s.status.graphLaunches = graph.launches;
			s.status.graphIdentities = graph.identities;
			s.status.graphFamilyIdentities = graph.familyIdentities;
			s.status.regionPairs = graph.pairs;
			s.status.graphMatchesQualified = graph.qualified;
			if (s.current->warmup && graph.qualified)
				s.probe->PrepareRuntimeModel();
			if (!graph.qualified)
				s.Note(graph.reason);
			s.current->packets = s.probe->TakeRuntimeFrame();
			s.current->finished = true;
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel frame completion failure");
		}
		return false;
	}
	bool ExperimentalKernelBatch::Submitted(const LifetimeFenceSnapshot& after) noexcept
	{
		auto& s = *state_;
		try {
			NrReplay::ProviderFloor::Require(s.current && s.current->finished && s.current->packets && !s.status.failed,
				"kernel submission lacks completed owned recording");
			s.current->completion = KernelFrameFence::Submission(s.current->before, after);
			const auto free = std::ranges::find_if(s.pending, [](const auto& item) { return !item; });
			NrReplay::ProviderFloor::Require(free != s.pending.end(), "kernel submitted arena has no retirement slot");
			++s.status.frames;
			s.status.warmupFrames += s.current->warmup;
			s.status.descriptorRefreshFrames += s.probe->RuntimeDescriptorRefresh();
			s.status.batchedFrames += s.current->counters.privateLaunches != 0;
			s.status.logicalLaunches += s.current->counters.logical;
			s.status.physicalLaunches += s.current->counters.physical;
			s.status.privateLaunches += s.current->counters.privateLaunches;
			*free = std::move(s.current);
			s.status.recording = false;
			s.Reclaim(after);
			return true;
		} catch (const std::exception& error) {
			s.Fail(error.what());
		} catch (...) {
			s.Fail("unknown experimental kernel submission identity failure");
		}
		s.status.retirementProven = false;
		return false;
	}
	void ExperimentalKernelBatch::Aborted() noexcept
	{
		auto& s = *state_;
		if (!s.status.retirementProven) {
			s.Fail("kernel frame submission is uncertain and cannot be discarded as an abort");
			return;
		}
		if (s.probe)
			s.probe->AbortRuntimeFrame();
		s.current.reset();
		s.pendingCreation = UINT_MAX;
		s.status.recording = false;
		s.status.epochStale = true;
	}
	bool ExperimentalKernelBatch::Shutdown(bool gpuIdle) noexcept
	{
		auto& s = *state_;
		if (s.retained)
			return false;
		if (!s.probe)
			return true;
		const bool safe = gpuIdle && !s.current && !s.status.recording;
		if (!s.probe->Restore(safe)) {
			s.retained = true;
			s.status.retirementProven = false;
			s.Fail("experimental kernel retirement unproven; native and frame owners retained until exit");
			return false;
		}
		for (auto& arena : s.pending)
			arena.reset();
		s.probe.reset();
		s.device.Reset();
		s.status.initialized = false;
		s.status.retirementProven = true;
		return true;
	}
	ExperimentalKernelBatch::Status ExperimentalKernelBatch::GetStatus() const
	{
		const auto& s = *state_;
		auto result = s.status;
		result.failed |= s.probe && !s.probe->Healthy();
		result.canFallback = result.failed && s.status.recording && s.current && !s.current->finished &&
		                     !s.retained && s.status.retirementProven && s.probe && s.probe->CanRetryOriginalBeforeSubmission();
		result.reason = s.probe && !s.probe->Healthy() ? s.probe->FailureReason() : std::string_view(s.reason.data());
		result.pendingFrames = static_cast<std::size_t>(std::ranges::count_if(s.pending, [](const auto& item) { return static_cast<bool>(item); }));
		result.epochStale |= s.probe && s.probe->RuntimeEpochStale();
		return result;
	}
}
