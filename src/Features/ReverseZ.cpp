#include "ReverseZ.h"

#include "Features/VRDepthCullingTemporal.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/DepthConvention.h"
#include "Utils/LazyShader.h"
#include "Utils/NativeDepthShader.h"
#include "Utils/UI.h"
#include <stdexcept>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

// C4324: CameraStateEntry embeds the alignas(16) ViewData.
#pragma warning(disable: 4324)

#define I18N_KEY_PREFIX "feature.reverse_z."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ReverseZ::Settings,
	EnableReverseZBuffer)

namespace
{
	using RENDER_TARGET_DEPTHSTENCIL = RE::RENDER_TARGETS_DEPTHSTENCIL::RENDER_TARGET_DEPTHSTENCIL;

	// Depth targets the engine rasterizes the visible scene through. A target left out here
	// keeps the standard convention, so anything reading it needs no ToStandardDepth wrapping.
	constexpr RENDER_TARGET_DEPTHSTENCIL kReverseTargets[] = {
		RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN,
		RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY,
		RE::RENDER_TARGETS_DEPTHSTENCIL::kDECAL_OCCLUSION,
		RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY,
		RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_WATER_COPY,
		RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_DOWNSAMPLE,
	};

	constexpr DXGI_FORMAT kStandardDepthTypeless = DXGI_FORMAT_R24G8_TYPELESS;
	constexpr DXGI_FORMAT kFloatDepthTypeless = DXGI_FORMAT_R32G8X24_TYPELESS;

	// Flat layout of the entry BSGraphics::State::BuildCameraStateData fills. ViewData is
	// alignas(16), so it lands at +0x10 after the NiCamera pointer, not at +0x08.
	struct CameraStateEntry
	{
		RE::NiCamera* referenceCamera;
		RE::BSGraphics::ViewData viewData;
	};
	static_assert(offsetof(CameraStateEntry, viewData) == 0x10);

	// VR layout of the same entry: one ViewData per eye, which a universal CommonLib build compiles out.
	struct CameraStateEntryVR
	{
		RE::NiCamera* referenceCamera;
		RE::BSTArray<RE::BSGraphics::ViewData> viewData;
	};
	static_assert(offsetof(CameraStateEntryVR, viewData) == 0x08);

	std::shared_mutex g_viewMutex;
	struct ViewVerdict
	{
		winrt::com_ptr<ID3D11DepthStencilView> view;  // pins the address so a released view cannot hand its verdict to a new one
		bool reverse;
	};
	std::unordered_map<ID3D11DepthStencilView*, ViewVerdict> g_viewIsReverse;

	std::mutex g_stateMutex;
	struct ReversedState
	{
		winrt::com_ptr<ID3D11DepthStencilState> source;
		winrt::com_ptr<ID3D11DepthStencilState> reversed;
	};
	std::unordered_map<ID3D11DepthStencilState*, ReversedState> g_reversedStates;

	std::mutex g_rasterMutex;
	struct ReversedRasterState
	{
		winrt::com_ptr<ID3D11RasterizerState> source;
		winrt::com_ptr<ID3D11RasterizerState> reversed;
	};
	std::unordered_map<ID3D11RasterizerState*, ReversedRasterState> g_reversedRasterStates;
	winrt::com_ptr<ID3D11DepthStencilState> g_rejectedDepthState;
	std::atomic_bool g_renderingRejected{ false };

	bool PrepareRejectedDepthState(ID3D11Device* device)
	{
		if (g_rejectedDepthState)
			return true;
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_NEVER;
		desc.FrontFace = desc.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
		if (FAILED(device->CreateDepthStencilState(&desc, g_rejectedDepthState.put())))
			return false;
		Util::SetResourceName(g_rejectedDepthState.get(), "ReverseZ::RejectedDepthState");
		return true;
	}

	winrt::com_ptr<ID3D11DepthStencilState> g_requestedState;
	ID3D11DepthStencilState* g_boundState = nullptr;
	UINT g_requestedStencilRef = 0;
	winrt::com_ptr<ID3D11RasterizerState> g_requestedRasterState;
	ID3D11RasterizerState* g_boundRasterState = nullptr;
	bool g_reverseTargetBound = false;
	bool g_projectionReversed = false;
	bool g_nearMaskBound = false;
	std::atomic<bool> g_reversalActive{ false };

	D3D11_VIEWPORT g_requestedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	D3D11_VIEWPORT g_mappedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	UINT g_requestedViewportCount = 0;

	bool ShouldFlip()
	{
		return g_reverseTargetBound && (g_projectionReversed || g_nearMaskBound || g_renderingRejected.load(std::memory_order_relaxed));
	}

	D3D11_VIEWPORT MirrorViewportDepth(const D3D11_VIEWPORT& a_viewport)
	{
		D3D11_VIEWPORT mirrored = a_viewport;
		mirrored.MinDepth = 1.0f - a_viewport.MaxDepth;
		mirrored.MaxDepth = 1.0f - a_viewport.MinDepth;
		return mirrored;
	}

