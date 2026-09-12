#include "Api/ServiceRegistryProvider.h"

#include "Api/ServiceRegistry.h"
#include "Api/ShaderService.h"
#include "Api/ScreenshotService.h"
#include "Api/ShaderCompatibilityService.h"
#include "Api/UpscalingService.h"
#include "BuildProvenance.h"

#include <nlohmann/json.hpp>

#include <mutex>

namespace CSX::Api
{
	void InitializeServiceRegistryProvider()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const auto json = BuildProvenance::GetProducer();
			ProducerIdentity producer;
			producer.component = json.value("component", std::string{ "CommunityShaders" });
			producer.buildId = json.value("buildId", std::string{});
			producer.artifactSha256 = json.value("artifactSha256", std::string{});
			producer.sourceCommit = json.value("sourceCommit", std::string{});
			producer.sourceDescribe = json.value("sourceDescribe", std::string{});
			producer.configuration = json.value("configuration", std::string{});
			producer.shaderCacheAbiId = json.value("shaderCacheAbiId", std::string{});
			producer.shaderCompilerIdentity = json.value("shaderCompilerIdentity", std::string{});
			producer.sourceDirty = json.value("sourceDirty", false);
			producer.manifestVerified = json.value("manifestVerified", false);
			if (const auto found = json.find("manifestError"); found != json.end() && found->is_string())
				producer.manifestError = found->get<std::string>();

			const auto status = GetProcessServiceRegistry().SetProducerIdentity(std::move(producer));
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to initialize CSX service-registry producer identity ({})", static_cast<std::uint32_t>(status));

			InitializeShaderService();
			InitializeScreenshotService();
			InitializeShaderCompatibilityService();
			InitializeUpscalingService();
		});
	}

	void HandleServiceRegistryMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message || a_message->type != ServiceAPI::RegistryMessageType ||
			!a_message->data || a_message->dataLen < sizeof(ServiceAPI::RegistryMessage001)) {
			return;
		}

		auto* request = static_cast<ServiceAPI::RegistryMessage001*>(a_message->data);
		request->registry = nullptr;
		request->status = ServiceAPI::Status::kInvalidArgument;
		if (request->structSize < sizeof(ServiceAPI::RegistryMessage001))
			return;
		if (request->requestedAbiMajor != ServiceAPI::RegistryAbiMajor ||
			request->minimumAbiMinor > ServiceAPI::RegistryAbiMinor) {
			request->status = ServiceAPI::Status::kIncompatibleRegistryVersion;
			return;
		}

		InitializeServiceRegistryProvider();
		request->registry = GetNativeServiceRegistry001();
		request->status = ServiceAPI::Status::kSuccess;
		logger::info("Provided CSX service registry to {}", a_message->sender ? a_message->sender : "<unknown>");
	}
}
