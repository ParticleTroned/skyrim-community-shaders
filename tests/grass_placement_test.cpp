#include "Features/GrassPlacementPolicy.h"
#include "Utils/LocationLayers.h"
#include <array>
#include <atomic>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

namespace PlacementRuntimeTest
{
	using namespace GrassPlacement;
	using VertexBuffer = int;
	struct Shape
	{
		struct Bound
		{
			float radius = 10;
		} worldBound;
	};
	struct ShapeState
	{
		bool translationOnly = true;
		std::string model;
		Envelope bound;
		std::vector<VertexBuffer*> buffers;
	};
	struct GroupState
	{
		Shape* owner = nullptr;
		History history;
		std::optional<float> offset;
		std::optional<CellKey> sourceCell, cell;
	};
#include "grass_placement_cell_profile.h"
	struct Runtime
	{
		std::mutex mutex;
		std::atomic_bool tracking{ true }, failed{ false };
		bool enabled = true;
		uint32_t frame = 1, interior = 0;
		float padding = 0;
		struct Configuration
		{
			Settings global;
			std::map<std::string, Layer> rules;
			uint64_t revision = 0;
			bool active = false;
		} configuration;
		std::map<Shape*, ShapeState> shapes;
		std::map<VertexBuffer*, GroupState> groups;
		std::map<CellKey, CellProfile> cells;
		std::vector<const Layer*> interiorLayers;
	};
	using Configuration = Runtime::Configuration;
	constexpr size_t kMaximumCells = 16384;
	constexpr size_t kMaximumShapes = 4096;
	namespace globals::features
	{
		struct Balance
		{
			struct Profile
			{
				Settings grassPlacement;
			};
			struct Rule
			{
				std::string key;
				Profile profile;
				bool layered = true;
			};
			struct Configuration
			{
				Profile globalProfile;
				std::vector<Rule> locationOverrides;
			} settings;
			uint64_t revision = 1;
			uint64_t GetGrassPlacementRevision() const { return revision; }
		} adaptiveBrightness;
	}
	Runtime& Get()
	{
		static Runtime runtime;
		return runtime;
	}
	bool IsAvailable() { return !Get().failed; }
	void Fail(const char*, const std::exception&) { Get().failed = true; }
#include "grass_placement_cells.h"
#include "grass_placement_invalidation.h"

#include "grass_placement_configuration.h"
#include "grass_placement_model.h"
#include "grass_placement_offsets.h"
	void CheckCachedPlacement()
	{
		auto& runtime = Get();
		VertexBuffer buffer{};
		auto& group = runtime.groups[&buffer];
		Shape shape;
		group.owner = &shape;
		runtime.shapes[&shape].model = "grass\\leaf.nif";
		runtime.configuration.global = { -2.f, { { "grass\\leaf.nif", -3.f } } };
		Require(GetOffsets(&buffer) == std::array{ -5.f, -5.f }, "First placement resolution failed");
		Require(group.offset == -5, "Resolved placement was not retained");
		runtime.configuration.global.verticalOffset = -6;
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ -5.f, -5.f }, "Unpublished settings bypassed cached placement");
		InvalidateOffsets(runtime);
		Require(GetOffsets(&buffer) == std::array{ -5.f, -5.f }, "Invalidation changed placement inside a frame");
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ -9.f, -5.f }, "Publication lost the previous rendered offset");
		runtime.enabled = false;
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ 0.f, -9.f }, "Disable did not retire the visible offset");
		runtime.enabled = true;
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ -9.f, 0.f }, "Reenable lost motion history");
		runtime.configuration.rules["cell"] = { { 4, {} } };
		group.sourceCell = CellKey{ 1, 2 };
		AssignCell(runtime, group);
		auto& profile = runtime.cells[*group.cell];
		Require(!profile.SetLayers({}), "An unruled cell must not invalidate resolved placement");
		Require(profile.SetLayers({ &runtime.configuration.rules.at("cell") }), "New cell rules must invalidate resolved placement");
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ -5.f, -9.f }, "Cell reassignment reused the old offset");
		AssignCell(runtime, group);
		Require(group.offset == -5, "Unchanged cell unnecessarily discarded cached placement");
		Require(!profile.SetLayers({ &runtime.configuration.rules.at("cell") }), "Unchanged cell layers unnecessarily invalidated resolved placement");
		Require(profile.SetLayers({}), "Removing cell layers must invalidate resolved placement");
		InvalidateOffsets(runtime);
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ -9.f, -5.f }, "Removing cell rules retained the old placement");
		group.sourceCell.reset();
		AssignCell(runtime, group);
		runtime.frame += 2;
		Require(GetOffsets(&buffer) == std::array{ -9.f, -9.f }, "Cell removal or an invisible-frame gap retained stale placement");
		runtime.configuration.global = {};
		InvalidateOffsets(runtime);
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ 0.f, -9.f } && group.offset.has_value(), "Zero offsets must be cacheable");
		runtime.failed = true;
		Require(GetOffsets(&buffer) == std::array{ 0.f, 0.f }, "Failed tracking must return neutral offsets");
	}

	void CheckPublicationInvalidation()
	{
		auto& runtime = Get();
		runtime.failed = false;
		runtime.groups.clear();
		runtime.shapes.clear();
		runtime.cells.clear();
		runtime.configuration = {};
		runtime.frame = 100;
		auto& balance = globals::features::adaptiveBrightness;
		balance.settings.globalProfile.grassPlacement = { 1.f, { { "grass\\a.nif", 2.f }, { "grass\\b.nif", 5.f } } };
		Require(PublishConfiguration(runtime), "Initial placement configuration was not published");
		Shape shape;
		VertexBuffer buffer{};
		auto& group = runtime.groups[&buffer];
		group.owner = &shape;
		runtime.shapes[&shape].buffers.push_back(&buffer);
		RecordModel(&shape, "Meshes/Grass/A.nif");
		Require(GetOffsets(&buffer) == std::array{ 3.f, 3.f }, "Published model correction was not resolved");
		RecordModel(&shape, "grass\\a.NIF");
		Require(group.offset == 3 && !PublishConfiguration(runtime), "Unchanged model or configuration discarded cached placement");
		RecordModel(&shape, "grass/b.nif");
		Require(!group.offset && GetOffsets(&buffer) == std::array{ 3.f, 3.f }, "Model changes must invalidate targets without changing the current frame");
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ 6.f, 3.f }, "Model replacement retained stale placement");
		balance.settings.globalProfile.grassPlacement.verticalOffset = -1;
		++balance.revision;
		Require(PublishConfiguration(runtime) && !group.offset, "Configuration publication did not invalidate cached targets");
		Require(GetOffsets(&buffer) == std::array{ 6.f, 3.f }, "Publication changed an already rendered frame");
		++runtime.frame;
		Require(GetOffsets(&buffer) == std::array{ 4.f, 6.f }, "Published configuration did not retain the previous offset");
		runtime.groups.clear();
		runtime.shapes.clear();
	}
}

