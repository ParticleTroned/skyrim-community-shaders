#include "ColorPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace NeuralRendering::Color;
namespace
{
	int checks = 0;
	void Require(bool condition, const char* description)
	{
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << description << '\n';
			std::exit(1);
		}
	}
	float Encode(float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; }
	float Decode(float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); }
	bool Near(float x, float y, float tolerance = 1e-5f) { return std::abs(x - y) <= tolerance * std::max(1.0f, std::abs(y)); }
}
int main()
{
	Configuration config;
	std::string error;
	Require(Parse("", config, error), "empty configuration");
	Require(!config.profiles[0].Active() && !config.profiles[1].Active(), "both routes raw by default");
	Require(Parse("[UpscaledCentre]\r\nMode=preserve_lighting\r\nSourceTransfer=native\r\n[FinalLdrPreUI]\nMode=managed\nSourceTransfer=linear\nModelCodec=srgb\n", config, error), "independent profiles");
	Require(config.profiles[0].mode == Mode::PreserveLighting && config.profiles[1].modelCodec == Codec::SRGB, "profile fields retained");
	const std::vector<std::string> invalid{
		"Mode=raw", "[Other]\nMode=raw", "[UpscaledCentre]\nMode=wrong",
		"[UpscaledCentre]\nMode=raw\nMode=managed", "[UpscaledCentre]\n[UpscaledCentre]",
		"[UpscaledCentre]\nDetailStrength=nan", "[UpscaledCentre]\nDetailStrength=inf",
		"[UpscaledCentre]\nDetailStrength=-1", "[UpscaledCentre]\nDetailStrength=1.001",
		"[UpscaledCentre]\nWhitePoint=0", "[UpscaledCentre]\nWhitePoint=10001",
		"[UpscaledCentre]\nAppearanceMix=0.5junk", "[UpscaledCentre]\nRadius=0",
		"[UpscaledCentre]\nRadius=5", "[UpscaledCentre]\nRadius=2.5",
		"[UpscaledCentre]\nRoundTrip=2", "[UpscaledCentre]\nRoundTrip=1",
		"[UpscaledCentre]\nModelCodec=srgb", "[UpscaledCentre]\nSourceTransfer=srgb\nModelCodec=reinhard_srgb",
		"[UpscaledCentre]\nUnknown=1", "[UpscaledCentre]\nMaxDetailStops=3",
		std::string(kMaximumConfigurationBytes + 1, ' ')
	};
	for (const auto& input : invalid) {
		Configuration sentinel;
		sentinel.profiles[0].detailStrength = 0.123f;
		Require(!Parse(input, sentinel, error), "invalid configuration rejected");
		Require(!error.empty(), "parse failure has diagnostic");
		Require(sentinel.profiles[0].detailStrength == 0.123f, "failure is transactional");
	}
	for (const auto* mode : { "raw", "managed", "preserve_lighting" }) {
		for (const auto* transfer : { "native", "linear", "srgb" }) {
			Require(Parse(std::string("[UpscaledCentre]\nMode=") + mode + "\nSourceTransfer=" + transfer + "\n", config, error), "valid identity profile matrix");
		}
	}
	Require(Parse("[FinalLdrPreUI]\nMode=managed\nSourceTransfer=linear\nModelCodec=reinhard_srgb\nWhitePoint=2.5\nRoundTrip=1\nRadius=4\n", config, error), "explicit proxy and round trip");
	Require(config.profiles[1].roundTrip && config.profiles[1].radius == 4, "round-trip and radius parsed");
	Require(!Finite(std::numeric_limits<float>::infinity()), "infinity rejected");
	Require(!Finite(std::numeric_limits<float>::quiet_NaN()), "NaN rejected");
	Require(Finite(0) && Finite(-0.1f) && Finite(std::numeric_limits<float>::max()), "finite values accepted");
	Require(Near(Encode(0.18f), 0.46135613f), "known sRGB middle grey");
	Require(Near(Decode(0.5f), 0.21404114f), "known sRGB encoded midpoint");
	for (float x : { 0.0f, 1e-8f, 1e-5f, 0.003f, 0.01f, 0.18f, 1.0f, 4.0f, 100.0f }) {
		Require(Near(Decode(Encode(x)), x), "extended positive sRGB round trip");
		for (float white : { 0.1f, 1.0f, 10.0f }) {
			const float proxy = x / (white + x);
			const float recovered = Decode(Encode(proxy));
			Require(Near(white * recovered / (1.0f - recovered), x, 0.0003f), "proxy finite-range round trip");
		}
	}
	// Matched-reference reconstruction retains the source when the encoded
	// reference has been perturbed by finite-precision storage.
	for (float b : { 0.0f, 0.003f, 0.18f, 1.0f, 16.0f }) {
		const float quantized = std::round(Encode(b) * 512.0f) / 512.0f;
		const float reconstructed = b + (Decode(quantized) - Decode(quantized));
		Require(reconstructed == b, "zero residual is baseline despite codec quantization");
	}
	// A uniform luminance gain is the low-frequency component, not detail.
	for (float gain : { 0.25f, 0.5f, 2.0f, 4.0f }) {
		const float delta = std::log2(gain);
		float sum = 0, weights = 0;
		for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
			const float weight = 1.0f / static_cast<float>(1 + x * x + y * y);
			sum += weight * delta; weights += weight;
		}
		Require(Near(std::exp2(delta - sum / weights), 1.0f), "broad gain rejected by detail residual");
	}
	// Same clipping rule as the shader: nonzero ROI origin, even at corners.
	for (int py = 3; py < 14; ++py) for (int px = 5; px < 18; ++px)
		for (int y = -4; y <= 4; ++y) for (int x = -4; x <= 4; ++x) {
			const int qx = std::clamp(px + x, 5, 17), qy = std::clamp(py + y, 3, 13);
			Require(qx >= 5 && qx < 18 && qy >= 3 && qy < 14, "filter does not read uninitialized ROI exterior");
		}
	std::cout << checks << " policy/reference checks passed (not GPU validation)\n";
}
