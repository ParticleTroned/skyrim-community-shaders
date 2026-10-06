#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
#include "vr_map_event_state_under_test.h"
	static_assert(std::atomic<VRMapMenuEventState>::is_always_lock_free);
	std::atomic_uint64_t g_neuralMenuQueryEpoch{ 1 };
	bool explicitMenu = true;
	bool presentationTail = false;
	bool csMenu = false;
	uint32_t menuQueries = 0;
	uint32_t tailQueries = 0;
	uint32_t g_vrMenuSemanticEpochDepth = 0;
	uint32_t g_vrMenuBridgeDirectDrawDepth = 0;
	const void* higherCallContext = nullptr;
	const void* GetCurrentVRMenuBridgeHigherCallContext() { return higherCallContext; }
	bool IsCommunityShadersMenuOpen() { return csMenu; }
	bool ShouldEmitUpscalingDiagLogs() { return false; }
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
}
namespace RE
{
	struct MapMenu
	{
		static constexpr auto MENU_NAME = "MapMenu";
	};
}
namespace globals
{
	struct State
	{
		bool isMapMenuOpen = false;
	} stateValue;
	State* state = &stateValue;
	namespace game
	{
		bool isVR = true;
		const void* ui = nullptr;
	}
}
namespace
{
#include "vr_map_context_under_test.h"
	std::atomic_bool g_vrStatsMenuOpenFromEvent{ false };
	std::atomic_bool g_vrDialogueMenuOpenFromEvent{ false };
	std::atomic_bool g_vrRaceSexMenuOpenFromEvent{ false };
	bool IsMainMenuContextActive() { return false; }
	bool IsLoadingMenuContextActive() { return false; }
	bool IsSkyrimMenuPresentationContextActive(const void*) { return explicitMenu; }
#include "vr_known_menu_context_under_test.h"
#include "vr_non_loading_menu_context_under_test.h"
	bool IsExplicitVRMenuPresentationContextActive()
	{
		++menuQueries;
		return globals::game::isVR && IsKnownGameMenuContextActive();
	}
	bool IsVRMenuPresentationTailActive(const globals::State*)
	{
		++tailQueries;
		return presentationTail;
	}
	[[maybe_unused]] bool IsVRMenuPresentationContextActive()
	{
		return IsExplicitVRMenuPresentationContextActive() ||
		       (globals::game::isVR && IsVRMenuPresentationTailActive(globals::state));
	}
}
namespace logger
{
	template <class... Args>
	void debug(const char*, const Args&...)
	{}
}

struct Upscaling
{
	static constexpr size_t kVRMenuTransactionMaxEpochs = 32;
#include "vr_menu_transaction_under_test.h"
	struct Texture
	{
		bool srv = true;
	};
	VRMenuFrameTransaction vrMenuFrameTransaction;
	std::unique_ptr<Texture> vrMenuFinalCompositeLayer = std::make_unique<Texture>();
	std::unique_ptr<Texture> vrMenuCommittedCompositeLayer = std::make_unique<Texture>();
	uint32_t vrMenuFinalCompositeFrame = UINT32_MAX;
	uint32_t vrMenuFinalCompositeLayerDrawCount = 0;
	uint32_t vrMenuDrawInterfaceDepth = 0;
	bool vrMenuParallelBridgeDrawInProgress = false;
	bool vrMenuCommittedLayerValid = false;
	bool vrMenuCommittedLayerOpaque = false;
	uint32_t vrMenuCommittedLayerFrame = UINT32_MAX;
	uint32_t vrMenuCommittedLayerOperationCount = 0;
	uint32_t vrMenuCommittedLayerPlanGeneration = 0;
	uint64_t vrMenuCommittedLayerGeneration = 0;
	uint32_t planGeneration = 1;
	std::atomic<uint64_t> vrMenuPresentationContextChangeSequence{ 0 };
	uint64_t vrMenuPresentationContextChangeConsumedSequence = 0;
	bool vrMapMenuUISupersamplingActive = false;
	uint64_t vrMenuDesktopPairGeneration = 0;
	uint32_t vrMenuDesktopPairFrame = UINT32_MAX;
	uint32_t vrMenuDesktopPairPlanGeneration = 0;
	uint32_t vrMenuDesktopPairReadyMask = 0;
	bool vrMenuDesktopPairPendingPresent = false;
	uint32_t vrMenuDesktopRetainedPairPlanGeneration = 0;
	bool vrMenuDesktopRetainedPairValid = false;

