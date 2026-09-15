#pragma once

namespace RE
{
	class ImageSpaceEffectDepthOfField;
	class ImageSpaceEffectParam;
}

struct ID3D11DeviceContext;

namespace UnderwaterDepthOfField
{
	void InstallHooks();
	/// Compose pending underwater input before the shared engine draw hook.
	void BeforeDraw();
	void RecordShaderConstants(const RE::ImageSpaceEffectDepthOfField* a_effect, RE::ImageSpaceEffectParam* a_param);
	void BeginRender();
	void EndRender();
}
