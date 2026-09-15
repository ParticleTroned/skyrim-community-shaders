#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <utility>

namespace CSX::Screenshot
{
	struct ManifestChildNode
	{
		std::shared_ptr<const ManifestChildNode> previous;
		nlohmann::json child = nlohmann::json::object();
		bool fallbacksPresent = false;
	};

	/** Release a snapshot on its worker without recursively destroying its chain. */
	inline void ReleaseManifestChildren(std::shared_ptr<const ManifestChildNode>& a_children)
	{
		while (a_children) {
			auto previous = a_children->previous;
			a_children.reset();
			a_children = std::move(previous);
		}
	}
}
