#include "Features/Upscaling/FSRSharedGuidePolicy.h"

#include <limits>

namespace
{
	using namespace FSRSharedGuidePolicy;
	// Changing the diagnostic mode or source never evicts a retained generation.
	static_assert(SelectRoute(false, true, true, true, true) == Route::Copy);
	static_assert(SelectRoute(true, false, true, true, true) == Route::Copy);
	static_assert(SelectRoute(true, true, false, false, false) == Route::Import);
	static_assert(SelectRoute(true, true, true, true, true) == Route::Direct);
	static_assert(SelectRoute(true, true, true, false, true) == Route::Copy);
	static_assert(SelectRoute(true, true, true, true, false) == Route::Copy);

	constexpr auto fullCopy = CountInputCopies(100, 50, 200, 100, 0);
	static_assert(fullCopy.copyCalls == 5 && fullCopy.activePixels == 25000 && fullCopy.avoidedPixels == 75000);
	constexpr auto allGuides = CountInputCopies(100, 50, 200, 100, 4);
	static_assert(allGuides.copyCalls == 1 && allGuides.activePixels == 5000 && allGuides.avoidedPixels == 95000);
	static_assert(allGuides.directGuides == 4 && allGuides.directGuidePixels == 20000);
	constexpr auto partialImport = CountInputCopies(100, 50, 200, 100, 2);
	static_assert(partialImport.copyCalls == 3 && partialImport.activePixels == 15000);
	static_assert(partialImport.activePixels + partialImport.avoidedPixels == fullCopy.activePixels + fullCopy.avoidedPixels);
	static_assert(CountInputCopies(200, 100, 200, 100, 0).avoidedPixels == 0);
	static_assert(CountInputCopies(0, 50, 200, 100, 0).copyCalls == 0);
	static_assert(CountInputCopies(201, 50, 200, 100, 0).copyCalls == 0);
	static_assert(CountInputCopies(100, 101, 200, 100, 0).copyCalls == 0);
	static_assert(CountInputCopies(100, 50, 200, 100, 5).copyCalls == 0);
	static_assert(CountInputCopies(1, 1, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 4).copyCalls == 0);
}

int main() {}
