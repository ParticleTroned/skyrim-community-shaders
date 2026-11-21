#pragma once

#include "Utils/Input.h"
#include <Windows.h>

namespace ScreenshotShortcut
{
	/** The default screenshot binding accepts either capture key; custom bindings remain exact. */
	inline bool UsesDefaultBinding(const std::vector<InputCombo>& binding)
	{
		return binding.size() == 1 && binding.front().GetDevice() == InputDeviceType::Keyboard &&
		       binding.front().GetKey() == VK_SNAPSHOT;
	}

	inline const char* DisplayName(const std::vector<InputCombo>& binding)
	{
		return UsesDefaultBinding(binding) ? "F12/Print Screen" : nullptr;
	}

	inline bool Matches(const std::vector<InputCombo>& binding, uint32_t key)
	{
		if (key == VK_F12 && UsesDefaultBinding(binding))
			key = VK_SNAPSHOT;
		return InputCombo::MatchesKeyboardCombo(binding, key);
	}
}
