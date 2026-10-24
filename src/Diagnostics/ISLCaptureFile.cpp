#include "Diagnostics/ReflexMeasurementInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/ServiceFoundation.h"
#	include "Diagnostics/ISLDiagnosticsInternal.h"
#	include "Utils/StringUtils.h"
#	include <filesystem>
#	include <winrt/base.h>

namespace CSX::Diagnostics::ReflexMeasurement
{
	namespace
	{
		class CaptureFile
		{
		public:
			explicit CaptureFile(const std::filesystem::path& path)
			{
				const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle == INVALID_HANDLE_VALUE)
					throw std::runtime_error("cannot create diagnostic capture: " + std::to_string(GetLastError()));
				file.attach(handle);
				buffer.reserve(1024 * 1024);
			}
			void Append(const json& value)
			{
				buffer += value.dump();
				buffer += '\n';
				if (buffer.size() >= 1024 * 1024)
					Flush();
			}
			uint64_t Finish()
			{
				Flush();
				if (!FlushFileBuffers(file.get()))
					throw std::runtime_error("cannot flush diagnostic capture: " + std::to_string(GetLastError()));
				file.close();
				return bytes;
			}

		private:
			void Flush()
			{
				size_t offset = 0;
				while (offset < buffer.size()) {
					DWORD written{};
					const auto length = static_cast<DWORD>(std::min<size_t>(buffer.size() - offset, 1024 * 1024));
					if (!WriteFile(file.get(), buffer.data() + offset, length, &written, nullptr) || written == 0)
						throw std::runtime_error("diagnostic capture write failed: " + std::to_string(GetLastError()));
					offset += written;
					bytes += written;
				}
				buffer.clear();
			}
			winrt::handle file;
			std::string buffer;
			uint64_t bytes{};
		};
	}

	json SaveCapture()
	{
		auto& s = State();
		auto& d = ISL::Data();
		if (!s.savedCapture.is_null())
			return s.savedCapture;
		if (!s.islPurpose || s.acquiring || !s.journal.frozen || !d.frozen || s.captureReleased || d.capture != s.journal.id)
			throw std::logic_error("save_capture requires the owning frozen joint ISL capture");
		const auto directory = SKSE::log::log_directory();
		if (!directory)
			throw std::runtime_error("SKSE log directory unavailable");
		const auto folder = *directory / "CSX-Captures";
		std::filesystem::create_directories(folder);
		const auto path = folder / ("ISL-" + CSX::Api::ServiceFoundation::NewId() + ".csxcap");
		const auto begin = Clock();
		auto raw = Export({ { "offset", 0 }, { "limit", 1 } }, false);
		raw.erase("records");
		std::scoped_lock lock(s.journal.mutex);
		raw["offset"] = 0;
		raw["nextOffset"] = nullptr;
		const auto total = raw.at("total").get<size_t>();
		auto side = d.frozenHeader;
		side["offset"] = 0;
		side["nextOffset"] = nullptr;
		side["total"] = d.frozenRows.size();
		json metrics = json::array(), callers = json::object();
		for (size_t i = 0; i < size_t(ISL::Metric::Count); ++i)
			metrics.push_back(ISL::MetricName(ISL::Metric(i)));
		for (const auto& row : d.frozenRows) {
			if (row.caller && !callers.contains(std::to_string(row.caller)))
				callers[std::to_string(row.caller)] = ISL::CallerJson(row.caller);
		}
		CaptureFile output(path);
		output.Append({ { "format", "csx-isl-capture-v1" }, { "raw", std::move(raw) }, { "isl", std::move(side) },
			{ "metrics", std::move(metrics) }, { "callers", std::move(callers) },
			{ "sideColumns", { "ordinal", "metric", "enterQpc", "exitQpc", "generation", "frame", "thread", "caller", "labelId", "cpu100ns", "cycles", "cpuStart100ns" } } });
		for (size_t offset = 0; offset < total; offset += 4096) {
			for (auto& row : ExportRecords(offset, 4096))
				output.Append(json::array({ 0, std::move(row) }));
		}
		for (size_t i = 0; i < d.frozenRows.size(); ++i) {
			const auto& row = d.frozenRows[i];
			output.Append(json::array({ 1, i, size_t(row.metric), row.enter, row.exit, row.generation,
				row.frame == ISL::UnknownFrame ? json(nullptr) : json(row.frame), row.thread, row.caller, row.label, row.cpuKnown ? json(row.cpu100ns) : json(nullptr), row.cyclesKnown ? json(row.cycles) : json(nullptr), row.cpuKnown ? json(row.cpuStart100ns) : json(nullptr) }));
		}
		output.Append({ { "complete", true }, { "rawTotal", total }, { "islTotal", d.frozenRows.size() } });
		const auto bytes = output.Finish();
		// A receipt only exists after the whole frozen journal is durable. Client acknowledgement remains separate.
		s.savedCapture = { { "saved", true }, { "format", "csx-isl-capture-v1" }, { "captureId", s.journal.id },
			{ "path", Util::PathToUtf8(path) }, { "bytes", bytes }, { "rawTotal", total }, { "islTotal", d.frozenRows.size() },
			{ "saveBeginQpc", begin }, { "saveEndQpc", Clock() }, { "analysis", "offline" } };
		s.contiguousExport = total;
		d.contiguous = d.frozenRows.size();
		return s.savedCapture;
	}
}
#endif
