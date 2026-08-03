#include "Diagnostics/VRPipelineDiagnostics.h"

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>

namespace VRPipelineDiagnostics
{
	namespace
	{
		std::mutex g_mutex;
		std::ofstream g_structuredStream;
		std::string g_sessionId;
		nlohmann::json g_latestRecord;
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

			if (!QueryPerformanceFrequency(&g_frequency) || g_frequency.QuadPart <= 0)
				g_frequency = {};
			if (!QueryPerformanceCounter(&g_startCounter)) {
				g_startCounter = {};
				g_frequency = {};
			}
			g_sessionId = MakeSessionId();
			g_initialized = true;
		}

		uint64_t GetTimestampUs()
		{
			LARGE_INTEGER now{};
			if (!QueryPerformanceCounter(&now))
				return 0;
			if (g_frequency.QuadPart <= 0 || now.QuadPart <= g_startCounter.QuadPart)
				return 0;

			constexpr long double microsecondsPerSecond = 1000000.0L;
			const long double elapsedMicroseconds =
				(static_cast<long double>(now.QuadPart - g_startCounter.QuadPart) * microsecondsPerSecond) /
				static_cast<long double>(g_frequency.QuadPart);
			if (elapsedMicroseconds >= static_cast<long double>(std::numeric_limits<uint64_t>::max()))
				return std::numeric_limits<uint64_t>::max();

			return static_cast<uint64_t>(elapsedMicroseconds);
		}

		std::filesystem::path GetStructuredLogPath()
		{
			auto logPath = logger::log_directory();
			if (logPath) {
				*logPath /= "VRPipeline-CS.jsonl";
				return *logPath;
			}

			std::error_code ec;
			const auto currentPath = std::filesystem::current_path(ec);
			return ec ?
			           std::filesystem::path{ "VRPipeline-CS.jsonl" } :
			           currentPath / "VRPipeline-CS.jsonl";
		}

		bool EnsureStructuredStream()
		{
			if (g_structuredStream.is_open())
				return true;

			const auto path = GetStructuredLogPath();
			std::error_code ec;
			if (!path.parent_path().empty())
				std::filesystem::create_directories(path.parent_path(), ec);
			if (ec) {
				if (!g_reportedStructuredOpenFailure) {
					logger::warn("[VRPIPE v1][CS][ERROR] failed to prepare structured diagnostics directory {}: {}", path.parent_path().string(), ec.message());
					g_reportedStructuredOpenFailure = true;
				}
				return false;
			}
			bool needsLineSeparator = false;
			{
				std::ifstream existing(path, std::ios::in | std::ios::binary);
				if (existing) {
					existing.seekg(0, std::ios::end);
					const auto endPosition = existing.tellg();
					if (endPosition != std::streampos(-1) && endPosition != std::streampos(0)) {
						existing.seekg(-1, std::ios::end);
						char lastByte = '\0';
						existing.get(lastByte);
						needsLineSeparator = lastByte != '\n';
					}
				}
			}

			g_structuredStream.clear();
			g_structuredStream.open(path, std::ios::out | std::ios::app);
			if (!g_structuredStream.is_open()) {
				if (!g_reportedStructuredOpenFailure) {
					logger::warn("[VRPIPE v1][CS][ERROR] failed to open structured diagnostics at {}", path.string());
					g_reportedStructuredOpenFailure = true;
				}
				return false;
			}
			if (needsLineSeparator) {
				g_structuredStream << '\n';
				g_structuredStream.flush();
				if (!g_structuredStream) {
					logger::warn("[VRPIPE v1][CS][ERROR] failed to isolate an unterminated structured diagnostics record at {}", path.string());
					g_structuredStream.close();
					g_structuredStream.clear();
					g_reportedStructuredOpenFailure = true;
					return false;
				}
			}

			g_reportedStructuredOpenFailure = false;
			logger::info("[VRPIPE v1][CS][STRUCTURED] path={}", path.string());
			return true;
		}
	}

	bool Emit(const Event& event, bool writeStructured, std::string_view textPayload, bool writeText)
	{
		std::scoped_lock lock(g_mutex);
		EnsureInitialized();

		const uint64_t sequence = ++g_sequence;
		const uint64_t timestampUs = GetTimestampUs();
		const auto* source = SourceToString(event.source);

		nlohmann::json record;
		record["schema"] = "vrpipe";
		record["version"] = 1;
		record["source"] = source;
		record["build"] = Plugin::VERSION.string();
		record["session"] = g_sessionId;
		record["sequence"] = sequence;
		record["timestampUs"] = timestampUs;
		record["event"] = event.type;
		record["reason"] = event.reason;
		record["fields"] = event.data;
		const std::string serializedRecord = record.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		g_latestRecord = nlohmann::json::parse(serializedRecord);
		if (writeText)
			logger::info("[VRPIPE v1][{}][{}] seq={} {}", source, event.type, sequence, textPayload);

		if (!writeStructured || !EnsureStructuredStream())
			return !writeStructured;

		g_structuredStream << serializedRecord << '\n';
		g_structuredStream.flush();
		if (!g_structuredStream) {
			if (!g_reportedStructuredOpenFailure) {
				logger::warn("[VRPIPE v1][CS][ERROR] failed to write structured diagnostics");
				g_reportedStructuredOpenFailure = true;
			}
			g_structuredStream.close();
			g_structuredStream.clear();
			return false;
		}

		return true;
	}

	nlohmann::json GetStatusSnapshot()
	{
		std::scoped_lock lock(g_mutex);
		return {
			{ "initialized", g_initialized },
			{ "session", g_sessionId },
			{ "lastSequence", g_sequence },
			{ "structuredWriterOpen", g_structuredStream.is_open() && g_structuredStream.good() },
			{ "latestRecord", g_latestRecord },
		};
	}
}
