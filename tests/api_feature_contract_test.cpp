#include "VRAPI/CSfeatureapi.h"
#include <cassert>
#include <string_view>
#include <type_traits>

int main()
{
	using namespace CSX::FeatureAPI;
	static_assert(std::is_standard_layout_v<FeatureDescriptor001>);
	static_assert(std::is_standard_layout_v<Interface001>);
	static_assert((ServiceCapabilities & kCapabilityConstraintInspection) != 0);
	static_assert((ServiceCapabilities & kCapabilityPreflightTokens) != 0);
	Snapshot001 snapshot; MutationRequest001 mutation; Interface001 api;
	assert(snapshot.structSize == sizeof(Snapshot001));
	assert(mutation.structSize == sizeof(MutationRequest001));
	assert(api.major == 1 && api.minor == 0 && api.schemaRevision == 1);
	assert(std::string_view(ServiceName) == "csx.features");
	return 0;
}
