#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;
	class BSRenderPass;
	class TESObjectREFR;
}

namespace VRRigidMotionHistory
{
	using GeometrySetup = void (*)(RE::BSShader*, RE::BSRenderPass*, std::uint32_t);

	/** Release expired geometry on its rendering thread; clear history across world transitions. */
	void Reset(std::uint32_t a_frame, bool a_clear);

	/** Supply rendered history for rigid actor attachments during native lighting setup. */
	void SetupGeometry(RE::BSShader* a_shader, RE::BSRenderPass* a_pass,
		std::uint32_t a_renderFlags, RE::TESObjectREFR* a_owner, GeometrySetup a_setup);
}
