#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterRecorder.h"
#	include <filesystem>
#	include <functional>
#	include <memory>
#	include <nlohmann/json.hpp>
#	include <optional>

namespace CSX::Diagnostics::Stutters
{
	/** Host dependencies are read on control/watchdog threads, never in render hooks. */
	struct CaptureEnvironment
	{
		std::function<std::optional<std::filesystem::path>()> logDirectory;
		std::function<nlohmann::json()> producer;
		std::function<std::optional<nlohmann::json>(const nlohmann::json&)> validateBuild;
		std::function<bool()> shutdownRequested;
		bool vr = false;
	};
	/** Raw monotonic counter; zero explicitly signals unavailable timing. */
	Tick ReadCounter() noexcept;
	/** Bounded Windows evidence collection independent of the game's update thread. */
	class Capture
	{
	public:
		explicit Capture(CaptureEnvironment environment);
		~Capture();
		Capture(const Capture&) = delete;
		Capture& operator=(const Capture&) = delete;
		/** All actions, including stop, return without waiting for OS probe cleanup. */
		nlohmann::json Dispatch(const nlohmann::json& args);
		/** Parses and validates the same request path used by the DevBench bridge. */
		nlohmann::json Request(const char* input);
		/** Writes exactly one response and prevents exceptions from crossing the host callback. */
		void Respond(const char* input, void* sink, void (*write)(void*, const char*)) noexcept;
		/** Describes the same bounded input contract enforced by Request. */
		static nlohmann::json Descriptor();
		Recorder recorder;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
#endif
