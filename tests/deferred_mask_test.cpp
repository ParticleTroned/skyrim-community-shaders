#define NOMINMAX
#include "commonlib_d3d_bridge_fake.h"
#include <array>
#include <d3d11.h>
#include <format>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <wrl/client.h>

#include "Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h"
#include "d3d_resource_naming.h"

using Microsoft::WRL::ComPtr;
using uint = unsigned;
namespace RE
{
	enum class RENDER_TARGET
	{
		Mask
	};
}
constexpr auto MASKS2 = RE::RENDER_TARGET::Mask;
namespace magic_enum
{
	constexpr std::string_view enum_name(RE::RENDER_TARGET) { return "Mask"; }
}
namespace DX
{
	void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
			throw std::runtime_error("Injected or actual D3D allocation failure");
	}
}
namespace logger
{
	unsigned errors = 0;
	template <class... T>
	void error(const char*, T...)
	{
		++errors;
	}
}
namespace NeuralRendering
{
	struct CharacterRendering
	{
		unsigned invalidations = 0;
		static CharacterRendering& Instance()
		{
			static CharacterRendering instance;
			return instance;
		}
		void Invalidate() { ++invalidations; }
	};
}
struct Target
{
	ID3D11Texture2D* texture = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	ID3D11RenderTargetView* RTV = nullptr;
	ID3D11UnorderedAccessView* UAV = nullptr;
};
struct Targets
{
	Target target;
	Target& operator[](RE::RENDER_TARGET) { return target; }
};
struct Renderer
{
	Targets renderTargets;
	Renderer& GetRuntimeData() { return *this; }
};
struct Device
{
	ComPtr<ID3D11Device> real;
	unsigned failStage = 0, calls = 0;
	bool Fail(unsigned stage)
	{
		++calls;
		return stage == failStage;
	}
	HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* i, ID3D11Texture2D** o)
	{
		return Fail(1) ? E_OUTOFMEMORY : real->CreateTexture2D(d, i, o);
	}
	HRESULT CreateShaderResourceView(ID3D11Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ID3D11ShaderResourceView** o)
	{
		return Fail(2) ? E_OUTOFMEMORY : real->CreateShaderResourceView(r, d, o);
	}
	HRESULT CreateRenderTargetView(ID3D11Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* d, ID3D11RenderTargetView** o)
	{
		return Fail(3) ? E_OUTOFMEMORY : real->CreateRenderTargetView(r, d, o);
	}
	HRESULT CreateUnorderedAccessView(ID3D11Resource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* d, ID3D11UnorderedAccessView** o)
	{
		return real->CreateUnorderedAccessView(r, d, o);
	}
};
namespace globals
{
	namespace game
	{
		Renderer* renderer;
	}
	namespace d3d
	{
		Device* device;
	}
	namespace features
	{
		struct Upscaling
		{
			bool configured = false;
			bool IsCharacterNeuralRenderingRouteConfigured() const { return configured; }
		} upscaling;
	}
}
class Deferred
{
public:
	void UpdateCharacterCategoryTarget();
	bool characterCategoryEnabled = false;
	std::optional<bool> failedCharacterCategoryMode;
};
#include "deferred_mask_under_test.h"

