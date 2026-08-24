#include "VRAPI/CSeditorapi.h"

#include <cassert>
#include <string_view>
#include <type_traits>

int main()
{
	using namespace CSX::EditorAPI;
	static_assert(std::is_standard_layout_v<Snapshot001>);
	static_assert(std::is_standard_layout_v<MutationRequest001>);
	static_assert(std::is_standard_layout_v<Interface001>);
	static_assert(ServiceMajor == 1 && ServiceMinor == 0 && SchemaRevision == 1);
	static_assert((ServiceCapabilities & kCapabilitySnapshot) != 0);
	static_assert((ServiceCapabilities & kCapabilityPreflightTokens) != 0);

	Snapshot001 snapshot;
	MutationRequest001 mutation;
	Interface001 interface;
	assert(snapshot.structSize == sizeof(Snapshot001));
	assert(mutation.structSize == sizeof(MutationRequest001));
	assert(interface.structSize == sizeof(Interface001));
	assert(interface.major == 1 && interface.minor == 0);
	assert(std::string_view(ServiceName) == "csx.editor");
	return 0;
}
