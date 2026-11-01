#include "Features/Upscaling/NeuralRendering/ExposurePolicy.h"
#include "Features/Upscaling/NeuralRendering/MonoPolicy.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;
using HRESULT = std::int32_t;
using UINT = unsigned;
constexpr UINT DXGI_PRESENT_TEST = 1;
constexpr HRESULT S_OK = 0;
constexpr bool SUCCEEDED(HRESULT value) { return value >= 0; }
struct IDXGISwapChain
{};
struct ID3D11Device
{};
struct ID3D11DeviceContext
{};
struct ID3D11Texture2D
{};
enum DXGI_FORMAT
{
	DXGI_FORMAT_R32G32B32A32_FLOAT,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R11G11B10_FLOAT,
	DXGI_FORMAT_R8G8B8A8_UNORM
};
#include "screenshot_neural_types.h"

namespace NeuralRendering::Color
{
	enum class ExposureBindingState
	{
		NotRequested,
		SnapshotQueued,
		WaitingForHDRPass
	};
	struct ExposureEvidence
	{
		ExposureStamp stamp{};
	};
	struct ExposureBinding
	{
		ExposureEvidence evidence;
		ExposureBindingState state = ExposureBindingState::NotRequested;
	};
	struct ExposureEvidenceLookup
	{
		ExposureStamp key;
	};
	const char* ExposureBindingName(ExposureBindingState) { return "test binding"; }
	struct ExposureCapture
	{
		ExposureStamp available{ 40, 2, 7, false };
		unsigned binds = 0;
		bool accept = true;
		static ExposureCapture& Instance()
		{
			static ExposureCapture value;
			return value;
		}
		ExposureEvidenceLookup GetSourceFrameEvidence(uint32_t frame) const
		{
			return { available.frame == frame ? available : ExposureStamp{ frame, available.epoch, 0, false } };
		}
		bool Bind(ID3D11DeviceContext*, ExposureBinding& binding, const ExposureTransaction& transaction)
		{
			++binds;
			const bool matched = accept && MatchesExposure(available, transaction, available.epoch);
			binding.state = matched ? ExposureBindingState::SnapshotQueued : ExposureBindingState::WaitingForHDRPass;
			binding.evidence.stamp = matched ? available : ExposureStamp{};
			return matched;
		}
	};
}
namespace NeuralRendering
{
	struct MonoRenderer
	{
		MonoSnapshot snapshot;
		static MonoRenderer& Instance()
		{
			static MonoRenderer value;
			return value;
		}
		MonoSnapshot Snapshot() const { return snapshot; }
	};
	struct TestState
	{
		bool historyValid = true;
		Color::ExposureBinding exposure;
		MonoSnapshot snapshot;
		void Status(const char*, const char*) {}
	};
}

struct ScreenshotFeature;
struct ScreenshotApi
{
	uint32_t frame = UINT32_MAX;
	void Tick(ScreenshotFeature&, uint32_t value) { frame = value; }
};
struct ScreenshotFeature
{
	ScreenshotApi api;
	ScreenshotApi* screenshotApi = &api;
	std::atomic_bool captureRequested{ false };
	std::vector<uint32_t> acquiredFrames;
	json lastEvidence;
	json RequestApiCapture(const char*) { return { { "ok", true } }; }
#include "screenshot_frame_members.h"
};
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
}
struct TestState
{
	uint32_t frameCount = 40;
	unsigned resets = 0;
	bool startupMenuBlurSourceReady = false;
	std::atomic_bool startupMenuInitializationComplete{ true };
	int tracyCtx = 0;
	void Reset()
	{
		++resets;
		++frameCount;
	}
};
namespace globals
{
	TestState stateStorage;
	TestState* state = &stateStorage;
	namespace d3d
	{
		ID3D11DeviceContext* context = nullptr;
	}
	namespace features
	{
		ScreenshotFeature screenshotFeature;
		struct HdrDisplay
		{
			bool frameGeneration = false;
			unsigned presentations = 0;
			HRESULT HandleSwapChainPresent(IDXGISwapChain* swapChain, UINT interval, UINT flags,
				const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& present)
			{
				++presentations;
				// An FG provider may perform several internal output presents for
				// one accepted game frame; only the outer hook owns acquisition.
				if (frameGeneration) {
					(void)present(swapChain, interval, flags);
					(void)present(swapChain, interval, flags);
				}
				return present(swapChain, interval, flags);
			}
		} hdrDisplay;
		struct Upscaling
		{
			bool ProcessBackendShutdownRequest() { return false; }
		} upscaling;
	}
}
namespace CSX::Api
{
	void AdvanceAcceptedDrawFrame(ID3D11DeviceContext*) {}
}
void TracyD3D11Collect(int) {}
void CompleteDeferredWindowClose() {}
#include "screenshot_neural_frame_under_test.h"

