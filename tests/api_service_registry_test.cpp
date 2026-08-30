#include "Api/ServiceRegistry.h"
#include "VRAPI/CSeditorapi.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

using CSX::Api::ProducerIdentity;
using CSX::Api::ServiceRegistration;
using CSX::Api::ServiceRegistry;
using CSX::ServiceAPI::ServiceDescriptor001;
using CSX::ServiceAPI::ServiceQuery001;
using CSX::ServiceAPI::Status;

static_assert(std::is_standard_layout_v<CSX::ServiceAPI::ProducerIdentity001>);
static_assert(std::is_standard_layout_v<CSX::ServiceAPI::ServiceDescriptor001>);
static_assert(std::is_standard_layout_v<CSX::ServiceAPI::ServiceQuery001>);
static_assert(std::is_standard_layout_v<CSX::ServiceAPI::Registry001>);
static_assert(std::is_standard_layout_v<CSX::ServiceAPI::RegistryMessage001>);

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}
}

int RunTest()
{
	ServiceRegistry registry;
	ProducerIdentity producer;
	producer.component = "CommunityShaders";
	producer.buildId = "build-id";
	producer.artifactSha256 = "artifact";
	producer.manifestVerified = true;
	Check(registry.SetProducerIdentity(std::move(producer)) == Status::kSuccess, "producer identity was rejected");
	Check(registry.GetProducerIdentity().buildId == "build-id", "producer identity was not retained");

	int service10 = 10;
	int service12 = 12;
	int weather = 20;
	const auto inspect = CSX::ServiceAPI::kCapabilityInspection;
	const auto mutate = CSX::ServiceAPI::kCapabilityRuntimeMutation;
	Check(registry.Register({ "csx.weather", 1, 0, 1, inspect, &weather }) == Status::kSuccess, "weather registration failed");
	Check(registry.Register({ "csx.test", 1, 2, 3, inspect | mutate, &service12 }) == Status::kSuccess, "test 1.2 registration failed");
	Check(registry.Register({ "csx.test", 1, 0, 1, inspect, &service10 }) == Status::kSuccess, "test 1.0 registration failed");
	Check(registry.Register({ "csx.test", 1, 0, 1, inspect, &service10 }) == Status::kAlreadyRegistered, "duplicate registration was accepted");
	Check(registry.Register({ "Invalid Service", 1, 0, 1, inspect, &service10 }) == Status::kInvalidArgument, "invalid service name was accepted");
	Check(registry.Count() == 3, "registry count is incorrect");

	ServiceDescriptor001 descriptor;
	Check(registry.Describe(0, descriptor) == Status::kSuccess, "descriptor enumeration failed");
	Check(std::string_view(descriptor.name) == "csx.test" && descriptor.minor == 0, "enumeration is not deterministic");

	ServiceQuery001 query;
	query.name = "csx.test";
	query.major = 1;
	query.minimumMinor = 0;
	query.maximumMinor = 2;
	query.requiredCapabilities = inspect;
	const void* selected = nullptr;
	Check(registry.Query(query, selected, &descriptor) == Status::kSuccess, "compatible query failed");
	Check(selected == &service12 && descriptor.minor == 2, "highest compatible minor was not selected");

	query.maximumMinor = 0;
	Check(registry.Query(query, selected, &descriptor) == Status::kSuccess && selected == &service10, "minor upper bound was ignored");
	query.requiredCapabilities = mutate;
	Check(registry.Query(query, selected, nullptr) == Status::kMissingCapabilities, "missing capability was not reported");
	query.major = 2;
	Check(registry.Query(query, selected, nullptr) == Status::kIncompatibleServiceVersion, "incompatible major was not reported");
	query.name = "csx.missing";
	Check(registry.Query(query, selected, nullptr) == Status::kServiceNotFound, "missing service was not reported");

	CSX::EditorAPI::Interface001 editor10{
		.minor = CSX::EditorAPI::MinimumServiceMinor,
		.schemaRevision = CSX::EditorAPI::LegacySchemaRevision,
		.capabilities = CSX::EditorAPI::LegacyServiceCapabilities,
	};
	CSX::EditorAPI::Interface001 editor11{
		.minor = CSX::EditorAPI::ServiceMinor,
		.schemaRevision = CSX::EditorAPI::SchemaRevision,
		.capabilities = CSX::EditorAPI::ServiceCapabilities,
	};
	Check(registry.Register({ CSX::EditorAPI::ServiceName, CSX::EditorAPI::ServiceMajor,
			  CSX::EditorAPI::MinimumServiceMinor, CSX::EditorAPI::LegacySchemaRevision, inspect, &editor10 }) == Status::kSuccess,
		"editor 1.0 compatibility registration failed");
	Check(registry.Register({ CSX::EditorAPI::ServiceName, CSX::EditorAPI::ServiceMajor,
			  CSX::EditorAPI::ServiceMinor, CSX::EditorAPI::SchemaRevision, inspect, &editor11 }) == Status::kSuccess,
		"editor 1.1 registration failed");

	ServiceQuery001 editorQuery;
	editorQuery.name = CSX::EditorAPI::ServiceName;
	editorQuery.major = CSX::EditorAPI::ServiceMajor;
	editorQuery.minimumMinor = CSX::EditorAPI::MinimumServiceMinor;
	editorQuery.maximumMinor = CSX::EditorAPI::MinimumServiceMinor;
	editorQuery.requiredCapabilities = inspect;
	Check(registry.Query(editorQuery, selected, &descriptor) == Status::kSuccess && selected == &editor10,
		"editor client capped at 1.0 lost its compatible interface");
	Check(descriptor.minor == CSX::EditorAPI::MinimumServiceMinor && descriptor.schemaRevision == CSX::EditorAPI::LegacySchemaRevision,
		"editor 1.0 descriptor metadata changed");
	Check((static_cast<const CSX::EditorAPI::Interface001*>(selected)->capabilities &
			  CSX::EditorAPI::kCapabilityLightPickerControl) == 0,
		"editor 1.0 interface advertised the 1.1 picker capability");

	editorQuery.maximumMinor = CSX::EditorAPI::ServiceMinor;
	Check(registry.Query(editorQuery, selected, &descriptor) == Status::kSuccess && selected == &editor11,
		"editor client accepting 1.1 did not receive the highest compatible interface");
	Check(descriptor.minor == CSX::EditorAPI::ServiceMinor && descriptor.schemaRevision == CSX::EditorAPI::SchemaRevision,
		"editor 1.1 descriptor metadata is incorrect");
	Check((static_cast<const CSX::EditorAPI::Interface001*>(selected)->capabilities &
			  CSX::EditorAPI::kCapabilityLightPickerControl) != 0,
		"editor 1.1 interface omitted the picker capability");

	auto& processRegistry = CSX::Api::GetProcessServiceRegistry();
	ProducerIdentity nativeProducer;
	nativeProducer.component = "CommunityShaders";
	nativeProducer.buildId = "native-build";
	Check(processRegistry.SetProducerIdentity(std::move(nativeProducer)) == Status::kSuccess, "native producer setup failed");
	int nativeService = 30;
	Check(processRegistry.Register({ "csx.native", 1, 1, 2, inspect, &nativeService }) == Status::kSuccess, "native service setup failed");

	const auto* nativeRegistry = CSX::Api::GetNativeServiceRegistry001();
	Check(nativeRegistry && nativeRegistry->abiMajor == CSX::ServiceAPI::RegistryAbiMajor, "native registry ABI is unavailable");
	CSX::ServiceAPI::ProducerIdentity001 nativeIdentity;
	Check(nativeRegistry->GetProducerIdentity(nativeRegistry->context, &nativeIdentity) == Status::kSuccess, "native producer query failed");
	Check(std::string_view(nativeIdentity.buildId) == "native-build", "native producer query returned the wrong build");

	ServiceQuery001 nativeQuery;
	nativeQuery.name = "csx.native";
	nativeQuery.major = 1;
	nativeQuery.minimumMinor = 1;
	nativeQuery.maximumMinor = 1;
	ServiceDescriptor001 nativeDescriptor;
	selected = nullptr;
	Check(nativeRegistry->QueryService(nativeRegistry->context, &nativeQuery, &selected, &nativeDescriptor) == Status::kSuccess, "native service query failed");
	Check(selected == &nativeService && nativeDescriptor.schemaRevision == 2, "native service query returned the wrong interface");

	ServiceDescriptor001 undersizedDescriptor;
	undersizedDescriptor.structSize = sizeof(std::uint32_t);
	Check(nativeRegistry->GetServiceDescriptor(nativeRegistry->context, 0, &undersizedDescriptor) == Status::kStructureTooSmall, "undersized native output was accepted");

	return 0;
}

int main()
{
	try {
		return RunTest();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
