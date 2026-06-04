#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Hooks.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/VRUtils.h"
#include <cmath>
#include <cstring>
#include <DirectXMath.h>
#include <limits>
#include <SimpleMath.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <imgui_impl_dx11.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;

using AttachMode = VR::Settings::OverlayAttachMode;

//=============================================================================
// IN-SCENE OVERLAY RENDERING VIA SUBMIT HOOK
//=============================================================================

namespace
{
	bool ShouldRenderInSceneMenu(const VR& vr)
	{
		return vr.ShouldUseInSceneOverlay() &&
		       globals::menu &&
		       (globals::menu->IsEnabled || globals::menu->overlayVisible) &&
		       vr.menuTexture &&
		       vr.settings.attachMode != AttachMode::None;
	}

	bool MatchesSubmitCopyDesc(const D3D11_TEXTURE2D_DESC& lhs, const D3D11_TEXTURE2D_DESC& rhs)
	{
		return lhs.Width == rhs.Width &&
		       lhs.Height == rhs.Height &&
		       lhs.MipLevels == rhs.MipLevels &&
		       lhs.ArraySize == rhs.ArraySize &&
		       lhs.Format == rhs.Format &&
		       lhs.SampleDesc.Count == rhs.SampleDesc.Count &&
		       lhs.SampleDesc.Quality == rhs.SampleDesc.Quality &&
		       lhs.Usage == rhs.Usage &&
		       lhs.BindFlags == rhs.BindFlags &&
		       lhs.CPUAccessFlags == rhs.CPUAccessFlags &&
		       lhs.MiscFlags == rhs.MiscFlags;
	}

	DXGI_FORMAT GetRenderTargetViewFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			return format;
		}
	}

	bool SupportsRenderTargetView(ID3D11Device* device, DXGI_FORMAT format)
	{
		if (!device || format == DXGI_FORMAT_UNKNOWN) {
			return false;
		}

		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support))) {
			return false;
		}
		return (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
	}

	bool SupportsUnorderedAccessView(ID3D11Device* device, DXGI_FORMAT format)
	{
		if (!device || format == DXGI_FORMAT_UNKNOWN) {
			return false;
		}

		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support))) {
			return false;
		}
		return (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
	}

	bool EnsureMenuTextureSRV(
		ID3D11Texture2D* texture,
		winrt::com_ptr<ID3D11ShaderResourceView>& srv,
		ID3D11Texture2D*& cachedTexture,
		const char* label)
	{
		if (!texture || !globals::d3d::device) {
			return false;
		}

		if (texture != cachedTexture || !srv) {
			srv = nullptr;
			if (FAILED(globals::d3d::device->CreateShaderResourceView(texture, nullptr, srv.put()))) {
				logger::error("VR: Failed to create {} menu texture SRV", label);
				cachedTexture = nullptr;
				return false;
			}
			cachedTexture = texture;
		}

		return true;
	}

	winrt::com_ptr<ID3D11Texture2D> ResolveSubmitTexture2D(void* handle)
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (!handle) {
			return texture;
		}

		auto* unknown = static_cast<IUnknown*>(handle);
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) {
			return texture;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11Resource), resource.put_void()))) {
			resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
			if (texture) {
				return texture;
			}
		}

		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11ShaderResourceView), srv.put_void()))) {
			resource = nullptr;
			srv->GetResource(resource.put());
			if (resource) {
				resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
				if (texture) {
					return texture;
				}
			}
		}

		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11RenderTargetView), rtv.put_void()))) {
			resource = nullptr;
			rtv->GetResource(resource.put());
			if (resource) {
				resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
			}
		}

		return texture;
	}

	constexpr char kSubmitCompositeCS[] = R"(
cbuffer OverlayCompositeCB : register(b0)
{
	uint2 TargetSize;
	uint2 DispatchOrigin;
	uint2 DispatchSize;
	uint2 Padding;
	float4 QuadPixels01;
	float4 QuadPixels23;
	float4 QuadInvW;
};

Texture2D<float4> MenuTexture : register(t0);
SamplerState MenuSampler : register(s0);
RWTexture2D<float4> Target : register(u0);