void Require(bool ok, const char* message)
{
	if (!ok)
		throw std::runtime_error(message);
}
void CheckTarget(const Target& target, DXGI_FORMAT format)
{
	D3D11_TEXTURE2D_DESC texture{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
	D3D11_RENDER_TARGET_VIEW_DESC rtv{};
	target.texture->GetDesc(&texture);
	target.SRV->GetDesc(&srv);
	target.RTV->GetDesc(&rtv);
	Require(texture.Format == format && srv.Format == format && rtv.Format == format, "Texture/view format mismatch");
	Require(texture.Width == 128 && texture.Height == 64 && texture.SampleDesc.Count == 1, "Layout switch changed dimensions or samples");
	Require(texture.BindFlags == (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE), "Layout switch changed bindings");
	for (ID3D11DeviceChild* resource : std::array<ID3D11DeviceChild*, 3>{ target.texture, target.SRV, target.RTV }) {
		std::array<char, 128> name{};
		UINT size = static_cast<UINT>(name.size());
		DX::ThrowIfFailed(resource->GetPrivateData(Util::WKPDID_D3DDebugObjectNameT, &size, name.data()));
		Require(std::string_view(name.data(), size).starts_with("Deferred::Mask"), "Resource debug name missing");
	}
}
int main()
{
	try {
		Renderer renderer;
		Device device;
		globals::game::renderer = &renderer;
		globals::d3d::device = &device;
		DX::ThrowIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &device.real, nullptr, nullptr));
		D3D11_TEXTURE2D_DESC texture{};
		texture.Width = 128;
		texture.Height = 64;
		texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		D3D11_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		SetupRenderTarget(MASKS2, texture, srv, rtv, {}, NeuralRendering::kVertexAoFormat,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		auto& target = renderer.renderTargets[MASKS2];
		Deferred deferred;
		for (unsigned failure = 1; failure <= 3; ++failure) {
			CheckTarget(target, NeuralRendering::kVertexAoFormat);
			const auto original = target;
			device.failStage = failure;
			globals::features::upscaling.configured = true;
			deferred.UpdateCharacterCategoryTarget();
			Require(!deferred.characterCategoryEnabled && target.texture == original.texture &&
						target.SRV == original.SRV && target.RTV == original.RTV,
				"Failure published a partial target/layout");
			const auto calls = device.calls;
			for (unsigned frame = 0; frame < 100; ++frame) deferred.UpdateCharacterCategoryTarget();
			Require(device.calls == calls, "Failed allocation retried every frame");
			globals::features::upscaling.configured = false;
			deferred.UpdateCharacterCategoryTarget();
			Require(!deferred.failedCharacterCategoryMode, "Toggling back did not rearm allocation");
		}
		device.failStage = 0;
		for (unsigned transition = 0; transition < 20; ++transition) {
			const bool requested = (transition % 2) == 0;
			globals::features::upscaling.configured = requested;
			const auto invalidations = NeuralRendering::CharacterRendering::Instance().invalidations;
			deferred.UpdateCharacterCategoryTarget();
			Require(deferred.characterCategoryEnabled == requested, "Layout was not published with its texture");
			Require(NeuralRendering::CharacterRendering::Instance().invalidations == invalidations + 1,
				"Layout transition retained stale category capture");
			CheckTarget(target, requested ? NeuralRendering::kCharacterCategoryFormat : NeuralRendering::kVertexAoFormat);
			const auto calls = device.calls;
			for (unsigned frame = 0; frame < 100; ++frame) deferred.UpdateCharacterCategoryTarget();
			Require(device.calls == calls, "Steady mode recreated its render target");
		}
		globals::features::upscaling.configured = true;
		deferred.UpdateCharacterCategoryTarget();
		for (unsigned failure = 1; failure <= 3; ++failure) {
			device.failStage = failure;
			globals::features::upscaling.configured = false;
			const auto original = target;
			deferred.UpdateCharacterCategoryTarget();
			Require(deferred.characterCategoryEnabled && target.texture == original.texture &&
						target.SRV == original.SRV && target.RTV == original.RTV,
				"Failed shrink changed the active layout");
			CheckTarget(target, NeuralRendering::kCharacterCategoryFormat);
			const auto calls = device.calls;
			deferred.UpdateCharacterCategoryTarget();
			Require(device.calls == calls, "Failed shrink retried without a transition");
			globals::features::upscaling.configured = true;
			deferred.UpdateCharacterCategoryTarget();
		}
		ReleaseRenderTargetSlot(MASKS2);
		Require(!target.texture && !target.SRV && !target.RTV, "Retirement left dangling target slots");
		Require(logger::errors == 6, "Allocation failures were not reported exactly once");
		std::cout << "Deferred mask: WARP toggles, allocation rollback, invalidation and retry bounds passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
