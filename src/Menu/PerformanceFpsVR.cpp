#include "PerformanceFpsVR.h"
#include "PerformanceQuickScan.h"
#include "Utils/StringUtils.h"

#include <Windows.h>

#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace PerformanceFpsVR
{
	using json = nlohmann::json;
	using namespace std::chrono_literals;
	namespace
	{
		struct CloseHandleDeleter
		{
			void operator()(HANDLE value) const
			{
				if (value && value != INVALID_HANDLE_VALUE)
					CloseHandle(value);
			}
		};
		using Handle = std::unique_ptr<void, CloseHandleDeleter>;

		bool ReadLine(std::istream& input, std::string& line)
		{
			std::array<char, 65538> buffer;
			input.getline(buffer.data(), buffer.size());
			if (input.bad() || (input.fail() && !input.eof()) || input.gcount() > 65537)
				throw std::runtime_error("Unreadable or oversized fpsVR CSV line.");
			if (input.eof())
				return false;
			line.assign(buffer.data(), static_cast<std::size_t>(input.gcount() - 1));
			return true;
		}

		std::vector<std::string_view> Fields(std::string_view line)
		{
			std::vector<std::string_view> fields;
			std::size_t start = 0;
			do {
				while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
				std::string_view field;
				std::size_t end = start;
				if (start < line.size() && line[start] == '"') {
					++start;
					end = start;
					for (;;) {
						end = line.find('"', end);
						if (end == std::string_view::npos)
							throw std::runtime_error("Unterminated fpsVR CSV field.");
						if (end + 1 == line.size() || line[end + 1] != '"')
							break;
						end += 2;
					}
					field = line.substr(start, end - start);
					++end;
					while (end < line.size() && (line[end] == ' ' || line[end] == '\t' || line[end] == '\r')) ++end;
					if (end < line.size() && line[end] != ',')
						throw std::runtime_error("Invalid quoted fpsVR CSV field.");
				} else {
					end = line.find(',', start);
					if (end == std::string_view::npos)
						end = line.size();
					field = line.substr(start, end - start);
					if (field.find('"') != std::string_view::npos)
						throw std::runtime_error("Invalid fpsVR CSV quoting.");
				}
				const auto first = field.find_first_not_of(" \t\r\n");
				const auto last = field.find_last_not_of(" \t\r\n");
				fields.push_back(first == std::string::npos ? "" : field.substr(first, last - first + 1));
				if (fields.size() > 256)
					throw std::runtime_error("Too many fpsVR CSV columns.");
				if (end == line.size())
					break;
				start = end + 1;
			} while (true);
			return fields;
		}

		struct Columns
		{
			std::size_t count, time, gpu, cpu;
		};
		std::optional<Columns> ReadColumns(std::istream& csv)
		{
			std::string line;
			if (!ReadLine(csv, line))
				return std::nullopt;
			if (line.starts_with("\xEF\xBB\xBF"))
				line.erase(0, 3);
			if (Fields(line) != std::vector<std::string_view>{ "sep=", "" })
				throw std::runtime_error("Unsupported fpsVR delimiter.");
			if (!ReadLine(csv, line))
				return std::nullopt;
			if (!line.starts_with("Frametimes/"))
				throw std::runtime_error("Not an fpsVR raw capture.");
			if (!ReadLine(csv, line))
				return std::nullopt;
			const auto fields = Fields(line);
			const std::set<std::string_view> unique(fields.begin(), fields.end());
			if (unique.size() != fields.size())
				throw std::runtime_error("Duplicate fpsVR columns.");
			const auto column = [&](std::string_view name) {
				const auto found = std::find(fields.begin(), fields.end(), name);
				if (found == fields.end())
					throw std::runtime_error("Missing fpsVR timing column.");
				return static_cast<std::size_t>(found - fields.begin());
			};
			return Columns{ fields.size(), column("SteamVR Time"), column("GPU frametime"), column("CPU frametime") };
		}

		double Number(std::string_view text)
		{
			double value = 0;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value) || value < 0)
				throw std::runtime_error("Invalid fpsVR numeric sample.");
			return value;
		}

		json Statistics(std::vector<double> values)
		{
			double mean = 0;
			for (std::size_t i = 0; i < values.size(); ++i) mean += (values[i] - mean) / (i + 1);
			std::sort(values.begin(), values.end());
			const auto percentile = [&](double quantile) {
				const double index = (values.size() - 1) * quantile;
				const auto lower = static_cast<std::size_t>(index);
				return std::lerp(values[lower], values[std::min(lower + 1, values.size() - 1)], index - lower);
			};
			return { { "meanMs", mean }, { "medianMs", percentile(0.5) }, { "p95Ms", percentile(0.95) }, { "p99Ms", percentile(0.99) } };
		}

		Observation Latest(const std::filesystem::path& directory, bool samples = true)
		{
			Observation result;
			if (directory.empty() || !std::filesystem::exists(directory))
				return result;
			std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
			for (const auto& entry : std::filesystem::directory_iterator(directory)) {
				if (entry.is_regular_file() && entry.path().filename().wstring().starts_with(L"Frametimes#Raw#") &&
					entry.path().extension() == L".csv" && entry.last_write_time() > newest) {
					result.file = entry.path();
					newest = entry.last_write_time();
				}
			}
			if (result.file.empty())
				return result;
			result.bytes = std::filesystem::file_size(result.file);
			if (!samples)
				return result;
			std::ifstream stream(result.file, std::ios::binary);
			if (!stream)
				throw std::runtime_error("Cannot inspect fpsVR raw CSV; recording state is unknown.");
			const auto columns = ReadColumns(stream);
			if (!columns)
				return result;
			// Only complete trailing records prove that the logger is advancing.
			if (result.bytes > 8192) {
				stream.seekg(static_cast<std::streamoff>(result.bytes - 8192));
				std::string partial;
				ReadLine(stream, partial);
			}
			std::string line;
			while (ReadLine(stream, line)) {
				const auto fields = Fields(line);
				if (fields.size() == columns->count)
					result.lastSample = Number(fields[columns->time]);
			}
			return result;
		}

		bool Growing(const Observation& before, const Observation& after)
		{
			return !before.file.empty() && before.file == after.file && after.bytes > before.bytes && after.lastSample > before.lastSample;
		}

		bool WriterOpen(const std::filesystem::path& file)
		{
			if (file.empty())
				return false;
			Handle handle(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (handle.get() != INVALID_HANDLE_VALUE)
				return false;
			if (GetLastError() == ERROR_SHARING_VIOLATION)
				return true;
			throw std::runtime_error("Cannot establish fpsVR recording ownership.");
		}

		void ToggleCommand(const std::filesystem::path& command)
		{
			SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
			HANDLE read = nullptr, write = nullptr;
			if (!CreatePipe(&read, &write, &security, 0))
				throw std::runtime_error("Could not capture fpsVRcmd acknowledgement.");
			Handle reader(read), writer(write);
			if (!SetHandleInformation(reader.get(), HANDLE_FLAG_INHERIT, 0))
				throw std::runtime_error("Could not isolate fpsVRcmd output.");
			Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (input.get() == INVALID_HANDLE_VALUE)
				throw std::runtime_error("Could not isolate fpsVRcmd input.");
			SIZE_T attributeBytes = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
			std::vector<char> storage(attributeBytes);
			auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
			if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
				throw std::runtime_error("Could not initialize fpsVRcmd handles.");
			struct Attributes
			{
				LPPROC_THREAD_ATTRIBUTE_LIST value;
				~Attributes() { DeleteProcThreadAttributeList(value); }
			} cleanupAttributes{ attributes };
			HANDLE inherited[]{ writer.get(), input.get() };
			if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
				throw std::runtime_error("Could not restrict fpsVRcmd handle inheritance.");
			std::wstring arguments = L"\"" + command.wstring() + L"\" logging_startstop";
			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdInput = input.get();
			startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writer.get();
			startup.lpAttributeList = attributes;
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(command.c_str(), arguments.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
					nullptr, command.parent_path().c_str(), &startup.StartupInfo, &process))
				throw std::runtime_error("Could not launch fpsVRcmd.");
			Handle processHandle(process.hProcess), threadHandle(process.hThread);
			struct CommandLifetime
			{
				HANDLE process;
				~CommandLifetime()
				{
					if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
						TerminateProcess(process, ERROR_TIMEOUT);
						WaitForSingleObject(process, 1000);
					}
				}
			} cleanupProcess{ processHandle.get() };
			writer.reset();
			std::string output;
			bool exited = false;
			const auto deadline = std::chrono::steady_clock::now() + 5s;
			for (;;) {
				DWORD available = 0;
				if (!PeekNamedPipe(reader.get(), nullptr, 0, nullptr, &available, nullptr) && GetLastError() != ERROR_BROKEN_PIPE)
					throw std::runtime_error("Could not read fpsVRcmd acknowledgement.");
				if (available) {
					char bytes[1024];
					DWORD received = 0;
					if (!ReadFile(reader.get(), bytes, std::min<DWORD>(available, sizeof(bytes)), &received, nullptr))
						throw std::runtime_error("Could not read fpsVRcmd output.");
					output.append(bytes, received);
					if (output.size() > 16384)
						throw std::runtime_error("Unexpectedly large fpsVRcmd acknowledgement.");
				} else {
					if (exited)
						break;
					const auto state = WaitForSingleObject(processHandle.get(), 25);
					if (state == WAIT_OBJECT_0) {
						exited = true;
						continue;
					}
					if (state != WAIT_TIMEOUT)
						throw std::runtime_error("fpsVRcmd state is unknown; no second toggle sent.");
				}
				if (std::chrono::steady_clock::now() >= deadline)
					throw std::runtime_error("fpsVRcmd acknowledgement timed out; no second toggle sent.");
			}
			DWORD exitCode = 1;
			if (!GetExitCodeProcess(processHandle.get(), &exitCode))
				throw std::runtime_error("fpsVRcmd exit status is unavailable.");
			RequireCommandSuccess(exitCode, output);
		}
	}

	void RequireCommandSuccess(std::uint32_t exitCode, std::string_view output)
	{
		std::string lower(output);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (exitCode || lower.find("can't connect") != std::string::npos || lower.find("cannot connect") != std::string::npos || lower.find("usage:") != std::string::npos)
			throw std::runtime_error("fpsVRcmd failed or could not connect; no second toggle sent.");
	}

	void Recording::Prepare(Logger& logger, const std::atomic_bool& cancelled)
	{
		if (cancelled.load(std::memory_order_acquire))
			throw std::runtime_error("fpsVR preparation cancelled.");
		const auto before = logger.Observe();
		logger.Wait(2s);
		auto current = logger.Observe();
		if (Growing(before, current)) {
			observation = current;
			confirmed = true;
			return;
		}
		if (before.file != current.file || logger.AnyWriter())
			throw std::runtime_error("Existing fpsVR recording is not advancing; left untouched.");
		if (!logger.Running() || cancelled.load(std::memory_order_acquire))
			throw std::runtime_error("fpsVR exited or preparation was cancelled before recording.");
		logger.Toggle();
		for (int attempt = 0; attempt < 32; ++attempt) {
			logger.Wait(250ms);
			auto next = logger.Observe(false);
			if (!next.file.empty() && next.file != before.file) {
				if (!observation.file.empty() && next.file != observation.file) {
					owned = false;
					throw std::runtime_error("fpsVR recording changed during admission; left untouched.");
				}
				observation = next;
				owned = logger.WriterOpen(next.file);
				next = logger.Observe();
				if (next.file != observation.file) {
					owned = false;
					throw std::runtime_error("fpsVR recording changed while reading samples; left untouched.");
				}
				observation = next;
				if (owned && Growing(current, next)) {
					confirmed = true;
					return;
				}
			}
			current = next;
		}
		throw std::runtime_error("fpsVR raw samples did not advance after the command; recording omitted.");
	}

	void Recording::Stop(Logger& logger)
	{
		if (!owned)
			return;
		owned = false;
		if (!logger.Running())
			throw std::runtime_error("fpsVR exited; no stop toggle sent.");
		const auto current = logger.Observe(false);
		if (current.file != observation.file || current.bytes < observation.bytes)
			throw std::runtime_error("fpsVR process or recording changed; no stop toggle sent.");
		if (!logger.WriterOpen(observation.file))
			return;
		logger.Toggle();
		for (int attempt = 0; attempt < 32 && logger.WriterOpen(observation.file); ++attempt)
			logger.Wait(250ms);
		if (logger.WriterOpen(observation.file))
			throw std::runtime_error("fpsVR stop was not confirmed; inspect fpsVR recording state.");
	}

	json Correlate(std::istream& csv, const json& captures)
	{
		if (!captures.is_array() || captures.size() > PerformanceQuickScan::kBatchScans)
			throw std::runtime_error("Invalid scan windows.");
		const auto columns = ReadColumns(csv);
		if (!columns)
			throw std::runtime_error("Incomplete fpsVR CSV header.");

		struct Window
		{
			double start, end;
			std::uint32_t scanIndex;
			std::vector<double> gpu, cpu;
		};
		std::vector<Window> windows;
		std::set<std::uint32_t> indices;
		for (const auto& capture : captures) {
			if (capture.value("phase", "") != "complete")
				continue;
			const double start = capture.value("startSteamVrSeconds", 0.0), end = capture.value("endSteamVrSeconds", 0.0);
			if (!std::isfinite(start) || !std::isfinite(end) || start <= 0 || end <= start || end - start > PerformanceQuickScan::kMaximumSeconds)
				throw std::runtime_error("A completed scan with a stable SteamVR clock is required for fpsVR matching.");
			const auto& index = capture.at("scanIndex");
			if (!index.is_number_integer() || index < 1 || index > PerformanceQuickScan::kBatchScans || !indices.insert(index.get<std::uint32_t>()).second ||
				(!windows.empty() && start <= windows.back().end))
				throw std::runtime_error("Duplicate, overlapping or invalid fpsVR scan windows.");
			windows.push_back({ start, end, index.get<std::uint32_t>(), {}, {} });
		}
		if (windows.empty())
			throw std::runtime_error("No completed scan windows to match.");
		std::optional<double> first, last;
		std::size_t lines = 0;
		std::string line;
		while (ReadLine(csv, line)) {
			if (++lines > 10000000)
				throw std::runtime_error("fpsVR CSV exceeds the supported size.");
			const auto row = Fields(line);
			if (row.size() != columns->count)
				throw std::runtime_error("Malformed fpsVR CSV row.");
			const double time = Number(row[columns->time]);
			if (last && time <= *last)
				throw std::runtime_error("fpsVR clock reset or duplicate sample; matching refused.");
			if (!first)
				first = time;
			last = time;
			for (auto& window : windows) {
				if (time >= window.start && time <= window.end) {
					if (window.cpu.size() >= kMaximumWindowSamples)
						throw std::runtime_error("Too many fpsVR samples in a scan window.");
					window.gpu.push_back(Number(row[columns->gpu]));
					window.cpu.push_back(Number(row[columns->cpu]));
				}
			}
		}
		json results = json::array();
		for (std::size_t i = 0; i < windows.size(); ++i) {
			const auto& window = windows[i];
			if (!first || *first > window.start || !last || *last < window.end || window.cpu.size() < 3)
				throw std::runtime_error("fpsVR raw CSV does not cover every scan window.");
			results.push_back({ { "scanIndex", window.scanIndex }, { "samples", window.cpu.size() },
				{ "startSteamVrSeconds", window.start }, { "endSteamVrSeconds", window.end },
				{ "cpu", Statistics(window.cpu) }, { "gpu", Statistics(window.gpu) } });
		}
		return results;
	}

	struct Session::Impl : Logger
	{
		std::filesystem::path directory, command;
		Recording recording;
		Handle fpsvr;
		std::atomic_bool cancelled = false;
		std::string error;
		std::future<void> preparation;

		bool Running() const override { return fpsvr && WaitForSingleObject(fpsvr.get(), 0) == WAIT_TIMEOUT; }
		Observation Observe(bool samples) override { return Latest(directory, samples); }
		bool WriterOpen(const std::filesystem::path& file) override { return PerformanceFpsVR::WriterOpen(file); }
		void Toggle() override { ToggleCommand(command); }
		void Wait(std::chrono::milliseconds duration) override { std::this_thread::sleep_for(duration); }
		bool AnyWriter() override
		{
			if (!std::filesystem::exists(directory))
				return false;
			for (const auto& file : std::filesystem::directory_iterator(directory))
				if (file.is_regular_file() && file.path().filename().wstring().starts_with(L"Frametimes#Raw#") && file.path().extension() == L".csv" && WriterOpen(file.path()))
					return true;
			return false;
		}

		void Prepare()
		try {
			Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
			PROCESSENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			for (bool available = Process32FirstW(snapshot.get(), &entry); available; available = Process32NextW(snapshot.get(), &entry)) {
				if (_wcsicmp(entry.szExeFile, L"fpsVR.exe") != 0)
					continue;
				if (fpsvr)
					throw std::runtime_error("Multiple fpsVR processes; recording omitted.");
				fpsvr.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID));
				wchar_t path[32768]{};
				DWORD length = static_cast<DWORD>(std::size(path));
				if (!fpsvr || !QueryFullProcessImageNameW(fpsvr.get(), 0, path, &length))
					throw std::runtime_error("Cannot inspect running fpsVR; recording omitted.");
				command = std::filesystem::path(path).parent_path() / L"fpsVRcmd.exe";
			}
			if (!fpsvr || directory.empty() || !std::filesystem::is_regular_file(command))
				throw std::runtime_error("Running fpsVR, its command, or Documents folder unavailable; recording omitted.");
			recording.Prepare(*this, cancelled);
		} catch (const std::exception& failure) {
			error = failure.what();
		}

		json Finish(const json& captures)
		{
			preparation.get();
			json result = { { "status", "omitted" }, { "owned", recording.owned }, { "rawCsv", Util::PathToUtf8(recording.observation.file) },
				{ "alignment", "Latest completed SteamVR timestamps at capture request and resolution; approximate boundaries include query latency" },
				{ "percentileMethod", "linear_interpolation" }, { "scans", json::array() } };
			try {
				if (recording.confirmed) {
					double end = 0;
					for (const auto& capture : captures)
						end = std::max(end, capture.value("endSteamVrSeconds", 0.0));
					for (int attempt = 0; attempt < 32; ++attempt) {
						const auto current = Latest(directory);
						if (current.file != recording.observation.file || current.lastSample >= end)
							break;
						std::this_thread::sleep_for(250ms);
					}
				}
			} catch (const std::exception& failure) {
				error = failure.what();
			}
			try {
				recording.Stop(*this);
				if (!recording.confirmed || !error.empty())
					throw std::runtime_error(error.empty() ? "fpsVR recording unavailable." : error);
				if (std::filesystem::file_size(recording.observation.file) > 512 * 1024 * 1024)
					throw std::runtime_error("fpsVR raw CSV exceeds 512 MiB; automatic matching omitted.");
				std::ifstream stream(recording.observation.file, std::ios::binary);
				result["scans"] = Correlate(stream, captures);
				result["status"] = result["scans"].size() == captures.size() ? "complete" : "partial";
				if (result["status"] == "partial")
					result["reason"] = "Only completed profiler scans were matched.";
			} catch (const std::exception& failure) {
				result["reason"] = failure.what();
			}
			return result;
		}
	};

	Session::Session(std::filesystem::path directory) : impl(std::make_unique<Impl>())
	{
		impl->directory = std::move(directory);
		impl->preparation = std::async(std::launch::async, [state = impl.get()] { state->Prepare(); });
	}
	Session::~Session() = default;
	bool Session::Prepared() const { return impl->preparation.wait_for(0s) == std::future_status::ready; }
	void Session::RequestFinish() { impl->cancelled.store(true, std::memory_order_release); }
	void Session::Finish(json& record)
	try {
		record["fpsVR"] = impl->Finish(record.value("captures", json::array()));
	} catch (const std::exception& error) {
		record["fpsVR"] = { { "status", "omitted" }, { "reason", error.what() }, { "scans", json::array() } };
	}
}
