#include "Features/Upscaling/NeuralRendering/MonoPolicy.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

using HRESULT = int;
constexpr bool FAILED(HRESULT value) { return value < 0; }
#define __uuidof(Type) 0
struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width = 1920, Height = 1080;
};
struct ID3D11Texture2D;
struct ID3D11Resource
{
	unsigned references = 1;
	bool isTexture = false;
	void AddRef() { ++references; }
	void Release()
	{
		assert(references);
		--references;
	}
	HRESULT QueryInterface(int, void** output);
};
struct ID3D11Texture2D : ID3D11Resource
{
	ID3D11Texture2D() { isTexture = true; }
	D3D11_TEXTURE2D_DESC description;
	uint32_t paintedFrame = UINT32_MAX;
	void GetDesc(D3D11_TEXTURE2D_DESC* output) const { *output = description; }
};
HRESULT ID3D11Resource::QueryInterface(int, void** output)
{
	*output = isTexture ? static_cast<ID3D11Texture2D*>(this) : nullptr;
	if (!isTexture)
		return -1;
	AddRef();
	return 0;
}
struct ID3D11View : ID3D11Resource
{
	ID3D11Resource* resource = nullptr;
	void GetResource(ID3D11Resource** output)
	{
		*output = resource;
		if (resource)
			resource->AddRef();
	}
};
using ID3D11ShaderResourceView = ID3D11View;
using ID3D11RenderTargetView = ID3D11View;
struct ID3D11Device
{};
struct ID3D11DeviceContext
{};
namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* value = nullptr;
		~com_ptr()
		{
			if (value)
				value->Release();
		}
		T* get() const { return value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
		void copy_from(T* source)
		{
			if (source)
				source->AddRef();
			if (value)
				value->Release();
			value = source;
		}
		T** put()
		{
			if (value)
				value->Release();
			value = nullptr;
			return &value;
		}
		void** put_void() { return reinterpret_cast<void**>(put()); }
	};
}
struct float2
{
	float x = 0, y = 0;
};
namespace RE
{
	enum RENDER_TARGET
	{
		kMAIN,
		kFRAMEBUFFER
	};
	using RENDER_TARGETS = RENDER_TARGET;
	struct ImageSpaceManager
	{};
	namespace BSGraphics
	{
		struct RenderTargetData
		{
			ID3D11Texture2D* texture = nullptr;
			ID3D11ShaderResourceView* SRV = nullptr;
			ID3D11RenderTargetView* RTV = nullptr;
		};
	}
}
struct Renderer
{
	struct RuntimeData
	{
		std::array<RE::BSGraphics::RenderTargetData, 2> renderTargets;
	} data;
	RuntimeData& GetRuntimeData() { return data; }
};
struct GraphicsState
{
	uint32_t screenWidth = 1920, screenHeight = 1080;
	struct RuntimeData
	{
		bool dynamicResolutionLock = false;
		float dynamicResolutionWidthRatio = 1, dynamicResolutionHeightRatio = 1;
	} data;
	RuntimeData& GetRuntimeData() { return data; }
};
struct State
{
	uint32_t frameCount = 50;
	bool isMapMenuOpen = false;
};
struct Texture
{
	winrt::com_ptr<ID3D11Texture2D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
};
struct HDRDisplay
{
	bool loaded = true, active = false, shaderReady = true, framebufferRedirected = false;
	Texture* hdrTexture = nullptr;
	ID3D11Texture2D* savedFramebufferTexture = nullptr;
	ID3D11ShaderResourceView* savedFramebufferSRV = nullptr;
	ID3D11RenderTargetView* savedFramebufferRTV = nullptr;
	bool IsHDROutputActive() const { return loaded && active; }
	bool GetHDROutputCS() const { return shaderReady; }
#include "neural_hdr_members.h"
	void RestoreFramebuffer();
};
#include "neural_scene_types.h"
namespace NeuralRendering
{
	struct MonoRenderer
	{
		MonoFrame frame;
		ID3D11Texture2D* committed = nullptr;
		unsigned applies = 0, acknowledgments = 0;
		static MonoRenderer& Instance()
		{
			static MonoRenderer value;
			return value;
		}
		ID3D11Texture2D* Apply(const MonoFrame& value, const MonoSettings&)
		{
			frame = value;
			++applies;
			return value.color;
		}
		bool Commit(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D* output)
		{
			committed = output;
			return true;
		}
		void AcknowledgePresentation() { ++acknowledgments; }
	};
}
struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kDLSS,
		kFSR
	};
	UpscaleMethod method = UpscaleMethod::kTAA;
	bool requested = true, mainMenu = false, loadingMenu = false;
	bool neuralFinalSceneIsHDR = false, pathSuccessful = true;
	uint32_t neuralGuidesFrame = UINT32_MAX, neuralWorldFrame = UINT32_MAX;
	float2 jitter{}, resolutionScale{ 1, 1 };
	NeuralRendering::MonoSettings neuralSettings;
	struct Streamline
	{
		static constexpr uint32_t kDLSSGDiagnosticFrameThreshold = 3;
		struct
		{
			uint32_t consecutiveMissingMarkerStartFrames = 0;
		} dlssgState;
		int viewport = 0;
		bool IsDLSSGReflexReadyForCurrentFrame() const { return true; }
		bool CheckFrameConstants(int) const { return true; }
	} streamlineDX12;
	UpscaleMethod GetUpscaleMethod() const { return method; }
	void PrepareHistoryResetForCurrentFrame() {}
	void CaptureNeuralFrameGuides();
	bool ShouldUseFrameGenerationThisFrame() const { return false; }
	bool CopySharedD3D12Resources(uint32_t&, uint32_t&) const { return true; }
	bool UsesDLSSGFrameGeneration() const { return false; }
	bool IsBackendShutdownRequested() const { return false; }
	void RecordFrameGenerationCopy(bool, bool, uint32_t, uint32_t) {}
	void RecordPerformanceCostExecutedPath(UpscaleMethod, bool) {}
	bool PerformUpscaling() const { return pathSuccessful; }
	bool ApplySharpening() const { return pathSuccessful; }
	bool IsNeuralRenderingRequested() const { return requested; }
	bool ShouldResetHistoryThisFrame() const { return false; }
	bool IsMainMenuContextActive() const { return mainMenu; }
	bool IsLoadingMenuContextActive() const { return loadingMenu; }
