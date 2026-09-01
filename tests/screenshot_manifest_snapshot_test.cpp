#include "Features/ScreenshotManifestSnapshot.h"
#include "Features/ScreenshotReferencePolicy.h"
#include "screenshot_burst_test.h"

#include <stdexcept>

int main()
{
	RunScreenshotBurstTests();
	using json = nlohmann::json;
	const auto referencePath = std::filesystem::current_path() / std::filesystem::path(L"golden-\u00e4\u773c.png");
	const json reference = { { "outputPath", Util::PathToUtf8(referencePath) }, { "requestId", "checkpoint-1" } };
	const auto vrReference = CSX::Screenshot::ReferenceCommand(reference, true);
	const auto flatReference = CSX::Screenshot::ReferenceCommand(reference, false);
	const auto encodedPath = vrReference.at("referenceOutputPath").get<std::string>();
	if (vrReference.at("capture").at("outputs").at(0).at("view") != "side_by_side" ||
		flatReference.at("capture").at("source").at("kind") != "desktop_mirror" ||
		vrReference.at("capture").at("source").at("fallback") != "reject" ||
		vrReference.at("capture").at("clipboard") != "none" ||
		std::filesystem::path(std::u8string(encodedPath.begin(), encodedPath.end())) != referencePath)
		throw std::runtime_error("reference capture lost native output or Unicode host path");
	for (const json invalid : {
			 json({ { "outputPath", "relative.png" }, { "requestId", "checkpoint" } }),
			 json({ { "outputPath", Util::PathToUtf8(std::filesystem::path(referencePath).replace_extension("bmp")) }, { "requestId", "checkpoint" } }),
			 json({ { "outputPath", reference.at("outputPath") }, { "requestId", "" } }),
			 json({ { "outputPath", reference.at("outputPath") }, { "requestId", std::string(129, 'a') } }),
			 json({ { "outputPath", reference.at("outputPath").get<std::string>() + std::string(1, '\0') }, { "requestId", "checkpoint" } }) }) {
		bool rejected = false;
		try {
			CSX::Screenshot::ReferenceCommand(invalid, true);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		if (!rejected)
			throw std::runtime_error("invalid reference capture was admitted");
	}
	json receipt = {
		{ "commandId", "host-reference" }, { "state", "completed" },
		{ "artifacts", json::array({ { { "committed", true }, { "path", reference.at("outputPath") },
						   { "bytes", 1234 }, { "sha256", "digest" }, { "actual", { { "width", 2048 }, { "height", 1024 } } } } }) }
	};
	const auto completion = CSX::Screenshot::ReferenceCompletion(receipt);
	if (!completion.at("ok").get<bool>() || completion.at("requestId") != "host-reference" ||
		completion.at("width") != 2048 || completion.at("height") != 1024 || completion.at("uiExcluded") != false)
		throw std::runtime_error("reference completion lost committed image identity");
	for (const char* state : { "failed", "cancelled", "failed_partial", "cancelled_partial", "encoding" }) {
		receipt["state"] = state;
		if (CSX::Screenshot::ReferenceCompletion(receipt).at("ok").get<bool>())
			throw std::runtime_error("incomplete reference was reported as successful");
	}
	receipt["state"] = "completed";
	receipt["artifacts"][0]["committed"] = false;
	if (CSX::Screenshot::ReferenceCompletion(receipt).at("ok").get<bool>())
		throw std::runtime_error("uncommitted reference was reported as successful");
	using namespace CSX::Screenshot;
	std::shared_ptr<const ManifestChildNode> children;
	std::shared_ptr<const ManifestChildNode> checkpoint;
	std::weak_ptr<const ManifestChildNode> first;
	for (int ordinal = 1; ordinal <= 10000; ++ordinal) {
		children = std::make_shared<ManifestChildNode>(ManifestChildNode{
			.previous = children,
			.child = { { "ordinal", ordinal }, { "artifacts", nlohmann::json::array() } },
			.fallbacksPresent = ordinal >= 123,
		});
		if (ordinal == 1)
			first = children;
		if (ordinal == 5000)
			checkpoint = children;
	}
	const std::weak_ptr<const ManifestChildNode> last = children;
	ReleaseManifestChildren(children);
	if (children || !last.expired() || first.expired())
		throw std::runtime_error("retirement must preserve a retained checkpoint only");
	int expected = 5000;
	for (auto child = checkpoint; child; child = child->previous, --expected) {
		if (child->child.at("ordinal") != expected || child->fallbacksPresent != (expected >= 123))
			throw std::runtime_error("retirement changed immutable checkpoint contents");
	}
	if (expected != 0)
		throw std::runtime_error("checkpoint lost children");
	ReleaseManifestChildren(checkpoint);
	ReleaseManifestChildren(checkpoint);
	if (!first.expired())
		throw std::runtime_error("retired checkpoint retained child allocations");

	for (int ordinal = 1; ordinal <= 10000; ++ordinal)
		children = std::make_shared<ManifestChildNode>(ManifestChildNode{ .previous = children });
	ReleaseManifestChildren(children);
	return 0;
}
