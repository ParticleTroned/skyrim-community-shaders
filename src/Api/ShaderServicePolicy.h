#pragma once

#include "VRAPI/CSshaderapi.h"

#include <cstdint>
#include <string>

namespace CSX::Api
{
	struct ShaderPolicyState
	{
		bool available = false;
		bool compiling = false;
		bool previousCacheAvailable = false;
		bool diskCacheHeld = false;
		bool featureFound = false;
		bool persistentMutationBlocked = false;
		std::uint64_t stateRevision = 0;
	};

	struct ShaderPolicyRequest
	{
		ShaderAPI::MutationAction action = ShaderAPI::MutationAction::kSetCustomShaders;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = ShaderAPI::kMutationNone;
		bool boolValue = false;
		std::string featureName;
	};

	struct ShaderPolicyDecision
	{
		ShaderAPI::Status status = ShaderAPI::Status::kSuccess;
		bool allowed = true;
		bool disruptive = false;
		bool destructive = false;
		bool restartRequired = false;
		bool shaderRecompileExpected = false;
		std::uint64_t requiredFlags = ShaderAPI::kMutationNone;
		std::string reasonCode;
		std::string message;
	};

	ShaderPolicyDecision EvaluateShaderMutation(
		const ShaderPolicyState& a_state,
		const ShaderPolicyRequest& a_request);
}
