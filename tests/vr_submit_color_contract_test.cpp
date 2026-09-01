#include "Features/Upscaling/VRSubmitColorContract.h"

namespace
{
	using namespace VRSubmitColorContract;

	constexpr bool CoversPresentationAndVendorAdmission()
	{
		const auto automatic = Resolve(true, SourceColorSpace::Automatic);
		const auto gamma = Resolve(true, SourceColorSpace::Gamma);
		const auto linear = Resolve(true, SourceColorSpace::Linear);
		if (automatic != gamma || !IsPresentationSupported(gamma) || !IsVendorSupported(gamma) || DLSSUsesHDR(gamma))
			return false;
		if (linear == gamma || !IsPresentationSupported(linear) || IsVendorSupported(linear) || DLSSUsesHDR(linear))
			return false;

		constexpr SourceColorSpace sources[]{ SourceColorSpace::Automatic, SourceColorSpace::Gamma, SourceColorSpace::Linear };
		for (const auto source : sources) {
			if (IsPresentationSupported(Resolve(false, source)) || IsVendorSupported(Resolve(false, source)))
				return false;
		}
		return !IsPresentationSupported(Resolve(true, SourceColorSpace::Unsupported)) &&
		       !IsVendorSupported(Resolve(true, static_cast<SourceColorSpace>(255)));
	}

	constexpr bool CoversIndependentRangeAndProcessing()
	{
		const Contract linearLDR{ Transfer::Linear, DynamicRange::LDR };
		const Contract linearHDR{ Transfer::Linear, DynamicRange::HDR };
		const Contract gammaHDR{ Transfer::Gamma, DynamicRange::HDR };
		return !DLSSUsesHDR(linearLDR) && DLSSUsesHDR(linearHDR) && DLSSUsesHDR(gammaHDR) &&
		       !IsPresentationSupported(linearHDR) && !IsVendorSupported(gammaHDR) &&
		       !IsPresentationSupported({}) && !IsVendorSupported({}) &&
		       kLegacyFsrHighDynamicRange;
	}

	static_assert(CoversPresentationAndVendorAdmission());
	static_assert(CoversIndependentRangeAndProcessing());
}

int main()
{
	return CoversPresentationAndVendorAdmission() && CoversIndependentRangeAndProcessing() ? 0 : 1;
}
