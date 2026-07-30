#include "Diagnostics/VRPipelineDiagnostics.h"

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace VRPipelineDiagnostics
{
	namespace
	{
		std::mutex g_mutex;
		std::ofstream g_structuredStream;
		std::string g_sessionId;
		uint64_t g_sequence = 0;
		LARGE_INTEGER g_startCounter{};
		LARGE_INTEGER g_frequency{};
		bool g_initialized = false;
		bool g_reportedStructuredOpenFailure = false;

		const char* SourceToString(Source source)
		{
			switch (source) {
			case Source::CS:
				return "CS";
			default:
				return "Unknown";
			}
		}

		std::string MakeSessionId()
		{
			SYSTEMTIME time{};
			GetSystemTime(&time);
			return std::format(
				"{:04}{:02}{:02}T{:02}{:02}{:02}Z-{:x}",
				time.wYear,
				time.wMonth,
				time.wDay,
				time.wHour,
				time.wMinute,
				time.wSecond,
				GetCurrentProcessId());
		}

		void EnsureInitialized()
		{
			if (g_initialized)
				return;

			QueryPerformanceFrequency(&g_frequency);
			QueryPerformanceCounter(&g_startCounter);
			g_sessionId = MakeSessionId();
			g_initialized = true;
		}

		uint64_t GetTimestampUs()
		{
			LARGE_INTEGER now{};
			QueryPerformanceCounter(&now);
			if (g_frequency.QuadPart <= 0)
				return 0;

			const auto ticks = now.QuadPart - g_startCounter.QuadPart;
			return static_cast<uint64_t>((ticks * 1000000LL) / g_frequency.QuadPart);
		}

		std::filesystem::path GetStructuredLogPath()
		{
			auto logPath = logger::log_directory();
			if (logPath) {
				*logPath /= "VRPipeline-CS.jsonl";
				return *logPath;
			}

			return std::filesystem::current_path() / "VRPipeline-CS.jsonl";
		}

		bool EnsureStructuredStream()
		{
			if (g_structuredStream.is_open())
				return true;

			const auto path = GetStructuredLogPath();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);

			g_structuredStream.open(path, std::ios::out | std::ios::app);
			if (!g_structuredStream.is_open()) {
				if (!g_reportedStructuredOpenFailure) {
					logger::warn("[VRPIPE v1][CS][ERROR] failed to open structured diagnostics at {}", path.string());
					g_reportedStructuredOpenFailure = true;
				}
				return false;
			}

			logger::info("[VRPIPE v1][CS][STRUCTURED] path={}", path.string());
			return true;
		}
	}

	void ResetSession()
	{
		std::scoped_lock lock(g_mutex);
		if (g_structuredStream.is_open())
			g_structuredStream.close();

		g_sessionId.clear();
		g_sequence = 0;
		g_initialized = false;
		g_reportedStructuredOpenFailure = false;
	}

	void Emit(const Event& event, bool writeStructured, std::string_view textPayload)
	{
		std::scoped_lock lock(g_mutex);
		EnsureInitialized();

		const uint64_t sequence = ++g_sequence;
		const auto* source = SourceToString(event.source);
		logger::info("[VRPIPE v1][{}][{}] seq={} {}", source, event.type, sequence, textPayload);

		if (!writeStructured || !EnsureStructuredStream())
			return;

		nlohmann::json record;
		record["schema"] = "vrpipe";
		record["version"] = 1;
		record["source"] = source;
		record["build"] = Plugin::VERSION.string();
		record["session"] = g_sessionId;
		record["sequence"] = sequence;
		record["timestampUs"] = GetTimestampUs();
		record["event"] = event.type;
		record["reason"] = event.reason;
		record["fields"] = event.data;

		g_structuredStream << record.dump() << '\n';
		g_structuredStream.flush();
	}
}
