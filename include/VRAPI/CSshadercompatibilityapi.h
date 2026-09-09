#pragma once

#include <cstdint>

namespace CSX::ShaderCompatibilityAPI
{
	inline constexpr char ServiceName[] = "csx.shader.compatibility";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;

	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kUnavailable = 3,
		kInvalidIdentity = 4,
		kInvalidVersion = 5,
		kInvalidScope = 6,
		kIdentityConflict = 7,
		kRegistrationClosed = 8,
		kNotFound = 9,
		kInternalError = 10
	};

	enum class Phase : std::uint32_t
	{
		kAcceptingRegistrations = 0,
		kFrozen = 1
	};

	enum class ScopeKind : std::uint32_t
	{
		kShaderFamily = 1,
		// Reserved in API v1 until runtime and offline source identities match.
		kShaderSource = 2,
		// Reserved in API v1 until runtime feature provenance is authoritative.
		kFeature = 3,
		kGlobal = 4
	};

	enum ServiceCapability : std::uint64_t
	{
		kCapabilityInspection = 1ull << 0,
		kCapabilityRegistration = 1ull << 1,
		kCapabilityVersionRanges = 1ull << 2,
		kCapabilityDeclarativeScopes = 1ull << 3,
		kCapabilityFreezeLifecycle = 1ull << 4
	};

	inline constexpr std::uint64_t ServiceCapabilities =
		kCapabilityInspection |
		kCapabilityRegistration |
		kCapabilityVersionRanges |
		kCapabilityDeclarativeScopes |
		kCapabilityFreezeLifecycle;

	struct Scope001
	{
		std::uint32_t structSize = sizeof(Scope001);
		ScopeKind kind = ScopeKind::kShaderFamily;
		const char* value = nullptr;
	};

	/**
	 * One atomic provider registration. CSX copies every supplied string and
	 * scope before returning. displayVersion is diagnostic and never affects
	 * shader-cache compatibility. Contract versions describe only the
	 * provider's shader-facing ABI.
	 */
	struct Registration001
	{
		std::uint32_t structSize = sizeof(Registration001);
		const char* identity = nullptr;
		const char* owner = nullptr;
		const char* displayVersion = nullptr;
		std::uint32_t contractMajor = 0;
		std::uint32_t currentMinor = 0;
		std::uint32_t minimumCompatibleMinor = 0;
		std::uint32_t maximumCompatibleMinor = 0;
		const char* resourceFingerprint = nullptr;
		const Scope001* scopes = nullptr;
		std::uint32_t scopeCount = 0;
	};

	struct RegistrationReceipt001
	{
		// Returned strings are borrowed thread-local views. Copy them before the
		// next call to this service on the same thread.
		std::uint32_t structSize = sizeof(RegistrationReceipt001);
		Status status = Status::kInternalError;
		std::uint32_t accepted = 0;
		std::uint32_t idempotent = 0;
		std::uint32_t restartRequired = 0;
		std::uint64_t registrationHandle = 0;
		std::uint64_t registryRevision = 0;
		const char* canonicalDigest = nullptr;
		const char* reasonCode = nullptr;
		const char* message = nullptr;
	};

	struct Snapshot001
	{
		// compatibilitySetDigest follows the same borrowed-view lifetime rule.
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		Phase phase = Phase::kAcceptingRegistrations;
		std::uint64_t registryRevision = 0;
		std::uint32_t registrationCount = 0;
		std::uint64_t capabilities = 0;
		const char* compatibilitySetDigest = nullptr;
	};

	struct RegistrationDescriptor001
	{
		// Returned strings follow the same borrowed-view lifetime rule.
		std::uint32_t structSize = sizeof(RegistrationDescriptor001);
		std::uint64_t registrationHandle = 0;
		const char* identity = nullptr;
		const char* owner = nullptr;
		const char* displayVersion = nullptr;
		std::uint32_t contractMajor = 0;
		std::uint32_t currentMinor = 0;
		std::uint32_t minimumCompatibleMinor = 0;
		std::uint32_t maximumCompatibleMinor = 0;
		const char* resourceFingerprint = nullptr;
		std::uint32_t scopeCount = 0;
		const char* canonicalDigest = nullptr;
	};

	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = ServiceMajor;
		std::uint32_t minor = ServiceMinor;
		std::uint32_t schemaRevision = SchemaRevision;
		std::uint64_t capabilities = ServiceCapabilities;
		const void* context = nullptr;

		Status (*GetSnapshot)(const void* context, Snapshot001* output) = nullptr;
		Status (*Register)(const void* context, const Registration001* registration, RegistrationReceipt001* output) = nullptr;
		Status (*GetRegistration)(const void* context, std::uint32_t index, RegistrationDescriptor001* output) = nullptr;
		// A returned scope value is a borrowed thread-local view.
		Status (*GetRegistrationScope)(const void* context, std::uint32_t registrationIndex, std::uint32_t scopeIndex, Scope001* output) = nullptr;
	};
}