#include "neural_upscaling_members.h"
	RuntimeResolutionPlan GetRuntimeResolutionPlan() const;
	void ApplyFinalNeuralFrame();
	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager*, uint32_t, RE::RENDER_TARGET, void*, bool);
		static void func(RE::ImageSpaceManager*, uint32_t, RE::RENDER_TARGET, void*, bool);
	};
};
using Streamline = Upscaling::Streamline;
namespace globals
{
	State stateStorage;
	State* state = &stateStorage;
	namespace game
	{
		Renderer rendererStorage;
		Renderer* renderer = &rendererStorage;
		GraphicsState graphicsStorage;
		GraphicsState* graphicsState = &graphicsStorage;
	}
	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
	}
	namespace features
	{
		Upscaling upscaling;
		HDRDisplay hdrDisplay;
		struct
		{
			bool loaded = true;
		} linearLighting;
	}
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
}
namespace Util
{
	void SetTemporal(bool) {}
	struct Camera
	{
		float x = 1, y = 1, z = 1, w = 1;
	};
	Camera GetCameraData() { return {}; }
}
#include "neural_mono_scene_under_test.h"

void Upscaling::CaptureNeuralFrameGuides()
{
	neuralGuidesFrame = globals::state->frameCount;
	neuralWorldFrame = UINT32_MAX;
	neuralFinalSceneIsHDR = false;
}
void Upscaling::Main_PostProcessing::func(RE::ImageSpaceManager*, uint32_t, RE::RENDER_TARGET, void*, bool)
{
	winrt::com_ptr<ID3D11Texture2D> owner;
	auto* scene = Util::ResolveRenderTargetTexture(globals::game::renderer->data.renderTargets[RE::kFRAMEBUFFER], owner);
	assert(scene);
	scene->paintedFrame = globals::state->frameCount;
}

