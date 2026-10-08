#include "Features/TextureStreaming/ConsumerInventory.h"
#include "Features/TextureStreaming/Policy.h"
#include "Features/TextureStreaming/Settings.h"
#include "Utils/GpuMemoryBudget.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace P = TextureStreamingPolicy;
using Memory = Util::GpuMemoryBudget;
void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
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
		inventory.Observe(1, 0);
		inventory.scanning.push_back(10);
		Require(!inventory.Expired(0), "Unfinished first scan was pruned");
		inventory.Promote(0);
		Require(inventory.committed.empty(), "Partial inventory was published");
		inventory.Observe(2, 1);
		Require(inventory.verified == 1 && inventory.committed == std::vector<int>{ 10 }, "Unvisited committed consumers were lost at the next scan");
		inventory.scanning.push_back(20);
		inventory.Promote(1);
		Require(inventory.committed == std::vector<int>{ 10 } && !inventory.Expired(1), "In-progress rescan replaced or pruned committed consumers");
		inventory.Promote(2);
		Require(inventory.verified == 2 && inventory.committed == std::vector<int>{ 20 }, "Complete inventory was not published");
		Require(inventory.Expired(3), "A texture absent from the completed scan was retained");
		inventory.Observe(4, 3);
		inventory.scanning.push_back(40);
		inventory.Observe(5, 3);
		Require(inventory.scanning.empty() && inventory.verified == 2, "An abandoned scan became committed");
		inventory.Clear();
		Require(inventory.committed.empty() && inventory.scanning.empty() && !inventory.verified && !inventory.seen, "World invalidation retained consumers");
		Require(inventory.Expired(0), "Invalidated full-size inventory pinned textures through loading");
		P::Pressure pressure;
		pressure.Update(1000, 1000, true, 10000 * P::MiB, 8100 * P::MiB);
		Require(pressure.conserving, "High pressure did not request reclamation");
		for (std::uint64_t time = 1250; time < 6250; time += 250) {
			pressure.Update(time, time, true, 10000 * P::MiB, 6000 * P::MiB);
			Require(pressure.conserving, "Refill started before the healthy observation window");
		}
		pressure.Update(6250, 6250, true, 10000 * P::MiB, 6000 * P::MiB);
		Require(!pressure.conserving, "Sustained recovery never allowed refill");
		pressure.Update(6500, 6500, true, 10000 * P::MiB, 8100 * P::MiB);
		pressure.Update(7000, 7000, true, 10000 * P::MiB, 6000 * P::MiB);
		pressure.Update(13000, 13000, true, 10000 * P::MiB, 6000 * P::MiB);
		Require(pressure.conserving, "A missing observation window authorized refill");
		pressure.Update(13501, 13000, true, 10000 * P::MiB, 6000 * P::MiB);
		Require(pressure.healthySince == 0, "Stale budget observation retained a recovery window");
		constexpr auto m = P::MiB;
		Require(Memory::StreamingFits(10000 * m, 6000 * m, 0, 500 * m, true, false, false), "Healthy optional refill rejected");
		Require(!Memory::StreamingFits(10000 * m, 6000 * m, 600 * m, 500 * m, true, false, false), "Outstanding rebuild allocation not included in peak");
		Require(!Memory::StreamingFits(10000 * m, 6000 * m, 0, 1100 * m, true, false, false), "Full replacement overlap was treated as net growth");
		Require(!Memory::StreamingFits(10000 * m, 3000 * m, 0, m, true, true, true), "Refill consumed transition headroom");
		Require(Memory::StreamingFits(10000 * m, 8000 * m, 0, m, false, false, true), "Reclamation could not proceed while high priority work waited");
		Require(Memory::StreamingFits(10000 * m, 9800 * m, 0, m, false, false, true), "Safe reclamation was blocked by critical pressure");
		Require(!Memory::StreamingFits(10000 * m, 9900 * m, 0, m, false, false, false), "Reclamation consumed its overlap safety headroom");
		Require(Memory::StreamingFits(10000 * m, 7500 * m, 0, 100 * m, true, true, false), "Required detail could not recover under moderate pressure");
		Require(!Memory::StreamingFits(0, 0, 0, m, false, false, false), "Unknown budgets must fail closed");
		Require(!Memory::StreamingFits(UINT64_MAX, UINT64_MAX - 10, 0, 20, false, false, false), "Allocation projection overflow admitted a texture");
		std::cout << "PASS: stereo demand, DLSS bias, minimum detail, inventory publication, hysteresis, full overlap and priority admission\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
