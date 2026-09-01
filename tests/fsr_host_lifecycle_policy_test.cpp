#include "Features/Upscaling/FSRHostLifecyclePolicy.h"

namespace
{
	using namespace FSRHostLifecyclePolicy;

	static_assert(!SupportsHostFsr3FeatureLevel(0xB000));
	static_assert(SupportsHostFsr3FeatureLevel(0xB100));
	static_assert(SupportsHostFsr3FeatureLevel(0xC000));
	static_assert(CanAttemptHostFallback(true, false));
	static_assert(!CanAttemptHostFallback(false, false));
	static_assert(!CanAttemptHostFallback(true, true));
	static_assert(!RequiresOwnershipQuarantine(CallDisposition::Succeeded));
	static_assert(RequiresOwnershipQuarantine(CallDisposition::ReturnedError));
	static_assert(RequiresOwnershipQuarantine(CallDisposition::Faulted));
	static_assert(!CanReleaseFailedInterfaceScratch(CallDisposition::Succeeded));
	static_assert(CanReleaseFailedInterfaceScratch(CallDisposition::ReturnedError));
	static_assert(!CanReleaseFailedInterfaceScratch(CallDisposition::Faulted));
	static_assert(ClassifyCallDisposition(false, true) == CallDisposition::Succeeded);
	static_assert(ClassifyCallDisposition(false, false) == CallDisposition::ReturnedError);
	static_assert(ClassifyCallDisposition(true, true) == CallDisposition::Faulted);
	static_assert(ClassifyCallDisposition(true, false) == CallDisposition::Faulted);

	constexpr bool CoversReleaseAdmission()
	{
		for (unsigned bits = 0; bits < 8; ++bits) {
			const bool quarantined = (bits & 1u) != 0;
			const bool valid = (bits & 2u) != 0;
			const bool indeterminate = (bits & 4u) != 0;
			const bool expected = !quarantined && !valid && !indeterminate;
			if (CanReleaseHostOwnership(quarantined, valid, indeterminate) != expected)
				return false;
		}
		return true;
	}

	static_assert(CoversReleaseAdmission());
}

int main() {}