bool Barycentric(float2 p, float2 a, float2 b, float2 c, out float3 bary)
{
	float2 v0 = b - a;
	float2 v1 = c - a;
	float2 v2 = p - a;
	float denom = v0.x * v1.y - v1.x * v0.y;
	if (abs(denom) < 1e-5f) {
		bary = 0.0f;
		return false;
	}

	float invDenom = rcp(denom);
	float u = (v2.x * v1.y - v1.x * v2.y) * invDenom;
	float v = (v0.x * v2.y - v2.x * v0.y) * invDenom;
	float w = 1.0f - u - v;
	bary = float3(w, u, v);
	return u >= 0.0f && v >= 0.0f && w >= 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= DispatchSize.x || dispatchThreadID.y >= DispatchSize.y) {
		return;
	}

	uint2 targetPixel = DispatchOrigin + dispatchThreadID.xy;
	if (targetPixel.x >= TargetSize.x || targetPixel.y >= TargetSize.y) {
		return;
	}

	float2 p = float2(targetPixel) + 0.5f;
	float2 q0 = QuadPixels01.xy;
	float2 q1 = QuadPixels01.zw;
	float2 q2 = QuadPixels23.xy;
	float2 q3 = QuadPixels23.zw;

	float3 bary = 0.0f;
	float2 uv = 0.0f;
	if (Barycentric(p, q0, q1, q2, bary)) {
		float invW = bary.x * QuadInvW.x + bary.y * QuadInvW.y + bary.z * QuadInvW.z;
		if (abs(invW) < 1e-6f) {
			return;
		}
		uv = (bary.x * float2(0.0f, 1.0f) * QuadInvW.x +
		      bary.y * float2(0.0f, 0.0f) * QuadInvW.y +
		      bary.z * float2(1.0f, 0.0f) * QuadInvW.z) / invW;
	} else if (Barycentric(p, q0, q2, q3, bary)) {
		float invW = bary.x * QuadInvW.x + bary.y * QuadInvW.z + bary.z * QuadInvW.w;
		if (abs(invW) < 1e-6f) {
			return;
		}
		uv = (bary.x * float2(0.0f, 1.0f) * QuadInvW.x +
		      bary.y * float2(1.0f, 0.0f) * QuadInvW.z +
		      bary.z * float2(1.0f, 1.0f) * QuadInvW.w) / invW;
	} else {
		return;
	}

	uv = saturate(uv);

	float4 menuColor = MenuTexture.SampleLevel(MenuSampler, uv, 0.0f);
	menuColor.a = saturate(menuColor.a);
	if (menuColor.a <= 0.001f) {
		return;
	}

	float4 sceneColor = Target[targetPixel];
	Target[targetPixel] = float4(lerp(sceneColor.rgb, menuColor.rgb, menuColor.a), sceneColor.a);
}
)";

	struct IVRCompositor_Submit
	{
		static vr::EVRCompositorError thunk(vr::IVRCompositor* _this, vr::EVREye eEye, const vr::Texture_t* pTexture, const vr::VRTextureBounds_t* pBounds, vr::EVRSubmitFlags nSubmitFlags)
		{
			auto& vr = globals::features::vr;
			auto& upscaling = globals::features::upscaling;

			// Only process DirectX textures - skip OpenGL/Vulkan to avoid undefined behavior
			if (pTexture && pTexture->handle && pTexture->eType == vr::TextureType_DirectX) {
				vr::Texture_t upscaledTexture{};
				vr::VRTextureBounds_t upscaledBounds{};
				if (upscaling.SubmitVRUpscaledFrame(eEye, pTexture, pBounds, upscaledTexture, upscaledBounds)) {
					if (ShouldRenderInSceneMenu(vr) &&
						upscaledTexture.handle &&
						upscaledTexture.eType == vr::TextureType_DirectX)
						vr.RenderInSceneOverlay(eEye, static_cast<ID3D11Texture2D*>(upscaledTexture.handle), &upscaledBounds);
					upscaling.LogVRCompositorSubmitPath(eEye, "cs-upscaled-submit", pTexture, pBounds, &upscaledTexture, &upscaledBounds, nSubmitFlags);
					return func(_this, eEye, &upscaledTexture, &upscaledBounds, nSubmitFlags);
				}

				vr::Texture_t overlayTexture{};
				if (vr.PrepareInSceneOverlaySubmitTexture(eEye, pTexture, pBounds, overlayTexture)) {
					upscaling.LogVRCompositorSubmitPath(eEye, "overlay-submit", pTexture, pBounds, &overlayTexture, pBounds, nSubmitFlags);
					return func(_this, eEye, &overlayTexture, pBounds, nSubmitFlags);
				}
			}
			upscaling.LogVRCompositorSubmitPath(eEye, "original-submit-fallback", pTexture, pBounds, nullptr, nullptr, nSubmitFlags);
			return func(_this, eEye, pTexture, pBounds, nSubmitFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void VR::InitInSceneResources()
{
	if (inSceneResources.initialized)
		return;

	InSceneResources temp = {};

	auto device = globals::d3d::device;

	// 1. Compile shaders - compile VS to get bytecode for input layout, PS separately
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	// Compile vertex shader
	if (FAILED(D3DCompileFromFile(L"Data\\Shaders\\VR\\InSceneOverlay.vs.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR InScene VS compile error: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	// Compile pixel shader
	if (FAILED(D3DCompileFromFile(L"Data\\Shaders\\VR\\InSceneOverlay.ps.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR InScene PS compile error: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		if (vsBlob)
			vsBlob->Release();
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	// Create shader objects from bytecode
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)) ||
		FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
		logger::error("VR: Failed to create shader objects");
		if (vs)
			vs->Release();
		if (ps)
			ps->Release();
		if (vsBlob)
			vsBlob->Release();
		if (psBlob)
			psBlob->Release();
		return;
	}

	temp.vs.attach(vs);
	temp.ps.attach(ps);
	if (psBlob)
		psBlob->Release();  // Don't need PS blob anymore

	// 2. Input Layout
	D3D11_INPUT_ELEMENT_DESC polygonLayout[2];
	polygonLayout[0].SemanticName = "POSITION";
	polygonLayout[0].SemanticIndex = 0;
	polygonLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout[0].InputSlot = 0;
	polygonLayout[0].AlignedByteOffset = 0;
	polygonLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[0].InstanceDataStepRate = 0;

	polygonLayout[1].SemanticName = "TEXCOORD";
	polygonLayout[1].SemanticIndex = 0;
	polygonLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout[1].InputSlot = 0;
	polygonLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[1].InstanceDataStepRate = 0;

	if (FAILED(device->CreateInputLayout(polygonLayout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), temp.inputLayout.put()))) {
		logger::error("VR: Failed to create input layout");
		vsBlob->Release();
		return;
	}

	vsBlob->Release();

	// 3. Buffers
	// Quad Vertices (XY plane, z=0, size=1)
	struct VertexType
	{
		XMFLOAT3 position;
		XMFLOAT2 texture;
	};
	VertexType vertices[4] = {
		{ XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) },  // Bottom Left
		{ XMFLOAT3(-0.5f, 0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) },   // Top Left
		{ XMFLOAT3(0.5f, 0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) },    // Top Right
		{ XMFLOAT3(0.5f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) }    // Bottom Right
	};

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(VertexType) * 4;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vertexData = {};
	vertexData.pSysMem = vertices;
	if (FAILED(device->CreateBuffer(&vertexBufferDesc, &vertexData, temp.vb.put()))) {
		logger::error("VR: Failed to create vertex buffer");
		return;
	}

	unsigned long indices[6] = { 0, 1, 2, 0, 2, 3 };
	D3D11_BUFFER_DESC indexBufferDesc = {};
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long) * 6;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA indexData = {};
	indexData.pSysMem = indices;
	if (FAILED(device->CreateBuffer(&indexBufferDesc, &indexData, temp.ib.put()))) {
		logger::error("VR: Failed to create index buffer");
		return;
	}

	D3D11_BUFFER_DESC matrixBufferDesc = {};
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(InSceneCB);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&matrixBufferDesc, nullptr, temp.cb.put()))) {
		logger::error("VR: Failed to create constant buffer");
		return;
	}

	// 4. States
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
	if (FAILED(device->CreateBlendState(&blendDesc, temp.blendState.put()))) {
		logger::error("VR: Failed to create blend state");
		return;
	}

	D3D11_DEPTH_STENCIL_DESC depthDesc = {};
	depthDesc.DepthEnable = FALSE;  // Always on top
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(device->CreateDepthStencilState(&depthDesc, temp.depthState.put()))) {
		logger::error("VR: Failed to create depth stencil state");
		return;
	}

	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterDesc, temp.rasterizerState.put()))) {
		logger::error("VR: Failed to create rasterizer state");
		return;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&samplerDesc, temp.sampler.put()))) {
		logger::error("VR: Failed to create sampler state");
		return;
	}
	Util::SetResourceName(temp.sampler.get(), "VR::InSceneOverlaySampler");

	ID3DBlob* csBlob = nullptr;
	if (FAILED(D3DCompile(kSubmitCompositeCS, sizeof(kSubmitCompositeCS) - 1, "VRSubmitMenuCompositeCS", nullptr, nullptr,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &csBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR submit menu composite CS compile error: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
			errorBlob->Release();
		}
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}
	if (FAILED(device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, temp.submitCompositeCS.put()))) {
		logger::error("VR: Failed to create submit menu composite compute shader");
		csBlob->Release();
		return;
	}
	csBlob->Release();
	Util::SetResourceName(temp.submitCompositeCS.get(), "VR::SubmitMenuCompositeCS");

	D3D11_BUFFER_DESC submitCompositeCBDesc = {};
	submitCompositeCBDesc.Usage = D3D11_USAGE_DYNAMIC;
	submitCompositeCBDesc.ByteWidth = sizeof(SubmitCompositeCB);
	submitCompositeCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	submitCompositeCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&submitCompositeCBDesc, nullptr, temp.submitCompositeCB.put()))) {
		logger::error("VR: Failed to create submit menu composite constant buffer");
		return;
	}
	Util::SetResourceName(temp.submitCompositeCB.get(), "VR::SubmitMenuCompositeCB");

	inSceneResources = std::move(temp);
	inSceneResources.initialized = true;
	logger::debug("VR: In-Scene Overlay resources initialized.");
}

