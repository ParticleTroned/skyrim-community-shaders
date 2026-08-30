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
	static_assert(ServiceMajor == 1 && MinimumServiceMinor == 0 && LegacySchemaRevision == 1);
	static_assert(ServiceMinor == 1 && SchemaRevision == 2);
	static_assert((ServiceCapabilities & kCapabilitySnapshot) != 0);
	static_assert((ServiceCapabilities & kCapabilityPreflightTokens) != 0);
	static_assert((ServiceCapabilities & kCapabilityLightPickerControl) != 0);
	static_assert((LegacyServiceCapabilities & kCapabilitySnapshot) != 0);
	static_assert((LegacyServiceCapabilities & kCapabilityPreflightTokens) != 0);
	static_assert((LegacyServiceCapabilities & kCapabilityLightPickerControl) == 0);
	static_assert(SupportsMutationAction(LegacyServiceCapabilities, MutationAction::kExitPreview));
	static_assert(!SupportsMutationAction(LegacyServiceCapabilities, MutationAction::kOpenLightEditor));
	static_assert(!SupportsMutationAction(LegacyServiceCapabilities, MutationAction::kBeginLightPick));
	static_assert(!SupportsMutationAction(LegacyServiceCapabilities, MutationAction::kCancelLightPick));
	static_assert(SupportsMutationAction(ServiceCapabilities, MutationAction::kOpenLightEditor));
	static_assert(!SupportsMutationAction(ServiceCapabilities, static_cast<MutationAction>(9)));
	static_assert(static_cast<std::uint32_t>(MutationAction::kOpenLightEditor) == 6);
	static_assert(static_cast<std::uint32_t>(MutationAction::kBeginLightPick) == 7);
	static_assert(static_cast<std::uint32_t>(MutationAction::kCancelLightPick) == 8);
	static_assert(sizeof(void*) == 8);
	static_assert(sizeof(Snapshot001) == 96);

	Snapshot001 snapshot;
	MutationRequest001 mutation;
	Interface001 interface;
	assert(snapshot.structSize == sizeof(Snapshot001));
	assert(mutation.structSize == sizeof(MutationRequest001));
	assert(interface.structSize == sizeof(Interface001));
	assert(interface.major == 1 && interface.minor == 1);
	assert(interface.schemaRevision == 2);
	assert(std::string_view(ServiceName) == "csx.editor");
	return 0;
}
