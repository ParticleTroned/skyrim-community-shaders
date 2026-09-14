#include "D3D12Interop.h"

namespace NeuralRendering
{
	bool D3D12Interop::RecordTransportSubmission(const D3D12InteropSubmissionTiming& timing)
	{
		std::scoped_lock lock(mutex_);
		if (!initialized_ || !recording_ || recordingContext_ >= commandContexts_.size() ||
			recordingThread_ != std::this_thread::get_id() || featureTimingOpen_ ||
			timingRecording_ || featureTimingCompleted_) {
			return RecordFailureLocked(E_UNEXPECTED, "RecordTransportSubmission state");
		}
		if (timing.evaluationCount != 0u || timing.pixelCount == 0u ||
			timing.logicalEyeCount == 0u || timing.logicalEyeCount > 2u ||
			!IsValidInsertionPoint(timing.insertionPoint) ||
			ClassifyFeatureSlotMask(timing.featureSlotMask) == FeatureSlotRoute::Unexpected) {
			return RecordFailureLocked(E_INVALIDARG, "RecordTransportSubmission metadata");
		}
		auto& context = commandContexts_[recordingContext_];
		context.timing = timing;
		context.timingPending = false;
		// EndD3D12 still enforces one complete metadata scope and its normal
		// queue/fence/D3D11 wait path. No timestamp is mistaken for NR inference.
		featureTimingCompleted_ = true;
		lastError_ = S_OK;
		lastOperation_ = "RecordTransportSubmission";
		return true;
	}
}