void TestSceneSelection(bool aliasOnly)
{
	auto& upscaling = globals::features::upscaling;
	auto& hdr = globals::features::hdrDisplay;
	auto& neural = NeuralRendering::MonoRenderer::Instance();
	ID3D11Texture2D framebuffer, hdrScene;
	ID3D11View framebufferView, hdrView;
	framebufferView.resource = &framebuffer;
	hdrView.resource = &hdrScene;
	Texture texture;
	texture.resource.copy_from(&hdrScene);
	texture.srv.copy_from(&hdrView);
	texture.rtv.copy_from(&hdrView);
	hdr.hdrTexture = &texture;
	for (const bool loaded : { false, true }) {
		for (const bool active : { false, true }) {
			for (const bool shader : { false, true }) {
				for (const auto method : { Upscaling::UpscaleMethod::kNONE, Upscaling::UpscaleMethod::kTAA,
						 Upscaling::UpscaleMethod::kDLSS, Upscaling::UpscaleMethod::kFSR }) {
					for (const bool pathSuccessful : { false, true }) {
						hdr.loaded = loaded;
						hdr.active = active;
						hdr.shaderReady = shader;
						upscaling.method = method;
						upscaling.pathSuccessful = pathSuccessful;
						globals::game::renderer->data.renderTargets[RE::kFRAMEBUFFER] = {
							aliasOnly ? nullptr : &framebuffer, &framebufferView, &framebufferView
						};
						++globals::state->frameCount;
						const bool redirected = loaded && active && shader && method != Upscaling::UpscaleMethod::kNONE;
						const auto expected = redirected ? &hdrScene : &framebuffer;
						Upscaling::Main_PostProcessing::thunk(nullptr, 0, RE::kFRAMEBUFFER, nullptr, false);
						assert(upscaling.neuralFinalSceneIsHDR == redirected);
						assert(!hdr.framebufferRedirected);
						assert(expected->paintedFrame == globals::state->frameCount);
						const auto applies = neural.applies, acknowledged = neural.acknowledgments;
						upscaling.ApplyFinalNeuralFrame();
						assert(neural.applies == applies + 1 && neural.acknowledgments == acknowledged + 1);
						assert(neural.frame.color == expected && neural.committed == expected);
						assert(neural.frame.linearColor == redirected);
						assert(framebuffer.references == 1);  // A view-only source cannot retain the swap chain across frames.
					}
				}
			}
		}
	}
	const auto calls = neural.applies;
	upscaling.requested = false;
	upscaling.ApplyFinalNeuralFrame();
	assert(neural.applies == calls);
	upscaling.requested = true;
	hdr.hdrTexture = nullptr;
}

void TestRenderDimensions()
{
	auto& upscaling = globals::features::upscaling;
	auto& graphics = globals::game::graphicsStorage;
	for (const auto size : { std::array<uint32_t, 2>{ 1280, 720 }, { 1920, 1080 }, { 2560, 1440 },
			 { 3440, 1440 }, { 3840, 2160 }, { 5120, 1440 } }) {
		graphics.screenWidth = size[0];
		graphics.screenHeight = size[1];
		for (uint32_t quality = 0; quality <= 6; ++quality) {
			const float scale = Upscaling::GetQualityModeResolutionScale(quality);
			const uint32_t renderWidth = static_cast<uint32_t>(static_cast<float>(size[0]) * scale);
			const uint32_t renderHeight = static_cast<uint32_t>(static_cast<float>(size[1]) * scale);
			upscaling.resolutionScale = { float(renderWidth) / float(size[0]), float(renderHeight) / float(size[1]) };
			graphics.data.dynamicResolutionWidthRatio = upscaling.resolutionScale.x;
			graphics.data.dynamicResolutionHeightRatio = upscaling.resolutionScale.y;
			const auto engine = Util::ConvertToDynamic({ float(size[0]), float(size[1]) }, true);
			const auto plan = upscaling.GetRuntimeResolutionPlan();
			assert(plan.engineRenderSize.x == static_cast<uint32_t>(engine.x));
			assert(plan.engineRenderSize.y == static_cast<uint32_t>(engine.y));
			assert(plan.finalOutputSize.x == size[0] && plan.finalOutputSize.y == size[1]);
		}
	}
	for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
		upscaling.resolutionScale = { invalid, invalid };
		const auto plan = upscaling.GetRuntimeResolutionPlan();
		assert(plan.engineRenderSize.x == 0 && plan.engineRenderSize.y == 0);
	}
	graphics.screenWidth = graphics.screenHeight = UINT32_MAX;
	upscaling.resolutionScale = { 1, 1 };
	assert(upscaling.GetRuntimeResolutionPlan().engineRenderSize.x == UINT32_MAX);
}

int main(int argc, char** argv)
{
	if (argc == 1 || std::string_view(argv[1]) == "scene")
		TestSceneSelection(false);
	if (argc == 1 || std::string_view(argv[1]) == "alias")
		TestSceneSelection(true);
	if (argc == 1 || std::string_view(argv[1]) == "dimensions")
		TestRenderDimensions();
	std::cout << "Production mono scene/HDR selection, view ownership and SE render dimensions passed\n";
}
