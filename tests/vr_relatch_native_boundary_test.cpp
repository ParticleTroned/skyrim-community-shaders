#include "Features/VR/VRRenderScaleFrameBoundaryPolicy.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <mutex>
#include <stdexcept>

namespace vr
{
	enum EVRCompositorError
	{
		None = 0
	};
	enum EVRSubmitFlags
	{
		Default = 0
	};
	struct Texture_t
	{
		void* handle = nullptr;
	};
	struct VRTextureBounds_t
	{};
}
namespace RE
{
	struct BSOpenVR
	{};
}
namespace REL
{
	template <class T>
	struct Relocation;
	template <class R, class... Args>
	struct Relocation<R(Args...)>
	{
		R (*callback)(Args...) = nullptr;
		R operator()(Args... a_args) const { return callback(a_args...); }
	};
}
namespace SKSE::stl
{
	template <class Callback>
	struct scope_exit
	{
		Callback callback;
		~scope_exit() { callback(); }
	};
}
namespace VRSubmitInputFreshnessPolicy
{
	uintptr_t CaptureSubmitTextureIdentity(const vr::Texture_t* a_texture)
	{
		return reinterpret_cast<uintptr_t>(a_texture->handle);
	}
}
struct NativeBoundary
{
	uint64_t token = 0, compositorCycle = 0;
	uint32_t frame = 0, thread = 0, flags = 0;
	uintptr_t source = 0;
	bool active = false;
} g_vrSubmitPairBoundaryState;
VRRenderScaleFrameBoundaryPolicy::PairCompletion g_vrRelatchPairCompletion;
std::recursive_mutex g_vrRenderScalePresentationWorkMutex;
std::atomic<uint64_t> g_vrSubmitPairBoundarySequence{ 0 }, g_openVRSubmitCycleState{ 20 };
uint32_t observedThread = 1, nativeDepth = 0, returnedEyes = 0;
uint32_t GetCurrentThreadId() { return observedThread; }
namespace globals
{
	struct State
	{
		uint32_t frameCount = 100;
	} currentState;
	State* state = &currentState;
	namespace features
	{
		struct Upscaling
		{
			uint32_t calls = 0;
			void ServiceVRRenderScaleRelatchAtFrameBoundary()
			{
				if (nativeDepth != 0 || returnedEyes < 2 || g_vrSubmitPairBoundaryState.active || g_vrRelatchPairCompletion.identity.token != 0)
					throw std::runtime_error("Relatch ran before native/eye return or before boundary ownership was cleared");
				++calls;
			}
		} upscaling;
	}
}

#include "vr_relatch_native_boundary_under_test.h"

namespace
{
	enum class Scenario
	{
		Complete,
		Missing,
		Duplicate,
		InvalidEye,
		NestedOnly,
		NestedThenComplete,
		ChangedFrame,
		ChangedThread,
		ChangedCycle,
		ThrowingNative
	};
	Scenario scenario = Scenario::Complete;
	vr::Texture_t texture;
	void ReturnedEye(uint32_t eEye)
	{
#include "vr_relatch_native_eye_completion_under_test.h"
		++returnedEyes;
	}

	vr::EVRCompositorError NativeSubmit(RE::BSOpenVR*, const vr::Texture_t*, const vr::VRTextureBounds_t*, vr::EVRSubmitFlags)
	{
		++nativeDepth;
		const SKSE::stl::scope_exit leaveNative([&]() { --nativeDepth; });
		if (scenario == Scenario::ThrowingNative)
			throw std::runtime_error("injected native failure");
		if (nativeDepth == 1 && (scenario == Scenario::NestedOnly || scenario == Scenario::NestedThenComplete)) {
			(void)BSOpenVR_Submit::thunk(nullptr, &texture, nullptr, vr::Default);
			if (globals::features::upscaling.calls != 0)
				throw std::runtime_error("Nested submit independently serviced the outer relatch");
			if (scenario == Scenario::NestedOnly)
				return vr::None;
		}
		ReturnedEye(0);
		if (scenario == Scenario::Duplicate)
			ReturnedEye(0);
		if (scenario == Scenario::InvalidEye)
			ReturnedEye(2);
		if (scenario != Scenario::Missing)
			ReturnedEye(1);
		if (scenario == Scenario::ChangedFrame)
			++globals::state->frameCount;
		if (scenario == Scenario::ChangedThread)
			++observedThread;
		if (scenario == Scenario::ChangedCycle)
			g_openVRSubmitCycleState += 2;
		return vr::None;
	}

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void NativePairOwnsOnePostReturnService()
	{
		for (const auto sample : { Scenario::Complete, Scenario::Missing, Scenario::Duplicate, Scenario::InvalidEye,
				 Scenario::NestedOnly, Scenario::NestedThenComplete, Scenario::ChangedFrame, Scenario::ChangedThread, Scenario::ChangedCycle }) {
			scenario = sample;
			g_vrSubmitPairBoundaryState = {};
			g_vrRelatchPairCompletion = {};
			globals::features::upscaling.calls = 0;
			globals::state->frameCount = 100;
			g_openVRSubmitCycleState = 20;
			observedThread = 1;
			returnedEyes = 0;
			(void)BSOpenVR_Submit::thunk(nullptr, &texture, nullptr, vr::Default);
			const bool complete = sample == Scenario::Complete || sample == Scenario::NestedThenComplete;
			Require(globals::features::upscaling.calls == (complete ? 1u : 0u),
				"Native callback service did not match the exact completed, unchanged outer stereo owner");
			Require(!g_vrSubmitPairBoundaryState.active && g_vrRelatchPairCompletion.identity.token == 0 && nativeDepth == 0,
				"Native submit failed to restore the previous boundary state");
		}
	}

	void NativeFailureRestoresOuterOwnership()
	{
		scenario = Scenario::ThrowingNative;
		g_vrSubmitPairBoundaryState = { .token = 31, .compositorCycle = 10, .frame = 100, .thread = 1, .active = true };
		g_vrRelatchPairCompletion = { .identity = { 31, 10, 100, 1 }, .completedEyeMask = 1 };
		globals::features::upscaling.calls = 0;
		bool caught = false;
		try {
			(void)BSOpenVR_Submit::thunk(nullptr, &texture, nullptr, vr::Default);
		} catch (const std::runtime_error&) {
			caught = true;
		}
		Require(caught && nativeDepth == 0 && globals::features::upscaling.calls == 0 &&
					g_vrSubmitPairBoundaryState.active && g_vrSubmitPairBoundaryState.token == 31 &&
					g_vrRelatchPairCompletion.identity.token == 31 && g_vrRelatchPairCompletion.completedEyeMask == 1,
			"A throwing nested native call leaked or discarded its outer ownership");
	}
}

int main()
{
	BSOpenVR_Submit::func.callback = NativeSubmit;
	try {
		NativePairOwnsOnePostReturnService();
		NativeFailureRestoresOuterOwnership();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