	DXGI_FORMAT ToFloatDepthFormat(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R24G8_TYPELESS:
			return DXGI_FORMAT_R32G8X24_TYPELESS;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
		default:
			return a_format;
		}
	}

	/** @brief Recreates a_source's view on a_texture in the float format; true when there is nothing to recreate. */
	bool RecreateFloatView(ID3D11Device* a_device, ID3D11Texture2D* a_texture, ID3D11DepthStencilView* a_source, winrt::com_ptr<ID3D11DepthStencilView>& o_view)
	{
		if (!a_source)
			return true;
		D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
		a_source->GetDesc(&desc);
		desc.Format = ToFloatDepthFormat(desc.Format);
		return SUCCEEDED(a_device->CreateDepthStencilView(a_texture, &desc, o_view.put()));
	}

	/** @copydoc RecreateFloatView */
	bool RecreateFloatView(ID3D11Device* a_device, ID3D11Texture2D* a_texture, ID3D11ShaderResourceView* a_source, winrt::com_ptr<ID3D11ShaderResourceView>& o_view)
	{
		if (!a_source)
			return true;
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		a_source->GetDesc(&desc);
		desc.Format = ToFloatDepthFormat(desc.Format);
		return SUCCEEDED(a_device->CreateShaderResourceView(a_texture, &desc, o_view.put()));
	}

	/** @brief Releases the engine's view in a_slot and hands it the rebuilt one, named a_name. */
	template <class EngineView, class View>
	void ReplaceEngineView(EngineView*& a_slot, winrt::com_ptr<View>& a_view, const char* a_name)
	{
		if (!a_slot)
			return;
		a_slot->Release();
		auto* view = a_view.detach();
		a_slot = view;
		Util::SetResourceName(view, "%s", a_name);
	}

	D3D11_COMPARISON_FUNC FlipComparison(D3D11_COMPARISON_FUNC a_func)
	{
		switch (a_func) {
		case D3D11_COMPARISON_LESS:
			return D3D11_COMPARISON_GREATER;
		case D3D11_COMPARISON_LESS_EQUAL:
			return D3D11_COMPARISON_GREATER_EQUAL;
		case D3D11_COMPARISON_GREATER:
			return D3D11_COMPARISON_LESS;
		case D3D11_COMPARISON_GREATER_EQUAL:
			return D3D11_COMPARISON_LESS_EQUAL;
		default:
			return a_func;
		}
	}

	enum class ZAxis
	{
		kUnknown,
		kRow2,
		kCol2,
	};

	ZAxis DetectZAxis(const Matrix& a_proj)
	{
		if (std::abs(a_proj.m[3][3]) > 1e-4f)
			return ZAxis::kUnknown;
		const bool col2 = std::abs(std::abs(a_proj.m[3][2]) - 1.0f) < 1e-3f;
		const bool row2 = std::abs(std::abs(a_proj.m[2][3]) - 1.0f) < 1e-3f;
		if (col2 && !row2)
			return ZAxis::kCol2;
		if (row2 && !col2)
			return ZAxis::kRow2;
		// A small near plane also pushes m[3][2] toward -1, so an ambiguous axis trusts m[2][3],
		// which the perspective divide makes exactly 1.
		return row2 ? ZAxis::kRow2 : ZAxis::kUnknown;
	}

	bool AlreadyReversed(const Matrix& a_proj, ZAxis a_axis)
	{
		const float perspective = a_axis == ZAxis::kCol2 ? a_proj.m[3][2] : a_proj.m[2][3];
		return a_proj.m[2][2] * perspective <= 0.0f;
	}

	void ReverseClipZ(Matrix& matrix, ZAxis axis, float nearPlane, float farPlane)
	{
		if (axis == ZAxis::kCol2)
			Depth::SetReverseUpload(matrix, nearPlane, farPlane);
		else
			Depth::SetReverseProjection(matrix, nearPlane, farPlane);
	}

	// Published and uploaded cameras can precede the engine's global near/far update.
	void ReverseClipZ(Matrix& matrix, ZAxis axis)
	{
		if (axis == ZAxis::kCol2) {
			for (int col = 0; col < 4; ++col)
				matrix.m[2][col] = matrix.m[3][col] - matrix.m[2][col];
		} else {
			for (int row = 0; row < 4; ++row)
				matrix.m[row][2] = matrix.m[row][3] - matrix.m[row][2];
		}
	}

	/**
	 * @brief The engine's published per-eye camera, which b12's camera matrices are filled from.
	 *
	 * EYE_POSITION's eye[] array is its only member, so the array's address is eye 0's ViewData
	 * and entries sit sizeof(ViewData) apart. getEye() returns by value and cannot be written through.
	 */
	RE::BSGraphics::ViewData* PublishedViewData(uint32_t a_eye)
	{
		auto* shadowState = globals::game::shadowState;
		if (!shadowState)
			return nullptr;
		if (globals::game::isVR) {
			auto* eyes = reinterpret_cast<RE::BSGraphics::ViewData*>(std::addressof(shadowState->GetVRRuntimeData().cameraData));
			return eyes + a_eye;
		}
		return a_eye == 0 ? reinterpret_cast<RE::BSGraphics::ViewData*>(std::addressof(shadowState->GetRuntimeData().cameraData)) : nullptr;
	}

	constexpr uint32_t kStereoEyeCount = 2;

	uint32_t PublishedEyeCount()
	{
		return globals::game::isVR ? kStereoEyeCount : 1u;
	}

	uint32_t PublishedDepthStencilTarget()
	{
		auto* shadowState = globals::game::shadowState;
		if (!shadowState)
			return static_cast<uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
		return globals::game::isVR ? shadowState->GetVRRuntimeData().depthStencil : shadowState->GetRuntimeData().depthStencil;
	}

	bool PublishedProjectionIsReversed()
	{
		for (uint32_t eye = 0; eye < PublishedEyeCount(); ++eye) {
			auto* viewData = PublishedViewData(eye);
			if (!viewData)
				return false;
			const auto axis = DetectZAxis(viewData->projMat);
			if (axis == ZAxis::kUnknown || !AlreadyReversed(viewData->projMat, axis))
				return false;
		}
		return true;
	}

	void ReapplyMappedStates(ID3D11DeviceContext* This);
	void ReapplyNearPlaneShader(ID3D11DeviceContext* context);
	void RefreshBoundDepthTarget();

	void SetProjectionReversed(bool a_reversed)
	{
		if (a_reversed == g_projectionReversed)
			return;
		g_projectionReversed = a_reversed;
		if (auto* context = globals::d3d::context)
			ReapplyMappedStates(context);
	}

	/** @brief The seven camera matrices b12 carries per eye, in the order they are reversed. */
	struct UploadedEye
	{
		Matrix& proj;
		Matrix& viewProj;
		Matrix& viewProjUnjittered;
		Matrix& projUnjittered;
		Matrix& projUnjitteredInverse;
		Matrix& viewProjInverse;
		Matrix& projInverse;
		Matrix& previousViewProj;
	};

	UploadedEye FlatUploadedEye(globals::FrameBuffer& a_frameBuffer)
	{
		return { a_frameBuffer.CameraProj, a_frameBuffer.CameraViewProj, a_frameBuffer.CameraViewProjUnjittered,
			a_frameBuffer.CameraProjUnjittered, a_frameBuffer.CameraProjUnjitteredInverse,
			a_frameBuffer.CameraViewProjInverse, a_frameBuffer.CameraProjInverse, a_frameBuffer.CameraPreviousViewProjUnjittered };
	}

	UploadedEye VRUploadedEye(globals::FrameBufferVR& a_frameBuffer, uint32_t a_eye)
	{
		return { a_frameBuffer.CameraProj[a_eye], a_frameBuffer.CameraViewProj[a_eye], a_frameBuffer.CameraViewProjUnjittered[a_eye],
			a_frameBuffer.CameraProjUnjittered[a_eye], a_frameBuffer.CameraProjUnjitteredInverse[a_eye],
			a_frameBuffer.CameraViewProjInverse[a_eye], a_frameBuffer.CameraProjInverse[a_eye], a_frameBuffer.CameraPreviousViewProjUnjittered[a_eye] };
	}

	bool UploadedProjectionIsStandard(const Matrix& a_proj)
	{
		return std::abs(a_proj.m[3][3]) < 1e-4f &&
		       std::abs(std::abs(a_proj.m[3][2]) - 1.0f) < 1e-3f &&
		       a_proj.m[2][2] * a_proj.m[3][2] >= 0.0f;
	}

	/** @brief Reverses one eye's uploaded matrices, which follow the standard convention until now. */
	void FixupUploadedEye(const UploadedEye& eye)
	{
		if (UploadedProjectionIsStandard(eye.proj)) {
			ReverseClipZ(eye.proj, ZAxis::kCol2);
			ReverseClipZ(eye.viewProj, ZAxis::kCol2);
			ReverseClipZ(eye.viewProjUnjittered, ZAxis::kCol2);
			ReverseClipZ(eye.projUnjittered, ZAxis::kCol2);
		}
		eye.projUnjitteredInverse = eye.projUnjittered.Invert();
		eye.viewProjInverse = eye.viewProj.Invert();
		eye.projInverse = eye.proj.Invert();
		float previousZForward = 0;
		for (int axis = 0; axis < 3; ++axis)
			previousZForward += eye.previousViewProj.m[2][axis] * eye.previousViewProj.m[3][axis];
		if (previousZForward > 0)
			ReverseClipZ(eye.previousViewProj, ZAxis::kCol2);
	}

}

