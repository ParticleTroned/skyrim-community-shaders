#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "CharacterSettings.h"
#	include "RegionCapacity.h"
#	include "RoiDescriptor.h"

namespace NeuralRendering
{
	/** Keeps the baseline envelope and allocation while testing current C context. */
	[[nodiscard]] inline bool ApplyCurrentContextExperiment(
		RoiDescriptor& a_roi, const CharacterSettings& a_settings,
		bool a_outputIsJittered, bool a_currentSupportProven) noexcept
	{
		if (!a_settings.experimentalCurrentContext || !a_settings.enabled ||
			!a_outputIsJittered || !a_currentSupportProven ||
			a_settings.debugView != CharacterDebugView::Off ||
			a_settings.maskTestMode != CharacterMaskTestMode::Authored ||
			!a_roi.samplingSupport || !a_roi.temporalEnvelope ||
			!GetRoiDescriptorViolation(a_roi, a_roi.inferenceContext, a_roi.allocationCapacity).empty())
			return false;

		// Reuse established spatial padding; only historical motion headroom is optional.
		const auto current = BuildCharacterProviderComputeSubrect(*a_roi.samplingSupport,
			a_roi.allocationCapacity.width, a_roi.allocationCapacity.height);
		if (!QualifiedExperimentalContextGeometry(current) ||
			!current.Fits(a_roi.allocationCapacity.width, a_roi.allocationCapacity.height) ||
			!ContainsComputeSubrect(*a_roi.temporalEnvelope, current))
			return false;
		a_roi.ownedOutput = current;
		a_roi.inferenceContext = current;
		a_roi.currentContextApplied = true;
		return true;
	}
}

#endif