	uint32_t GetActiveVRRenderScaleContractGeneration() const { return planGeneration; }
	bool IsVRMapMenuPresentationActive() const;
	void ReleaseVRMapMenuUISupersampling() { vrMapMenuUISupersamplingActive = false; }
	void PoisonVRMenuFrameTransaction(const char*) { vrMenuFrameTransaction.poisoned = true; }
	void BeginVRMenuFinalCompositeFrame(uint32_t);
	void ResetVRMenuDesktopEyePairState();
	void InvalidateVRMenuCommittedLayer(const char*);
	void NotifyVRMenuPresentationContextChange(const char*);
	void ConsumeVRMenuPresentationContextChange(uint32_t);
	bool SealVRMenuFrameTransaction(uint32_t);
};

#include "vr_map_presentation_context_under_test.h"
#include "vr_menu_begin_frame_under_test.h"
#include "vr_menu_layer_lifetime_under_test.h"

namespace
{
	void PrepareLayer(Upscaling& a_upscaling, uint32_t a_frame)
	{
		a_upscaling.BeginVRMenuFinalCompositeFrame(a_frame);
		auto& transaction = a_upscaling.vrMenuFrameTransaction;
		transaction.recognizedOperations = 3;
		transaction.capturedOperations = 3;
		transaction.suppressedOperations = 3;
		transaction.renderComplete = true;
		a_upscaling.vrMenuFinalCompositeLayerDrawCount = 3;
	}

	void PublishLayer(Upscaling& a_upscaling, uint32_t a_frame)
	{
		PrepareLayer(a_upscaling, a_frame);
		Require(a_upscaling.SealVRMenuFrameTransaction(a_frame), "complete layer did not publish");
		a_upscaling.vrMenuDesktopPairPendingPresent = true;
		a_upscaling.vrMenuDesktopRetainedPairValid = true;
	}

	void TestCloseFrameRecapture()
	{
		Upscaling upscaling;
		explicitMenu = true;
		presentationTail = true;
		PublishLayer(upscaling, 100);
		upscaling.NotifyVRMenuPresentationContextChange("map-close");
		// The event precedes refresh of State::isMapMenuOpen. A final native
		// consumer can therefore publish again after the close invalidation.
		PublishLayer(upscaling, 101);
		Require(upscaling.vrMenuPresentationContextChangeConsumedSequence == 1, "close event was not consumed");
		explicitMenu = false;
		for (uint32_t frame = 102; frame < 132; ++frame) {
			upscaling.BeginVRMenuFinalCompositeFrame(frame);
			Require(!upscaling.vrMenuCommittedLayerValid, "closed map layer survived in the presentation tail");
			Require(!upscaling.vrMenuDesktopPairPendingPresent && !upscaling.vrMenuDesktopRetainedPairValid,
				"desktop retained closed map content");
		}
		Require(presentationTail, "menu pixel retirement changed the presentation route tail");
	}

	void TestStereoCloseBoundary()
	{
		Upscaling upscaling;
		explicitMenu = true;
		PublishLayer(upscaling, 200);
		upscaling.vrMenuFrameTransaction.presentationDecisionLatched = true;
		upscaling.vrMenuFrameTransaction.presentationStarted = true;
		explicitMenu = false;
		upscaling.NotifyVRMenuPresentationContextChange("map-close-between-eyes");
		upscaling.BeginVRMenuFinalCompositeFrame(200);
		Require(upscaling.vrMenuCommittedLayerValid, "right eye lost the left eye's layer");
		Require(upscaling.vrMenuPresentationContextChangeConsumedSequence == 0, "close was consumed between eyes");
		upscaling.BeginVRMenuFinalCompositeFrame(201);
		Require(!upscaling.vrMenuCommittedLayerValid, "closed stereo layer survived the next frame");
		Require(!upscaling.vrMenuFrameTransaction.poisoned, "next clean world frame was poisoned");
	}

