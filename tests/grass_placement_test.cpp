#include "Features/GrassPlacementPolicy.h"
#include "Utils/LocationLayers.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	using namespace GrassPlacement;
	Require(ModelKey("Meshes/Grass/AspenLeaves.NIF") == "grass\\aspenleaves.nif", "Mesh identity normalization");
	for (const auto* path : { "../leaf.nif", "c:\\leaf.nif", "\\leaf.nif", "grass//leaf.nif", "grass/./leaf.nif", "leaf.dds", "grass/leaf?.nif", "grass /leaf.nif", "grass./leaf.nif" })
		Require(ModelKey(path).empty(), "Unsafe model path accepted");
	Require(Clamp(std::numeric_limits<float>::infinity()) == 0 && Clamp(-100) == -32, "Invalid offset handling");
	Settings global{ -2, { { "grass\\leaf.nif", -3 } } };
	Layer location{ { -1, { { "grass\\leaf.nif", -4 } } } };
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