std::pair<std::string, std::vector<std::string>> ReverseZ::GetFeatureSummary()
{
	return { "Improves distant depth precision using floating-point reversed scene depth. Requires a restart and additional depth memory/bandwidth.",
		{ "Reverse Z is enabled by default", "Preserves existing near/far planes and culling modes" } };
}
void ReverseZ::LoadSettings(json& value) { settings = value; }
void ReverseZ::SaveSettings(json& value) { value = settings; }
void ReverseZ::RestoreDefaultSettings() { settings = {}; }
json ReverseZ::GetDiagnostics() const
{
	std::scoped_lock lock(failureMutex);
	return { { "requested", settings.EnableReverseZBuffer }, { "bootRequested", requestedThisBoot },
		{ "active", IsActive() }, { "renderingBlocked", g_renderingRejected.load(std::memory_order_acquire) },
		{ "generation", GetGeneration() }, { "additionalDepthBytesPerPixel", GetAdditionalDepthBytesPerPixel() },
		{ "convertedTargetMask", convertedTargetMask }, { "failure", failure },
		{ "failurePhase", failurePhase }, { "failureGeneration", failureGeneration },
		{ "activationRejected", activationRejectedThisBoot },
		{ "restartRequired", !failure.empty() || (bootLatched && requestedThisBoot != (loaded && settings.EnableReverseZBuffer)) } };
}
void ReverseZ::DrawSettings()
{
	if (!globals::state || !globals::state->IsDeveloperMode())
		return;
	ImGui::SeparatorText("Depth precision");
	ImGui::Checkbox("Enable Reverse Z (restart required)", &settings.EnableReverseZBuffer);
	ImGui::TextUnformatted(activeThisBoot ? "Active: Reverse Z" : "Active: Standard Z");
	std::scoped_lock lock(failureMutex);
	if (!failure.empty())
		ImGui::TextWrapped("%s (phase: %s, target generation: %llu). %s Restart to retry Reverse Z, or disable it and restart.",
			failure.c_str(), failurePhase, static_cast<unsigned long long>(failureGeneration),
			IsActive() ? "Scene rendering is blocked." : "Standard depth remains active for this session.");
}

void ReverseZ::LatchBootState()
{
	if (bootLatched)
		return;
	bootLatched = true;
	requestedThisBoot = loaded && settings.EnableReverseZBuffer;
	logger::info("ReverseZ boot request: {}", requestedThisBoot ? "enabled" : "disabled");
}

bool ReverseZ::HasShaderDefine(RE::BSShader::Type)
{
	LatchBootState();
	return activeThisBoot;
}

void ReverseZ::DataLoaded()
{
	LatchBootState();
}

void ReverseZ::SetupResources() {}

