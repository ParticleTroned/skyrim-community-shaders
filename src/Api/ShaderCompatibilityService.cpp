#include "Api/ShaderCompatibilityService.h"

#include "Api/ServiceRegistry.h"
#include "Api/ShaderCompatibilityRegistry.h"

#include <mutex>

namespace
{
	using namespace CSX;
	using ShaderCompatibilityAPI::Status;

	struct Response
	{
		std::string digest;
		std::string reason;
		std::string message;
		Api::ShaderCompatibilityRegistration registration;
		Api::ShaderCompatibilityScope scope;
	};

	Status GetSnapshot(const void*, ShaderCompatibilityAPI::Snapshot001* a_output)
	{
		if (!a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(*a_output))
			return Status::kStructureTooSmall;
		*a_output = { .structSize = sizeof(*a_output) };
		try {
			thread_local Response response;
			const auto snapshot = Api::GetShaderCompatibilityRegistry().GetSnapshot();
			response.digest = snapshot.compatibilitySetDigest;
			*a_output = {
				.structSize = sizeof(*a_output),
				.available = 1,
				.phase = snapshot.phase,
				.registryRevision = snapshot.revision,
				.registrationCount = snapshot.registrationCount,
				.capabilities = ShaderCompatibilityAPI::ServiceCapabilities,
				.compatibilitySetDigest = response.digest.empty() ? nullptr : response.digest.c_str()
			};
			return Status::kSuccess;
		} catch (...) {
			*a_output = { .structSize = sizeof(*a_output) };
			return Status::kInternalError;
		}
	}

	Status Register(const void*, const ShaderCompatibilityAPI::Registration001* a_registration, ShaderCompatibilityAPI::RegistrationReceipt001* a_output)
	{
		if (!a_registration || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(*a_output))
			return Status::kStructureTooSmall;
		*a_output = { .structSize = sizeof(*a_output), .status = Status::kInternalError };
		try {
			thread_local Response response;
			const auto result = Api::GetShaderCompatibilityRegistry().Register(*a_registration);
			response.digest = result.digest;
			response.reason = result.reasonCode;
			response.message = result.message;
			*a_output = {
				.structSize = sizeof(*a_output),
				.status = result.status,
				.accepted = result.accepted ? 1u : 0u,
				.idempotent = result.idempotent ? 1u : 0u,
				.restartRequired = result.restartRequired ? 1u : 0u,
				.registrationHandle = result.handle,
				.registryRevision = result.revision,
				.canonicalDigest = response.digest.empty() ? nullptr : response.digest.c_str(),
				.reasonCode = response.reason.c_str(),
				.message = response.message.c_str()
			};
			return result.status;
		} catch (...) {
			*a_output = { .structSize = sizeof(*a_output), .status = Status::kInternalError };
			return Status::kInternalError;
		}
	}

	Status GetRegistration(const void*, std::uint32_t a_index, ShaderCompatibilityAPI::RegistrationDescriptor001* a_output)
	{
		if (!a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(*a_output))
			return Status::kStructureTooSmall;
		*a_output = { .structSize = sizeof(*a_output) };
		try {
			thread_local Response response;
			if (!Api::GetShaderCompatibilityRegistry().GetRegistration(a_index, response.registration))
				return Status::kNotFound;
			const auto& value = response.registration;
			*a_output = {
				.structSize = sizeof(*a_output),
				.registrationHandle = value.handle,
				.identity = value.identity.c_str(),
				.owner = value.owner.c_str(),
				.displayVersion = value.displayVersion.c_str(),
				.contractMajor = value.contractMajor,
				.currentMinor = value.currentMinor,
				.minimumCompatibleMinor = value.minimumCompatibleMinor,
				.maximumCompatibleMinor = value.maximumCompatibleMinor,
				.resourceFingerprint = value.resourceFingerprint.c_str(),
				.scopeCount = static_cast<std::uint32_t>(value.scopes.size()),
				.canonicalDigest = value.digest.c_str()
			};
			return Status::kSuccess;
		} catch (...) {
			*a_output = { .structSize = sizeof(*a_output) };
			return Status::kInternalError;
		}
	}

	Status GetRegistrationScope(const void*, std::uint32_t a_registrationIndex, std::uint32_t a_scopeIndex, ShaderCompatibilityAPI::Scope001* a_output)
	{
		if (!a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(*a_output))
			return Status::kStructureTooSmall;
		*a_output = { .structSize = sizeof(*a_output) };
		try {
			thread_local Response response;
			if (!Api::GetShaderCompatibilityRegistry().GetScope(a_registrationIndex, a_scopeIndex, response.scope))
				return Status::kNotFound;
			*a_output = { .structSize = sizeof(*a_output), .kind = response.scope.kind, .value = response.scope.value.c_str() };
			return Status::kSuccess;
		} catch (...) {
			*a_output = { .structSize = sizeof(*a_output) };
			return Status::kInternalError;
		}
	}
}

namespace CSX::Api
{
	const ShaderCompatibilityAPI::Interface001* GetShaderCompatibilityService001()
	{
		static const ShaderCompatibilityAPI::Interface001 serviceInterface{
			.structSize = sizeof(ShaderCompatibilityAPI::Interface001),
			.major = ShaderCompatibilityAPI::ServiceMajor,
			.minor = ShaderCompatibilityAPI::ServiceMinor,
			.schemaRevision = ShaderCompatibilityAPI::SchemaRevision,
			.capabilities = ShaderCompatibilityAPI::ServiceCapabilities,
			.context = &GetShaderCompatibilityRegistry(),
			.GetSnapshot = ::GetSnapshot,
			.Register = ::Register,
			.GetRegistration = ::GetRegistration,
			.GetRegistrationScope = ::GetRegistrationScope,
		};
		return &serviceInterface;
	}

	void InitializeShaderCompatibilityService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const auto status = GetProcessServiceRegistry().Register({
				.name = ShaderCompatibilityAPI::ServiceName,
				.major = ShaderCompatibilityAPI::ServiceMajor,
				.minor = ShaderCompatibilityAPI::ServiceMinor,
				.schemaRevision = ShaderCompatibilityAPI::SchemaRevision,
				.capabilities = ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityTransactions,
				.interfacePointer = GetShaderCompatibilityService001(),
			});
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register {} service ({})", ShaderCompatibilityAPI::ServiceName, static_cast<std::uint32_t>(status));
		});
	}
}
