#pragma once

namespace MenuUI::ThemePalette
{
	struct Color
	{
		float r, g, b, a;
	};
	inline constexpr Color Background{ 0.035f, 0.038f, 0.040f, 0.40f };
	inline constexpr Color Text{ 0.92f, 0.91f, 0.86f, 1.0f };
	inline constexpr Color WindowBorder{ 0.42f, 0.40f, 0.35f, 0.80f };
	inline constexpr Color FrameBorder{ 0.082f, 0.086f, 0.089f, 0.62f };
	inline constexpr Color Separator{ 0.34f, 0.33f, 0.30f, 0.62f };
	inline constexpr Color ResizeGrip{ 0.78f, 0.74f, 0.64f, 0.70f };
	inline constexpr Color Disable{ 0.52f, 0.51f, 0.48f, 0.90f };
	inline constexpr Color Error{ 0.95f, 0.34f, 0.32f, 1.0f };
	inline constexpr Color Warning{ 1.00f, 0.64f, 0.18f, 1.0f };
	inline constexpr Color RestartNeeded{ 0.70f, 0.76f, 0.80f, 1.0f };
	inline constexpr Color CurrentHotkey{ 1.00f, 0.78f, 0.28f, 1.0f };
	inline constexpr Color Success{ 0.76f, 0.78f, 0.72f, 1.0f };
	inline constexpr Color Info{ 0.98f, 0.73f, 0.22f, 1.0f };

	/** Construct the consumer's color type without adding an ImGui dependency to exporters. */
	template <class ColorType>
	constexpr ColorType As(Color value)
	{
		return { value.r, value.g, value.b, value.a };
	}
}