bool ReverseZ::PrepareDepthTargets()
{
	LatchBootState();
	if (!requestedThisBoot || activationRejectedThisBoot)
		return true;
	if (g_renderingRejected.load(std::memory_order_acquire))
		return RejectDepthPreparation("Scene rendering already blocked until restart");
	auto* renderer = globals::game::renderer;
	auto* device = globals::d3d::device;
	if (!globals::shaderCache || !globals::shaderCache->IsEnabled() || !globals::state ||
		!globals::state->enableVShaders || !globals::state->enablePShaders)
		return RejectDepthPreparation("Reverse Z requires scene vertex and pixel shader replacements");
	if (!renderer || !device || !globals::d3d::context)
		return RejectDepthPreparation("Renderer, device or context unavailable during depth preparation");
	if (FAILED(device->GetDeviceRemovedReason()))
		return RejectDepthPreparation("D3D device lost during depth preparation");
	if (globals::game::isVR && !globals::IsVRFrameBufferUploadHookReady())
		return RejectDepthPreparation("Validated VR framebuffer upload hook unavailable");
	InstallRuntimeHooks();
	if (!runtimeHooksInstalled || !cameraHooksInstalled)
		return RejectDepthPreparation("Required depth hooks unavailable");
	if (!PrepareNativeShaders())
		return RejectDepthPreparation("Required native VR shader preparation failed");
	if (!PrepareRejectedDepthState(device) || !GetReversedState(nullptr) || g_renderingRejected.load(std::memory_order_relaxed))
		return RejectDepthPreparation("Default reversed depth state unavailable");
	using Data = std::remove_reference_t<decltype(renderer->GetDepthStencilData().depthStencils[0])>;
	constexpr size_t count = std::extent_v<decltype(Data::views)>;
	struct Staged
	{
		Data* target{};
		uint32_t index{};
		winrt::com_ptr<ID3D11Texture2D> texture;
		std::array<winrt::com_ptr<ID3D11DepthStencilView>, count> views, readOnly;
		winrt::com_ptr<ID3D11ShaderResourceView> depth, stencil;
	};
	std::vector<Staged> staged;
	winrt::com_ptr<ID3D11DepthStencilView> boundDepth;
	std::array<winrt::com_ptr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> boundColors;
	std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colorViews{};
	globals::d3d::context->OMGetRenderTargets(static_cast<UINT>(colorViews.size()), colorViews.data(), boundDepth.put());
	for (size_t i = 0; i < colorViews.size(); ++i) boundColors[i].attach(colorViews[i]);
	ID3D11DepthStencilView* replacementBoundDepth = nullptr;
	uint64_t extraBytes = 0, mainPixels = 0;
	for (auto target : kReverseTargets) {
		auto& data = renderer->GetDepthStencilData().depthStencils[target];
		if (!data.texture) {
			if (target == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN ||
				(globals::game::isVR && target == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_DOWNSAMPLE))
				return RejectDepthPreparation("Required scene depth target absent");
			continue;
		}
		Staged item;
		item.target = &data;
		item.index = static_cast<uint32_t>(target);
		D3D11_TEXTURE2D_DESC desc{};
		data.texture->GetDesc(&desc);
		if (desc.Format != kStandardDepthTypeless)
			return RejectDepthPreparation("Unexpected scene depth format");
		if (target == RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN)
			mainPixels = uint64_t(desc.Width) * desc.Height;
		for (UINT mip = 0; mip < desc.MipLevels; ++mip)
			extraBytes += uint64_t(std::max(1u, desc.Width >> mip)) * std::max(1u, desc.Height >> mip) * desc.ArraySize * desc.SampleDesc.Count * 4u;
		desc.Format = kFloatDepthTypeless;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, item.texture.put())))
			return RejectDepthPreparation("Floating-point scene depth allocation failed");
		Util::SetResourceName(item.texture.get(), "ReverseZ::SceneDepth");
		for (size_t i = 0; i < count; ++i)
			if (!RecreateFloatView(device, item.texture.get(), data.views[i], item.views[i]) ||
				!RecreateFloatView(device, item.texture.get(), data.readOnlyViews[i], item.readOnly[i]))
				return RejectDepthPreparation("Scene depth view preparation failed");
		if (!RecreateFloatView(device, item.texture.get(), data.depthSRV, item.depth) ||
			!RecreateFloatView(device, item.texture.get(), data.stencilSRV, item.stencil))
			return RejectDepthPreparation("Scene depth SRV preparation failed");
		// Staged views are not registered yet, so these clears use physical depth.
		for (const auto& view : item.views)
			if (view)
				globals::d3d::context->ClearDepthStencilView(view.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
		staged.push_back(std::move(item));
	}
	if (staged.empty())
		return RejectDepthPreparation("No scene depth targets available");
	// The owner retains the preceding generation before creation. These new
	// standard resources have never been submitted to the GPU.
	decltype(g_viewIsReverse) preparedViews;
	preparedViews.reserve(staged.size() * count * 2);
	for (const auto& item : staged) {
		for (const auto* views : { &item.views, &item.readOnly })
			for (const auto& view : *views)
				if (view)
					preparedViews.emplace(view.get(), ViewVerdict{ view, true });
	}
	std::unique_lock lock(g_viewMutex);
	g_viewIsReverse.swap(preparedViews);
	convertedTargetMask = 0;
	for (auto& item : staged) {
		auto& data = *item.target;
		for (size_t i = 0; i < count; ++i) {
			if (data.views[i] == boundDepth.get())
				replacementBoundDepth = item.views[i].get();
			if (data.readOnlyViews[i] == boundDepth.get())
				replacementBoundDepth = item.readOnly[i].get();
			ReplaceEngineView(data.views[i], item.views[i], "ReverseZ::SceneDepth DSV");
			ReplaceEngineView(data.readOnlyViews[i], item.readOnly[i], "ReverseZ::SceneDepth RODSV");
		}
		ReplaceEngineView(data.depthSRV, item.depth, "ReverseZ::SceneDepth SRV");
		ReplaceEngineView(data.stencilSRV, item.stencil, "ReverseZ::SceneDepth Stencil SRV");
		data.texture->Release();
		data.texture = item.texture.detach();
		convertedTargetMask |= 1u << item.index;
	}
	lock.unlock();
	if (!activeThisBoot && globals::shaderCache)
		globals::shaderCache->Clear();
	depthTargetsConverted = true;
	extraDepthBytesPerPixel = mainPixels ? double(extraBytes) / mainPixels : 0;
	VRDepthCullingTemporal::InvalidateDepthGeneration();
	generation.fetch_add(1, std::memory_order_release);
	activeThisBoot.store(true, std::memory_order_release);
	if (replacementBoundDepth)
		globals::d3d::context->OMSetRenderTargets(static_cast<UINT>(colorViews.size()), colorViews.data(), replacementBoundDepth);
	return true;
}

bool ReverseZ::IsConvertedDepthTarget(uint32_t a_target) const
{
	return a_target < 32 && ((convertedTargetMask >> a_target) & 1u) != 0;
}

bool ReverseZ::IsReverseDepthView(ID3D11DepthStencilView* view) const
{
	if (!activeThisBoot || !view || (!g_reversalActive.load(std::memory_order_acquire) && !g_renderingRejected.load(std::memory_order_acquire)))
		return false;
	std::shared_lock lock(g_viewMutex);
	return g_viewIsReverse.contains(view);
}
void ReverseZ::RegisterSceneDepthView(ID3D11DepthStencilView* view)
{
	if (!activeThisBoot || !view)
		return;
	std::unique_lock lock(g_viewMutex);
	ViewVerdict verdict{ {}, true };
	verdict.view.copy_from(view);
	g_viewIsReverse.insert_or_assign(view, std::move(verdict));
}

ID3D11DepthStencilView* ReverseZ::ResolveCubemapFaceDepthView(ID3D11RenderTargetView* a_renderTarget, ID3D11DepthStencilView* a_depthView) const
{
	if (!a_renderTarget || !a_depthView || !IsReverseDepthView(a_depthView))
		return a_depthView;
	auto* renderer = globals::game::renderer;
	if (!renderer)
		return a_depthView;
	const auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
	for (auto* faceView : cubemap.cubeSideRTV) {
		if (faceView == a_renderTarget) {
			auto* cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS].views[0];
			return cubemapDepth ? cubemapDepth : a_depthView;
		}
	}
	return a_depthView;
}

void ReverseZ::RecordFailure(const char* reason, const char* phase, uint64_t targetGeneration)
{
	std::scoped_lock lock(failureMutex);
	if (!failure.empty())
		return;
	failure = reason;
	failurePhase = phase;
	failureGeneration = targetGeneration;
	logger::error("ReverseZ: {} (phase: {}, target generation: {}). {} Restart to retry Reverse Z, or disable it and restart.",
		failure, failurePhase, failureGeneration,
		IsActive() ? "Scene rendering blocked." : "Activation declined; standard depth retained for this session.");
}

bool ReverseZ::RejectDepthPreparation(const char* reason)
{
	const bool active = IsActive();
	g_renderingRejected.store(active, std::memory_order_release);
	RecordFailure(reason, active ? "target_recreation" : "activation", GetGeneration() + 1);
	if (active) {
		std::scoped_lock lock(failureMutex);
		throw std::runtime_error(failure);
	}
	activationRejectedThisBoot = true;
	return true;
}