	void TestPublishedMapCloseOverridesCachedOpen()
	{
		Upscaling upscaling;
		explicitMenu = false;
		presentationTail = true;
		globals::state->isMapMenuOpen = true;
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Open;
		PublishLayer(upscaling, 250);
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Closed;
		upscaling.NotifyVRMenuPresentationContextChange("map-close-before-cache-refresh");
		Require(!IsKnownGameMenuContextActive() && !IsNonLoadingVRGameMenuPresentationContextActive() &&
					!upscaling.IsVRMapMenuPresentationActive(),
			"published map close was overridden by cached open state");
		upscaling.BeginVRMenuFinalCompositeFrame(251);
		Require(!upscaling.vrMenuCommittedLayerValid, "cached map state kept the closed layer alive");
		Require(!upscaling.vrMenuFrameTransaction.poisoned, "close poisoned a new world transaction");
		globals::state->isMapMenuOpen = false;
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Open;
		Require(IsKnownGameMenuContextActive() && upscaling.IsVRMapMenuPresentationActive(),
			"reopen waited for the cached menu state");
		PublishLayer(upscaling, 252);
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Unknown;
		Require(!IsMapMenuContextActive(), "unknown map state ignored cached close");
		globals::state->isMapMenuOpen = true;
		Require(IsMapMenuContextActive(), "unknown map state ignored cached open");
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Closed;
		globals::game::isVR = false;
		Require(IsMapMenuContextActive(), "VR events changed non-VR menu state");
		Require(!upscaling.IsVRMapMenuPresentationActive(), "non-VR state admitted a VR map");
		globals::state->isMapMenuOpen = false;
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Open;
		Require(IsMapMenuContextActive(), "non-VR lost its event-open fallback");
		globals::game::isVR = true;
		globals::state->isMapMenuOpen = false;
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Unknown;
	}

	void TestInitialMapStatePublication()
	{
		struct UI
		{
			bool sampledOpen;
			VRMapMenuEventState eventDuringSample;
			bool IsMenuOpen(const char*) const
			{
				if (eventDuringSample != VRMapMenuEventState::Unknown)
					g_vrMapMenuStateFromEvent.store(eventDuringSample, std::memory_order_release);
				return sampledOpen;
			}
		};
		constexpr VRMapMenuEventState states[]{ VRMapMenuEventState::Unknown, VRMapMenuEventState::Closed, VRMapMenuEventState::Open };
		for (auto before : states) {
			for (bool sampledOpen : { false, true }) {
				for (auto during : states) {
					g_vrMapMenuStateFromEvent = before;
					UI sample{ sampledOpen, during };
					auto* ui = &sample;
#include "vr_map_initial_state_under_test.h"
					const auto expected = during != VRMapMenuEventState::Unknown ? during :
					                      before != VRMapMenuEventState::Unknown ? before :
					                      sampledOpen                            ? VRMapMenuEventState::Open :
					                                                               VRMapMenuEventState::Closed;
					Require(g_vrMapMenuStateFromEvent.load() == expected,
						"initial UI sample replaced an authoritative map event");
				}
			}
		}
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Unknown;
	}

