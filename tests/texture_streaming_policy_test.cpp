#include "Features/TextureStreaming/ConsumerInventory.h"
#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/Policy.h"
#include "Features/TextureStreaming/Settings.h"
#include "Utils/GpuMemoryBudget.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace P = TextureStreamingPolicy;
using Memory = Util::GpuMemoryBudget;
void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}
namespace
{
	struct OwnedConsumer
	{
		std::shared_ptr<int> owner;
		explicit OwnedConsumer(const std::shared_ptr<int>& value) : owner(value) {}
		OwnedConsumer(OwnedConsumer&&) = default;
		OwnedConsumer& operator=(OwnedConsumer&&) = default;
		OwnedConsumer(const OwnedConsumer&) = delete;
		OwnedConsumer& operator=(const OwnedConsumer&) = delete;
	};

	void CheckInventoryRetirement()
	{
		StreamingTextures::ConsumerInventory<OwnedConsumer> inventory;
		const auto committed = std::make_shared<int>(1);
		const auto scanning = std::make_shared<int>(2);
		Require(!inventory.Current(0) && !inventory.Observe(1, 0, 0), "Unknown generation activated an inventory");
		Require(inventory.Observe(1, 0, 1), "Fresh inventory failed to activate");
		inventory.scanning.emplace_back(committed);
		inventory.scanning.emplace_back(committed);
		inventory.Promote(1, 1);
		Require(inventory.Observe(2, 1, 1), "Current generation rejected a rescan");
		inventory.scanning.emplace_back(scanning);
		inventory.scanning.emplace_back(scanning);
		Require(committed.use_count() == 3 && scanning.use_count() == 3, "Inventory lost owning consumer references");

		inventory.Promote(2, 2);
		Require(!inventory.Current(2) && inventory.Expired(2, 2) && inventory.verified == 1 && inventory.committed.size() == 2,
			"Invalidated consumers remained current or were promoted");
		for (const auto generation : { 2u, 3u, 4u })
			Require(!inventory.Observe(3, 2, generation) && inventory.generation == 1 && committed.use_count() == 3 && scanning.use_count() == 3,
				"Repeated invalidation activated or discarded retained consumers");

		Require(inventory.RetireOne() && committed.use_count() == 3 && scanning.use_count() == 2 && inventory.verified == 1,
			"Retirement released more than one scanning reference");
		Require(!inventory.Observe(3, 0, 4), "A partially retired inventory activated");
		Require(inventory.RetireOne() && committed.use_count() == 3 && scanning.use_count() == 1,
			"Retirement did not finish scanning references first");
		Require(inventory.RetireOne() && committed.use_count() == 2 && !inventory.Empty(),
			"Retirement released more than one committed reference");
		Require(!inventory.Observe(3, 0, 4), "An inventory with one stale consumer activated");
		Require(inventory.RetireOne() && committed.use_count() == 1 && inventory.Empty() && !inventory.seen && !inventory.verified && inventory.generation == 1,
			"Final retirement retained scan metadata or activated a new generation");
		Require(!inventory.RetireOne() && inventory.generation == 1, "Empty retirement authorized a new generation");

		Require(inventory.Observe(3, 0, 4) && inventory.Current(4), "Retired inventory failed to activate the latest generation");
		inventory.scanning.emplace_back(scanning);
		inventory.Promote(3, 1);
		Require(!inventory.verified && inventory.committed.empty() && scanning.use_count() == 2, "An old generation promoted fresh consumers");
		inventory.Promote(3, 4);
		Require(inventory.verified == 3 && inventory.committed.size() == 1, "A complete fresh inventory failed to publish");
		inventory.Clear();
		Require(inventory.Empty() && !inventory.generation && scanning.use_count() == 1, "Explicit clear retained consumers or generation identity");
	}
}
int main()
{
	try {
		for (const auto& value : { nlohmann::json(1), nlohmann::json(2u), nlohmann::json(3) })
			Require(StreamingTextures::ParseMaximumMipDrop(value) == value.get<std::uint32_t>(), "Valid mip limit rejected");
		for (const auto& value : { nlohmann::json(1.5), nlohmann::json(2.0), nlohmann::json(-1), nlohmann::json(0),
				 nlohmann::json(4), nlohmann::json(UINT64_MAX), nlohmann::json(true), nlohmann::json("2"), nlohmann::json(nullptr) }) {
			bool rejected = false;
			try {
				(void)StreamingTextures::ParseMaximumMipDrop(value);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Malformed mip limit escaped boundary validation");
		}
		for (unsigned selected = 0; selected < 32; ++selected) {
			StreamingTextures::Categories categories{ (selected & 1u) != 0, (selected & 2u) != 0, (selected & 4u) != 0, (selected & 8u) != 0, (selected & 16u) != 0 };
			for (unsigned required = 0; required < 32; ++required)
				Require(categories.Allows(required) == ((required & ~selected) == 0), "A shared consumer escaped category consent");
			Require(!categories.Allows(32u), "Unknown category was admitted");
			Require(StreamingTextures::ParseCategories(StreamingTextures::SerializeCategories(categories)) == categories, "Category settings lost a selection");
		}
		Require(StreamingTextures::ParseCategories(nlohmann::json::object()) == StreamingTextures::Categories{ false, false, false, false, false }, "An empty replacement retained default coverage");
		Require(StreamingTextures::ParseCategories({ { "emissiveStatics", true } }) == StreamingTextures::Categories{ false, false, true, false, false }, "Independent category selection enabled another category");
		for (const auto& value : { nlohmann::json(nullptr), nlohmann::json(true), nlohmann::json::array(),
				 nlohmann::json{ { "water", true } }, nlohmann::json{ { "alphaTestedStatics", 1 } },
				 nlohmann::json{ { "landscapeStatics", "true" } }, nlohmann::json{ { "emissiveStatics", nullptr } } }) {
			bool rejected = false;
			try {
				(void)StreamingTextures::ParseCategories(value);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Malformed or unknown category settings were accepted");
		}
		Require(StreamingTextures::PathCategories("textures\\architecture\\wall.dds") == 0u, "Ordinary texture became optional");
		Require(StreamingTextures::PathCategories("textures\\landscape\\rocks\\rock.dds") == StreamingTextures::LandscapeStatics, "Static landscape texture lost its category");
		for (const auto* path : { "textures\\terrain\\tamriel\\tamriel.4.0.0.dds", "textures\\landscape\\water\\water.dds",
				 "textures\\water\\water.dds", "textures\\lod\\water.dds", "textures\\dyndolod\\atlas.dds",
				 "textures\\architecture\\wall_lod.dds", "textures\\trees\\tamriel.treelod.dds", "textures\\trees\\tamriel.objects_n.dds",
				 "textures\\actors\\character\\face.dds", "textures\\effects\\fire.dds", "textures\\cubemaps\\map.dds",
				 "textures\\interface\\icon.dds", "textures\\sky\\cloud.dds", "textures\\..\\wall.dds",
				 "textures\\wall.png", "textures\\d:wall.dds", "architecture\\wall.dds" })
			Require(!StreamingTextures::PathCategories(path), "Protected water, LOD or special texture path was admitted");
		std::array<P::Eye, 2> eyes{ P::Eye{ 2000, 2000, 2000, 2000, 1, 1, 1000 }, P::Eye{ 2000, 2000, 2000, 2000, 1, 1, 500 } };
		const auto native = P::RequiredEdge(eyes, 2, 256, 0);
		Require(native == 1024, "Demand must use the closer eye");
		Require(P::DesiredDrop(4096, 13, native, 3) == 2, "Mip selection lost required stereo detail");
		for (auto scale : { 1.0, 0.8, 0.5, 0.333 }) {
			auto dlss = eyes;
			for (auto& eye : dlss) {
				eye.renderWidth *= scale;
				eye.renderHeight *= scale;
			}
			const double demand = P::RequiredEdge(dlss, 2, 256, std::log2(scale) - 1.0);
			Require(std::abs(demand - native * 2) < 1e-8, "DLSS finer mip request was lost at lower rendering resolution");
		}
		eyes[1].renderHeight = 4000;
		Require(P::RequiredEdge(eyes, 2, 256, 0) == native * 2, "Demand ignored vertical rendering extent");
		eyes[1].distanceToBound = 0;
		Require(P::DesiredDrop(4096, 13, P::RequiredEdge(eyes, 2, 256, 0), 3) == 0, "Invalid camera or near bounds must preserve full detail");
		Require(P::DesiredDrop(1024, 11, 1, 999) == 2, "Minimum edge or drop limit escaped validation");
		Require(P::DesiredDrop(8192, 14, 1, 3) == 3, "Maximum mip drop not enforced");
		StreamingTextures::ConsumerInventory<int> inventory;
		Require(inventory.Observe(1, 0, 1), "First scan could not activate");
		inventory.scanning.push_back(10);
		Require(!inventory.Expired(0, 1), "Unfinished first scan was pruned");
		inventory.Promote(0, 1);
		Require(inventory.committed.empty(), "Partial inventory was published");
		Require(inventory.Observe(2, 1, 1), "Current inventory rejected its next scan");
		Require(inventory.verified == 1 && inventory.committed == std::vector<int>{ 10 }, "Unvisited committed consumers were lost at the next scan");
		inventory.scanning.push_back(20);
		inventory.Promote(1, 1);
		Require(inventory.committed == std::vector<int>{ 10 } && !inventory.Expired(1, 1), "In-progress rescan replaced or pruned committed consumers");
		inventory.Promote(2, 1);
		Require(inventory.verified == 2 && inventory.committed == std::vector<int>{ 20 }, "Complete inventory was not published");
		Require(inventory.Expired(3, 1), "A texture absent from the completed scan was retained");
		Require(inventory.Observe(4, 3, 1), "Current inventory rejected an observation");
		inventory.scanning.push_back(40);
		Require(inventory.Observe(5, 3, 1), "Current inventory rejected an abandoned-scan restart");
		Require(inventory.scanning.empty() && inventory.verified == 2, "An abandoned scan became committed");
		inventory.Clear();
		Require(inventory.committed.empty() && inventory.scanning.empty() && !inventory.verified && !inventory.seen, "World invalidation retained consumers");
		Require(inventory.Expired(0, 1), "Invalidated full-size inventory pinned textures through loading");
		CheckInventoryRetirement();
		Require(P::CappedDrop(8192, 256, 14, 1024) == 3, "Size cap was overridden by the distance detail floor");
		Require(P::CappedDrop(4096, 4096, 13, 0) == 0, "Unlimited retained a size cap");
		Require(P::CappedDrop(4096, 4096, 13, 8192) == 0, "Cap upscaled a smaller texture");
		for (const auto reserve : { 256u, 1024u, 8192u }) {
			StreamingTextures::Options options{ reserve, false, 1024 };
			Require(StreamingTextures::ParseOptions(StreamingTextures::SerializeOptions(options)) == options, "Residency options did not round-trip");
		}
		for (const auto bad : { -1, 0, 255, 257, 8193 }) {
			auto options = StreamingTextures::SerializeOptions({});
			options["reserveMiB"] = bad;
			bool rejected = false;
			try {
				(void)StreamingTextures::ParseOptions(options);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Require(rejected, "Invalid reserve escaped validation");
		}
		P::Pressure adjusted;
		Require(adjusted.Update(1, 1, true, 10000 * P::MiB, 8100 * P::MiB) == P::PressureChange::None, "Old percentage threshold overrode the configured reserve");
		Require(adjusted.Update(2, 2, true, 10000 * P::MiB, 8100 * P::MiB, 2048 * P::MiB) == P::PressureChange::Entered, "Configured reserve failed to trigger");
		Require(Memory::StreamingFits(10000 * P::MiB, 8000 * P::MiB, 0, 100 * P::MiB, true, true, false, 0, 512 * P::MiB), "Low configured reserve did not permit visible restoration");
		Require(!Memory::StreamingFits(10000 * P::MiB, 8000 * P::MiB, 0, 100 * P::MiB, true, true, false, 0, 2048 * P::MiB), "Refill crossed configured reserve");
		P::Pressure pressure;
		Require(pressure.Update(1000, 1000, true, 10000 * P::MiB, 9100 * P::MiB) == P::PressureChange::Entered, "Pressure entry event missing");
		for (unsigned frame = 0; frame < 1000; ++frame)
			Require(pressure.Update(1000, 1000, true, 10000 * P::MiB, 9100 * P::MiB) == P::PressureChange::None, "Repeated pressure sample emitted another event");
		Require(pressure.conserving, "High pressure did not request reclamation");
		for (std::uint64_t time = 1250; time < 6250; time += 250) {
			pressure.Update(time, time, true, 10000 * P::MiB, 6000 * P::MiB);
			Require(pressure.conserving, "Refill started before the healthy observation window");
		}
		Require(pressure.Update(6250, 6250, true, 10000 * P::MiB, 6000 * P::MiB) == P::PressureChange::Recovered, "Recovery event missing");
		Require(pressure.Update(6250, 6250, true, 10000 * P::MiB, 6000 * P::MiB) == P::PressureChange::None, "Repeated recovery emitted another event");
		Require(!pressure.conserving, "Sustained recovery never allowed refill");
		pressure.Update(6500, 6500, true, 10000 * P::MiB, 9100 * P::MiB);
		pressure.Update(7000, 7000, true, 10000 * P::MiB, 6000 * P::MiB);
		pressure.Update(13000, 13000, true, 10000 * P::MiB, 6000 * P::MiB);
		Require(pressure.conserving, "A missing observation window authorized refill");
		pressure.Update(13501, 13000, true, 10000 * P::MiB, 6000 * P::MiB);
		Require(pressure.healthySince == 0, "Stale budget observation retained a recovery window");
		constexpr auto m = P::MiB;
		Require(Memory::StreamingFits(10000 * m, 6000 * m, 0, 500 * m, true, false, false), "Healthy optional refill rejected");
		Require(!Memory::StreamingFits(10000 * m, 6000 * m, 1600 * m, 500 * m, true, false, false), "Outstanding rebuild allocation not included in peak");
		Require(!Memory::StreamingFits(10000 * m, 6000 * m, 0, 2100 * m, true, false, false), "Full replacement overlap was treated as net growth");
		Require(!Memory::StreamingFits(10000 * m, 3000 * m, 0, m, true, true, true), "Refill consumed transition headroom");
		Require(Memory::StreamingFits(10000 * m, 8000 * m, 0, m, false, false, true), "Reclamation could not proceed while high priority work waited");
		Require(Memory::StreamingFits(10000 * m, 9800 * m, 0, m, false, false, true), "Safe reclamation was blocked by critical pressure");
		Require(!Memory::StreamingFits(10000 * m, 9900 * m, 0, m, false, false, false), "Reclamation consumed its overlap safety headroom");
		Require(Memory::StreamingFits(10000 * m, 7500 * m, 0, 100 * m, true, true, false), "Required detail could not recover under moderate pressure");
		Require(!Memory::StreamingFits(10000 * m, 6000 * m, 0, 500 * m, true, false, false, 1600 * m), "Optional refill consumed future NR recovery headroom");
		Require(Memory::StreamingFits(10000 * m, 7500 * m, 0, 100 * m, true, true, false, 4000 * m), "Waiting NR starved required visible detail");
		Require(Memory::StreamingFits(10000 * m, 9800 * m, 0, m, false, false, false, UINT64_MAX), "Future NR demand blocked safe reclamation");
		Require(!Memory::StreamingFits(10000 * m, 7500 * m, 1600 * m, 100 * m, true, true, false, 0), "Required detail ignored an actual outstanding allocation");
		Require(!Memory::StreamingFits(10000 * m, 0, 0, m, true, false, false, UINT64_MAX), "Recovery demand overflow admitted optional refill");
		StreamingTextures::PressureEpisode episode;
		Require(!episode.SummaryDue(20000), "Inactive pressure emitted diagnostics");
		episode.active = true;
		episode.startedAtMs = 1000;
		Require(!episode.SummaryDue(999) && !episode.SummaryDue(10999), "Pressure diagnostics reported before the observation window");
		Require(episode.SummaryDue(11000), "Sustained pressure produced no effectiveness summary");
		for (std::uint64_t now = 11000; now <= 120000; now += 10)
			Require(!episode.SummaryDue(now), "Sustained pressure produced frame-rate-dependent logging");
		episode = { true, false, false, 200000 };
		Require(episode.SummaryDue(210000), "A new pressure episode inherited the old reporting limit");
		Require(!Memory::StreamingFits(0, 0, 0, m, false, false, false), "Unknown budgets must fail closed");
		Require(!Memory::StreamingFits(UINT64_MAX, UINT64_MAX - 10, 0, 20, false, false, false), "Allocation projection overflow admitted a texture");
		std::cout << "PASS: stereo demand, DLSS bias, minimum detail, inventory publication, hysteresis, full overlap and priority admission\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
