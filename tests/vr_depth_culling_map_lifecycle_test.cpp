#include "Features/VRDepthCullingSubmissionPolicy.h"
#include "Features/VRDepthCullingTemporal.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::fprintf(stderr, "%s\n", a_message);
			std::exit(EXIT_FAILURE);
		}
	}
}

namespace RE
{
	enum class BSEventNotifyControl
	{
		kContinue
	};
	template <class T>
	struct BSTEventSource
	{};
	template <class T>
	struct BSTEventSink
	{
		virtual ~BSTEventSink() = default;
		virtual BSEventNotifyControl ProcessEvent(const T*, BSTEventSource<T>*) = 0;
	};
	struct MenuOpenCloseEvent
	{
		std::string_view menuName;
		bool opening;
	};
	struct MapMenu
	{
		static constexpr std::string_view MENU_NAME = "MapMenu";
	};
	struct BSImagespaceShader
	{
		std::array<std::byte, 0xE0> bytes;
	};
	struct BSSceneGraph
	{};
	struct Camera
	{
		struct
		{
			int rotate = 1, translate = 2;
		} world;
	} camera;
	struct Main
	{
		static Camera* WorldRootCamera() { return &camera; }
	};
	struct UI
	{
		unsigned subscriptions = 0;
		BSTEventSink<MenuOpenCloseEvent>* sink = nullptr;
		void AddEventSink(BSTEventSink<MenuOpenCloseEvent>* a_sink)
		{
			++subscriptions;
			sink = a_sink;
		}
	};
}

namespace REL
{
	inline unsigned runtimeVersion = 1415;
	struct Module
	{
		static Module& get()
		{
			static Module module;
			return module;
		}
		unsigned version() const { return runtimeVersion; }
	};
	template <class T>
	using Relocation = std::conditional_t<std::is_function_v<T>, T*, T>;
	inline bool depthAvailable = true;
	const bool* Offset(std::uintptr_t) { return &depthAvailable; }
}

namespace SKSE
{
	inline constexpr unsigned RUNTIME_VR_1_4_15 = 1415;
}

namespace logger
{
	unsigned errors = 0;
	void error(const char*) { ++errors; }
}

namespace globals
{
	struct State
	{
		std::uint32_t frameCount = 10;
	} testState;
	State* state = &testState;
	namespace game
	{
		bool isVR = true;
		RE::UI* ui = nullptr;
	}
}

namespace VRDepthCullingTemporal
{
	namespace
	{
		namespace SubmissionPolicy = VRDepthCullingSubmissionPolicy;
		using SubmissionPolicy::Reason;
		SubmissionPolicy::Handoff g_handoff;
		SubmissionPolicy::ReadbackProjection g_pendingReadbackProjection;
		struct DepthEvidence
		{
			Reason reason = Reason::MissingDepth;
			std::uintptr_t camera = 1;
			std::array<SubmissionPolicy::Frustum, 2> readbackFrusta{};
		} g_preparedDepth, g_producerDepth;
		struct Pose
		{
			int rotation = 0, translation = 0;
			bool valid = false;
		} g_producerPose;
		int g_depthResources = 0;
		bool g_depthPrepared = false;
		std::atomic_bool g_installed{ true }, g_cullingEnabled{ true };
		std::atomic<Mode> g_mode{ Mode::Balanced };
		std::atomic_uint64_t g_cullingEpoch{ 1 }, g_policyEpoch{ 2 }, g_producerPoseEpoch{ 0 };
		unsigned nativeRenders = 0, nativeReadbacks = 0, nativeDownscales = 0, validRecoveries = 0;
		std::function<void()> duringRender, duringReadback, duringDownscale, duringEvidence;
		std::byte* nativeCuller = nullptr;
		bool nativeLayoutSupported = true;
		std::byte** ResolveNativeMapCuller() { return nativeLayoutSupported ? &nativeCuller : nullptr; }

