#include "Api/ShaderCompatibilityRegistry.h"

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <sstream>

namespace
{
	using CSX::Api::ShaderCompatibilityRegistration;
	using CSX::Api::ShaderCompatibilityResult;
	using CSX::Api::ShaderCompatibilityScope;
	using CSX::ShaderCompatibilityAPI::ScopeKind;
	using CSX::ShaderCompatibilityAPI::Status;

	constexpr std::size_t kMaximumIdentityLength = 128;
	constexpr std::size_t kMaximumTextLength = 256;
	constexpr std::size_t kMaximumFingerprintLength = 512;
	constexpr std::size_t kMaximumScopeValueLength = 512;
	constexpr std::uint32_t kMaximumScopes = 64;

	std::string Lower(std::string_view a_value)
	{
		std::string result(a_value);
		std::ranges::transform(result, result.begin(), [](unsigned char a_character) {
			return static_cast<char>(a_character >= 'A' && a_character <= 'Z' ? a_character + ('a' - 'A') : a_character);
		});
		return result;
	}

	bool CopyBounded(const char* a_value, std::size_t a_maximum, std::string& a_output, bool a_required)
	{
		if (!a_value)
			return !a_required;
		std::size_t length = 0;
		while (length <= a_maximum && a_value[length] != '\0')
			++length;
		if (length > a_maximum || (a_required && length == 0))
			return false;
		a_output.assign(a_value, length);
		return true;
	}

	bool IsCanonicalText(std::string_view a_value)
	{
		return std::ranges::none_of(a_value, [](unsigned char a_character) {
			return a_character < 0x20 || a_character == 0x7f;
		});
	}

	bool IsIdentity(std::string_view a_value)
	{
		if (a_value.empty() || a_value.size() > kMaximumIdentityLength || a_value.front() == '.' || a_value.back() == '.')
			return false;
		return std::ranges::all_of(a_value, [](unsigned char a_character) {
			return (a_character >= 'a' && a_character <= 'z') || (a_character >= '0' && a_character <= '9') ||
			       a_character == '.' || a_character == '_' || a_character == '-';
		});
	}

	std::string ScopeName(ScopeKind a_kind)
	{
		switch (a_kind) {
		case ScopeKind::kShaderFamily:
			return "family";
		case ScopeKind::kShaderSource:
			return "source";
		case ScopeKind::kFeature:
			return "feature";
		case ScopeKind::kGlobal:
			return "global";
		default:
			return "invalid";
		}
	}

	std::string BuildCanonical(const ShaderCompatibilityRegistration& a_registration)
	{
		std::ostringstream value;
		value << "identity=" << a_registration.identity
			  << "\ncontract=" << a_registration.contractMajor << '.' << a_registration.currentMinor
			  << "\ncompatible=" << a_registration.minimumCompatibleMinor << '-' << a_registration.maximumCompatibleMinor
			  << "\nresource=" << a_registration.resourceFingerprint;
		for (const auto& scope : a_registration.scopes)
			value << "\nscope=" << ScopeName(scope.kind) << ':' << scope.value;
		return value.str();
	}

	std::string BuildDomainCanonical(const ShaderCompatibilityRegistration& a_registration)
	{
		std::ostringstream value;
		value << "identity=" << a_registration.identity
			  << "\ncontract-major=" << a_registration.contractMajor
			  << "\nresource=" << a_registration.resourceFingerprint;
		for (const auto& scope : a_registration.scopes)
			value << "\nscope=" << ScopeName(scope.kind) << ':' << scope.value;
		return value.str();
	}

	ShaderCompatibilityResult Failure(Status a_status, std::string a_reason, std::string a_message)
	{
		return { .status = a_status, .reasonCode = std::move(a_reason), .message = std::move(a_message) };
	}
}

