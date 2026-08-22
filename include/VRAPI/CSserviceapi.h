#pragma once

#include <cstdint>
#include <limits>

namespace CSX::ServiceAPI
{
	inline constexpr char ProviderName[] = "CommunityShaders";
	inline constexpr std::uint32_t RegistryMessageType = 0x43535852;  // "CSXR"
	inline constexpr std::uint32_t RegistryAbiMajor = 1;
	inline constexpr std::uint32_t RegistryAbiMinor = 0;

	/** Result codes are stable within registry ABI major 1. */
	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kIncompatibleRegistryVersion = 3,
		kServiceNotFound = 4,
		kIncompatibleServiceVersion = 5,
		kMissingCapabilities = 6,
		kAlreadyRegistered = 7,
		kInternalError = 8
	};

	/**
	 * Coarse discovery flags. Detailed capabilities remain service-specific.
	 * These flags allow a client to reject a service before casting its opaque
	 * interface pointer.
	 */
	enum ServiceCapability : std::uint64_t
	{
		kCapabilityNone = 0,
		kCapabilityInspection = 1ull << 0,
		kCapabilityRuntimeMutation = 1ull << 1,
		kCapabilityPersistentMutation = 1ull << 2,
		kCapabilityDestructiveOperations = 1ull << 3,
		kCapabilityAsynchronousOperations = 1ull << 4,
		kCapabilityEventStream = 1ull << 5,
		kCapabilityTransactions = 1ull << 6
	};

	/**
	 * Exact identity of the loaded provider. All strings are UTF-8, NUL
	 * terminated, owned by CSX, and must be copied by the caller before the next
	 * registry call. Empty strings mean that evidence is unavailable.
	 */
	struct ProducerIdentity001
	{
		std::uint32_t structSize = sizeof(ProducerIdentity001);
		const char* component = nullptr;
		const char* buildId = nullptr;
		const char* artifactSha256 = nullptr;
		const char* sourceCommit = nullptr;
		const char* sourceDescribe = nullptr;
		const char* configuration = nullptr;
		const char* shaderCacheAbiId = nullptr;
		const char* shaderCompilerIdentity = nullptr;
		const char* manifestError = nullptr;
		std::uint32_t sourceDirty = 0;
		std::uint32_t manifestVerified = 0;
	};

	/** Description of one registered service interface revision. */
	struct ServiceDescriptor001
	{
		std::uint32_t structSize = sizeof(ServiceDescriptor001);
		const char* name = nullptr;
		std::uint32_t major = 0;
		std::uint32_t minor = 0;
		std::uint32_t schemaRevision = 0;
		std::uint64_t capabilities = kCapabilityNone;
	};

	/**
	 * A bounded service query. Major versions are ABI-breaking. Minor versions
	 * are additive, but callers may still bound the accepted range. The provider
	 * returns the highest compatible registered minor revision.
	 */
	struct ServiceQuery001
	{
		std::uint32_t structSize = sizeof(ServiceQuery001);
		const char* name = nullptr;
		std::uint32_t major = 0;
		std::uint32_t minimumMinor = 0;
		std::uint32_t maximumMinor = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t requiredCapabilities = kCapabilityNone;
	};

	struct Registry001
	{
		std::uint32_t structSize = sizeof(Registry001);
		std::uint32_t abiMajor = RegistryAbiMajor;
		std::uint32_t abiMinor = RegistryAbiMinor;
		const void* context = nullptr;

		Status (*GetProducerIdentity)(const void* context, ProducerIdentity001* output) = nullptr;
		std::uint32_t (*GetServiceCount)(const void* context) = nullptr;
		Status (*GetServiceDescriptor)(const void* context, std::uint32_t index, ServiceDescriptor001* output) = nullptr;
		Status (*QueryService)(const void* context, const ServiceQuery001* query, const void** outputInterface, ServiceDescriptor001* outputDescriptor) = nullptr;
	};

	/**
	 * Dispatch this structure to ProviderName with RegistryMessageType after
	 * SKSE kMessage_PostLoad. CSX fills status and registry; no legacy CSAP
	 * structures or entry points are changed.
	 */
	struct RegistryMessage001
	{
		enum : std::uint32_t
		{
			kMessage_GetRegistry = RegistryMessageType
		};

		std::uint32_t structSize = sizeof(RegistryMessage001);
		std::uint32_t requestedAbiMajor = RegistryAbiMajor;
		std::uint32_t minimumAbiMinor = RegistryAbiMinor;
		Status status = Status::kInternalError;
		const Registry001* registry = nullptr;
	};
}
