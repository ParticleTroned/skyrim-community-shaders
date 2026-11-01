#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

constexpr int VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12;
constexpr int VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_LMENU = 0xA4, VK_RMENU = 0xA5;
std::array<std::uint16_t, 256> keyStates{};
std::uint16_t GetAsyncKeyState(int key) { return keyStates.at(key); }

#include "Utils/Input.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	using json = nlohmann::json;
	for (const auto& invalid : { json(-1), json(-4294967231ll), json(4294967361ull),
			 json(std::numeric_limits<std::uint64_t>::max()), json(65.5), json(true), json("65"), json(nullptr) }) {
		std::vector<InputCombo> keys{ InputCombo::Keyboard(65) };
		InputCombo::ComboList::from_json(invalid, keys);
		Require(keys.empty(), "Malformed key wrapped to an active binding");
		auto key = invalid.get<InputCombo>();
		Require(!key.IsValid(), "Malformed packed key remained bound");
	}
	std::vector<InputCombo> keys;
	InputCombo::ComboList::from_json(json{ 0, 65, -1, 4294967362ull, (4u << 16) | 2u, (5u << 16) | 8u,
										 (2u << 16) | 3u, (6u << 16) | 1u, 3u << 16 },
		keys);
	Require(keys == std::vector{ InputCombo::Keyboard(65), InputCombo::Mouse(2), InputCombo::Gamepad(8) },
		"Desktop key decoding lost valid keys or admitted reserved devices");
	json encoded;
	InputCombo::ComboList::to_json(encoded, keys);
	std::vector<InputCombo> roundtrip;
	InputCombo::ComboList::from_json(encoded, roundtrip);
	Require(roundtrip == keys, "Keyboard/mouse/gamepad roundtrip changed the binding");
	for (unsigned modifiers = 0; modifiers < 8; ++modifiers) {
		keyStates[VK_CONTROL] = modifiers & 1 ? 0x8000 : 0;
		keyStates[VK_SHIFT] = modifiers & 2 ? 0x8000 : 0;
		keyStates[VK_MENU] = modifiers & 4 ? 0x8000 : 0;
		std::vector<InputCombo> combo;
		if (modifiers & 1)
			combo.push_back(InputCombo::Keyboard(VK_LCONTROL));
		if (modifiers & 2)
			combo.push_back(InputCombo::Keyboard(VK_RSHIFT));
		if (modifiers & 4)
			combo.push_back(InputCombo::Keyboard(VK_MENU));
		combo.push_back(InputCombo::Keyboard(65));
		Require(InputCombo::MatchesKeyboardCombo(combo, 65) && !InputCombo::MatchesKeyboardCombo(combo, 66),
			"Exact modifier matching changed");
	}
	keyStates.fill(0);
	Require(!InputCombo::MatchesKeyboardCombo({ InputCombo::Mouse(VK_CONTROL), InputCombo::Keyboard(65) }, 65),
		"A non-keyboard modifier was silently ignored");
	Require(!InputCombo::MatchesKeyboardCombo({ InputCombo::Keyboard(66), InputCombo::Keyboard(65) }, 65),
		"A non-modifier prefix was silently ignored");
	Require(!InputCombo::MatchesKeyboardCombo({ InputCombo::Gamepad(65) }, 65), "A gamepad key fired a desktop hotkey");
}