void TestSourceFrameHandoff()
{
	using namespace NeuralRendering;
	auto& capture = globals::features::screenshotFeature;
	auto& exposure = Color::ExposureCapture::Instance();
	auto& snapshot = MonoRenderer::Instance().snapshot;
	IDXGISwapChain swapChain;
	unsigned presents = 0;
	const auto present = [&](IDXGISwapChain* value, UINT, UINT) { assert(value == &swapChain); ++presents; return S_OK; };
	for (const bool frameGeneration : { false, true }) {
		globals::features::hdrDisplay.frameGeneration = frameGeneration;
		for (unsigned i = 0; i != 3; ++i) {
			const uint32_t source = globals::state->frameCount;
			snapshot.presentationFrame = snapshot.evaluationFrame = frameGeneration ? UINT32_MAX : source;
			exposure.available = { source, 2, 7 + i, false };
			assert(Hooks::RunSwapChainPresent(&swapChain, 0, 0, present) == S_OK);
			assert(globals::state->frameCount == source + 1);
			assert(capture.acquiredFrames.back() == source);
			assert(capture.api.frame == source);
			assert(capture.lastEvidence.at("available").get<bool>() == !frameGeneration);
			assert(capture.lastEvidence.at("sourceWorldFrame") == source);
			assert(capture.lastEvidence.at("engineExposure").at("sequence") == 7 + i);
		}
	}
	const auto resetCount = globals::state->resets;
	const auto acquisitions = capture.acquiredFrames.size();
	assert(Hooks::RunSwapChainPresent(&swapChain, 0, DXGI_PRESENT_TEST, present) == S_OK);
	assert(globals::state->resets == resetCount && capture.acquiredFrames.size() == acquisitions);
	assert(presents == 13 && acquisitions == 6);
}

void TestCurrentExposureEvidence()
{
	using namespace NeuralRendering;
	auto& capture = Color::ExposureCapture::Instance();
	MonoFrame frame{ .frame = 100, .width = 1920, .height = 1080, .linearColor = true };
	MonoSettings settings;
	settings.colorMode = 2;
	NeuralRendering::TestState state;
	ColorConstants result;
	for (const auto format : { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
			 DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM }) {
		for (const unsigned source : { 1u, 2u }) {
			settings.exposureSource = source;
			settings.colorMode = 2;
			frame.linearColor = true;
			capture.available = { frame.frame - (source == 2 ? 1u : 0u), 4, 23, false };
			assert(ConfigureColorForTest(state, frame, settings, format, result));
			assert((result.flags & 4u) != 0);
			assert(state.snapshot.exposureFrame == capture.available.frame && state.snapshot.exposureSequence == 23);
			const auto binds = capture.binds;
			for (const unsigned transition : { 0u, 1u, 2u }) {
				settings.exposureSource = transition == 0 ? 0u : source;
				settings.colorMode = transition == 1 ? 0u : 2u;
				frame.linearColor = transition != 2;
				assert(ConfigureColorForTest(state, frame, settings, format, result));
				assert((result.flags & 4u) == 0 && capture.binds == binds);
				assert(state.snapshot.exposureFrame == UINT32_MAX);
				assert(state.snapshot.exposureEpoch == 0 && state.snapshot.exposureSequence == 0);
			}
		}
	}
	frame.linearColor = true;
	settings.exposureSource = 1;
	capture.accept = false;
	assert(!ConfigureColorForTest(state, frame, settings, DXGI_FORMAT_R16G16B16A16_FLOAT, result));
	assert(!state.historyValid);
}

int main(int argc, char** argv)
{
	if (argc == 1 || std::string_view(argv[1]) == "frame")
		TestSourceFrameHandoff();
	if (argc == 1 || std::string_view(argv[1]) == "exposure")
		TestCurrentExposureEvidence();
	std::cout << "Production Present source-frame and current-exposure evidence regressions passed\n";
}
