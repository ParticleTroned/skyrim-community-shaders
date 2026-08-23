#pragma once

#include <cstdint>

namespace CSPluginAPI
{
	/** Eye selection for VR capture. Flat runtimes treat every value as mono. */
	enum class CaptureEye001 : std::uint32_t
	{
		kLeft = 0,
		kRight = 1,
		kBoth = 2
	};

	enum class CaptureState001 : std::uint32_t
	{
		kIdle = 0,
		kCapturing = 1,
		kFlushing = 2,
		kComplete = 3,
		kFailed = 4
	};

	enum class CaptureResult001 : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kFeatureDisabled = 2,
		kBusy = 3,
		kNoActiveSequence = 4,
		kOutputUnavailable = 5,
		kInternalError = 6,
		kBufferTooSmall = 7
	};

	/** Fixed-layout status structure safe to pass between independently built SKSE plugins. */
	struct CaptureStatus001
	{
		std::uint32_t structSize = sizeof(CaptureStatus001);
		CaptureState001 state = CaptureState001::kIdle;
		CaptureEye001 eye = CaptureEye001::kLeft;
		std::uint64_t sessionId = 0;
		std::uint64_t framesScheduled = 0;
		std::uint64_t framesWritten = 0;
		std::uint64_t framesDropped = 0;
	};

	/**
	 * Codec-neutral capture surface. CSX owns lossless stills and frame sets only;
	 * clients own all video composition and encoding.
	 */
	struct ICSCaptureInterface001
	{
		virtual CaptureResult001 RequestScreenshot(CaptureEye001 eye) = 0;
		virtual CaptureResult001 StartFrameSequence(CaptureEye001 eye) = 0;
		virtual CaptureResult001 StopFrameSequence() = 0;
		virtual CaptureResult001 GetCaptureStatus(CaptureStatus001* status) = 0;

		/**
		 * Copies the UTF-8 path of a completed or active sequence. Call with a null
		 * buffer to query the required byte count, including the NUL terminator.
		 */
		virtual CaptureResult001 CopySequencePath(
			std::uint64_t sessionId,
			char* buffer,
			std::uint32_t bufferBytes,
			std::uint32_t* requiredBytes) = 0;
	};
}
