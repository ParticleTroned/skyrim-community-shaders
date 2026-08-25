#include "Api/ShaderCompatibilityRegistry.h"

#include <cassert>
#include <string>

using namespace CSX;

namespace
{
	ShaderCompatibilityAPI::Registration001 Registration(
		const char* a_identity,
		std::uint32_t a_minor,
		const ShaderCompatibilityAPI::Scope001* a_scopes,
		std::uint32_t a_scopeCount)
	{
		return {
			.structSize = sizeof(ShaderCompatibilityAPI::Registration001),
			.identity = a_identity,
			.owner = "test provider",
			.displayVersion = "42.7.9",
			.contractMajor = 1,
			.currentMinor = a_minor,
			.minimumCompatibleMinor = a_minor,
			.maximumCompatibleMinor = a_minor,
			.resourceFingerprint = "resource:test",
			.scopes = a_scopes,
			.scopeCount = a_scopeCount,
		};
	}
}

int main()
{
	static_assert(ShaderCompatibilityAPI::ServiceMajor == 1);
	static_assert(sizeof(ShaderCompatibilityAPI::Interface001) >= sizeof(void*) * 5);

	Api::ShaderCompatibilityRegistry registry;
	const ShaderCompatibilityAPI::Scope001 waterScopes[]{
		{ .structSize = sizeof(ShaderCompatibilityAPI::Scope001), .kind = ShaderCompatibilityAPI::ScopeKind::kShaderFamily, .value = "Water" },
		{ .structSize = sizeof(ShaderCompatibilityAPI::Scope001), .kind = ShaderCompatibilityAPI::ScopeKind::kShaderSource, .value = "Shaders/Water.hlsl" },
	};
	auto water = Registration("org.example.water", 2, waterScopes, 2);
	const auto first = registry.Register(water);
	assert(first.status == ShaderCompatibilityAPI::Status::kSuccess);
	assert(first.accepted && !first.idempotent && first.handle != 0);
	assert(!first.digest.empty());

	const auto duplicate = registry.Register(water);
	assert(duplicate.status == ShaderCompatibilityAPI::Status::kSuccess);
	assert(duplicate.idempotent && duplicate.handle == first.handle);
	assert(duplicate.digest == first.digest);

	auto conflict = water;
	conflict.currentMinor = 3;
	conflict.minimumCompatibleMinor = 3;
	conflict.maximumCompatibleMinor = 3;
	assert(registry.Register(conflict).status == ShaderCompatibilityAPI::Status::kIdentityConflict);

	const auto waterSet = registry.BuildRequirementSet("water", "shaders/water.hlsl");
	assert(waterSet.handles.size() == 1 && waterSet.handles.front() == first.handle);
	assert(!waterSet.canonical.empty() && !waterSet.digest.empty());
	const auto unrelatedSet = registry.BuildRequirementSet("grass", "shaders/grass.hlsl");
	assert(unrelatedSet.handles.empty());
	assert(waterSet.digest != unrelatedSet.digest);

	registry.Freeze();
	const auto snapshot = registry.GetSnapshot();
	assert(snapshot.phase == ShaderCompatibilityAPI::Phase::kFrozen);
	assert(snapshot.registrationCount == 1);
	assert(!snapshot.compatibilitySetDigest.empty());
	assert(registry.Register(water).idempotent);

	const ShaderCompatibilityAPI::Scope001 globalScope{
		.structSize = sizeof(ShaderCompatibilityAPI::Scope001),
		.kind = ShaderCompatibilityAPI::ScopeKind::kGlobal,
	};
	auto late = Registration("org.example.late", 0, &globalScope, 1);
	const auto rejected = registry.Register(late);
	assert(rejected.status == ShaderCompatibilityAPI::Status::kRegistrationClosed);
	assert(rejected.restartRequired && !rejected.accepted);

	auto invalid = Registration("Not Stable", 0, &globalScope, 1);
	assert(registry.Register(invalid).status == ShaderCompatibilityAPI::Status::kInvalidIdentity);
	return 0;
}
