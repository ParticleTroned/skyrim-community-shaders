#pragma once

#include "VRAPI/CSshadercompatibilityapi.h"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CSX::Api
{
	struct ShaderCompatibilityScope
	{
		ShaderCompatibilityAPI::ScopeKind kind{};
		std::string value;

		bool operator==(const ShaderCompatibilityScope&) const = default;
	};

	struct ShaderCompatibilityRegistration
	{
		std::uint64_t handle = 0;
		std::string identity;
		std::string owner;
		std::string displayVersion;
		std::uint32_t contractMajor = 0;
		std::uint32_t currentMinor = 0;
		std::uint32_t minimumCompatibleMinor = 0;
		std::uint32_t maximumCompatibleMinor = 0;
		std::string resourceFingerprint;
		std::vector<ShaderCompatibilityScope> scopes;
		std::string canonical;
		std::string digest;
	};

	struct ShaderCompatibilityResult
	{
		ShaderCompatibilityAPI::Status status = ShaderCompatibilityAPI::Status::kInternalError;
		bool accepted = false;
		bool idempotent = false;
		bool restartRequired = false;
		std::uint64_t handle = 0;
		std::uint64_t revision = 0;
		std::string digest;
		std::string reasonCode;
		std::string message;
	};

	struct ShaderCompatibilitySnapshot
	{
		ShaderCompatibilityAPI::Phase phase = ShaderCompatibilityAPI::Phase::kAcceptingRegistrations;
		std::uint64_t revision = 1;
		std::uint32_t registrationCount = 0;
		std::string compatibilitySetDigest;
	};

	struct ShaderCompatibilityRequirementSet
	{
		std::string canonical;
		std::string digest;
		std::vector<std::uint64_t> handles;
	};

	class ShaderCompatibilityRegistry
	{
	public:
		ShaderCompatibilityResult Register(const ShaderCompatibilityAPI::Registration001& a_registration);
		ShaderCompatibilitySnapshot GetSnapshot() const;
		bool GetRegistration(std::uint32_t a_index, ShaderCompatibilityRegistration& a_output) const;
		bool GetScope(std::uint32_t a_registrationIndex, std::uint32_t a_scopeIndex, ShaderCompatibilityScope& a_output) const;
		ShaderCompatibilityRequirementSet BuildRequirementSet(
			std::string_view a_shaderFamily,
			std::string_view a_shaderSource,
			std::span<const std::string> a_features = {}) const;
		void Freeze();

	private:
		mutable std::mutex mutex;
		ShaderCompatibilityAPI::Phase phase = ShaderCompatibilityAPI::Phase::kAcceptingRegistrations;
		std::uint64_t revision = 1;
		std::uint64_t nextHandle = 1;
		std::vector<ShaderCompatibilityRegistration> registrations;
		std::string compatibilitySetDigest;
		mutable std::unordered_map<std::string, ShaderCompatibilityRequirementSet> requirementCache;

		static ShaderCompatibilityResult ValidateAndCopy(
			const ShaderCompatibilityAPI::Registration001& a_input,
			ShaderCompatibilityRegistration& a_output);
		static bool Applies(
			const ShaderCompatibilityRegistration& a_registration,
			std::string_view a_shaderFamily,
			std::string_view a_shaderSource,
			std::span<const std::string> a_features);
	};

	ShaderCompatibilityRegistry& GetShaderCompatibilityRegistry();
	void FreezeShaderCompatibilityRegistrations();
	ShaderCompatibilityRequirementSet GetShaderCompatibilityRequirementSet(
		std::string_view a_shaderFamily,
		std::string_view a_shaderSource,
		std::span<const std::string> a_features = {});
}