	void TestMapContextWithoutState()
	{
		auto* savedState = globals::state;
		globals::state = nullptr;
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			g_vrMapMenuStateFromEvent = VRMapMenuEventState::Unknown;
			Require(!IsMapMenuContextActive(), "missing state admitted an unknown map");
			g_vrMapMenuStateFromEvent = VRMapMenuEventState::Closed;
			Require(!IsMapMenuContextActive(), "missing state reopened a closed map");
			g_vrMapMenuStateFromEvent = VRMapMenuEventState::Open;
			Require(IsMapMenuContextActive(), "missing state discarded a published open event");
		}
		globals::game::isVR = true;
		globals::state = savedState;
		g_vrMapMenuStateFromEvent = VRMapMenuEventState::Unknown;
	}

	void TestMissingCloseAndReopen()
	{
		Upscaling upscaling;
		explicitMenu = true;
		PublishLayer(upscaling, 300);
		explicitMenu = false;
		upscaling.BeginVRMenuFinalCompositeFrame(301);
		Require(!upscaling.vrMenuCommittedLayerValid, "missing close event retained stale pixels");
		explicitMenu = true;
		upscaling.NotifyVRMenuPresentationContextChange("map-reopen");
		PublishLayer(upscaling, 302);
		Require(upscaling.vrMenuCommittedLayerFrame == 302 && upscaling.vrMenuCommittedLayerGeneration == 2,
			"reopened map did not publish fresh content");
	}

	void TestOpenMenuFallbackAndOtherInvalidations()
	{
		Upscaling upscaling;
		explicitMenu = true;
		presentationTail = false;
		PublishLayer(upscaling, 400);
		upscaling.BeginVRMenuFinalCompositeFrame(401);
		Require(upscaling.vrMenuCommittedLayerValid, "open menu lost its complete fallback layer");
		upscaling.vrMenuFrameTransaction.poisoned = true;
		upscaling.BeginVRMenuFinalCompositeFrame(401);
		Require(upscaling.vrMenuCommittedLayerValid, "failed open-menu transaction lost its fallback");
		csMenu = true;
		upscaling.BeginVRMenuFinalCompositeFrame(402);
		Require(!upscaling.vrMenuCommittedLayerValid, "CSX menu did not retire the game layer");
		csMenu = false;
		PublishLayer(upscaling, 403);
		++upscaling.planGeneration;
		upscaling.BeginVRMenuFinalCompositeFrame(404);
		Require(!upscaling.vrMenuCommittedLayerValid, "changed resolution plan retained an incompatible layer");
	}

	void TestContextQueryBudget()
	{
		Upscaling upscaling;
		explicitMenu = true;
		presentationTail = true;
		PublishLayer(upscaling, 500);
		menuQueries = tailQueries = 0;
		upscaling.BeginVRMenuFinalCompositeFrame(501);
		Require(menuQueries == 1 && tailQueries == 0, "open menu repeated context queries");
		explicitMenu = false;
		menuQueries = tailQueries = 0;
		upscaling.BeginVRMenuFinalCompositeFrame(502);
		Require(menuQueries == 1 && tailQueries == 0, "closing menu queried an unused presentation tail");
		menuQueries = tailQueries = 0;
		upscaling.BeginVRMenuFinalCompositeFrame(503);
		Require(menuQueries == 0 && tailQueries == 0, "empty world frame queried menu context");
		upscaling.vrMenuFrameTransaction.presentationDecisionLatched = true;
		menuQueries = tailQueries = 0;
		upscaling.BeginVRMenuFinalCompositeFrame(503);
		Require(menuQueries == 0 && tailQueries == 0, "latched stereo decision queried mutable menu state");
	}

	void TestPlanChangeBetweenEyes()
	{
		Upscaling upscaling;
		explicitMenu = true;
		PublishLayer(upscaling, 600);
		upscaling.vrMenuFrameTransaction.presentationDecisionLatched = true;
		upscaling.vrMenuFrameTransaction.presentationStarted = true;
		++upscaling.planGeneration;
		upscaling.BeginVRMenuFinalCompositeFrame(600);
		Require(!upscaling.vrMenuCommittedLayerValid && upscaling.vrMenuFrameTransaction.poisoned,
			"resolution change between eyes bypassed the existing fail-open path");
	}

	void TestUnsealedWorkWithoutRetainedLayer()
	{
		for (bool tailActive : { false, true }) {
			for (uint32_t work = 0; work != 5; ++work) {
				Upscaling upscaling;
				explicitMenu = false;
				presentationTail = tailActive;
				upscaling.BeginVRMenuFinalCompositeFrame(650);
				auto& transaction = upscaling.vrMenuFrameTransaction;
				switch (work) {
				case 0:
					transaction.recognizedOperations = 1;
					break;
				case 1:
					transaction.capturedOperations = 1;
					break;
				case 2:
					transaction.suppressedOperations = 1;
					break;
				case 3:
					transaction.mapDisplayEpochs = 1;
					break;
				case 4:
					transaction.presentationStarted = true;
					break;
				}
				menuQueries = tailQueries = 0;
				upscaling.BeginVRMenuFinalCompositeFrame(650);
				Require(transaction.poisoned == !tailActive && menuQueries == 1 && tailQueries == 1,
					"unsealed work bypassed context validation without a retained layer");
				upscaling.NotifyVRMenuPresentationContextChange("map-close-with-unsealed-work");
				upscaling.BeginVRMenuFinalCompositeFrame(650);
				Require(transaction.poisoned, "presentation tail concealed an explicit context change");
			}
		}
	}

	void TestFailedCaptureRetirement()
	{
		for (uint32_t failure = 0; failure != 7; ++failure) {
			Upscaling upscaling;
			explicitMenu = true;
			presentationTail = true;
			PublishLayer(upscaling, 700);
			PrepareLayer(upscaling, 701);
			auto& transaction = upscaling.vrMenuFrameTransaction;
			switch (failure) {
			case 0:
				transaction.renderComplete = false;
				break;
			case 1:
				transaction.drawInterfaceDepth = 1;
				break;
			case 2:
				transaction.capturedOperations = 2;
				break;
			case 3:
				transaction.mapLayerRequired = true;
				break;
			case 4:
				upscaling.vrMenuFinalCompositeLayer->srv = false;
				break;
			case 5:
				upscaling.vrMenuFinalCompositeLayer.reset();
				break;
			case 6:
				transaction.planGeneration = 0;
				break;
			}
			Require(!upscaling.SealVRMenuFrameTransaction(701), "incomplete capture was published");
			Require(upscaling.vrMenuCommittedLayerValid && upscaling.vrMenuCommittedLayerFrame == 700,
				"failed capture discarded the open menu's complete fallback");
			explicitMenu = false;
			upscaling.BeginVRMenuFinalCompositeFrame(702);
			Require(!upscaling.vrMenuCommittedLayerValid && !upscaling.vrMenuDesktopRetainedPairValid,
				"failed capture kept a closed menu's fallback alive");
		}
	}

	void TestMenuReplacementAndNonVR()
	{
		Upscaling upscaling;
		explicitMenu = true;
		PublishLayer(upscaling, 800);
		upscaling.NotifyVRMenuPresentationContextChange("map-to-inventory");
		upscaling.BeginVRMenuFinalCompositeFrame(801);
		Require(!upscaling.vrMenuCommittedLayerValid, "replacement menu inherited the previous layer");
		PublishLayer(upscaling, 801);
		Require(upscaling.vrMenuCommittedLayerGeneration == 2, "replacement menu did not publish");
		globals::game::isVR = false;
		menuQueries = tailQueries = 0;
		upscaling.BeginVRMenuFinalCompositeFrame(802);
		Require(!upscaling.vrMenuCommittedLayerValid && tailQueries == 0,
			"non-VR state acquired VR tail ownership");
		globals::game::isVR = true;
	}
}

int main()
try {
	TestCloseFrameRecapture();
	TestStereoCloseBoundary();
	TestPublishedMapCloseOverridesCachedOpen();
	TestInitialMapStatePublication();
	TestMapContextWithoutState();
	TestMissingCloseAndReopen();
	TestOpenMenuFallbackAndOtherInvalidations();
	TestContextQueryBudget();
	TestPlanChangeBetweenEyes();
	TestUnsealedWorkWithoutRetainedLayer();
	TestFailedCaptureRetirement();
	TestMenuReplacementAndNonVR();
	std::cout << "VR menu layer lifetime: close-frame recapture, stereo close, missing event, reopen, "
				 "desktop retirement, open-menu fallback, query budget and plan changes passed\n";
	return 0;
} catch (const std::exception& e) {
	std::cerr << e.what() << '\n';
	return 1;
}
