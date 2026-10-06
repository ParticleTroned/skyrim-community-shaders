#pragma once

namespace RE
{
	class ImageSpaceEffectDepthOfField;
	class ImageSpaceEffectParam;
}

namespace UnderwaterDepthOfField
{
	void InstallHooks();
	/** Applies pending fog composition before the shared D3D draw hook executes. */
	void BeforeDraw();
	void RecordShaderConstants(const RE::ImageSpaceEffectDepthOfField* a_effect, RE::ImageSpaceEffectParam* a_param);
	void BeginRender();
	void EndRender();
}