void VR::RenderInSceneOverlay(vr::EVREye eye, ID3D11Texture2D* targetTexture, const vr::VRTextureBounds_t* bounds, ID3D11RenderTargetView* targetRTV)
{
	auto context = globals::d3d::context;
	if (!context || !targetTexture) {
		return;
	}

	if (!inSceneResources.initialized)
		InitInSceneResources();
	if (!inSceneResources.initialized) {
		return;
	}

	// Only render if overlay should be visible
	if (!ShouldRenderInSceneMenu(*this)) {
		return;
	}

	// We can't render if we don't have HMD pose
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return;
	}

	// Get HMD Pose and Eye matrices
	const bool hasState = globals::state != nullptr;
	const uint32_t currentFrame = hasState ? globals::state->frameCount : 0;
	const bool shouldRefreshPoses =
		!hasState ||
		!inSceneResources.cachedPosesValid ||
		inSceneResources.cachedPoseFrame != currentFrame;

	if (shouldRefreshPoses) {
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		if (!compositor) {
			compositor = openvr->vrContext.vrCompositor;
		}
		if (!compositor) {
			return;
		}

		auto compositorError = compositor->GetLastPoses(
			inSceneResources.cachedRenderPoses,
			vr::k_unMaxTrackedDeviceCount,
			nullptr,
			0);
		if (compositorError != vr::VRCompositorError_None) {
			return;
		}

		inSceneResources.cachedPoseFrame = currentFrame;
		inSceneResources.cachedPosesValid = true;
	}

	const vr::TrackedDevicePose_t& hmdPose = inSceneResources.cachedRenderPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if (!hmdPose.bPoseIsValid) {
		return;
	}

	Matrix hmdWorld = Matrix::Identity;
	Matrix eyeToHead = Matrix::Identity;
	Matrix proj = Matrix::Identity;
	Matrix vpHeadSpace = Matrix::Identity;   // For HMD-relative rendering (head space)
	Matrix vpWorldSpace = Matrix::Identity;  // For world/controller rendering (world space)

	// Always get Eye and Projection matrices
	eyeToHead = Util::HmdMatrix34ToMatrix(openvr->vrSystem->GetEyeToHeadTransform(eye));

	// Use GetProjectionRaw to build a DirectX-compatible projection matrix (Depth [0, 1])
	// IMPORTANT: OpenVR GetProjectionRaw has a known bug (Valve issue #110, open since 2016):
	// The 3rd parameter (named "pTop") actually returns the BOTTOM tangent, and
	// the 4th parameter (named "pBottom") actually returns the TOP tangent.
	// We name our variables to match the ACTUAL values, not the misleading parameter names.
	float left, right, bottom, top;
	openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
	float nearZ = 0.1f;
	float farZ = 1000.0f;

	proj = DirectX::XMMatrixPerspectiveOffCenterRH(left * nearZ, right * nearZ, bottom * nearZ, top * nearZ, nearZ, farZ);

	// Log projection values once per eye
	static bool projLogged[2] = { false, false };
	if (!projLogged[(int)eye]) {
		logger::debug("VR Projection Eye {}: L={:.4f} R={:.4f} B={:.4f} T={:.4f}, EyeX={:.4f}",
			(int)eye, left, right, bottom, top, eyeToHead._41);
		projLogged[(int)eye] = true;
	}

	// Head-space VP (for HMD-relative mode)
	vpHeadSpace = eyeToHead.Invert() * proj;

	// World-space VP (for controller attach and fixed world position modes)
	if (hmdPose.bPoseIsValid) {
		hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
		// SimpleMath uses row-vector transforms, so compose local-to-world as
		// eye -> head -> tracking world. Reversing this leaves the eye offset in
		// tracking axes and breaks stereo when the HMD is rotated.
		Matrix eyeToWorld = eyeToHead * hmdWorld;
		vpWorldSpace = eyeToWorld.Invert() * proj;
	}

	// Get or create cached RTV for the target texture
	D3D11_TEXTURE2D_DESC texDesc;
	targetTexture->GetDesc(&texDesc);

	int eyeIdx = (int)eye;
	if (eyeIdx < 0 || eyeIdx >= 2) {
		return;
	}

	ID3D11RenderTargetView* rtvPtr = targetRTV;
	if (!rtvPtr) {
		auto& cachedRTV = inSceneResources.cachedEyeRTVs[eyeIdx];
		if (cachedRTV.texture != targetTexture) {
			cachedRTV.rtv = nullptr;
			cachedRTV.texture = nullptr;

			winrt::com_ptr<ID3D11Device> targetDevice;
			targetTexture->GetDevice(targetDevice.put());
			auto* rtvDevice = targetDevice.get() ? targetDevice.get() : globals::d3d::device;
			if (!rtvDevice) {
				return;
			}

			const DXGI_FORMAT rtvFormat = GetRenderTargetViewFormat(texDesc.Format);
			if (!SupportsRenderTargetView(rtvDevice, rtvFormat)) {
				logger::error("VR: Eye texture format cannot be used as an RTV ({}x{}, Format: {}, RTVFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X})",
					texDesc.Width,
					texDesc.Height,
					(uint32_t)texDesc.Format,
					(uint32_t)rtvFormat,
					texDesc.ArraySize,
					texDesc.SampleDesc.Count,
					texDesc.BindFlags);
				return;
			}

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = rtvFormat;

			if (texDesc.ArraySize > 1) {
				if (texDesc.SampleDesc.Count > 1) {
					rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
					rtvDesc.Texture2DMSArray.FirstArraySlice = (UINT)eye;
					rtvDesc.Texture2DMSArray.ArraySize = 1;
				} else {
					rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					rtvDesc.Texture2DArray.FirstArraySlice = (UINT)eye;
					rtvDesc.Texture2DArray.ArraySize = 1;
					rtvDesc.Texture2DArray.MipSlice = 0;
				}
			} else if (texDesc.SampleDesc.Count > 1) {
				rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			} else {
				rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				rtvDesc.Texture2D.MipSlice = 0;
			}

			HRESULT hr = rtvDevice->CreateRenderTargetView(targetTexture, &rtvDesc, cachedRTV.rtv.put());
			if (FAILED(hr)) {
				logger::error("VR: Failed to create RTV for eye texture ({}x{}, Format: {}, RTVFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X}). HRESULT: 0x{:08X}",
					texDesc.Width,
					texDesc.Height,
					(uint32_t)texDesc.Format,
					(uint32_t)rtvFormat,
					texDesc.ArraySize,
					texDesc.SampleDesc.Count,
					texDesc.BindFlags,
					(uint32_t)hr);
				return;
			}
			cachedRTV.texture = targetTexture;
		}
		rtvPtr = cachedRTV.rtv.get();
	}
	if (!rtvPtr) {
		return;
	}

	// Save State
	ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	ID3D11DepthStencilView* oldDSV;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, &oldDSV);

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);

	ID3D11BlendState* oldBlend = nullptr;
	FLOAT oldBlendFactor[4];
	UINT oldSampleMask;
	context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldSampleMask);

	ID3D11DepthStencilState* oldDepth = nullptr;
	UINT oldStencilRef;
	context->OMGetDepthStencilState(&oldDepth, &oldStencilRef);

	// Setup Render
	context->OMSetRenderTargets(1, &rtvPtr, nullptr);  // No DSV

	// Viewport: Use bounds if provided (for SBS textures), otherwise use full texture
	D3D11_VIEWPORT vpDesc = {};
	if (bounds) {
		vpDesc.TopLeftX = bounds->uMin * texDesc.Width;
		vpDesc.TopLeftY = bounds->vMin * texDesc.Height;
		vpDesc.Width = (bounds->uMax - bounds->uMin) * texDesc.Width;
		vpDesc.Height = (bounds->vMax - bounds->vMin) * texDesc.Height;
	} else {
		vpDesc.TopLeftX = 0.0f;
		vpDesc.TopLeftY = 0.0f;
		vpDesc.Width = (float)texDesc.Width;
		vpDesc.Height = (float)texDesc.Height;
	}
	vpDesc.MinDepth = 0.0f;
	vpDesc.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vpDesc);

	// Log texture and viewport details once per eye per session
	static bool textureInfoLogged[2] = { false, false };
	if (!textureInfoLogged[eyeIdx]) {
		logger::debug("VR Submit Texture Info (Eye {}):", eyeIdx);
		logger::debug("  Texture Size: {}x{}, Format: {}, ArraySize: {}, SampleCount: {}",
			texDesc.Width, texDesc.Height, (uint32_t)texDesc.Format, texDesc.ArraySize, texDesc.SampleDesc.Count);
		if (bounds) {
			logger::debug("  Bounds: uMin={:.3f}, vMin={:.3f}, uMax={:.3f}, vMax={:.3f}",
				bounds->uMin, bounds->vMin, bounds->uMax, bounds->vMax);
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		} else {
			logger::debug("  No bounds provided (full texture per eye, or texture array)");
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		}
		logger::debug("  RTV Dimension: {}",
			(texDesc.ArraySize > 1 && texDesc.SampleDesc.Count > 1) ? "Texture2DMSArray" :
			(texDesc.ArraySize > 1)                                 ? "Texture2DArray (per-eye slice)" :
			(texDesc.SampleDesc.Count > 1)                          ? "Texture2DMS" :
																	  "Texture2D (single)");
		textureInfoLogged[eyeIdx] = true;
	}

	// Helper to draw the overlay quad with a given WVP matrix
	auto drawOverlayQuad = [&](ID3D11DeviceContext* ctx,
		                       const InSceneCB& cbData,
		                       ID3D11Texture2D* texture,
		                       winrt::com_ptr<ID3D11ShaderResourceView>& srv,
		                       ID3D11Texture2D*& cachedTexture,
		                       const char* label) {
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		if (SUCCEEDED(ctx->Map(inSceneResources.cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
			memcpy(mappedResource.pData, &cbData, sizeof(InSceneCB));
			ctx->Unmap(inSceneResources.cb.get(), 0);
		}

		ctx->VSSetShader(inSceneResources.vs.get(), nullptr, 0);
		ctx->PSSetShader(inSceneResources.ps.get(), nullptr, 0);
		ID3D11Buffer* cb = inSceneResources.cb.get();
		ctx->VSSetConstantBuffers(0, 1, &cb);

		struct VT
		{
			XMFLOAT3 p;
			XMFLOAT2 t;
		};
		UINT stride = sizeof(VT);
		UINT offset = 0;
		ID3D11Buffer* vb = inSceneResources.vb.get();
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetIndexBuffer(inSceneResources.ib.get(), DXGI_FORMAT_R32_UINT, 0);
		ctx->IASetInputLayout(inSceneResources.inputLayout.get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ctx->OMSetBlendState(inSceneResources.blendState.get(), nullptr, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(inSceneResources.depthState.get(), 0);
		ctx->RSSetState(inSceneResources.rasterizerState.get());

		if (!EnsureMenuTextureSRV(texture, srv, cachedTexture, label)) {
			return;
		}
		ID3D11ShaderResourceView* srvPtr = srv.get();
		ctx->PSSetShaderResources(0, 1, &srvPtr);

		ID3D11SamplerState* sampler = inSceneResources.sampler.get();
		ctx->PSSetSamplers(0, 1, &sampler);

		ctx->DrawIndexed(6, 0, 0);
	};

	// --- Render HMD Overlay ---
	if ((settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) && menuTexture) {
		InSceneCB cbData;

		Matrix modelMatrix;
		Matrix vp;
		if (settings.VRMenuPositioningMethod == 1) {  // Fixed World Position
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * fixedWorldOverlayPosition.m;
			vp = vpWorldSpace;
		} else {  // HMD Relative
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * offset;
			vp = vpHeadSpace;
		}
		cbData.wvp = (modelMatrix * vp).Transpose();

		drawOverlayQuad(
			context,
			cbData,
			menuTexture.get(),
			inSceneResources.menuSRV,
			inSceneResources.cachedMenuTexture,
			"HMD");
	}

	// --- Render Controller Overlay ---
	if ((settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) && (menuControllerTexture || menuTexture)) {
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex != vr::k_unTrackedDeviceIndexInvalid && attachIndex < vr::k_unMaxTrackedDeviceCount) {
			const vr::TrackedDevicePose_t& controllerPose = inSceneResources.cachedRenderPoses[attachIndex];
			if (controllerPose.bPoseIsValid) {
				Matrix controllerWorld = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);
				Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
				Matrix modelMatrix = VR::Config::CreateOverlayScaleMatrix(settings.VRMenuScale) * offset * controllerWorld;

				// Backface culling: hide overlay when viewed from behind
				// Use the unscaled controller+offset transform for correct normal direction
				Matrix overlayTransform = offset * controllerWorld;
				Vector3 overlayNormal(overlayTransform._31, overlayTransform._32, overlayTransform._33);
				overlayNormal.Normalize();
				Matrix eyeWorld = eyeToHead * hmdWorld;
				Vector3 eyePos = eyeWorld.Translation();
				Vector3 overlayPos = overlayTransform.Translation();
				Vector3 toEye = eyePos - overlayPos;
				toEye.Normalize();
				// Quad front face is +Z in local space (D3D default CW winding).
				// Render when eye is on the +Z side of the overlay (dot > 0).
				float dot = overlayNormal.Dot(toEye);
				if (dot > 0.0f) {
					InSceneCB cbData;
					cbData.wvp = (modelMatrix * vpWorldSpace).Transpose();
					if (menuControllerTexture) {
						drawOverlayQuad(
							context,
							cbData,
							menuControllerTexture.get(),
							inSceneResources.menuControllerSRV,
							inSceneResources.cachedMenuControllerTexture,
							"controller");
					} else {
						drawOverlayQuad(
							context,
							cbData,
							menuTexture.get(),
							inSceneResources.menuSRV,
							inSceneResources.cachedMenuTexture,
							"HMD");
					}
				}
			}
		}
	}

	// Restore State
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV);
	context->RSSetViewports(numViewports, oldViewports);
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
	context->OMSetDepthStencilState(oldDepth, oldStencilRef);
	if (oldRS) {
		context->RSSetState(oldRS);
		oldRS->Release();
	}
	if (oldBlend)
		oldBlend->Release();
	if (oldDepth)
		oldDepth->Release();
	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		if (oldRTVs[i])
			oldRTVs[i]->Release();
	if (oldDSV)
		oldDSV->Release();

}

