#include "Features/Upscaling/NeuralRendering/CharacterMaskWorkPolicy.h"
#include "Features/Upscaling/NeuralRendering/CharacterRendering.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

using namespace NeuralRendering;

static void Require(bool value)
{
	if (!value)
		throw std::runtime_error("Prepared selection lost its source contract");
}

struct MaskView final : ID3D11ShaderResourceView
{
	ULONG references = 0;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override
	{
		if (object)
			*object = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override { return --references; }
	void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override { *device = nullptr; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
	void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override { *resource = nullptr; }
	void STDMETHODCALLTYPE GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC* desc) override { *desc = {}; }
};
static MaskView firstMask, secondMask;

namespace NeuralRendering::Color
{
	struct Registry
	{
		static Registry& Instance()
		{
			static Registry value;
			return value;
		}
		bool enabled = true;
		std::uint64_t epoch = 7;
		bool CaptureEvidenceEnabled() const { return enabled; }
		std::uint64_t CaptureEpoch() const { return epoch; }
	};
}

struct Fixture
{
	struct Slot
	{
		struct
		{
			std::uint32_t sourceWorldFrame = 10;
			std::uint64_t generation = 3, settings = 5;
			std::uint64_t sourceCaptureSerial = 9;
			UpscalingDLSS::ViewportCrop crop{};
			float captureJitterX = 0, captureJitterY = 0;
			bool outputIsJittered = false;
		} prepareKey;
		bool prepared = true, maskUniform = false;
		bool requiresEvaluation = true;
		bool maskInitialized = true, resetHistoryAfterEmpty = false;
		bool zeroCoverageBypassResolved = false, zeroCoverageBypassed = false;
		CharacterFeature18Disposition feature18Disposition = CharacterFeature18Disposition::Unresolved;
		CharacterEmptyProofKind emptyProof = CharacterEmptyProofKind::None;
		float uniformMaskValue = 0;
		std::uint64_t contentSerial = 1;
		std::uint32_t width = 99, height = 73;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskSrv;
		ComputeSubrect maskWorkSubrect{ 7, 9, 13, 11 };
		ComputeSubrect computeSubrect{ 3, 5, 40, 30 };
		CharacterComputeRegionPlan computeRegions{};
		std::optional<RoiDescriptor> roi;
	};
	std::array<Slot, 4> slots_{};
	std::uint64_t earlyMaskCaptureSerial_ = 9;
	std::uint32_t capturedFrame_ = 10;
	CharacterSnapshot snapshot_{};
	std::array<std::shared_ptr<const CharacterPreparationEvidence>, 4> latestPreparationEvidence_{};
	mutable std::mutex mutex_;

#include "neural_empty_proof_under_test.h"
#include "neural_preparation_evidence_under_test.h"
#include "neural_prepared_result_under_test.h"
#include "neural_prepared_slot_under_test.h"

	void Publish(unsigned frame, unsigned slot)
	{
		auto& entry = snapshot_.preparedFrames[0];
		if (entry.frame != frame)
			entry = {};
		entry.frame = frame;
		entry.preparedSlotMask |= 1u << slot;
		entry.sourceWorldFrames[slot] = slots_[slot].prepareKey.sourceWorldFrame;
		entry.generations[slot] = slots_[slot].prepareKey.generation;
		entry.contentSerials[slot] = slots_[slot].contentSerial;
		entry.widths[slot] = slots_[slot].width;
		entry.heights[slot] = slots_[slot].height;
	}
};
static Fixture* producer = nullptr;

namespace NeuralRendering
{
	static void Increment(std::uint64_t& value) { ++value; }
#include "neural_full_compute_subrect_under_test.h"

