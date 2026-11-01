#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterRecorder.h"
#	include <nlohmann/json_fwd.hpp>
#	include <optional>
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
	/** Records a loader stage and inherits a same-kind operation on this thread. */
	class AssetScope
	{
	public:
		AssetScope(AssetKind kind, std::string_view path, std::string_view stage) noexcept;
		~AssetScope() noexcept;
		AssetScope(const AssetScope&) = delete;
		AssetScope& operator=(const AssetScope&) = delete;
		/** Preserve the observed native result without interpreting unknown outcomes. */
		void Result(bool success) noexcept;
		void Result(bool success, std::int64_t code) noexcept;
		void CacheHit(bool hit) noexcept;
		/** Count payload bytes handled by this stage, rather than unique disk reads. */
		void Bytes(std::uint64_t bytes) noexcept;
		/** Preserve the resource-creation request descriptor, including zero mip counts. */
		void Dimensions(std::uint32_t width, std::uint32_t height, std::uint32_t mips, std::uint32_t format) noexcept;

	private:
		Recorder* recorder_ = nullptr;
		Token token_;
	};
	/** Admit loader instrumentation only during an explicitly enabled asset capture. */
	bool AssetRecordingEnabled() noexcept;
	/** Admit additional CPU evaluation scopes only during an explicitly enabled capture. */
	bool CodeEvaluationEnabled() noexcept;
	/** Adds detailed CPU phases only when the capture explicitly requests them. */
	class CodeEvaluationScope
	{
	public:
		explicit CodeEvaluationScope(std::string_view name) noexcept
		{
			if (CodeEvaluationEnabled())
				scope.emplace(name);
		}

	private:
		std::optional<Scope> scope;
	};
	/** Copies engine context on the game thread only while capture is enabled. */
	void PublishGameContext() noexcept;
	/** Controls capture without dispatching to a potentially stalled game thread. */
	nlohmann::json Handle(const nlohmann::json& args);
	/** Registers the DevBench-only diagnostic tool. */
	void Install();
}
#	define CSX_CODE_EVALUATION_SCOPE(variable, name) CSX::Diagnostics::Stutters::CodeEvaluationScope variable(name)
#else
#	define CSX_CODE_EVALUATION_SCOPE(variable, name) ((void)0)
#endif
