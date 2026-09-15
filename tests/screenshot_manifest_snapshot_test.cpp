#include "Features/ScreenshotManifestSnapshot.h"

#include <stdexcept>

int main()
{
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
