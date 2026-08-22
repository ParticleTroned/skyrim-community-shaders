#pragma once

#include "VRAPI/CSserviceapi.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace CSX::Api
{
	struct ProducerIdentity
	{
		std::string component;
		std::string buildId;
		std::string artifactSha256;
		std::string sourceCommit;
		std::string sourceDescribe;
		std::string configuration;
		std::string shaderCacheAbiId;
		std::string shaderCompilerIdentity;
		std::string manifestError;
		bool sourceDirty = false;
		bool manifestVerified = false;
	};

	struct ServiceRegistration
	{
		std::string name;
		std::uint32_t major = 1;
		std::uint32_t minor = 0;
		std::uint32_t schemaRevision = 1;
		std::uint64_t capabilities = ServiceAPI::kCapabilityNone;
		const void* interfacePointer = nullptr;
	};

	/**
	 * Process-local registry behind the stable native ABI. Registrations and
	 * their interface objects must remain valid for the process lifetime; domain
	 * availability changes are reported by the service, not by unregistering it.
	 */
	class ServiceRegistry
	{
	public:
		ServiceAPI::Status SetProducerIdentity(ProducerIdentity a_identity);
		ProducerIdentity GetProducerIdentity() const;

		ServiceAPI::Status Register(ServiceRegistration a_registration);
		std::uint32_t Count() const;
		ServiceAPI::Status Describe(std::uint32_t a_index, ServiceAPI::ServiceDescriptor001& a_output) const;
		ServiceAPI::Status Query(
			const ServiceAPI::ServiceQuery001& a_query,
			const void*& a_outputInterface,
			ServiceAPI::ServiceDescriptor001* a_outputDescriptor = nullptr) const;

	private:
		struct Entry
		{
			ServiceRegistration registration;
		};

		mutable std::mutex mutex;
		ProducerIdentity producer;
		std::vector<std::unique_ptr<Entry>> entries;

		static bool IsValidServiceName(const std::string& a_name);
		static void FillDescriptor(const Entry& a_entry, ServiceAPI::ServiceDescriptor001& a_output);
	};

	ServiceRegistry& GetProcessServiceRegistry();
	const ServiceAPI::Registry001* GetNativeServiceRegistry001();
}
