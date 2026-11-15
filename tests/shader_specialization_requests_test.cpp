#include "Utils/ShaderSpecializationRequests.h"

#include <cassert>
#include <iostream>

int main()
{
	using namespace Util;
	ShaderSpecializationRequests history;
	assert(history.Add({ SpecializationFamily::Lighting, 0x00200101 }));
	assert(!history.Add({ SpecializationFamily::Lighting, 0x00200101 }));
	assert(history.Add({ SpecializationFamily::Effect, 0x10000001 }));
	for (uint32_t descriptor : { 1u, 2u, 3u, 5u, 6u, 7u })
		assert(history.Add({ SpecializationFamily::Composite, descriptor }));
	assert(!history.Add({ SpecializationFamily::Composite, 0 }));
	assert(!history.Add({ SpecializationFamily::Composite, 4 }));
	assert(!history.Add({ SpecializationFamily::Composite, 8 }));
	assert(!history.Add({ static_cast<SpecializationFamily>(3), 1 }));
	ShaderSpecializationRequests nextLaunch;
	nextLaunch.Load(history.Serialize("VR"), "VR");
	assert(nextLaunch.Snapshot() == history.Snapshot());
	const auto before = nextLaunch.Snapshot();
	const auto reject = [&](std::string_view text, std::string_view runtime = "VR") {
		bool rejected = false;
		try {
			nextLaunch.Load(text, runtime);
		} catch (const std::exception&) {
			rejected = true;
		}
		assert(rejected && nextLaunch.Snapshot() == before);
	};
	reject(history.Serialize("SE"));
	reject(R"({"version":2,"runtime":"VR","requests":[]})");
	reject(R"({"version":4294967297,"runtime":"VR","requests":[]})");
	reject(R"({"version":1.5,"runtime":"VR","requests":[]})");
	reject(std::string(1000, '[') + std::string(1000, ']'));
	reject(R"({"version":1,"runtime":"VR","requests":[[0,-1]]})");
	reject(R"({"version":1,"runtime":"VR","requests":[[0,4294967296]]})");
	reject(R"({"version":1,"runtime":"VR","requests":[[3,1]]})");
	reject(R"({"version":1,"runtime":"VR","requests":[[2,4]]})");
	reject(R"({"version":1,"runtime":"VR","requests":[["Lighting","../../shader"]]})");
	reject("{truncated");
	reject(std::string(ShaderSpecializationRequests::kMaximumFileBytes + 1, ' '));
	ShaderSpecializationRequests bounded;
	for (uint32_t i = 0; i < ShaderSpecializationRequests::kMaximumRequests; ++i)
		assert(bounded.Add({ SpecializationFamily::Lighting, i }));
	assert(!bounded.Add({ SpecializationFamily::Lighting, 0xffffffff }));
	assert(bounded.Size() == ShaderSpecializationRequests::kMaximumRequests);
	nextLaunch.Load(bounded.Serialize("SE"), "SE");
	assert(nextLaunch.Snapshot() == bounded.Snapshot());
	auto overflow = nlohmann::json::parse(bounded.Serialize("VR"));
	overflow["requests"].push_back({ 0u, 0xffffffffu });
	bool rejected = false;
	try {
		nextLaunch.Load(overflow.dump(), "VR");
	} catch (const std::exception&) {
		rejected = true;
	}
	assert(rejected && nextLaunch.Snapshot() == bounded.Snapshot());
	std::cout << "Specialization history: restart replay, runtime separation, atomic rejection and bounds passed\n";
}
