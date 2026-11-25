#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace PerformanceResults
{
	using Json = nlohmann::json;
	/** Creates a collision-resistant, filename-safe UTC run identity. */
	std::string NewId();
	std::string Timestamp();
	/** Rejects malformed or unsupported history before displaying or exporting it. */
	void Validate(const Json& record);
	/** One row per metric; unavailable values remain null. */
	Json Metrics(const Json& record);
	/** Summarize only the repeats within one run, retaining each value and its scan index. */
	Json SummaryMetrics(const Json& record);
	/** Resolve a stored scene name, retaining a cell identity when no display name exists. */
	std::string SceneLabel(const Json& context);
	/** Matches measurement identity and units; never adds independent feature costs. */
	Json Compare(const Json& first, const Json& second);
	/** Self-contained offline report, including settings and optional run comparison. */
	std::string Html(const Json& record, const Json* comparison = nullptr);
	/** Combined report with independent settings and context for every run. */
	std::string HtmlSession(const Json& records);
	/** Spreadsheet-safe metric rows with context and optional comparison differences. */
	std::string Csv(const Json& record, const Json* comparison = nullptr);

	struct View
	{
		std::shared_ptr<const Json> history = std::make_shared<const Json>(Json::array());
		std::shared_ptr<const Json> first, second;
		std::string message, error;
		bool busy = false;
		std::size_t unsaved = 0;
		std::size_t sessionRuns = 0;
		std::string sessionReport;
	};

	/** Owns disk work. A measurement lease excludes all worker jobs without waiting for I/O. */
	class Store
	{
	public:
		using Writer = std::function<void(const std::filesystem::path&, std::string_view, bool)>;
		using FolderPicker = std::function<std::filesystem::path(const std::filesystem::path&)>;
		Store(std::filesystem::path logDirectory, Writer writer, FolderPicker picker = {});
		~Store();
		Store(const Store&) = delete;
		Store& operator=(const Store&) = delete;
		bool TryPause();
		bool Busy() const;
		void Resume();
		/** Optional worker finalizer attaches external measurements before saving/exporting. */
		void Save(Json record, std::function<void(Json&)> finalize = {});
		void Refresh();
		void Load(std::string id, bool comparison);
		/** HTML comparisons use current-session runs; an empty destination opens a folder picker. */
		void Export(std::string id, std::string comparisonId, std::string format, std::filesystem::path destination = {}, bool session = false);
		void Retry();
		View Snapshot() const;
		const std::filesystem::path& Folder() const { return folder; }

	private:
		void Submit(std::function<void()> job);
		void Run();
		Json Read(const std::string& id) const;
		void WriteRecord(const Json& record);
		void UpdateHistory(const Json& record);
		Json SessionRecords() const;
		std::filesystem::path folder, logPath;
		const std::string sessionId = NewId();
		std::vector<Json> sessionEntries;
		Writer writer;
		FolderPicker picker;
		mutable std::mutex mutex;
		std::condition_variable wake;
		std::deque<std::function<void()>> jobs;
		View view;
		std::map<std::string, Json> unsaved;
		bool paused = false, working = false, stopping = false;
		std::thread worker;
	};
}