void ReverseZ::RejectRendering(const char* reason)
{
	if (!g_renderingRejected.exchange(true, std::memory_order_acq_rel)) {
		RecordFailure(reason, IsActive() ? "rendering" : "activation", GetGeneration() + (IsActive() ? 0 : 1));
		// An invalid first world camera must also block the already-bound menu depth target.
		if (IsActive() && !g_reversalActive.load(std::memory_order_acquire))
			RefreshBoundDepthTarget();
	}
}

ID3D11RasterizerState* ReverseZ::GetReversedRasterizerState(ID3D11RasterizerState* a_state)
{
	if (!a_state || g_renderingRejected.load(std::memory_order_relaxed))
		return nullptr;

	std::scoped_lock lock(g_rasterMutex);
	if (auto it = g_reversedRasterStates.find(a_state); it != g_reversedRasterStates.end())
		return it->second.reversed ? it->second.reversed.get() : a_state;

	ReversedRasterState entry;
	entry.source.copy_from(a_state);

	D3D11_RASTERIZER_DESC desc{};
	a_state->GetDesc(&desc);
	if (desc.DepthBias == 0 && desc.SlopeScaledDepthBias == 0.0f && desc.DepthBiasClamp == 0.0f) {
		g_reversedRasterStates.insert_or_assign(a_state, std::move(entry));
		return a_state;
	}
	if (desc.DepthBias == std::numeric_limits<INT>::min() || !std::isfinite(desc.SlopeScaledDepthBias) || !std::isfinite(desc.DepthBiasClamp)) {
		RejectRendering("Depth bias cannot be represented");
		return nullptr;
	}
	desc.DepthBias = -desc.DepthBias;
	desc.SlopeScaledDepthBias = -desc.SlopeScaledDepthBias;
	desc.DepthBiasClamp = -desc.DepthBiasClamp;

	if (FAILED(globals::d3d::device->CreateRasterizerState(&desc, entry.reversed.put()))) {
		RejectRendering("Rasterizer state creation failed");
		return nullptr;
	}
	Util::SetResourceName(entry.reversed.get(), "ReverseZ::RasterizerState");
	auto* raw = entry.reversed.get();
	g_reversedRasterStates.insert_or_assign(a_state, std::move(entry));
	return raw;
}

ID3D11DepthStencilState* ReverseZ::GetReversedState(ID3D11DepthStencilState* a_state)
{
	if (g_renderingRejected.load(std::memory_order_relaxed))
		return g_rejectedDepthState.get();
	std::scoped_lock lock(g_stateMutex);
	if (auto it = g_reversedStates.find(a_state); it != g_reversedStates.end())
		return it->second.reversed ? it->second.reversed.get() : a_state;

	ReversedState entry;
	entry.source.copy_from(a_state);

	D3D11_DEPTH_STENCIL_DESC desc{};
	if (a_state)
		a_state->GetDesc(&desc);
	else {
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_LESS;
		desc.StencilReadMask = desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		desc.FrontFace = desc.BackFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS };
	}
	desc.DepthFunc = FlipComparison(desc.DepthFunc);

	if (FAILED(globals::d3d::device->CreateDepthStencilState(&desc, entry.reversed.put()))) {
		RejectRendering("Depth state creation failed");
		return g_rejectedDepthState.get();
	}
	Util::SetResourceName(entry.reversed.get(), "ReverseZ::DepthStencilState");
	auto* raw = entry.reversed.get();
	g_reversedStates.insert_or_assign(a_state, std::move(entry));
	return raw;
}