		SubmissionPolicy::Batch ReadBatch(void* a_shader) { return { reinterpret_cast<std::uintptr_t>(a_shader), 1, 2, 1, 0 }; }
		bool IsDepthPreparationCurrent() { return true; }
		SubmissionPolicy::ReadbackProjection CaptureReadbackProjection(std::uint64_t a_cullingEpoch, std::uint64_t a_policyEpoch)
		{
			const SubmissionPolicy::Frustum frustum{ { -1, 1, 1, -1, 1, 1000 }, false };
			return { { frustum, frustum }, 1, a_cullingEpoch, a_policyEpoch };
		}
		void CaptureDepthEvidence(DepthEvidence& a_evidence)
		{
			a_evidence = {};
			a_evidence.reason = Reason::None;
			if (duringEvidence)
				duringEvidence();
		}
		void RecoverHighRiskObjects(const SubmissionPolicy::Batch& a_batch, const SubmissionPolicy::Handoff::Consumed& a_consumed,
			const SubmissionPolicy::ReadbackProjection&)
		{
			const auto epoch = g_cullingEpoch.load();
			if (g_producerPoseEpoch.load() == epoch &&
				SubmissionPolicy::Match(a_consumed.stamp, a_batch, a_consumed.sequence, epoch, g_policyEpoch.load()) == Reason::None &&
				SubmissionPolicy::CanRecover(g_producerDepth.reason))
				++validRecoveries;
		}
	}
}

#include "vr_depth_culling_map_under_test.h"

namespace
{
	using namespace VRDepthCullingTemporal;

	void Event(bool a_open, std::string_view a_name = RE::MapMenu::MENU_NAME)
	{
		const RE::MenuOpenCloseEvent event{ a_name, a_open };
		MapMenuEvents sink{ &nativeCuller };
		Require(sink.ProcessEvent(&event, nullptr) == RE::BSEventNotifyControl::kContinue,
			"Map event handler stopped UI event delivery");
	}

	RE::BSImagespaceShader Shader()
	{
		RE::BSImagespaceShader shader;
		shader.bytes.fill(std::byte{ 0x5A });
		shader.bytes[0xC9] = std::byte{ 1 };
		return shader;
	}

	void WarmProducer()
	{
		g_depthPrepared = true;
		g_preparedDepth.reason = Reason::None;
		g_producerPose.valid = true;
		g_producerPoseEpoch.store(g_cullingEpoch.load());
	}

	void Reset(Mode a_mode)
	{
		InvalidateDepthGeneration();
		g_cullingEnabled.store(true);
		g_mode.store(a_mode);
		g_cullingEpoch.store(1);
		g_policyEpoch.store(2);
		nativeCuller = nullptr;
		nativeRenders = nativeReadbacks = nativeDownscales = validRecoveries = 0;
		duringRender = duringReadback = duringDownscale = duringEvidence = {};
		DepthCullingRender::func = [](RE::BSImagespaceShader*, std::uint32_t) {
			++nativeRenders;
			if (duringRender)
				duringRender();
		};
		DepthCullingReadback::func = [](void*) {
			++nativeReadbacks;
			if (duringReadback)
				duringReadback();
		};
		DepthCullingDownscale::func = [](RE::BSSceneGraph*) {
			++nativeDownscales;
			if (duringDownscale)
				duringDownscale();
		};
	}

	void TestImmediateRepair(Mode a_mode)
	{
		Reset(a_mode);
		auto shader = Shader();
		nativeCuller = shader.bytes.data();
		const auto original = shader.bytes;
		WarmProducer();
		g_depthResources = 42;
		const auto epoch = g_cullingEpoch.load();
		Event(true);
		Require(shader.bytes == original && g_cullingEpoch.load() != epoch && g_producerPoseEpoch.load() == 0,
			"Opening the map changed suppression or retained a valid producer epoch");
		Event(false);
		auto expected = original;
		expected[kMapSuppressionOffset] = std::byte{ 0 };
		Require(shader.bytes == expected && nativeRenders == 0 && nativeReadbacks == 0,
			"Map close did not immediately repair exactly the suppression byte");
		Require(g_depthResources == 42, "Map transition discarded unchanged graphics resources");
		g_cullingEnabled.store(false);
		shader.bytes = original;
		Event(false);
		Require(shader.bytes == expected && !g_cullingEnabled.load(),
			"Suppression repair changed location-specific culling enablement");
		auto replacement = Shader();
		shader.bytes = original;
		nativeCuller = replacement.bytes.data();
		Event(false);
		Require(shader.bytes == original && replacement.bytes == expected,
			"Map repair cached an obsolete culler object");
		nativeCuller = nullptr;
		WarmProducer();
		Event(false);
		Require(g_producerPoseEpoch.load() == 0, "Missing native culler prevented temporal invalidation");
	}