	class CharacterRendering::State : public Fixture
	{};
	CharacterRendering::CharacterRendering() : state_(std::make_unique<State>()) { producer = state_.get(); }
	CharacterRendering::~CharacterRendering() = default;
	CharacterRendering& CharacterRendering::Instance()
	{
		static CharacterRendering instance;
		return instance;
	}

#include "neural_disposition_under_test.h"
#include "neural_prepared_selection_under_test.h"
}

int main()
{
	auto& rendering = CharacterRendering::Instance();
	for (unsigned slotIndex = 0; slotIndex < 4; ++slotIndex) {
		auto& slot = producer->slots_[slotIndex];
		slot.maskSrv = &firstMask;
		slot.computeRegions.count = 2;
		slot.computeRegions.regions = { ComputeSubrect{ 3, 5, 19, 30 }, ComputeSubrect{ 24, 5, 19, 30 } };
		for (unsigned region = 0; region < 2; ++region)
			slot.computeRegions.roi[region] = BuildRoiDescriptor(std::nullopt,
				slot.computeRegions.regions[region], { 99, 73 }, true);
		const auto originalPlan = slot.computeRegions;
		producer->Publish(11, slotIndex);
		const auto read = [&](unsigned frame = 11) {
			return rendering.GetPreparedSelection(slotIndex, frame, 10, 3, 99, 73);
		};
		const auto first = read();
		Require(first.maskSupport == ComputeSubrect{ 6, 8, 15, 13 });
		Require(first.computeSubrect == slot.computeSubrect);
		// Simulate a producer publishing between consumer operations. The old
		// value must stay coherent, and the old identity must not see the new slot.
		++slot.contentSerial;
		slot.maskSrv = &secondMask;
		Require(firstMask.references == 1);
		slot.maskUniform = true;
		slot.uniformMaskValue = 0;
		slot.computeSubrect = { 0, 0, 99, 73 };
		slot.computeRegions = {};
		Require(!read().computeSubrect.IsValid());
		producer->Publish(12, slotIndex);
		const auto empty = read(12);
		Require(!read().computeSubrect.IsValid());
		Require(first.mask.Get() == &firstMask && empty.mask.Get() == &secondMask);
		Require(empty.maskSupport.Area() == 0 && empty.computeSubrect == slot.computeSubrect);
		Require(first.maskSupport == ComputeSubrect{ 6, 8, 15, 13 });
		Require(first.computeSubrect == ComputeSubrect{ 3, 5, 40, 30 });
		Require(first.computeRegions == originalPlan && empty.computeRegions.count == 0);
		slot.uniformMaskValue = 1;
		Require(read(12).maskSupport == ComputeSubrect{ 0, 0, 99, 73 });
		slot.maskUniform = false;
		slot.maskWorkSubrect = {};
		Require(read(12).maskSupport == ComputeSubrect{ 0, 0, 99, 73 });
		Require(!rendering.GetPreparedSelection(slotIndex, 12, 9, 3, 99, 73).computeSubrect.IsValid());
		Require(!rendering.GetPreparedSelection(slotIndex, 12, 10, 4, 99, 73).computeSubrect.IsValid());
		Require(!rendering.GetPreparedSelection(slotIndex, 12, 10, 3, 100, 73).computeSubrect.IsValid());
		Require(!rendering.GetPreparedSelection(slotIndex, 12, 10, 3, 99, 74).computeSubrect.IsValid());
		auto evidence = std::make_shared<CharacterPreparationEvidence>();
		evidence->prepared = true;
		evidence->key = { 12, 10, slotIndex & 1u, slotIndex, 3, slot.contentSerial, 5, 7 };
		evidence->key.sourceCaptureSerial = 9;
		producer->latestPreparationEvidence_[slotIndex] = evidence;
		Require(producer->FindPreparationEvidence(12, 10, 3, slotIndex) == evidence);
		for (const auto change : { 0, 1, 2, 3, 4, 5, 6 }) {
			auto stale = std::make_shared<CharacterPreparationEvidence>(*evidence);
			if (change == 0)
				stale->key.eye ^= 1u;
			if (change == 1)
				++stale->key.frame;
			if (change == 2)
				++stale->key.sourceWorldFrame;
			if (change == 3)
				++stale->key.generation;
			if (change == 4)
				++stale->key.contentSerial;
			if (change == 5)
				++stale->key.captureEpoch;
			if (change == 6)
				stale->key.featureSlot ^= 2u;
			producer->latestPreparationEvidence_[slotIndex] = stale;
			Require(!producer->FindPreparationEvidence(12, 10, 3, slotIndex));
		}
		producer->latestPreparationEvidence_[slotIndex] = evidence;
		for (const auto change : { 0, 1, 2, 3, 4, 5 }) {
			const auto key = slot.prepareKey;
			if (change == 0)
				++slot.prepareKey.settings;
			if (change == 1)
				++slot.prepareKey.crop.output.left;
			if (change == 2)
				slot.prepareKey.captureJitterX = 0.25f;
			if (change == 3)
				slot.prepareKey.captureJitterY = -0.25f;
			if (change == 4)
				slot.prepareKey.outputIsJittered = true;
			if (change == 5)
				++slot.prepareKey.sourceCaptureSerial;
			Require(!producer->FindPreparationEvidence(12, 10, 3, slotIndex));
			slot.prepareKey = key;
		}
		producer->snapshot_.preparedFrames[0].preparationEvidence[slotIndex] = evidence;
		auto failure = std::make_shared<CharacterPreparationEvidence>(*evidence);
		failure->prepared = false;
		producer->latestPreparationEvidence_[slotIndex] = failure;
		Require(producer->FindPreparationEvidence(12, 10, 3, slotIndex) == failure);
		Require(producer->FindPreparationEvidence(12, 10, 3, slotIndex, true) == evidence);
		CharacterMaskPrepareArgs args;
		args.frameId = 12;
		args.sourceWorldFrame = 10;
		args.generation = 3;
		args.featureSlot = slotIndex;
		for (bool capture : { false, true }) {
			Color::Registry::Instance().enabled = capture;
			auto result = producer->BuildPreparedResult(args, slot);
			Require(result.prepared && result.computeSubrect == slot.computeSubrect);
			Require(result.computeRegions == slot.computeRegions && result.roi == slot.roi);
#ifdef DEVBENCH_BRIDGE_ENABLED
			Require(result.evidence == (capture ? evidence : nullptr));
#else
			Require(!result.evidence);
#endif
		}
		producer->latestPreparationEvidence_[slotIndex] = {};
		Require(producer->FindPreparationEvidence(12, 10, 3, slotIndex) == evidence);
		++Color::Registry::Instance().epoch;
		Require(!producer->FindPreparationEvidence(12, 10, 3, slotIndex));
		Require(!producer->BuildPreparedResult(args, slot).evidence);
		--Color::Registry::Instance().epoch;
		++slot.contentSerial;
		Require(!producer->FindPreparationEvidence(12, 10, 3, slotIndex));
		Require(!producer->BuildPreparedResult(args, slot).evidence);
		slot.contentSerial = 0;
		auto zeroSerial = std::make_shared<CharacterPreparationEvidence>(*evidence);
		zeroSerial->key.contentSerial = 0;
		producer->latestPreparationEvidence_[slotIndex] = zeroSerial;
		Require(!producer->FindPreparationEvidence(12, 10, 3, slotIndex));
		slot.requiresEvaluation = false;
		Require(!producer->BuildPreparedResult(args, slot).requiresEvaluation);
	}
	Require(!rendering.GetPreparedSelection(4, 12, 10, 3, 99, 73).mask);
	Require(!producer->FindPreparationEvidence(12, 10, 3, 4));
	// Exercise the production receipt path, including asymmetric eyes and
	// repeated publication. Proofs belong to immutable contents, not a frame age.
	for (const auto kind : { CharacterEmptyProofKind::CpuSelection,
			 CharacterEmptyProofKind::GpuCategorySuperset, CharacterEmptyProofKind::DiagnosticZero }) {
		producer->snapshot_ = {};
		producer->slots_ = {};
		auto& empty = producer->slots_[0];
		empty.maskUniform = true;
		empty.requiresEvaluation = false;
		empty.emptyProof = kind;
		empty.resetHistoryAfterEmpty = true;
		producer->Publish(20, 0);
		producer->Publish(20, 1);
		Require(producer->HasCurrentEmptyProof(empty));
		for (unsigned change = 0; change < 7; ++change) {
			auto stale = empty;
			switch (change) {
			case 0:
				++stale.prepareKey.sourceCaptureSerial;
				break;
			case 1:
				++stale.prepareKey.sourceWorldFrame;
				break;
			case 2:
				stale.contentSerial = 0;
				break;
			case 3:
				stale.emptyProof = CharacterEmptyProofKind::None;
				break;
			case 4:
				stale.prepared = false;
				break;
			case 5:
				stale.maskInitialized = false;
				break;
			case 6:
				stale.uniformMaskValue = 1;
				break;
			}
			Require(!producer->HasCurrentEmptyProof(stale));
		}
		CharacterMaskPrepareArgs args;
		Require(!producer->BuildPreparedResult(args, empty).resetHistory);
		rendering.ResolveFeature18Disposition(20, 10, 3, 3, 2, 2, 1);
		const auto& frame = producer->snapshot_.preparedFrames[0];
		Require(frame.bypassedSlotMask == 1 && frame.evaluatedSlotMask == 2 && frame.successfulSlotMask == 2);
		Require(empty.feature18Disposition == CharacterFeature18Disposition::EmptyBypass);
		Require(empty.resetHistoryAfterEmpty);
		const auto count = producer->snapshot_.provenEmptyFeatureBypasses;
		Require(count == (kind == CharacterEmptyProofKind::DiagnosticZero ? 0u : 1u));
		rendering.ResolveFeature18Disposition(20, 10, 3, 3, 2, 2, 1);
		Require(producer->snapshot_.provenEmptyFeatureBypasses == count);
		// Re-enter with the same frozen source: failure keeps the reset pending.
		empty.requiresEvaluation = true;
		empty.emptyProof = CharacterEmptyProofKind::None;
		producer->Publish(21, 0);
		Require(producer->BuildPreparedResult(args, empty).resetHistory);
		rendering.ResolveFeature18Disposition(21, 10, 3, 1, 1, 0, 0);
		Require(empty.resetHistoryAfterEmpty);
		producer->Publish(22, 0);
		rendering.ResolveFeature18Disposition(22, 10, 3, 1, 1, 1, 0);
		Require(!empty.resetHistoryAfterEmpty);
		// Stale proof cannot claim a bypass receipt even if a caller asks for one.
		empty.requiresEvaluation = false;
		empty.emptyProof = kind;
		++empty.prepareKey.sourceCaptureSerial;
		producer->Publish(23, 0);
		Require(!producer->FindPreparedSlot(0, 23, 10, 3, 99, 73));
		rendering.ResolveFeature18Disposition(23, 10, 3, 1, 0, 0, 1);
		Require(empty.feature18Disposition == CharacterFeature18Disposition::Aborted);
		Require(producer->snapshot_.preparedFrames[0].bypassedSlotMask == 0);
	}
}
