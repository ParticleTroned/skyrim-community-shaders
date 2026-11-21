#include "Menu/PerformanceResults.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PerformanceResults;
using namespace std::chrono_literals;

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Wait(Store& store)
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (store.Busy() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
		Require(!store.Busy(), "worker did not finish");
	}

	Json Record()
	{
		return { { "schema", 1 }, { "id", NewId() }, { "started", Timestamp() }, { "kind", "feature_batch" }, { "status", "complete" },
			{ "context", { { "runtime", "VR" }, { "timingDefinitions", 2 } } }, { "settings", Json::object() }, { "failure", nullptr },
			{ "results", Json::array({ { { "id", "test" }, { "label", "=test,<script>alert(1)</script>\"\nline" }, { "relativeTo", "Off" },
							 { "metrics", { { "gpu", { { "unit", "ms" }, { "value", 0.0 }, { "standardError", 0.05 } } },
											  { "cpu", { { "unit", "ms" }, { "value", nullptr } } } } } } }) } };
	}
}

int main()
try {
	const auto root = std::filesystem::temp_directory_path() / ("csx-performance-test-" + NewId());
	std::filesystem::create_directories(root);
	struct Cleanup
	{
		std::filesystem::path root;
		~Cleanup()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	} cleanup{ root };
	auto record = Record();
	Validate(record);
	Require(Html(record).find("<script>") == std::string::npos, "HTML injection");
	Require(Html(record).find("&lt;script&gt;") != std::string::npos, "HTML escaping missing");
	Require(Csv(record).find("\"'=test") != std::string::npos, "CSV formula injection");
	Require(Csv(record).find("Unavailable") != std::string::npos, "missing replaced with zero");
	auto second = record;
	second["results"][0]["metrics"]["gpu"]["value"] = 2.0;
	const auto difference = Compare(record, second);
	Require(difference["rows"][0]["difference"].is_null(), "unavailable comparison invented");
	Require(difference["rows"][1]["difference"] == 2.0, "zero baseline lost");
	second["kind"] = "quick_scan";
	Require(!Compare(record, second)["compatible"].get<bool>(), "mixed scan and cost semantics");
	second = record;
	second["context"]["runtime"] = "AE";
	Require(Compare(record, second)["rows"][1]["difference"].is_null(), "mixed runtime comparison");
	auto invalid = record;
	invalid["id"] = "../escape";
	bool rejected = false;
	try {
		Validate(invalid);
	} catch (const std::exception&) {
		rejected = true;
	}
	Require(rejected, "unsafe identity accepted");
	std::atomic_bool fail{ false }, entered{ false }, hold{ false };
	auto writer = [&](const auto& path, std::string_view text, bool overwrite) {
		entered = true;
		while (hold) std::this_thread::sleep_for(1ms);
		if (fail)
			throw std::runtime_error("test disk failure");
		if (!overwrite && std::filesystem::exists(path))
			throw std::runtime_error("file exists");
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << text;
		if (!output)
			throw std::runtime_error("test write failure");
	};
	int picks = 0;
	{
		Store store(root, writer, [&](const auto& initial) { ++picks; Require(initial == store.Folder(), "wrong picker default"); return root; });
		Wait(store);
		Require(store.TryPause(), "cannot reserve measurement");
		entered = false;
		store.Save(record);
		std::this_thread::sleep_for(20ms);
		Require(!entered, "disk activity during measurement");
		Require(!store.TryPause(), "pending checkpoint ignored");
		store.Resume();
		Wait(store);
		Require(std::filesystem::exists(store.Folder() / (record["id"].get<std::string>() + ".json")), "save missing");
		store.Load(record["id"], false);
		Wait(store);
		Require(store.Snapshot().first && *store.Snapshot().first == record, "round trip changed values");
		store.Export(record["id"], "", "both");
		Wait(store);
		Require(picks == 1 && store.Snapshot().error.empty(), "folder picker/export failed");
		std::size_t reports = 0;
		for (const auto& file : std::filesystem::directory_iterator(root))
			if (file.path().extension() == ".csv" || file.path().extension() == ".html")
				++reports;
		Require(reports == 2, "on-demand formats missing");
		const auto unicodeFolder = root / L"reports-\u65e5\u672c";
		std::filesystem::create_directory(unicodeFolder);
		store.Export(record["id"], "", "html", unicodeFolder);
		Wait(store);
		Require(store.Snapshot().error.empty(), "Unicode export folder failed");
		record["status"] = "running";
		store.Save(record);
		Wait(store);
		store.Load(record["id"], false);
		Wait(store);
		Require(store.Snapshot().first->at("status") == "interrupted", "partial run not identified");
		fail = true;
		record["status"] = "cancelled";
		store.Save(record);
		Wait(store);
		Require(store.Snapshot().unsaved == 1 && !store.Snapshot().error.empty(), "failed save lost");
		fail = false;
		store.Retry();
		Wait(store);
		Require(store.Snapshot().unsaved == 0, "retry failed");
		store.Load("../escape", false);
		Wait(store);
		Require(!store.Snapshot().error.empty(), "load path traversal");
		hold = true;
		entered = false;
		store.Save(record);
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
		const bool reserved = store.TryPause();
		hold = false;
		Require(!reserved, "measurement started during disk write");
		Wait(store);
	}
	{
		Store store(root, writer, [](const auto&) { return std::filesystem::path{}; });
		Wait(store);
		store.Refresh();
		Wait(store);
		Require(store.Snapshot().history->size() == 1 && store.Snapshot().history->at(0)["status"] == "cancelled", "history did not survive restart");
		store.Export(record["id"], "", "html");
		Wait(store);
		Require(store.Snapshot().message == "Export cancelled.", "picker cancellation mishandled");
		std::ifstream session(root / "CSX-Performance.log");
		std::string bytes((std::istreambuf_iterator<char>(session)), {});
		Require(bytes.find("cancelled") == std::string::npos, "session log not reset");
	}
	std::cout << "Performance history, exports, exclusion and failure recovery passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
