#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ReflexCapture.h"
#	include <nlohmann/json_fwd.hpp>

class Streamline;

namespace ReflexDiagnostics
{
	/** QPC timestamps share the clock used by the other DevBench captures. */
	uint64_t Tick();
	/** Called by DevBench; never invokes a Streamline function on the tool thread. */
	nlohmann::json Dispatch(const nlohmann::json& args);
	/** Internal suite owner; external trace mutations are blocked during a guided test. */
	nlohmann::json DispatchOwned(const nlohmann::json& args);
	/** Copy runtime module identities on a worker before timed capture begins. */
	nlohmann::json Environment(bool hashFiles);

	/** Records one input-hook invocation, including every early return. */
	class Scope
	{
	public:
		explicit Scope(Streamline& streamline);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		bool Active() const { return active_; }
		Record record;

	private:
		Streamline& streamline_;
		bool active_ = false;
	};
}
#endif
