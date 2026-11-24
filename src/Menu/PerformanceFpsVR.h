#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string_view>

namespace PerformanceFpsVR
{
	inline constexpr std::size_t kMaximumWindowSamples = 100000;
	struct Observation
	{
		std::filesystem::path file;
		std::uintmax_t bytes = 0;
		double lastSample = 0;
	};

	/** Narrow logger boundary keeps recording ownership independent of Windows discovery. */
	struct Logger
	{
		virtual ~Logger() = default;
		virtual bool Running() const = 0;
		virtual Observation Observe(bool samples = true) = 0;
		virtual void Toggle() = 0;
		virtual void Wait(std::chrono::milliseconds duration) = 0;
	};

	/** Selected recordings are stopped after the scan; uncertain toggles are never retried. */
	struct Recording
	{
		Observation observation;
		bool owned = false, confirmed = false;
		void Prepare(Logger& logger, const std::atomic_bool& cancelled);
		void Stop(Logger& logger);
	};
	/** fpsVRcmd can report a connection failure despite a successful exit code. */
	void RequireCommandSuccess(std::uint32_t exitCode, std::string_view output);
	/** Match raw SteamVR-clock samples to bounded scan windows; never edit the source CSV. */
	nlohmann::json Correlate(std::istream& csv, const nlohmann::json& captures);

	/** Optional production recorder. Discovery and logger admission run off the render thread. */
	class Session
	{
	public:
		explicit Session(std::filesystem::path csvDirectory);
		~Session();
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		bool Prepared() const;
		/** Prevent a pending preparation from dispatching a new recording command. */
		void RequestFinish();
		/** Worker-thread finalization stops the selected recording and attaches matched results. */
		void Finish(nlohmann::json& record);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