namespace CSX::Api
{
	ShaderCompatibilityResult ShaderCompatibilityRegistry::ValidateAndCopy(
		const ShaderCompatibilityAPI::Registration001& a_input,
		ShaderCompatibilityRegistration& a_output)
	{
		if (a_input.structSize < sizeof(ShaderCompatibilityAPI::Registration001))
			return Failure(Status::kStructureTooSmall, "structure-too-small", "registration structure is smaller than Registration001");
		if (!CopyBounded(a_input.identity, kMaximumIdentityLength, a_output.identity, true) || !IsIdentity(a_output.identity))
			return Failure(Status::kInvalidIdentity, "invalid-identity", "identity must be a bounded lower-case stable identifier");
		if (!CopyBounded(a_input.owner, kMaximumTextLength, a_output.owner, true) ||
			!CopyBounded(a_input.displayVersion, kMaximumTextLength, a_output.displayVersion, false) ||
			!CopyBounded(a_input.resourceFingerprint, kMaximumFingerprintLength, a_output.resourceFingerprint, false))
			return Failure(Status::kInvalidArgument, "invalid-text", "registration text is missing or exceeds its bounded size");
		if (!IsCanonicalText(a_output.resourceFingerprint))
			return Failure(Status::kInvalidArgument, "invalid-fingerprint", "resource fingerprint must not contain control characters");
		if (a_input.contractMajor == 0 || a_input.minimumCompatibleMinor > a_input.currentMinor ||
			a_input.currentMinor > a_input.maximumCompatibleMinor)
			return Failure(Status::kInvalidVersion, "invalid-version-range", "contract major must be non-zero and min <= current <= max");
		if (!a_input.scopes || a_input.scopeCount == 0 || a_input.scopeCount > kMaximumScopes)
			return Failure(Status::kInvalidScope, "invalid-scope-count", "one to 64 compatibility scopes are required");

		a_output.contractMajor = a_input.contractMajor;
		a_output.currentMinor = a_input.currentMinor;
		a_output.minimumCompatibleMinor = a_input.minimumCompatibleMinor;
		a_output.maximumCompatibleMinor = a_input.maximumCompatibleMinor;
		a_output.scopes.reserve(a_input.scopeCount);
		for (std::uint32_t index = 0; index < a_input.scopeCount; ++index) {
			const auto& input = a_input.scopes[index];
			if (input.structSize < sizeof(ShaderCompatibilityAPI::Scope001))
				return Failure(Status::kStructureTooSmall, "scope-structure-too-small", "scope structure is smaller than Scope001");
			if (input.kind < ScopeKind::kShaderFamily || input.kind > ScopeKind::kGlobal)
				return Failure(Status::kInvalidScope, "invalid-scope-kind", "scope kind is not recognized");
			if (input.kind == ScopeKind::kShaderSource || input.kind == ScopeKind::kFeature)
				return Failure(Status::kInvalidScope, "unsupported-scope-kind", "shader-source and feature scopes are reserved until runtime and offline identity share authoritative provenance");
			std::string value;
			const bool global = input.kind == ScopeKind::kGlobal;
			if (!CopyBounded(input.value, kMaximumScopeValueLength, value, !global))
				return Failure(Status::kInvalidScope, "invalid-scope-value", "scope value is missing or exceeds its bounded size");
			if (global)
				value.clear();
			else if (!IsCanonicalText(value))
				return Failure(Status::kInvalidScope, "invalid-scope-value", "scope values must not contain control characters");
			else
				value = Lower(value);
			a_output.scopes.push_back({ input.kind, std::move(value) });
		}
		std::ranges::sort(a_output.scopes, {}, [](const ShaderCompatibilityScope& a_scope) {
			return std::pair{ static_cast<std::uint32_t>(a_scope.kind), a_scope.value };
		});
		a_output.scopes.erase(std::ranges::unique(a_output.scopes).begin(), a_output.scopes.end());
		a_output.canonical = BuildCanonical(a_output);
		a_output.digest = Util::CryptoHash::Sha256Hex(a_output.canonical);
		return { .status = Status::kSuccess, .accepted = true, .digest = a_output.digest, .reasonCode = "accepted", .message = "compatibility registration accepted" };
	}

	ShaderCompatibilityResult ShaderCompatibilityRegistry::Register(const ShaderCompatibilityAPI::Registration001& a_registration)
	{
		ShaderCompatibilityRegistration candidate;
		auto result = ValidateAndCopy(a_registration, candidate);
		if (result.status != Status::kSuccess)
			return result;

		std::scoped_lock lock(mutex);
		result.revision = revision;
		const auto found = std::ranges::find(registrations, candidate.identity, &ShaderCompatibilityRegistration::identity);
		if (found != registrations.end()) {
			if (found->canonical != candidate.canonical)
				return { .status = Status::kIdentityConflict, .restartRequired = phase == ShaderCompatibilityAPI::Phase::kFrozen, .revision = revision, .reasonCode = "identity-conflict", .message = "identity is already registered with a different shader-facing contract" };
			result.idempotent = true;
			result.handle = found->handle;
			result.digest = found->digest;
			result.reasonCode = "already-registered";
			result.message = "identical compatibility registration already exists";
			return result;
		}
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen)
			return { .status = Status::kRegistrationClosed, .restartRequired = true, .revision = revision, .digest = candidate.digest, .reasonCode = "registration-frozen", .message = "registration closed before shader-cache validation; restart and register earlier" };

