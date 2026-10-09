#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterRecorder.h"
#	include <nlohmann/json_fwd.hpp>
namespace CSX::Diagnostics::Stutters
{
	/** Measures CPU wall duration, including blocking; never GPU execution time. */
	class Scope
	{
	public:
		explicit Scope(std::string_view name, Boundary boundary = Boundary::None) noexcept;
		~Scope() noexcept;
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Recorder* recorder_ = nullptr;
		Token token_;
	};
	/** Copies engine context on the game thread only while capture is enabled. */
	void PublishGameContext() noexcept;
	/** Controls capture without dispatching to a potentially stalled game thread. */
	nlohmann::json Handle(const nlohmann::json& args);
	/** Registers the DevBench-only diagnostic tool. */
	void Install();
}
#endif
