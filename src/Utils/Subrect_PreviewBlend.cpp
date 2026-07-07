#include "Globals.h"
#include "Utils/D3D.h"
#include "Utils/Subrect.h"

#include <PCH.h>
#include <d3d11.h>
#include <imgui.h>

namespace Util::Subrect
{
	void OpaquePreviewBlendCallback(const ImDrawList*, const ImDrawCmd*)
	{
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!device || !context) {
			return;
		}

		static winrt::com_ptr<ID3D11BlendState> opaqueBlend;
		if (!opaqueBlend) {
			D3D11_BLEND_DESC desc{};
			desc.RenderTarget[0].BlendEnable = FALSE;
			desc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED |
				D3D11_COLOR_WRITE_ENABLE_GREEN |
				D3D11_COLOR_WRITE_ENABLE_BLUE;
			if (FAILED(device->CreateBlendState(&desc, opaqueBlend.put()))) {
				return;
			}
			Util::SetResourceName(opaqueBlend.get(), "Subrect::OpaquePreviewBlend");
		}

		context->OMSetBlendState(opaqueBlend.get(), nullptr, 0xFFFFFFFF);
	}

	void ImageOpaque(ID3D11ShaderResourceView* a_srv, const ImVec2& a_size, const ImVec2& a_uv0, const ImVec2& a_uv1)
	{
		if (!a_srv) {
			return;
		}

		auto* drawList = ImGui::GetWindowDrawList();
		drawList->AddCallback(OpaquePreviewBlendCallback, nullptr);
		ImGui::Image(reinterpret_cast<ImTextureID>(a_srv), a_size, a_uv0, a_uv1);
		drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
	}
}  // namespace Util::Subrect
