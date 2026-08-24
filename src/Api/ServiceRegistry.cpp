#include "Api/ServiceRegistry.h"

#include <algorithm>
#include <limits>

namespace
{
	using CSX::ServiceAPI::ProducerIdentity001;
	using CSX::ServiceAPI::Registry001;
	using CSX::ServiceAPI::ServiceDescriptor001;
	using CSX::ServiceAPI::ServiceQuery001;
	using CSX::ServiceAPI::Status;

	CSX::Api::ServiceRegistry* RegistryFrom(const void* a_context)
	{
		return const_cast<CSX::Api::ServiceRegistry*>(
			static_cast<const CSX::Api::ServiceRegistry*>(a_context));
	}

	Status GetProducerIdentity(const void* a_context, ProducerIdentity001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(ProducerIdentity001))
			return Status::kStructureTooSmall;

		try {
			const auto producer = RegistryFrom(a_context)->GetProducerIdentity();
			// The public view must refer to process-stable storage, not this copy.
			// Use a thread-local snapshot so callers can copy it before the next call.
			thread_local CSX::Api::ProducerIdentity snapshot;
			snapshot = producer;
			*a_output = {
				.structSize = sizeof(ProducerIdentity001),
				.component = snapshot.component.c_str(),
				.buildId = snapshot.buildId.c_str(),
				.artifactSha256 = snapshot.artifactSha256.c_str(),
				.sourceCommit = snapshot.sourceCommit.c_str(),
				.sourceDescribe = snapshot.sourceDescribe.c_str(),
				.configuration = snapshot.configuration.c_str(),
				.shaderCacheAbiId = snapshot.shaderCacheAbiId.c_str(),
				.shaderCompilerIdentity = snapshot.shaderCompilerIdentity.c_str(),
				.manifestError = snapshot.manifestError.c_str(),
				.sourceDirty = snapshot.sourceDirty ? 1u : 0u,
				.manifestVerified = snapshot.manifestVerified ? 1u : 0u,
			};
			return Status::kSuccess;
		} catch (...) {
			return Status::kInternalError;
		}
	}

	std::uint32_t GetServiceCount(const void* a_context)
	{
		if (!a_context)
			return 0;
		try {
			return RegistryFrom(a_context)->Count();
		} catch (...) {
			return 0;
		}
	}

	Status GetServiceDescriptor(const void* a_context, std::uint32_t a_index, ServiceDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(ServiceDescriptor001))
			return Status::kStructureTooSmall;
		try {
			return RegistryFrom(a_context)->Describe(a_index, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status QueryService(
		const void* a_context,
		const ServiceQuery001* a_query,
		const void** a_outputInterface,
		ServiceDescriptor001* a_outputDescriptor)
	{
		if (a_outputInterface)
			*a_outputInterface = nullptr;
		if (!a_context || !a_query || !a_outputInterface)
			return Status::kInvalidArgument;
		if (a_query->structSize < sizeof(ServiceQuery001))
			return Status::kStructureTooSmall;
		if (a_outputDescriptor && a_outputDescriptor->structSize < sizeof(ServiceDescriptor001))
			return Status::kStructureTooSmall;
		try {
			return RegistryFrom(a_context)->Query(*a_query, *a_outputInterface, a_outputDescriptor);
		} catch (...) {
			return Status::kInternalError;
		}
	}
}

namespace CSX::Api
{
	ServiceAPI::Status ServiceRegistry::SetProducerIdentity(ProducerIdentity a_identity)
	{
		if (a_identity.component.empty())
			return ServiceAPI::Status::kInvalidArgument;
		std::lock_guard lock(mutex);
		producer = std::move(a_identity);
		return ServiceAPI::Status::kSuccess;
	}

	ProducerIdentity ServiceRegistry::GetProducerIdentity() const
	{
		std::lock_guard lock(mutex);
		return producer;
	}

	ServiceAPI::Status ServiceRegistry::Register(ServiceRegistration a_registration)
	{
		if (!IsValidServiceName(a_registration.name) || a_registration.major == 0 || !a_registration.interfacePointer)
			return ServiceAPI::Status::kInvalidArgument;

		std::lock_guard lock(mutex);
		const auto duplicate = std::ranges::find_if(entries, [&](const auto& a_entry) {
			return a_entry->registration.name == a_registration.name &&
			       a_entry->registration.major == a_registration.major &&
			       a_entry->registration.minor == a_registration.minor;
		});
		if (duplicate != entries.end())
			return ServiceAPI::Status::kAlreadyRegistered;

		auto entry = std::make_unique<Entry>();
		entry->registration = std::move(a_registration);
		entries.push_back(std::move(entry));
		std::ranges::sort(entries, [](const auto& a_left, const auto& a_right) {
			const auto& left = a_left->registration;
			const auto& right = a_right->registration;
			if (left.name != right.name)
				return left.name < right.name;
			if (left.major != right.major)
				return left.major < right.major;
			return left.minor < right.minor;
		});
		return ServiceAPI::Status::kSuccess;
	}

	std::uint32_t ServiceRegistry::Count() const
	{
		std::lock_guard lock(mutex);
		return static_cast<std::uint32_t>(std::min<std::size_t>(entries.size(), std::numeric_limits<std::uint32_t>::max()));
	}

	ServiceAPI::Status ServiceRegistry::Describe(
		std::uint32_t a_index,
		ServiceAPI::ServiceDescriptor001& a_output) const
	{
		std::lock_guard lock(mutex);
		if (a_index >= entries.size())
			return ServiceAPI::Status::kServiceNotFound;
		FillDescriptor(*entries[a_index], a_output);
		return ServiceAPI::Status::kSuccess;
	}

	ServiceAPI::Status ServiceRegistry::Query(
		const ServiceAPI::ServiceQuery001& a_query,
		const void*& a_outputInterface,
		ServiceAPI::ServiceDescriptor001* a_outputDescriptor) const
	{
		a_outputInterface = nullptr;
		if (!a_query.name || a_query.name[0] == '\0' || a_query.major == 0 || a_query.minimumMinor > a_query.maximumMinor)
			return ServiceAPI::Status::kInvalidArgument;

		std::lock_guard lock(mutex);
		const Entry* selected = nullptr;
		bool foundName = false;
		bool foundVersion = false;
		for (const auto& entry : entries) {
			const auto& registration = entry->registration;
			if (registration.name != a_query.name)
				continue;
			foundName = true;
			if (registration.major != a_query.major ||
				registration.minor < a_query.minimumMinor ||
				registration.minor > a_query.maximumMinor) {
				continue;
			}
			foundVersion = true;
			if ((registration.capabilities & a_query.requiredCapabilities) != a_query.requiredCapabilities)
				continue;
			if (!selected || registration.minor > selected->registration.minor)
				selected = entry.get();
		}

		if (!foundName)
			return ServiceAPI::Status::kServiceNotFound;
		if (!foundVersion)
			return ServiceAPI::Status::kIncompatibleServiceVersion;
		if (!selected)
			return ServiceAPI::Status::kMissingCapabilities;

		a_outputInterface = selected->registration.interfacePointer;
		if (a_outputDescriptor)
			FillDescriptor(*selected, *a_outputDescriptor);
		return ServiceAPI::Status::kSuccess;
	}

	bool ServiceRegistry::IsValidServiceName(const std::string& a_name)
	{
		if (a_name.empty() || a_name.size() > 128)
			return false;
		return std::ranges::all_of(a_name, [](unsigned char a_character) {
			return (a_character >= 'a' && a_character <= 'z') ||
			       (a_character >= '0' && a_character <= '9') ||
			       a_character == '.' || a_character == '_' || a_character == '-';
		});
	}

	void ServiceRegistry::FillDescriptor(
		const Entry& a_entry,
		ServiceAPI::ServiceDescriptor001& a_output)
	{
		const auto& registration = a_entry.registration;
		a_output = {
			.structSize = sizeof(ServiceAPI::ServiceDescriptor001),
			.name = registration.name.c_str(),
			.major = registration.major,
			.minor = registration.minor,
			.schemaRevision = registration.schemaRevision,
			.capabilities = registration.capabilities,
		};
	}

	ServiceRegistry& GetProcessServiceRegistry()
	{
		static ServiceRegistry registry;
		return registry;
	}

	const ServiceAPI::Registry001* GetNativeServiceRegistry001()
	{
		static const ServiceAPI::Registry001 registry{
			.structSize = sizeof(ServiceAPI::Registry001),
			.abiMajor = ServiceAPI::RegistryAbiMajor,
			.abiMinor = ServiceAPI::RegistryAbiMinor,
			.context = &GetProcessServiceRegistry(),
			.GetProducerIdentity = ::GetProducerIdentity,
			.GetServiceCount = ::GetServiceCount,
			.GetServiceDescriptor = ::GetServiceDescriptor,
			.QueryService = ::QueryService,
		};
		return &registry;
	}
}
