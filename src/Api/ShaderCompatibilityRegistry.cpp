#include "Api/ShaderCompatibilityRegistry.h"

#include "Utils/CryptoHash.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
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
			return static_cast<char>(std::tolower(a_character));
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

	std::optional<std::string> NormalizeSource(std::string_view a_value)
	{
		auto normalized = Lower(a_value);
		std::ranges::replace(normalized, '\\', '/');
		if (normalized.empty() || normalized.front() == '/' || normalized.find(':') != std::string::npos)
			return std::nullopt;

		std::vector<std::string_view> components;
		std::size_t begin = 0;
		while (begin <= normalized.size()) {
			const auto end = normalized.find('/', begin);
			const auto component = std::string_view(normalized).substr(begin, end == std::string::npos ? normalized.size() - begin : end - begin);
			if (component == "..")
				return std::nullopt;
			if (!component.empty() && component != ".")
				components.push_back(component);
			if (end == std::string::npos)
				break;
			begin = end + 1;
		}
		if (!components.empty() && components.front() == "data")
			components.erase(components.begin());
		std::string result;
		for (const auto component : components) {
			if (!result.empty())
				result.push_back('/');
			result.append(component);
		}
		return result.empty() ? std::nullopt : std::optional{ std::move(result) };
	}

	bool IsIdentity(std::string_view a_value)
	{
		if (a_value.empty() || a_value.size() > kMaximumIdentityLength || a_value.front() == '.' || a_value.back() == '.')
			return false;
		return std::ranges::all_of(a_value, [](unsigned char a_character) {
			return std::islower(a_character) || std::isdigit(a_character) || a_character == '.' || a_character == '_' || a_character == '-';
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
			std::string value;
			const bool global = input.kind == ScopeKind::kGlobal;
			if (!CopyBounded(input.value, kMaximumScopeValueLength, value, !global))
				return Failure(Status::kInvalidScope, "invalid-scope-value", "scope value is missing or exceeds its bounded size");
			if (global)
				value.clear();
			else if (input.kind == ScopeKind::kShaderSource) {
				const auto normalized = NormalizeSource(value);
				if (!normalized)
					return Failure(Status::kInvalidScope, "invalid-source-scope", "shader source scopes must be relative normalized paths without traversal");
				value = *normalized;
			} else
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
		std::string_view a_shaderFamily,
		std::string_view a_shaderSource,
		std::span<const std::string> a_features)
	{
		const auto family = Lower(a_shaderFamily);
		const auto normalizedSource = NormalizeSource(a_shaderSource);
		const std::string source = normalizedSource.value_or(std::string{});
		return std::ranges::any_of(a_registration.scopes, [&](const ShaderCompatibilityScope& a_scope) {
			switch (a_scope.kind) {
			case ScopeKind::kGlobal:
				return true;
			case ScopeKind::kShaderFamily:
				return a_scope.value == family;
			case ScopeKind::kShaderSource:
				return a_scope.value == source ||
				       (source.size() > a_scope.value.size() && source.ends_with(a_scope.value) &&
						   source[source.size() - a_scope.value.size() - 1] == '/');
			case ScopeKind::kFeature:
				return std::ranges::any_of(a_features, [&](const std::string& a_feature) {
					return a_scope.value == a_feature;
				});
			default:
				return false;
			}
		});
	}

	ShaderCompatibilityRequirementSet ShaderCompatibilityRegistry::BuildRequirementSet(
		std::string_view a_shaderFamily,
		std::string_view a_shaderSource,
		std::span<const std::string> a_features) const
	{
		std::scoped_lock lock(mutex);
		auto normalizedFeatures = std::vector<std::string>(a_features.begin(), a_features.end());
		for (auto& feature : normalizedFeatures)
			feature = Lower(feature);
		std::ranges::sort(normalizedFeatures);
		normalizedFeatures.erase(std::ranges::unique(normalizedFeatures).begin(), normalizedFeatures.end());
		const auto normalizedSource = NormalizeSource(a_shaderSource).value_or(std::string{});
		std::ostringstream cacheKeyValue;
		cacheKeyValue << Lower(a_shaderFamily) << '\n'
					  << normalizedSource;
		for (const auto& feature : normalizedFeatures)
			cacheKeyValue << '\n'
						  << feature;
		const auto cacheKey = cacheKeyValue.str();
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen) {
			if (const auto cached = requirementCache.find(cacheKey); cached != requirementCache.end())
				return cached->second;
		}
		std::vector<const ShaderCompatibilityRegistration*> applicable;
		for (const auto& registration : registrations) {
			if (Applies(registration, a_shaderFamily, normalizedSource, normalizedFeatures))
				applicable.push_back(&registration);
		}
		std::ranges::sort(applicable, {}, [](const auto* a_registration) { return a_registration->identity; });
		ShaderCompatibilityRequirementSet result;
		std::ostringstream canonical;
		for (const auto* registration : applicable) {
			canonical << registration->canonical.size() << ':' << registration->canonical << '\n';
			result.handles.push_back(registration->handle);
		}
		result.canonical = canonical.str();
		result.digest = Util::CryptoHash::Sha256Hex(result.canonical);
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen)
			requirementCache.insert_or_assign(cacheKey, result);
		return result;
	}

	void ShaderCompatibilityRegistry::Freeze()
	{
		std::scoped_lock lock(mutex);
		if (phase == ShaderCompatibilityAPI::Phase::kFrozen)
			return;
		std::vector<const ShaderCompatibilityRegistration*> ordered;
		ordered.reserve(registrations.size());
		for (const auto& registration : registrations)
			ordered.push_back(&registration);
		std::ranges::sort(ordered, {}, [](const auto* a_registration) { return a_registration->identity; });
		std::ostringstream canonical;
		for (const auto* registration : ordered)
			canonical << registration->canonical.size() << ':' << registration->canonical << '\n';
		compatibilitySetDigest = Util::CryptoHash::Sha256Hex(canonical.str());
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
		std::string_view a_shaderSource,
		std::span<const std::string> a_features)
	{
		return GetShaderCompatibilityRegistry().BuildRequirementSet(a_shaderFamily, a_shaderSource, a_features);
	}
}