void VR::CompositeInSceneOverlaySubmitTexture(vr::EVREye eye, ID3D11Texture2D* targetTexture, ID3D11UnorderedAccessView* targetUAV, const D3D11_TEXTURE2D_DESC& targetDesc, const vr::VRTextureBounds_t* bounds)
{
	auto* context = globals::d3d::context;
	auto* device = globals::d3d::device;
	if (!context || !device || !targetTexture || !targetUAV || !menuTexture || !inSceneResources.initialized || !inSceneResources.submitCompositeCS || !inSceneResources.submitCompositeCB || !inSceneResources.sampler) {
		return;
	}

	const float targetWidth = static_cast<float>(targetDesc.Width);
	const float targetHeight = static_cast<float>(targetDesc.Height);
	float viewX = 0.0f;
	float viewY = 0.0f;
	float viewW = targetWidth;
	float viewH = targetHeight;
	if (bounds) {
		viewX = std::clamp(bounds->uMin, 0.0f, 1.0f) * targetWidth;
		viewY = std::clamp(bounds->vMin, 0.0f, 1.0f) * targetHeight;
		viewW = std::max(1.0f, (std::clamp(bounds->uMax, 0.0f, 1.0f) - std::clamp(bounds->uMin, 0.0f, 1.0f)) * targetWidth);
		viewH = std::max(1.0f, (std::clamp(bounds->vMax, 0.0f, 1.0f) - std::clamp(bounds->vMin, 0.0f, 1.0f)) * targetHeight);
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return;
	}

	const bool hasState = globals::state != nullptr;
	const uint32_t currentFrame = hasState ? globals::state->frameCount : 0;
	const bool shouldRefreshPoses =
		!hasState ||
		!inSceneResources.cachedPosesValid ||
		inSceneResources.cachedPoseFrame != currentFrame;

	if (shouldRefreshPoses) {
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		if (!compositor) {
			compositor = openvr->vrContext.vrCompositor;
		}
		if (!compositor) {
			return;
		}

		auto compositorError = compositor->GetLastPoses(
			inSceneResources.cachedRenderPoses,
			vr::k_unMaxTrackedDeviceCount,
			nullptr,
			0);
		if (compositorError != vr::VRCompositorError_None) {
			return;
		}

		inSceneResources.cachedPoseFrame = currentFrame;
		inSceneResources.cachedPosesValid = true;
	}

	const vr::TrackedDevicePose_t& hmdPose = inSceneResources.cachedRenderPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if (!hmdPose.bPoseIsValid) {
		return;
	}

	Matrix hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
	Matrix eyeToHead = Util::HmdMatrix34ToMatrix(openvr->vrSystem->GetEyeToHeadTransform(eye));

	float left, right, bottom, top;
	openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
	const float nearZ = 0.1f;
	const float farZ = 1000.0f;
	Matrix proj = DirectX::XMMatrixPerspectiveOffCenterRH(left * nearZ, right * nearZ, bottom * nearZ, top * nearZ, nearZ, farZ);
	Matrix vpHeadSpace = eyeToHead.Invert() * proj;
	Matrix eyeToWorld = eyeToHead * hmdWorld;
	Matrix vpWorldSpace = eyeToWorld.Invert() * proj;

	Matrix modelMatrix = Matrix::Identity;
	Matrix viewProjection = vpHeadSpace;
	const bool showOnHMD = settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both;
	const bool showOnController = settings.attachMode == AttachMode::ControllerOnly;
	if (showOnHMD) {
		if (settings.VRMenuPositioningMethod == 1) {
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * fixedWorldOverlayPosition.m;
			viewProjection = vpWorldSpace;
		} else {
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * offset;
			viewProjection = vpHeadSpace;
		}
	} else if (showOnController) {
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex == vr::k_unTrackedDeviceIndexInvalid || attachIndex >= vr::k_unMaxTrackedDeviceCount) {
			return;
		}
		const vr::TrackedDevicePose_t& controllerPose = inSceneResources.cachedRenderPoses[attachIndex];
		if (!controllerPose.bPoseIsValid) {
			return;
		}

		Matrix controllerWorld = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);
		Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
		modelMatrix = VR::Config::CreateOverlayScaleMatrix(settings.VRMenuScale) * offset * controllerWorld;
		viewProjection = vpWorldSpace;
	} else {
		return;
	}

	ID3D11ShaderResourceView* overlaySRV = nullptr;
	if (showOnHMD) {
		if (!EnsureMenuTextureSRV(menuTexture.get(), inSceneResources.menuSRV, inSceneResources.cachedMenuTexture, "HMD")) {
			return;
		}
		overlaySRV = inSceneResources.menuSRV.get();
	} else if (showOnController) {
		if (menuControllerTexture) {
			if (!EnsureMenuTextureSRV(
					menuControllerTexture.get(),
					inSceneResources.menuControllerSRV,
					inSceneResources.cachedMenuControllerTexture,
					"controller")) {
				return;
			}
			overlaySRV = inSceneResources.menuControllerSRV.get();
		} else {
			if (!EnsureMenuTextureSRV(menuTexture.get(), inSceneResources.menuSRV, inSceneResources.cachedMenuTexture, "HMD")) {
				return;
			}
			overlaySRV = inSceneResources.menuSRV.get();
		}
	}

	const Matrix worldViewProjection = modelMatrix * viewProjection;
	const XMFLOAT3 vertices[4] = {
		XMFLOAT3(-0.5f, -0.5f, 0.0f),
		XMFLOAT3(-0.5f, 0.5f, 0.0f),
		XMFLOAT3(0.5f, 0.5f, 0.0f),
		XMFLOAT3(0.5f, -0.5f, 0.0f)
	};

	SubmitCompositeCB cbData{};
	cbData.targetSize[0] = targetDesc.Width;
	cbData.targetSize[1] = targetDesc.Height;

	float minX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::lowest();
	float maxY = std::numeric_limits<float>::lowest();
	for (size_t i = 0; i < 4; ++i) {
		const XMVECTOR local = XMVectorSet(vertices[i].x, vertices[i].y, vertices[i].z, 1.0f);
		const XMVECTOR clip = XMVector4Transform(local, worldViewProjection);
		const float w = XMVectorGetW(clip);
		if (std::abs(w) < 1e-5f) {
			return;
		}
		cbData.quadInvW[i] = 1.0f / w;

		const float ndcX = XMVectorGetX(clip) / w;
		const float ndcY = XMVectorGetY(clip) / w;
		if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
			return;
		}

		const float pixelX = viewX + (ndcX * 0.5f + 0.5f) * viewW;
		const float pixelY = viewY + (0.5f - ndcY * 0.5f) * viewH;
		cbData.quadPixels[i * 2] = pixelX;
		cbData.quadPixels[i * 2 + 1] = pixelY;
		minX = std::min(minX, pixelX);
		minY = std::min(minY, pixelY);
		maxX = std::max(maxX, pixelX);
		maxY = std::max(maxY, pixelY);
	}

	const int dispatchLeft = std::clamp(static_cast<int>(std::floor(minX)) - 1, 0, static_cast<int>(targetDesc.Width));
	const int dispatchTop = std::clamp(static_cast<int>(std::floor(minY)) - 1, 0, static_cast<int>(targetDesc.Height));
	const int dispatchRight = std::clamp(static_cast<int>(std::ceil(maxX)) + 1, 0, static_cast<int>(targetDesc.Width));
	const int dispatchBottom = std::clamp(static_cast<int>(std::ceil(maxY)) + 1, 0, static_cast<int>(targetDesc.Height));
	if (dispatchRight <= dispatchLeft || dispatchBottom <= dispatchTop) {
		return;
	}

	cbData.dispatchOrigin[0] = static_cast<uint32_t>(dispatchLeft);
	cbData.dispatchOrigin[1] = static_cast<uint32_t>(dispatchTop);
	cbData.dispatchSize[0] = static_cast<uint32_t>(dispatchRight - dispatchLeft);
	cbData.dispatchSize[1] = static_cast<uint32_t>(dispatchBottom - dispatchTop);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(inSceneResources.submitCompositeCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	std::memcpy(mapped.pData, &cbData, sizeof(cbData));
	context->Unmap(inSceneResources.submitCompositeCB.get(), 0);

	ID3D11ComputeShader* oldCS = nullptr;
	ID3D11ShaderResourceView* oldSRV = nullptr;
	ID3D11UnorderedAccessView* oldUAV = nullptr;
	ID3D11SamplerState* oldSampler = nullptr;
	ID3D11Buffer* oldCB = nullptr;
	context->CSGetShader(&oldCS, nullptr, nullptr);
	context->CSGetShaderResources(0, 1, &oldSRV);
	context->CSGetUnorderedAccessViews(0, 1, &oldUAV);
	context->CSGetSamplers(0, 1, &oldSampler);
	context->CSGetConstantBuffers(0, 1, &oldCB);

	ID3D11ShaderResourceView* srv = overlaySRV;
	ID3D11UnorderedAccessView* uav = targetUAV;
	ID3D11SamplerState* sampler = inSceneResources.sampler.get();
	ID3D11Buffer* cb = inSceneResources.submitCompositeCB.get();
	context->CSSetShader(inSceneResources.submitCompositeCS.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetSamplers(0, 1, &sampler);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->Dispatch((cbData.dispatchSize[0] + 7) / 8, (cbData.dispatchSize[1] + 7) / 8, 1);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(oldCS, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &oldCB);
	context->CSSetShaderResources(0, 1, &oldSRV);
	context->CSSetSamplers(0, 1, &oldSampler);
	context->CSSetUnorderedAccessViews(0, 1, &oldUAV, nullptr);

	if (oldCS)
		oldCS->Release();
	if (oldSRV)
		oldSRV->Release();
	if (oldUAV)
		oldUAV->Release();
	if (oldSampler)
		oldSampler->Release();
	if (oldCB)
		oldCB->Release();
}

void VR::EnsureInSceneOverlaySubmitCopyResources()
{
	auto* device = globals::d3d::device;
	if (!device) {
		return;
	}

	for (int eyeIdx = 0; eyeIdx < 2; ++eyeIdx) {
		auto& submitCopy = inSceneResources.submitCopies[eyeIdx];
		if (!submitCopy.pendingCreate) {
			continue;
		}

		const auto sourceDesc = submitCopy.pendingSourceDesc;
		if (sourceDesc.ArraySize != 1 || sourceDesc.SampleDesc.Count != 1) {
			logger::error("VR: Cannot composite in-scene menu into submit texture with array={} samples={}", sourceDesc.ArraySize, sourceDesc.SampleDesc.Count);
			submitCopy.pendingCreate = false;
			continue;
		}

		const DXGI_FORMAT viewFormat = GetRenderTargetViewFormat(sourceDesc.Format);
		if (!SupportsUnorderedAccessView(device, viewFormat)) {
			logger::error("VR: Cannot create in-scene menu submit copy UAV (eye={}, {}x{}, Format: {}, ViewFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X})",
				eyeIdx,
				sourceDesc.Width,
				sourceDesc.Height,
				(uint32_t)sourceDesc.Format,
				(uint32_t)viewFormat,
				sourceDesc.ArraySize,
				sourceDesc.SampleDesc.Count,
				sourceDesc.BindFlags);
			submitCopy.pendingCreate = false;
			continue;
		}

		D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		winrt::com_ptr<ID3D11Texture2D> texture;
		HRESULT hr = device->CreateTexture2D(&copyDesc, nullptr, texture.put());
		if (FAILED(hr)) {
			logger::error("VR: Failed to create in-scene menu submit copy texture (eye={}, {}x{}, Format: {}, ArraySize: {}, Samples: {}, HRESULT: 0x{:08X})",
				eyeIdx,
				copyDesc.Width,
				copyDesc.Height,
				(uint32_t)copyDesc.Format,
				copyDesc.ArraySize,
				copyDesc.SampleDesc.Count,
				(uint32_t)hr);
			submitCopy.pendingCreate = false;
			continue;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = viewFormat;
		if (copyDesc.ArraySize > 1) {
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = (UINT)eyeIdx;
			uavDesc.Texture2DArray.ArraySize = 1;
			uavDesc.Texture2DArray.MipSlice = 0;
		} else {
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
		}

		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		hr = device->CreateUnorderedAccessView(texture.get(), &uavDesc, uav.put());
		if (FAILED(hr)) {
			logger::error("VR: Failed to create in-scene menu submit copy UAV (eye={}, {}x{}, Format: {}, ViewFormat: {}, ArraySize: {}, Samples: {}, HRESULT: 0x{:08X})",
				eyeIdx,
				copyDesc.Width,
				copyDesc.Height,
				(uint32_t)copyDesc.Format,
				(uint32_t)viewFormat,
				copyDesc.ArraySize,
				copyDesc.SampleDesc.Count,
				(uint32_t)hr);
			submitCopy.pendingCreate = false;
			continue;
		}

		submitCopy.texture = std::move(texture);
		submitCopy.uav = std::move(uav);
		submitCopy.sourceDesc = sourceDesc;
		submitCopy.pendingCreate = false;
		inSceneResources.cachedEyeRTVs[eyeIdx].texture = nullptr;
		inSceneResources.cachedEyeRTVs[eyeIdx].rtv = nullptr;
		Util::SetResourceName(submitCopy.texture.get(), eyeIdx == 0 ? "VR::InSceneOverlaySubmitCopyLeft" : "VR::InSceneOverlaySubmitCopyRight");
		Util::SetResourceName(submitCopy.uav.get(), eyeIdx == 0 ? "VR::InSceneOverlaySubmitCopyLeft UAV" : "VR::InSceneOverlaySubmitCopyRight UAV");
		logger::debug("VR: Created in-scene menu submit copy for eye {} ({}x{}, format={}, array={}, samples={})",
			eyeIdx,
			copyDesc.Width,
			copyDesc.Height,
			(uint32_t)copyDesc.Format,
			copyDesc.ArraySize,
			copyDesc.SampleDesc.Count);
	}
}

bool VR::PrepareInSceneOverlaySubmitTexture(vr::EVREye eye, const vr::Texture_t* inputTexture, const vr::VRTextureBounds_t* bounds, vr::Texture_t& outputTexture)
{
	if (!inputTexture || !inputTexture->handle || inputTexture->eType != vr::TextureType_DirectX || !ShouldRenderInSceneMenu(*this)) {
		return false;
	}

	auto sourceTexture = ResolveSubmitTexture2D(inputTexture->handle);
	auto* context = globals::d3d::context;
	if (!sourceTexture || !context) {
		logger::error("VR: OpenVR submit handle is not a D3D11 texture; skipping in-scene menu compositing");
		return false;
	}

	const int eyeIdx = static_cast<int>(eye);
	if (eyeIdx < 0 || eyeIdx >= 2) {
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);

	auto& submitCopy = inSceneResources.submitCopies[eyeIdx];
	if (!submitCopy.texture || !submitCopy.uav || !MatchesSubmitCopyDesc(submitCopy.sourceDesc, sourceDesc)) {
		submitCopy.texture = nullptr;
		submitCopy.uav = nullptr;
		submitCopy.pendingSourceDesc = sourceDesc;
		submitCopy.pendingCreate = true;
		return false;
	}

	context->CopyResource(submitCopy.texture.get(), sourceTexture.get());
	CompositeInSceneOverlaySubmitTexture(eye, submitCopy.texture.get(), submitCopy.uav.get(), sourceDesc, bounds);

	outputTexture = *inputTexture;
	outputTexture.handle = submitCopy.texture.get();
	return true;
}

void VR::InstallSubmitHook()
{
	static bool installed = false;
	static bool warnedUnavailable = false;
	if (installed) {
		inSceneResources.submitHookInstalled = true;
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* compositor = openvr ? RE::BSOpenVR::GetIVRCompositor() : nullptr;
	if (!compositor && openvr) {
		compositor = openvr->vrContext.vrCompositor;
	}

	if (openvr && compositor) {
		logger::info("VR: Installing IVRCompositor::Submit hook for in-scene overlay rendering");

		// Log comprehensive VR system parameters (debug only)
		logger::debug("=== VR System Configuration ===");

		// Get and log IPD
		float ipd = Util::GetIPDFromHMD();
		logger::debug("IPD: {:.4f} meters ({:.2f} mm)", ipd, ipd * 1000.0f);

		// Get and log eye transforms
		if (openvr->vrSystem) {
			vr::HmdMatrix34_t leftEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Left);
			vr::HmdMatrix34_t rightEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Right);

			logger::debug("Left Eye Transform:");
			logger::debug("  Translation: X={:.4f}, Y={:.4f}, Z={:.4f}",
				leftEye.m[0][3], leftEye.m[1][3], leftEye.m[2][3]);
			logger::debug("Right Eye Transform:");
			logger::debug("  Translation: X={:.4f}, Y={:.4f}, Z={:.4f}",
				rightEye.m[0][3], rightEye.m[1][3], rightEye.m[2][3]);
			logger::debug("Calculated Eye Separation: {:.4f} meters ({:.2f} mm)",
				std::abs(leftEye.m[0][3] - rightEye.m[0][3]),
				std::abs(leftEye.m[0][3] - rightEye.m[0][3]) * 1000.0f);

			// Get projection matrices
			vr::HmdMatrix44_t leftProj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Left, 0.1f, 1000.0f);
			vr::HmdMatrix44_t rightProj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Right, 0.1f, 1000.0f);

			logger::debug("Projection Matrices (near=0.1, far=1000.0):");
			logger::debug("  Left [0][0]={:.4f}, [1][1]={:.4f}, [0][2]={:.4f}",
				leftProj.m[0][0], leftProj.m[1][1], leftProj.m[0][2]);
			logger::debug("  Right [0][0]={:.4f}, [1][1]={:.4f}, [0][2]={:.4f}",
				rightProj.m[0][0], rightProj.m[1][1], rightProj.m[0][2]);
		}

		logger::debug("Convergence Formula Info:");
		logger::debug("  Formula: stereoShift = (IPD/2) / (depth * tan(hFOV/2))");
		logger::debug("  - Shift is independent of scale (scale only controls size)");
		logger::debug("  - Depth is controlled by OffsetZ (negative = in front)");
		float halfIPD = ipd / 2.0f;
		if (openvr->vrSystem) {
			vr::HmdMatrix44_t proj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Left, 0.1f, 1000.0f);
			float tanHFOV = 1.0f / proj.m[0][0];
			logger::debug("  tan(hFOV/2) = {:.4f}", tanHFOV);
			logger::debug("  Example: At depth 1.0m, shift={:.6f}", halfIPD / (1.0f * tanHFOV));
			logger::debug("  Example: At depth 2.0m, shift={:.6f}", halfIPD / (2.0f * tanHFOV));
			logger::debug("  Example: At depth 5.0m, shift={:.6f}", halfIPD / (5.0f * tanHFOV));
		}
		logger::debug("================================");

		// IVRCompositor::Submit is index 5
		stl::detour_vfunc<5, IVRCompositor_Submit>(compositor);
		installed = true;
		inSceneResources.submitHookInstalled = true;

		logger::info("VR: In-scene overlay initialized");
	} else if (!warnedUnavailable) {
		logger::warn("VR: Failed to install IVRCompositor::Submit hook - Interface not available");
		warnedUnavailable = true;
	}
}
