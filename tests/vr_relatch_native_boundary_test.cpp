#include "Features/Upscaling/VRSubmitInputFreshnessBoundary.h"
#include "Features/VR/VRRenderScaleFrameBoundaryPolicy.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

struct ID3D11Texture2D
{
	uintptr_t vtable = 0x1234;
	uint64_t unrelatedStorage = std::numeric_limits<uint64_t>::max();
};
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
VRSubmitInputFreshnessPolicy::OuterPairBoundaryState g_vrSubmitPairBoundaryState;
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

static_assert(std::is_same_v<decltype(BSOpenVR_Submit::thunk), void(RE::BSOpenVR*, ID3D11Texture2D*)>);

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
		DeepNestedThenComplete,
		NestedThrowThenComplete,
		ChangedFrame,
		ChangedThread,
		ChangedCycle,
		NullTexture,
		NullOuterThenNested,
		ThrowingNative
	};
	Scenario scenario = Scenario::Complete;
	RE::BSOpenVR nativeInstance;
	ID3D11Texture2D texture;
	struct NativeFailure : std::runtime_error
	{
		NativeFailure() : std::runtime_error("injected native failure") {}
	};
	void ReturnedEye(uint32_t eEye, ID3D11Texture2D* a_texture)
	{
		const vr::Texture_t descriptor{ a_texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
		const auto observation = VRSubmitInputFreshnessPolicy::ObserveNestedSubmit(
			g_vrSubmitPairBoundaryState, &descriptor, g_openVRSubmitCycleState.load() >> 1u,
			globals::state->frameCount, GetCurrentThreadId(), vr::Submit_Default);
		const auto expectedRejection = nativeDepth == 1 && a_texture ?
		                                   VRSubmitInputFreshnessPolicy::OuterBoundaryRejection::None :
		                                   VRSubmitInputFreshnessPolicy::OuterBoundaryRejection::MissingBoundary;
		if (VRSubmitInputFreshnessPolicy::ResolveOuterBoundaryRejection(observation) != expectedRejection)
			throw std::runtime_error("Native raw texture did not produce the expected nested submit identity");
#include "vr_relatch_native_eye_completion_under_test.h"
		++returnedEyes;
	}

	void NativeSubmit(RE::BSOpenVR* a_this, ID3D11Texture2D* a_texture)
	{
		++nativeDepth;
		const SKSE::stl::scope_exit leaveNative([&]() { --nativeDepth; });
		const bool nullOuter = nativeDepth == 1 &&
		                       (scenario == Scenario::NullTexture || scenario == Scenario::NullOuterThenNested);
		if (a_this != &nativeInstance || a_texture != (nullOuter ? nullptr : &texture))
			throw std::runtime_error("Native submit did not receive the original instance and raw texture");
		if (scenario == Scenario::ThrowingNative ||
			(scenario == Scenario::NestedThrowThenComplete && nativeDepth == 2))
			throw NativeFailure();
		if (nativeDepth != 1 || (scenario != Scenario::NestedOnly && scenario != Scenario::NullOuterThenNested))
			ReturnedEye(0, a_texture);
		if ((nativeDepth == 1 && (scenario == Scenario::NestedOnly || scenario == Scenario::NestedThenComplete ||
									 scenario == Scenario::NestedThrowThenComplete || scenario == Scenario::NullOuterThenNested)) ||
			(scenario == Scenario::DeepNestedThenComplete && nativeDepth < 3)) {
			const auto savedBoundary = g_vrSubmitPairBoundaryState;
			const auto savedCompletion = g_vrRelatchPairCompletion;
			bool caught = false;
			try {
				BSOpenVR_Submit::thunk(&nativeInstance, &texture);
			} catch (const NativeFailure&) {
				if (scenario != Scenario::NestedThrowThenComplete)
					throw;
				caught = true;
			}
			if (caught != (scenario == Scenario::NestedThrowThenComplete) ||
				g_vrSubmitPairBoundaryState.active != savedBoundary.active ||
				g_vrSubmitPairBoundaryState.token != savedBoundary.token ||
				g_vrRelatchPairCompletion.identity.token != savedCompletion.identity.token ||
				g_vrRelatchPairCompletion.completedEyeMask != savedCompletion.completedEyeMask)
				throw std::runtime_error("Nested native call failed to preserve outer ownership");
			if (globals::features::upscaling.calls != 0)
				throw std::runtime_error("Nested submit independently serviced the outer relatch");
			if (scenario == Scenario::NestedOnly || scenario == Scenario::NullOuterThenNested)
				return;
		}
		if (scenario == Scenario::Duplicate)
			ReturnedEye(0, a_texture);
		if (scenario == Scenario::InvalidEye)
			ReturnedEye(2, a_texture);
		if (scenario != Scenario::Missing)
			ReturnedEye(1, a_texture);
		if (scenario == Scenario::ChangedFrame)
			++globals::state->frameCount;
		if (scenario == Scenario::ChangedThread)
			++observedThread;
		if (scenario == Scenario::ChangedCycle)
			g_openVRSubmitCycleState += 2;
	}

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void NativePairOwnsOnePostReturnService()
	{
		for (const auto sample : { Scenario::Complete, Scenario::Missing, Scenario::Duplicate, Scenario::InvalidEye,
				 Scenario::NestedOnly, Scenario::NestedThenComplete, Scenario::DeepNestedThenComplete, Scenario::NestedThrowThenComplete,
				 Scenario::ChangedFrame, Scenario::ChangedThread, Scenario::ChangedCycle, Scenario::NullTexture, Scenario::NullOuterThenNested }) {
			scenario = sample;
			g_vrSubmitPairBoundaryState = {};
			g_vrRelatchPairCompletion = {};
			globals::features::upscaling.calls = 0;
			globals::state->frameCount = 100;
			g_openVRSubmitCycleState = 20;
			observedThread = 1;
			returnedEyes = 0;
			BSOpenVR_Submit::thunk(&nativeInstance,
				(sample == Scenario::NullTexture || sample == Scenario::NullOuterThenNested) ? nullptr : &texture);
			const bool complete = sample == Scenario::Complete || sample == Scenario::NestedThenComplete ||
			                      sample == Scenario::DeepNestedThenComplete || sample == Scenario::NestedThrowThenComplete;
			Require(globals::features::upscaling.calls == (complete ? 1u : 0u),
				"Native callback service did not match the exact completed, unchanged outer stereo owner");
			Require(!g_vrSubmitPairBoundaryState.active && g_vrRelatchPairCompletion.identity.token == 0 && nativeDepth == 0,
				"Native submit failed to restore the previous boundary state");
		}
	}

	void NativeFailureClearsOwnership()
	{
		scenario = Scenario::ThrowingNative;
		g_vrSubmitPairBoundaryState = {};
		g_vrRelatchPairCompletion = {};
		globals::features::upscaling.calls = 0;
		bool caught = false;
		try {
			BSOpenVR_Submit::thunk(&nativeInstance, &texture);
		} catch (const NativeFailure&) {
			caught = true;
		}
		Require(caught && nativeDepth == 0 && globals::features::upscaling.calls == 0 &&
					!g_vrSubmitPairBoundaryState.active && g_vrSubmitPairBoundaryState.token == 0 &&
					g_vrRelatchPairCompletion.identity.token == 0 && g_vrRelatchPairCompletion.completedEyeMask == 0,
			"A throwing native call leaked its boundary ownership");
	}
}

int main()
{
	BSOpenVR_Submit::func.callback = NativeSubmit;
	try {
		NativeFailureClearsOwnership();
		NativePairOwnsOnePostReturnService();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