void ReverseZ::ApplyReverseProjection(void* a_cameraStateEntry, const RE::NiCamera* a_camera)
{
	if (!activeThisBoot || !depthTargetsConverted || !a_cameraStateEntry || !a_camera)
		return;
	if (a_camera != RE::Main::WorldRootCamera())
		return;

	std::span<RE::BSGraphics::ViewData> eyes;
	if (globals::game::isVR) {
		if (!a_camera->GetVRRuntimeData().viewFrustumArray) {
			RejectRendering("Missing stereo camera frusta");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
		auto& vrEyes = static_cast<CameraStateEntryVR*>(a_cameraStateEntry)->viewData;
		if (vrEyes.size() != kStereoEyeCount || !vrEyes.data()) {
			RejectRendering("Incomplete stereo camera state");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
		eyes = { vrEyes.data(), vrEyes.size() };
	} else {
		eyes = { &static_cast<CameraStateEntry*>(a_cameraStateEntry)->viewData, 1 };
	}

	const auto* frusta = globals::game::isVR ? a_camera->GetVRRuntimeData().viewFrustumArray : &a_camera->GetRuntimeData2().viewFrustum;
	// Admit the complete camera before changing either eye's depth convention.
	std::array<ZAxis, kStereoEyeCount> axes{};
	for (size_t eye = 0; eye < eyes.size(); ++eye) {
		const auto& frustum = frusta[eye];
		if (!Depth::IsValidRange(frustum.fNear, frustum.fFar)) {
			RejectRendering("Invalid camera frustum");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
		axes[eye] = DetectZAxis(eyes[eye].projMat);
		if ((axes[eye] == ZAxis::kUnknown) != (axes[0] == ZAxis::kUnknown)) {
			RejectRendering("Inconsistent stereo projection kinds");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
	}
	bool reversed = false;
	uint32_t eyeIndex = 0;
	for (auto& viewData : eyes) {
		const auto axis = axes[eyeIndex];
		const auto& frustum = frusta[eyeIndex++];
		if (axis == ZAxis::kUnknown || AlreadyReversed(viewData.projMat, axis))
			continue;

		ReverseClipZ(viewData.projMat, axis, frustum.fNear, frustum.fFar);
		ReverseClipZ(viewData.projMatrixUnjittered, axis, frustum.fNear, frustum.fFar);
		ReverseClipZ(viewData.viewProjMat, axis, frustum.fNear, frustum.fFar);
		ReverseClipZ(viewData.viewProjMatrixUnjittered, axis, frustum.fNear, frustum.fFar);
		reversed = true;
	}
	if (reversed && !g_reversalActive.exchange(true, std::memory_order_acq_rel)) {
		RefreshBoundDepthTarget();
		logger::info("ReverseZ: world-camera depth reversal activated");
	}
}

void ReverseZ::ReversePublishedProjection()
{
	// A reversal before the targets exist would put the standard depth targets behind a
	// reversed projection, which inverts every depth test and clear with nothing to match it.
	if (!activeThisBoot || !depthTargetsConverted || !g_reversalActive.load(std::memory_order_acquire))
		return;
	if (!IsConvertedDepthTarget(PublishedDepthStencilTarget()))
		return;

	std::array<RE::BSGraphics::ViewData*, kStereoEyeCount> eyes{};
	std::array<ZAxis, kStereoEyeCount> axes{};
	const auto eyeCount = PublishedEyeCount();
	for (uint32_t eye = 0; eye < eyeCount; ++eye) {
		eyes[eye] = PublishedViewData(eye);
		if (!eyes[eye]) {
			RejectRendering("Published scene camera unavailable");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
		axes[eye] = DetectZAxis(eyes[eye]->projMat);
		if ((axes[eye] == ZAxis::kUnknown) != (axes[0] == ZAxis::kUnknown)) {
			RejectRendering("Inconsistent published projection kinds");
			ReapplyMappedStates(globals::d3d::context);
			return;
		}
	}
	bool reversed = false;
	uint32_t eyeIndex = 0;
	for (auto* viewData : std::span{ eyes.data(), eyeCount }) {
		const auto axis = axes[eyeIndex++];
		if (axis == ZAxis::kUnknown || AlreadyReversed(viewData->projMat, axis))
			continue;
		reversed = true;
		ReverseClipZ(viewData->projMat, axis);
		ReverseClipZ(viewData->projMatrixUnjittered, axis);
		ReverseClipZ(viewData->viewProjMat, axis);
		ReverseClipZ(viewData->viewProjMatrixUnjittered, axis);
	}
	if (reversed)
		g_reversalActive.store(true, std::memory_order_relaxed);
}

bool ReverseZ::ExpectPublishedReversal(const RE::NiCamera* a_camera, bool a_renderingCubemap) const
{
	if (!activeThisBoot || !depthTargetsConverted || !a_camera || !g_reversalActive.load(std::memory_order_acquire))
		return false;
	if (a_camera == RE::Main::WorldRootCamera())
		return true;
	if (a_renderingCubemap)
		return false;
	if (!IsConvertedDepthTarget(PublishedDepthStencilTarget()))
		return false;
	return !a_camera->GetRuntimeData2().viewFrustum.bOrtho;
}

void ReverseZ::FixupMappedFrameBuffer(globals::FrameBuffer& buffer)
{
	if (activeThisBoot && g_reversalActive.load(std::memory_order_acquire) && g_projectionReversed) {
		FixupUploadedEye(FlatUploadedEye(buffer));
	}
}
void ReverseZ::FixupMappedFrameBufferVR(globals::FrameBufferVR& buffer)
{
	if (!activeThisBoot || !g_reversalActive.load(std::memory_order_acquire) || !g_projectionReversed)
		return;
	for (uint32_t eye = 0; eye < kStereoEyeCount; ++eye) FixupUploadedEye(VRUploadedEye(buffer, eye));
}

namespace
{
	struct BSGraphics_State_BuildCameraStateData
	{
		static void thunk(RE::BSGraphics::State* a_state, void* a_entry, RE::NiCamera* a_camera, char a_jitter)
		{
			func(a_state, a_entry, a_camera, a_jitter);
			globals::features::reverseZ.ApplyReverseProjection(a_entry, a_camera);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_State_SetCameraData
	{
		static void thunk(RE::BSGraphics::State* a_state, const RE::NiCamera* a_camera, std::uint32_t a_flags)
		{
			const bool renderingCubemap = globals::state && (globals::state->permutationData.ExtraShaderDescriptor & static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections)) != 0;
			const bool expectReversed = globals::features::reverseZ.ExpectPublishedReversal(a_camera, renderingCubemap);
			SetProjectionReversed(expectReversed);
			func(a_state, a_camera, a_flags);
			if (expectReversed && !renderingCubemap)
				globals::features::reverseZ.ReversePublishedProjection();
			SetProjectionReversed(g_reversalActive.load(std::memory_order_relaxed) && PublishedProjectionIsReversed());
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_OMSetDepthStencilState
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef)
		{
			if (This == globals::d3d::context) {
				g_requestedState.copy_from(pDepthStencilState);
				g_requestedStencilRef = StencilRef;
				g_boundState = ShouldFlip() ? globals::features::reverseZ.GetReversedState(pDepthStencilState) : pDepthStencilState;
				func(This, g_boundState, StencilRef);
				return;
			}
			func(This, pDepthStencilState, StencilRef);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_RSSetState
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11RasterizerState* pRasterizerState)
		{
			if (This == globals::d3d::context) {
				g_requestedRasterState.copy_from(pRasterizerState);
				g_boundRasterState = ShouldFlip() ? globals::features::reverseZ.GetReversedRasterizerState(pRasterizerState) : pRasterizerState;
				if (ShouldFlip() && g_renderingRejected.load(std::memory_order_relaxed)) {
					g_boundState = g_rejectedDepthState.get();
					ID3D11DeviceContext_OMSetDepthStencilState::func(This, g_boundState, g_requestedStencilRef);
				}
				func(This, g_boundRasterState);
				return;
			}
			func(This, pRasterizerState);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_RSSetViewports
	{
		static void thunk(ID3D11DeviceContext* This, UINT NumViewports, const D3D11_VIEWPORT* pViewports)
		{
			if (This != globals::d3d::context || NumViewports > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE || (NumViewports && !pViewports)) {
				func(This, NumViewports, pViewports);
				return;
			}
			D3D11_VIEWPORT requested[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			D3D11_VIEWPORT mapped[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			const bool flip = ShouldFlip();
			for (UINT i = 0; i < NumViewports; ++i) {
				requested[i] = pViewports[i];
				mapped[i] = flip ? MirrorViewportDepth(requested[i]) : requested[i];
			}
			std::memcpy(g_requestedViewports, requested, sizeof(D3D11_VIEWPORT) * NumViewports);
			std::memcpy(g_mappedViewports, mapped, sizeof(D3D11_VIEWPORT) * NumViewports);
			g_requestedViewportCount = NumViewports;
			func(This, NumViewports, mapped);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void ReapplyMappedStates(ID3D11DeviceContext* This)
	{
		const bool flip = ShouldFlip();
		{
			g_boundState = flip ? globals::features::reverseZ.GetReversedState(g_requestedState.get()) : g_requestedState.get();
			ID3D11DeviceContext_OMSetDepthStencilState::func(This, g_boundState, g_requestedStencilRef);
		}
		{
			g_boundRasterState = flip ? globals::features::reverseZ.GetReversedRasterizerState(g_requestedRasterState.get()) : g_requestedRasterState.get();
			ID3D11DeviceContext_RSSetState::func(This, g_boundRasterState);
			if (flip && g_renderingRejected.load(std::memory_order_relaxed)) {
				g_boundState = g_rejectedDepthState.get();
				ID3D11DeviceContext_OMSetDepthStencilState::func(This, g_boundState, g_requestedStencilRef);
			}
		}
		if (g_requestedViewportCount > 0) {
			for (UINT i = 0; i < g_requestedViewportCount; ++i)
				g_mappedViewports[i] = flip ? MirrorViewportDepth(g_requestedViewports[i]) : g_requestedViewports[i];
			ID3D11DeviceContext_RSSetViewports::func(This, g_requestedViewportCount, g_mappedViewports);
		}
	}

	void UpdateBoundDepthTarget(ID3D11DeviceContext* This, ID3D11DepthStencilView* pDepthStencilView)
	{
		if (This != globals::d3d::context)
			return;
		const bool reverseBound = globals::features::reverseZ.IsReverseDepthView(pDepthStencilView);
		if (reverseBound == g_reverseTargetBound)
			return;
		g_reverseTargetBound = reverseBound;
		ReapplyMappedStates(This);
		ReapplyNearPlaneShader(This);
	}

	void RefreshBoundDepthTarget()
	{
		if (auto* context = globals::d3d::context) {
			winrt::com_ptr<ID3D11DepthStencilView> depth;
			context->OMGetRenderTargets(0, nullptr, depth.put());
			UpdateBoundDepthTarget(context, depth.get());
		}
	}

	struct ID3D11DeviceContext_ClearDepthStencilView
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
		{
			// Converted targets also host standard-projection menu passes.
			if (This == globals::d3d::context && g_projectionReversed && (ClearFlags & D3D11_CLEAR_DEPTH) && globals::features::reverseZ.IsReverseDepthView(pDepthStencilView))
				Depth = 1.0f - Depth;
			func(This, pDepthStencilView, ClearFlags, Depth, Stencil);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	ID3D11RenderTargetView* g_lastClearedCubeFace = nullptr;

	ID3D11DepthStencilView* ResolveFaceDepth(ID3D11DeviceContext* This, ID3D11RenderTargetView* a_renderTarget, ID3D11DepthStencilView* a_depthView)
	{
		auto* resolved = globals::features::reverseZ.ResolveCubemapFaceDepthView(a_renderTarget, a_depthView);
		if (resolved == a_depthView) {
			if (a_renderTarget)
				g_lastClearedCubeFace = nullptr;
			return a_depthView;
		}
		if (a_renderTarget != g_lastClearedCubeFace) {
			g_lastClearedCubeFace = a_renderTarget;
			ID3D11DeviceContext_ClearDepthStencilView::func(This, resolved, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}
		return resolved;
	}

	struct ID3D11DeviceContext_OMSetRenderTargets
	{
		static void thunk(ID3D11DeviceContext* This, UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView)
		{
			if (This == globals::d3d::context && NumViews > 0 && ppRenderTargetViews)
				pDepthStencilView = ResolveFaceDepth(This, ppRenderTargetViews[0], pDepthStencilView);
			func(This, NumViews, ppRenderTargetViews, pDepthStencilView);
			UpdateBoundDepthTarget(This, pDepthStencilView);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews
	{
		static void thunk(ID3D11DeviceContext* This, UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts)
		{
			if (This == globals::d3d::context && NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL && NumRTVs > 0 && ppRenderTargetViews)
				pDepthStencilView = ResolveFaceDepth(This, ppRenderTargetViews[0], pDepthStencilView);
			func(This, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
			if (NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
				UpdateBoundDepthTarget(This, pDepthStencilView);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_OMGetDepthStencilState
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11DepthStencilState** ppDepthStencilState, UINT* pStencilRef)
		{
			func(This, ppDepthStencilState, pStencilRef);
			if (This != globals::d3d::context || !ppDepthStencilState || !*ppDepthStencilState)
				return;
			if (*ppDepthStencilState != g_boundState || g_requestedState.get() == g_boundState)
				return;
			if (g_requestedState)
				g_requestedState->AddRef();
			(*ppDepthStencilState)->Release();
			*ppDepthStencilState = g_requestedState.get();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_RSGetState
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11RasterizerState** ppRasterizerState)
		{
			func(This, ppRasterizerState);
			if (This != globals::d3d::context || !ppRasterizerState)
				return;
			if (*ppRasterizerState != g_boundRasterState || !g_requestedRasterState || g_requestedRasterState.get() == g_boundRasterState)
				return;
			g_requestedRasterState->AddRef();
			if (*ppRasterizerState)
				(*ppRasterizerState)->Release();
			*ppRasterizerState = g_requestedRasterState.get();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_RSGetViewports
	{
		static void thunk(ID3D11DeviceContext* This, UINT* pNumViewports, D3D11_VIEWPORT* pViewports)
		{
			const UINT capacity = pNumViewports ? *pNumViewports : 0;
			func(This, pNumViewports, pViewports);
			if (This != globals::d3d::context || !pNumViewports || !pViewports)
				return;
			const UINT count = std::min({ *pNumViewports, capacity, UINT(D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) });
			if (*pNumViewports == g_requestedViewportCount && std::memcmp(pViewports, g_mappedViewports, sizeof(D3D11_VIEWPORT) * count) == 0)
				std::memcpy(pViewports, g_requestedViewports, sizeof(D3D11_VIEWPORT) * count);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	thread_local bool t_inOccluderDownscale = false;
	ID3D11VertexShader* g_boundVertexShader = nullptr;
	Util::LazyShader<ID3D11VertexShader> g_hiddenAreaMeshVS;
	Util::LazyShader<ID3D11PixelShader> g_occluderDownscalePS;

	/// Tracks whether a pixel shader is bound and swaps the depth-copy pixel shader during the VR occluder downscale.
	struct ID3D11DeviceContext_PSSetShader
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
		{
			if (g_reversalActive.load(std::memory_order_acquire) && globals::features::reverseZ.IsActive() && t_inOccluderDownscale && pPixelShader && This == globals::d3d::context) {
				if (auto* replacement = g_occluderDownscalePS.get())
					pPixelShader = replacement;
			}
			func(This, pPixelShader, ppClassInstances, NumClassInstances);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderDepthBuffer4xDownscale_Render
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			const bool previous = t_inOccluderDownscale;
			const SKSE::stl::scope_exit restore([&]() noexcept { t_inOccluderDownscale = previous; });
			t_inOccluderDownscale = true;
			func(imageSpaceShader, shape, param);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	constexpr std::array<uint32_t, 4> kOccluderBoxVSChecksum{ 0x1cdddbca, 0xd69271b6, 0xcfcb199a, 0xcc9674f9 };
	Depth::NativeVertexShaderRegistry g_nativeVertexShaders;
	Util::LazyShader<ID3D11VertexShader> g_occluderBoxVS;

	/// Records the engine's occlusion box vertex shader by its DXBC checksum so VSSetShader can swap it.
	struct ID3D11Device_CreateVertexShader
	{
		static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader)
		{
			const HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
			constexpr size_t kChecksumOffset = 4;
			if (This == globals::d3d::device && SUCCEEDED(hr) && ppVertexShader && *ppVertexShader && pShaderBytecode && BytecodeLength >= kChecksumOffset + sizeof(kOccluderBoxVSChecksum)) {
				std::array<uint32_t, 4> checksum;
				std::memcpy(checksum.data(), static_cast<const uint8_t*>(pShaderBytecode) + kChecksumOffset, sizeof(checksum));
				const bool occluder = checksum == kOccluderBoxVSChecksum;
				if (occluder || Depth::IsFixedNearPlaneVertexProgram(pShaderBytecode, BytecodeLength)) {
					g_nativeVertexShaders.Register(*ppVertexShader, occluder ? Depth::NativeVertexProgram::Occluder : Depth::NativeVertexProgram::NearPlane);
				}
			}
			return hr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_VSSetShader
	{
		static void thunk(ID3D11DeviceContext* This, ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances)
		{
			if (This == globals::d3d::context)
				g_boundVertexShader = pVertexShader;
			if (This == globals::d3d::context) {
				const bool active = g_reversalActive.load(std::memory_order_acquire) && globals::features::reverseZ.IsActive();
				const auto program = active ? g_nativeVertexShaders.Classify(pVertexShader) : Depth::NativeVertexProgram::Other;
				if (program == Depth::NativeVertexProgram::Occluder)
					pVertexShader = g_occluderBoxVS.get();
				const bool nearMask = active && pVertexShader &&
				                      (pVertexShader == g_hiddenAreaMeshVS.get() || program == Depth::NativeVertexProgram::NearPlane);
				if (nearMask && g_reverseTargetBound)
					pVertexShader = g_hiddenAreaMeshVS.get();
				if (nearMask != g_nearMaskBound) {
					g_nearMaskBound = nearMask;
					ReapplyMappedStates(This);
				}
			}
			func(This, pVertexShader, ppClassInstances, NumClassInstances);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void ReapplyNearPlaneShader(ID3D11DeviceContext* context)
	{
		if (g_nearMaskBound && g_boundVertexShader)
			ID3D11DeviceContext_VSSetShader::func(context, g_reverseTargetBound ? g_hiddenAreaMeshVS.get() : g_boundVertexShader, nullptr, 0);
	}

	struct ID3D11DeviceContext_ClearState
	{
		static void thunk(ID3D11DeviceContext* context)
		{
			func(context);
			if (context != globals::d3d::context)
				return;
			g_requestedState = nullptr;
			g_boundState = nullptr;
			g_requestedStencilRef = 0;
			g_requestedRasterState = nullptr;
			g_boundRasterState = nullptr;
			g_requestedViewportCount = 0;
			g_reverseTargetBound = false;
			g_nearMaskBound = false;
			g_lastClearedCubeFace = nullptr;
			g_boundVertexShader = nullptr;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void ReverseZ::InstallRuntimeHooks()
{
	LatchBootState();
	if (runtimeHooksInstalled || !requestedThisBoot)
		return;
	// Frame-buffer observers must be ready before reversed depth targets are published.
	auto* context = globals::d3d::context;
	if (!context)
		return;
	context->OMGetDepthStencilState(g_requestedState.put(), &g_requestedStencilRef);
	g_boundState = g_requestedState.get();
	context->RSGetState(g_requestedRasterState.put());
	g_boundRasterState = g_requestedRasterState.get();
	g_requestedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&g_requestedViewportCount, g_requestedViewports);
	std::copy_n(g_requestedViewports, g_requestedViewportCount, g_mappedViewports);
	runtimeHooksInstalled = true;

	stl::detour_vfunc<33, ID3D11DeviceContext_OMSetRenderTargets>(context);
	stl::detour_vfunc<34, ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews>(context);
	stl::detour_vfunc<36, ID3D11DeviceContext_OMSetDepthStencilState>(context);
	stl::detour_vfunc<43, ID3D11DeviceContext_RSSetState>(context);
	stl::detour_vfunc<44, ID3D11DeviceContext_RSSetViewports>(context);
	stl::detour_vfunc<53, ID3D11DeviceContext_ClearDepthStencilView>(context);
	stl::detour_vfunc<92, ID3D11DeviceContext_OMGetDepthStencilState>(context);
	stl::detour_vfunc<94, ID3D11DeviceContext_RSGetState>(context);
	stl::detour_vfunc<95, ID3D11DeviceContext_RSGetViewports>(context);
	stl::detour_vfunc<110, ID3D11DeviceContext_ClearState>(context);
	if (globals::game::isVR) {
		stl::detour_vfunc<9, ID3D11DeviceContext_PSSetShader>(context);
		stl::detour_vfunc<11, ID3D11DeviceContext_VSSetShader>(context);
		stl::detour_vfunc<12, ID3D11Device_CreateVertexShader>(globals::d3d::device);
		stl::write_vfunc<0x1, BSImagespaceShaderDepthBuffer4xDownscale_Render>(RE::VTABLE_BSImagespaceShaderDepthBuffer4xDownscale[3]);
	}

	logger::info("ReverseZ: installed depth-state, depth-clear and state read-back hooks");
}

bool ReverseZ::PrepareNativeShaders()
{
	if (!globals::game::isVR)
		return true;
	const std::vector<std::pair<const char*, const char*>> defines{ { "REVERSE_Z", "1" } };
	return g_occluderDownscalePS.Get(L"Data/Shaders/ReverseZ/OccluderDepthDownscalePS.hlsl", defines, "ps_5_0") &&
	       g_occluderBoxVS.Get(L"Data/Shaders/ReverseZ/OccluderBoxVS.hlsl", defines, "vs_5_0") &&
	       g_hiddenAreaMeshVS.Get(L"Data/Shaders/ReverseZ/HiddenAreaMeshVS.hlsl", defines, "vs_5_0");
}

void ReverseZ::PostPostLoad()
{
	LatchBootState();
	if (!requestedThisBoot)
		return;
	stl::detour_thunk<BSGraphics_State_BuildCameraStateData>(REL::RelocationID(75711, 77520));
	stl::detour_thunk<BSGraphics_State_SetCameraData>(REL::RelocationID(75694, 77503));
	cameraHooksInstalled = true;
	logger::info("ReverseZ: installed camera state build and publish hooks");
}

#undef I18N_KEY_PREFIX