		candidate.handle = nextHandle++;
		result.handle = candidate.handle;
		registrations.push_back(std::move(candidate));
		++revision;
		result.revision = revision;
		compatibilitySetDigest.clear();
		requirementCache.clear();
		return result;
	}

	ShaderCompatibilitySnapshot ShaderCompatibilityRegistry::GetSnapshot() const
	{
		std::scoped_lock lock(mutex);
		return { phase, revision, static_cast<std::uint32_t>((std::min)(registrations.size(), static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))), compatibilitySetDigest };
	}

	bool ShaderCompatibilityRegistry::GetRegistration(std::uint32_t a_index, ShaderCompatibilityRegistration& a_output) const
	{
		std::scoped_lock lock(mutex);
		if (a_index >= registrations.size())
			return false;
		a_output = registrations[a_index];
		return true;
	}

	bool ShaderCompatibilityRegistry::GetScope(std::uint32_t a_registrationIndex, std::uint32_t a_scopeIndex, ShaderCompatibilityScope& a_output) const
	{
		std::scoped_lock lock(mutex);
		if (a_registrationIndex >= registrations.size() || a_scopeIndex >= registrations[a_registrationIndex].scopes.size())
			return false;
		a_output = registrations[a_registrationIndex].scopes[a_scopeIndex];
		return true;
	}

	bool ShaderCompatibilityRegistry::Applies(
		const ShaderCompatibilityRegistration& a_registration,
		std::string_view a_shaderFamily)
	{
		return std::ranges::any_of(a_registration.scopes, [&](const ShaderCompatibilityScope& a_scope) {
			switch (a_scope.kind) {
			case ScopeKind::kGlobal:
				return true;
			case ScopeKind::kShaderFamily:
				return a_scope.value == a_shaderFamily;
			case ScopeKind::kShaderSource:
				return false;
			case ScopeKind::kFeature:
				return false;
			default:
				return false;
			}
		});
	}

	ShaderCompatibilityRequirementSet ShaderCompatibilityRegistry::BuildRequirementSet(
		std::string_view a_shaderFamily,
		std::string_view) const
	{
		std::scoped_lock lock(mutex);
		const auto cacheKey = Lower(a_shaderFamily);
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen) {
			if (const auto cached = requirementCache.find(cacheKey); cached != requirementCache.end())
				return cached->second;
		}
		std::vector<ShaderCompatibilityRegistration> requirements;
		for (const auto& registration : registrations) {
			if (Applies(registration, cacheKey))
				requirements.push_back(registration);
		}
		auto result = BuildShaderCompatibilityRequirementSet(std::move(requirements));
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen)
			requirementCache.insert_or_assign(cacheKey, result);
		return result;
	}

	ShaderCompatibilityRequirementSet BuildShaderCompatibilityRequirementSet(
		std::vector<ShaderCompatibilityRegistration> a_registrations)
	{
		std::ranges::sort(a_registrations, {}, &ShaderCompatibilityRegistration::identity);
		ShaderCompatibilityRequirementSet result;
		std::ostringstream canonical;
		std::ostringstream domainCanonical;
		for (auto& registration : a_registrations) {
			registration.canonical = BuildCanonical(registration);
			registration.digest = Util::CryptoHash::Sha256Hex(registration.canonical);
			canonical << registration.canonical.size() << ':' << registration.canonical << '\n';
			const auto domain = BuildDomainCanonical(registration);
			domainCanonical << domain.size() << ':' << domain << '\n';
			result.handles.push_back(registration.handle);
		}
		result.canonical = canonical.str();
		result.digest = Util::CryptoHash::Sha256Hex(result.canonical);
		result.domainCanonical = domainCanonical.str();
		result.domainDigest = Util::CryptoHash::Sha256Hex(result.domainCanonical);
		result.registrations = std::move(a_registrations);
		return result;
	}

	bool AreShaderCompatibilityRequirementSetsCompatible(
		const ShaderCompatibilityRequirementSet& a_cached,
		const ShaderCompatibilityRequirementSet& a_current)
	{
		if (a_cached.domainCanonical != a_current.domainCanonical ||
			a_cached.registrations.size() != a_current.registrations.size())
			return false;

		for (std::size_t index = 0; index < a_current.registrations.size(); ++index) {
			const auto& cached = a_cached.registrations[index];
			const auto& current = a_current.registrations[index];
			if (cached.identity != current.identity ||
				cached.contractMajor != current.contractMajor ||
				cached.resourceFingerprint != current.resourceFingerprint ||
				cached.scopes != current.scopes)
				return false;

			const auto minimum = (std::max)(cached.minimumCompatibleMinor, current.minimumCompatibleMinor);
			const auto maximum = (std::min)(cached.maximumCompatibleMinor, current.maximumCompatibleMinor);
			if (minimum > maximum)
				return false;
		}
		return true;
	}

	void ShaderCompatibilityRegistry::Freeze()
	{
		std::scoped_lock lock(mutex);
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen)
			return;
		compatibilitySetDigest = BuildShaderCompatibilityRequirementSet(registrations).digest;
		phase = ShaderCompatibilityAPI::Phase::kFrozen;
		++revision;
	}

	ShaderCompatibilityRegistry& GetShaderCompatibilityRegistry()
	{
		static ShaderCompatibilityRegistry registry;
		return registry;
	}

	void FreezeShaderCompatibilityRegistrations()
	{
		GetShaderCompatibilityRegistry().Freeze();
	}

	ShaderCompatibilityRequirementSet GetShaderCompatibilityRequirementSet(
		std::string_view a_shaderFamily,
		std::string_view a_shaderSource)
	{
		return GetShaderCompatibilityRegistry().BuildRequirementSet(a_shaderFamily, a_shaderSource);
	}
}