	void TestEpochBoundaries()
	{
		Reset(Mode::Balanced);
		auto shader = Shader();
		nativeCuller = shader.bytes.data();
		WarmProducer();
		DepthCullingRender::thunk(&shader, 0);
		Event(true);
		Event(false);
		DepthCullingReadback::thunk(&shader);
		Require(nativeReadbacks == 1 && validRecoveries == 0, "A map cycle reused pre-map recovery evidence");
		WarmProducer();
		duringRender = [] { Event(true); Event(false); };
		DepthCullingRender::thunk(&shader, 0);
		Require(!g_handoff.Consume().stamp.produced, "An in-flight producer crossed a map boundary");
		duringRender = {};
		duringReadback = [] { Event(true); Event(false); };
		DepthCullingReadback::thunk(&shader);
		Require(g_pendingReadbackProjection.camera == 0, "Readback retained camera evidence across a map boundary");
		duringReadback = {};
		DepthCullingReadback::thunk(&shader);
		DepthCullingDownscale::thunk(nullptr);
		DepthCullingRender::thunk(&shader, 0);
		DepthCullingReadback::thunk(&shader);
		Require(validRecoveries == 1, "Advanced culling did not resume with fresh world evidence");
	}

	void TestPreparationBoundaries(bool a_duringEvidence)
	{
		Reset(Mode::Balanced);
		auto shader = Shader();
		nativeCuller = shader.bytes.data();
		DepthCullingReadback::thunk(&shader);
		auto& transition = a_duringEvidence ? duringEvidence : duringDownscale;
		transition = [] { Event(true); Event(false); };
		DepthCullingDownscale::thunk(nullptr);
		Require(nativeDownscales == 1 && g_producerPoseEpoch.load() == 0,
			"Depth preparation relabeled pre-map evidence with a post-map producer epoch");
		DepthCullingRender::thunk(&shader, 0);
		Require(!g_handoff.Consume().stamp.produced, "Depth preparation spanning a map boundary published a producer");
	}

	void TestRegistrationAndUnrelatedEvents()
	{
		Reset(Mode::Balanced);
		WarmProducer();
		const auto epoch = g_cullingEpoch.load();
		Event(false, "Loading Menu");
		MapMenuEvents sink{ &nativeCuller };
		sink.ProcessEvent(nullptr, nullptr);
		Require(g_cullingEpoch.load() == epoch && g_producerPoseEpoch.load() == epoch,
			"Unrelated or null menu event invalidated culling state");
		RE::UI ui;
		globals::game::ui = &ui;
		globals::game::isVR = false;
		RegisterMapEvents();
		globals::game::isVR = true;
		REL::runtimeVersion = 0;
		RegisterMapEvents();
		Require(ui.subscriptions == 0, "Registration ignored the supported runtime gate");
		REL::runtimeVersion = SKSE::RUNTIME_VR_1_4_15;
		globals::game::ui = nullptr;
		RegisterMapEvents();
		globals::game::ui = &ui;
		nativeLayoutSupported = false;
		RegisterMapEvents();
		Require(ui.subscriptions == 0 && logger::errors == 2, "Missing UI or unrecognized native layout did not fail closed");
		nativeLayoutSupported = true;
		g_installed.store(false);
		RegisterMapEvents();
		RegisterMapEvents();
		Require(ui.subscriptions == 1 && ui.sink, "Map repair depended on temporal hooks or duplicated registration");
		g_installed.store(true);
		auto shader = Shader();
		nativeCuller = shader.bytes.data();
		const RE::MenuOpenCloseEvent close{ RE::MapMenu::MENU_NAME, false };
		ui.sink->ProcessEvent(&close, nullptr);
		Require(shader.bytes[kMapSuppressionOffset] == std::byte{ 0 }, "Registered sink did not restore native culling");
		globals::game::ui = nullptr;
	}
}

int main()
{
	for (const auto mode : { Mode::Legacy, Mode::Balanced }) TestImmediateRepair(mode);
	TestEpochBoundaries();
	TestPreparationBoundaries(false);
	TestPreparationBoundaries(true);
	TestRegistrationAndUnrelatedEvents();
	std::puts("Passed 6 map-close and culling epoch integration cases");
}
