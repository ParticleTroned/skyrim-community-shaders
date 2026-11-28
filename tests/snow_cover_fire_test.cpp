#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RE
{
	struct NiBound
	{
		struct Point
		{
			float x = 0, y = 0, z = 0;
			bool operator==(const Point&) const = default;
		} center;
		float radius = 1;
		bool operator==(const NiBound&) const = default;
	};
	struct BSGeometry;
	struct NiNode;
	struct NiAVObject
	{
		bool hidden = false;
		virtual ~NiAVObject() = default;
		bool GetAppCulled() const { return hidden; }
		virtual BSGeometry* AsGeometry() { return nullptr; }
		virtual NiNode* AsNode() { return nullptr; }
	};
	struct BSGeometry : NiAVObject
	{
		bool fire = false;
		NiBound worldBound;
		BSGeometry* AsGeometry() override { return this; }
	};
	struct NiNode : NiAVObject
	{
		std::vector<std::unique_ptr<NiAVObject>> children;
		NiNode* AsNode() override { return this; }
		auto& GetChildren() { return children; }
	};
}

namespace SnowEnvironment
{
	unsigned materialChecks = 0;
	bool IsFireGeometry(RE::BSGeometry* geometry)
	{
		++materialChecks;
		return geometry->fire;
	}
	// clang-format off
#include "snow_fire_geometry_under_test.h"
	// clang-format on
}

namespace
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Fire traversal changed classification, visible bounds or pruning");
	}

	void Reference(RE::NiAVObject* object, bool hidden, bool& hasFire, std::vector<RE::NiBound>& bounds)
	{
		if (!object)
			return;
		hidden = hidden || object->hidden;
		if (auto* geometry = object->AsGeometry()) {
			if (geometry->fire) {
				hasFire = true;
				const auto& bound = geometry->worldBound;
				if (!hidden && bound.radius > 0 && std::isfinite(bound.radius) && std::isfinite(bound.center.x) && std::isfinite(bound.center.y) && std::isfinite(bound.center.z))
					bounds.push_back(bound);
			}
		} else if (auto* node = object->AsNode()) {
			for (const auto& child : node->children)
				Reference(child.get(), hidden, hasFire, bounds);
		}
	}

	void Check(RE::NiAVObject* root)
	{
		std::vector<RE::NiBound> expected;
		bool expectedFire = false;
		Reference(root, false, expectedFire, expected);
		for (bool classify : { false, true }) {
			std::vector<RE::NiBound> actual;
			bool hasFire = false;
			SnowEnvironment::CollectFireGeometry(root, false, classify, hasFire, actual);
			Require(actual == expected);
			if (classify)
				Require(hasFire == expectedFire);
		}
	}
}

int main()
{
	Check(nullptr);
	RE::NiNode root;
	root.hidden = true;
	auto fire = std::make_unique<RE::BSGeometry>();
	fire->fire = true;
	root.children.push_back(std::move(fire));
	std::vector<RE::NiBound> bounds;
	bool hasFire = false;
	SnowEnvironment::CollectFireGeometry(&root, false, false, hasFire, bounds);
	Require(SnowEnvironment::materialChecks == 0 && bounds.empty());
	SnowEnvironment::CollectFireGeometry(&root, false, true, hasFire, bounds);
	Require(SnowEnvironment::materialChecks == 1 && hasFire && bounds.empty());
	std::mt19937 random(8219);
	for (unsigned trial = 0; trial < 1000; ++trial) {
		root.hidden = random() % 7 == 0;
		root.children.clear();
		for (unsigned branch = 0; branch < 8; ++branch) {
			auto node = std::make_unique<RE::NiNode>();
			node->hidden = random() % 3 == 0;
			node->children.push_back(nullptr);
			for (unsigned index = 0; index < 8; ++index) {
				auto geometry = std::make_unique<RE::BSGeometry>();
				geometry->fire = random() % 2 == 0;
				geometry->hidden = random() % 4 == 0;
				geometry->worldBound.center.x = float(index + branch * 8);
				if (random() % 13 == 0)
					geometry->worldBound.radius = 0;
				if (random() % 17 == 0)
					geometry->worldBound.center.z = std::numeric_limits<float>::infinity();
				if (random() % 19 == 0)
					geometry->worldBound.radius = std::numeric_limits<float>::quiet_NaN();
				node->children.push_back(std::move(geometry));
			}
			root.children.push_back(std::move(node));
		}
		Check(&root);
	}
}
