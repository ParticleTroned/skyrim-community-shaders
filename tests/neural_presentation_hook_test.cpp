#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RE
{
	struct UI
	{};
	struct BSFixedString
	{};
	namespace RENDER_TARGETS
	{
		constexpr uint32_t kVR_FRAMEBUFFER = 114;
	}
}
namespace REL
{
	template <class>
	struct Relocation;
	template <class R, class... Args>
	struct Relocation<R(Args...)>
	{
		R (*callback)(Args...) = nullptr;
		R operator()(Args... args) const { return callback(args...); }
	};
}
template <class F>
struct ScopeExit
{
	F callback;
	~ScopeExit() { callback(); }
};
std::vector<std::string> events;
namespace globals::features
{
	struct Upscaling
	{
		bool ready = false, committed = false, repair = false, succeed = true;
		uint32_t evaluations = 0;
		void ApplyMainFinalLdrNeuralStereo()
		{
			if (!std::exchange(ready, false))
				return;
			++evaluations;
			events.emplace_back("NR");
			repair = committed = succeed;
		}
		void FinalizeMainFinalLdrNeuralPresentation()
		{
			if (std::exchange(repair, false))
				events.emplace_back("mask");
		}
	} upscaling;
}
#include "neural_presentation_hook_under_test.h"

RE::UI ui;
RE::BSFixedString menu;
uint32_t expectedTarget = RE::RENDER_TARGETS::kVR_FRAMEBUFFER;
bool throwOverlay = false;
void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
void DrawOverlay(RE::UI* actualUI, uint32_t target, const RE::BSFixedString& actualMenu, uint32_t width, uint32_t height)
{
	Require(actualUI == &ui && &actualMenu == &menu && target == expectedTarget && width == 4032 && height == 2240,
		"All engine arguments must pass through unchanged");
	events.emplace_back("overlay");
	if (throwOverlay)
		throw std::runtime_error("overlay failure");
}
int main()
{
	try {
		VRFinalLdrPresentationHook::func.callback = DrawOverlay;
		auto& nr = globals::features::upscaling;
		for (bool success : { false, true }) {
			nr = {};
			nr.ready = true;
			nr.succeed = success;
			events.clear();
			VRFinalLdrPresentationHook::thunk(&ui, expectedTarget, menu, 4032, 2240);
			events.emplace_back("HMD submit");
			Require(nr.evaluations == 1 && nr.committed == success, "HMD must receive this frame's transaction outcome");
			const auto expected = success ? std::vector<std::string>{ "NR", "overlay", "mask", "HMD submit" } :
			                                std::vector<std::string>{ "NR", "overlay", "HMD submit" };
			Require(events == expected, "NR must precede overlay and HMD submission; repair must follow overlay");
			VRFinalLdrPresentationHook::thunk(&ui, expectedTarget, menu, 4032, 2240);
			Require(nr.evaluations == 1, "Repeated presentation must not evaluate framebuffer feedback");
		}
		nr = {};
		nr.ready = true;
		expectedTarget = 0;
		events.clear();
		VRFinalLdrPresentationHook::thunk(&ui, expectedTarget, menu, 4032, 2240);
		Require(nr.ready && nr.evaluations == 0 && events == std::vector<std::string>{ "overlay" },
			"Other menu targets must not consume the headset transaction");
		expectedTarget = RE::RENDER_TARGETS::kVR_FRAMEBUFFER;
		throwOverlay = true;
		events.clear();
		try {
			VRFinalLdrPresentationHook::thunk(&ui, expectedTarget, menu, 4032, 2240);
			throw std::logic_error("Expected original overlay exception");
		} catch (const std::runtime_error& error) {
			Require(std::string(error.what()) == "overlay failure", "Original failure must propagate");
		}
		Require(!nr.repair && events == std::vector<std::string>{ "NR", "overlay", "mask" },
			"Presentation cleanup must remain deterministic on unwinding");
		std::cout << "Native HMD presentation ordering, fallback, forwarding and single consumption passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