int main()
{
	using namespace GrassPlacement;
	PlacementRuntimeTest::CheckCachedPlacement();
	PlacementRuntimeTest::CheckPublicationInvalidation();
	Require(ModelKey("Meshes/Grass/AspenLeaves.NIF") == "grass\\aspenleaves.nif", "Mesh identity normalization");
	for (const auto* path : { "../leaf.nif", "c:\\leaf.nif", "\\leaf.nif", "grass//leaf.nif", "grass/./leaf.nif", "leaf.dds", "grass/leaf?.nif", "grass /leaf.nif", "grass./leaf.nif" })
		Require(ModelKey(path).empty(), "Unsafe model path accepted");
	Require(Clamp(std::numeric_limits<float>::infinity()) == 0 && Clamp(-100) == -32, "Invalid offset handling");
	Settings global{ -2, { { "grass\\leaf.nif", -3.0f } } };
	Layer location{ { -1, { { "grass\\leaf.nif", -4.0f } } } };
	std::array<const Layer*, 1> locationLayers{ &location };
	Require(Resolve(global, locationLayers, "grass\\leaf.nif") == -10 && Resolve(global, locationLayers, "grass\\tall.nif") == -3,
		"Mesh corrections affected unrelated grass");
	Layer broad{ { 32, {} } }, specific{ { -32, {} } };
	std::array<const Layer*, 2> additive{ &broad, &specific };
	Require(Resolve({ 32, {} }, additive, "") == 32, "An intermediate clamp changed the final sum");
	Require(Resolve({ -30, {} }, std::array<const Layer*, 1>{ &specific }, "") == -32, "Effective offset exceeded bounds");
	specific.layered = false;
	Require(Resolve(global, additive, "grass\\leaf.nif") == -32, "Replacement layer retained broader location changes");
	specific.settings.verticalOffset = 2;
	Require(Resolve(global, additive, "grass\\leaf.nif") == -3, "Replacement layer discarded the global mesh correction");
	Envelope envelope;
	float bound = 10;
	envelope.Apply(bound, 32);
	envelope.Apply(bound, 32);
	Require(bound == 42, "Repeated observation accumulated native bounds padding");
	envelope.Restore(bound);
	bound = 20;
	envelope.Apply(bound, 32);
	Require(bound == 52, "Native bound recomputation was lost");
	envelope.Restore(bound);
	Require(bound == 20, "Disabling placement did not restore native bounds");
	bound = 52;
	envelope.Apply(bound, 32);
	Require(bound == 84, "Native recomputation equal to a prior padded value lost its envelope");
	envelope.Apply(bound, 0);
	Require(bound == 52, "Retiring the envelope failed to restore the native size");
	Require(SourceCell(-.5f, -4096.f) == CellKey{ -1, -1 } && SourceCell(4096.f, 0) == CellKey{ 1, 0 }, "Negative or boundary cell ownership is wrong");
	Require(!SourceCell(NAN, 0) && !SourceCell(INFINITY, 0) && !SourceCell(float(INT32_MAX), 0), "Unrepresentable source cell was accepted");
	Require(nlohmann::json::object().get<Settings>() == Settings{}, "Older presets changed placement");
	Require(nlohmann::json(global).get<Settings>() == global, "Placement persistence lost values");
	std::string error;
	Require(!Validate({ { "verticalOffset", 33 } }, error), "Out-of-range mutation accepted");
	Require(!Validate({ { "unknown", 0 } }, error), "Unknown field accepted");
	Require(!Validate({ { "modelOffsets", { { "grass/leaf.nif", -2 }, { "GRASS\\LEAF.NIF", -3 } } } }, error), "Duplicate normalized model accepted");
	Require(Validate({ { "verticalOffset", 0 }, { "modelOffsets", nlohmann::json::object() } }, error), "Reset mutation rejected");
	History history;
	history.Update(20, -4);
	Require(history.current == -4 && history.previous == -4, "New geometry inherited unrelated motion");
	history.Update(21, -8);
	history.Update(21, -12);
	Require(history.current == -8 && history.previous == -4, "Passes disagreed on placement history");
	history.Update(23, -10);
	Require(history.current == history.previous, "Invisible-frame gap retained stale motion");
	history.Update(24, 0);
	Require(history.previous == -10 && history.current == 0, "Disabling placement lost final motion");
	struct World
	{
		int id;
		const World* parentWorld = nullptr;
	} outer{ 1 }, inner{ 2, &outer };
	struct Location
	{
		int id;
		const Location* parentLoc = nullptr;
	} hold{ 3 }, town{ 4, &hold };
	struct Cell
	{
		int id;
	} cell{ 5 };
	std::vector<int> order;
	Util::VisitLocationLayers(&inner, &town, &cell, [&](const auto* value) { order.push_back(value->id); });
	Require(order == std::vector<int>({ 1, 2, 3, 4, 5 }), "Location inheritance order changed");
	outer.parentWorld = &inner;
	order.clear();
	Util::VisitLocationLayers(&inner, &town, &cell, [&](const auto* value) { order.push_back(value->id); });
	Require(order.size() == 5, "Cyclic location ancestry repeated layers");
	std::cout << "Grass placement settings, location order and temporal history passed\n";
}
