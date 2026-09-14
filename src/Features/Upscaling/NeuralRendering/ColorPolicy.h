#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace NeuralRendering::Color
{
	enum class Mode : std::uint32_t { Raw, Managed, PreserveLighting };
	enum class Transfer : std::uint32_t { Native, Linear, SRGB };
	enum class Codec : std::uint32_t { Identity, SRGB, ReinhardSRGB };

	struct Settings
	{
		Mode mode = Mode::Raw;
		Transfer sourceTransfer = Transfer::Native;
		Codec modelCodec = Codec::Identity;
		float whitePoint = 1.0f;
		float detailStrength = 0.65f;
		float appearanceMix = 0.0f;
		float maxDetailStops = 1.0f;
		std::uint32_t radius = 2;
		bool roundTrip = false;
		[[nodiscard]] bool Active() const noexcept { return mode != Mode::Raw; }
	};

	// Explicit profiles, not exposure inferred from a DXGI format or an SR option.
	struct Configuration { std::array<Settings, 2> profiles{}; };
	inline constexpr std::size_t kMaximumConfigurationBytes = 16384;

	[[nodiscard]] inline bool Finite(float a_value) noexcept
	{
		// Remains meaningful in builds that enable /fp:fast.
		return (std::bit_cast<std::uint32_t>(a_value) & 0x7f800000u) != 0x7f800000u;
	}

	[[nodiscard]] inline std::string_view Trim(std::string_view a_text) noexcept
	{
		const auto first = a_text.find_first_not_of(" \t\r");
		if (first == std::string_view::npos)
			return {};
		return a_text.substr(first, a_text.find_last_not_of(" \t\r") - first + 1);
	}

	[[nodiscard]] inline bool Number(std::string_view a_text, float& a_value,
		float a_minimum, float a_maximum) noexcept
	{
		float value = 0;
		const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
		if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() ||
			!Finite(value) || value < a_minimum || value > a_maximum)
			return false;
		a_value = value;
		return true;
	}

	// Strict and transactional: failure never publishes a partly parsed profile.
	[[nodiscard]] inline bool Parse(std::string_view a_text,
		Configuration& a_output, std::string& a_error)
	{
		Configuration candidate;
		std::array<std::uint32_t, 2> seen{};
		std::array<bool, 2> sections{};
		int section = -1;
		std::uint32_t lineNumber = 0;
		a_error.clear();
		auto fail = [&](std::string_view reason) {
			a_error = "line " + std::to_string(lineNumber) + ": " + std::string(reason);
			return false;
		};
		if (a_text.size() > kMaximumConfigurationBytes)
			return fail("configuration exceeds 16 KiB");
		while (!a_text.empty()) {
			++lineNumber;
			const auto end = a_text.find('\n');
			auto line = Trim(a_text.substr(0, end));
			a_text = end == std::string_view::npos ? std::string_view{} : a_text.substr(end + 1);
			if (line.empty() || line.front() == '#' || line.front() == ';')
				continue;
			if (line.front() == '[') {
				if (line == "[UpscaledCentre]") section = 0;
				else if (line == "[FinalLdrPreUI]") section = 1;
				else return fail("unknown section");
				if (sections[section]) return fail("duplicate section");
				sections[section] = true;
				continue;
			}
			const auto equals = line.find('=');
			if (section < 0 || equals == std::string_view::npos)
				return fail("expected key=value inside a named section");
			const auto key = Trim(line.substr(0, equals));
			const auto value = Trim(line.substr(equals + 1));
			auto& s = candidate.profiles[section];
			std::uint32_t bit = 0;
			bool valid = true;
			if (key == "Mode") {
				bit = 1u << 0;
				if (value == "raw") s.mode = Mode::Raw;
				else if (value == "managed") s.mode = Mode::Managed;
				else if (value == "preserve_lighting") s.mode = Mode::PreserveLighting;
				else valid = false;
			} else if (key == "SourceTransfer") {
				bit = 1u << 1;
				if (value == "native") s.sourceTransfer = Transfer::Native;
				else if (value == "linear") s.sourceTransfer = Transfer::Linear;
				else if (value == "srgb") s.sourceTransfer = Transfer::SRGB;
				else valid = false;
			} else if (key == "ModelCodec") {
				bit = 1u << 2;
				if (value == "identity") s.modelCodec = Codec::Identity;
				else if (value == "srgb") s.modelCodec = Codec::SRGB;
				else if (value == "reinhard_srgb") s.modelCodec = Codec::ReinhardSRGB;
				else valid = false;
			} else if (key == "WhitePoint") {
				bit = 1u << 3; valid = Number(value, s.whitePoint, 0.0001f, 10000.0f);
			} else if (key == "DetailStrength") {
				bit = 1u << 4; valid = Number(value, s.detailStrength, 0.0f, 1.0f);
			} else if (key == "AppearanceMix") {
				bit = 1u << 5; valid = Number(value, s.appearanceMix, 0.0f, 1.0f);
			} else if (key == "MaxDetailStops") {
				bit = 1u << 6; valid = Number(value, s.maxDetailStops, 0.0f, 2.0f);
			} else if (key == "Radius") {
				bit = 1u << 7;
				valid = value.size() == 1 && value[0] >= '1' && value[0] <= '4';
				if (valid) s.radius = static_cast<std::uint32_t>(value[0] - '0');
			} else if (key == "RoundTrip") {
				bit = 1u << 8; valid = value == "0" || value == "1";
				s.roundTrip = value == "1";
			} else return fail("unknown key");
			if (!valid) return fail("invalid value for " + std::string(key));
			if ((seen[section] & bit) != 0) return fail("duplicate key");
			seen[section] |= bit;
		}
		for (const auto& s : candidate.profiles) {
			if (s.modelCodec != Codec::Identity && s.sourceTransfer != Transfer::Linear)
				return fail("non-identity ModelCodec requires explicit SourceTransfer=linear");
			if (s.roundTrip && !s.Active())
				return fail("RoundTrip requires a managed or preserve_lighting mode");
		}
		a_output = candidate;
		return true;
	}

	[[nodiscard]] inline const char* Name(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::Managed: return "managed";
		case Mode::PreserveLighting: return "preserve_lighting";
		default: return "raw";
		}
	}
}
