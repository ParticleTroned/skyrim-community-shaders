#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace UpscalingTextures
{
	struct VRInputSourceDescriptions
	{
		bool matches = true;
		static inline bool captureSucceeds = true;
		bool Matches(void*, void*, void*, void*) const { return matches; }
		bool Capture(void*, void*, void*, void*) const { return captureSucceeds; }
	};
}
namespace sl
{
	using ViewportHandle = uint32_t;
}
enum class DLSSViewportRole
{
	FullEye
};

void Require(bool value, const char* message);

struct Upscaling
{
	static constexpr uint32_t kDLSSPresetK = 11;
	enum class UpscaleMethod
	{
		kDLSS,
		kFSR
	};
	struct VRExistingVendorProviderSnapshot
	{
		bool valid = true;
		UpscaleMethod method = UpscaleMethod::kDLSS;
		uint32_t renderEyeWidth = 80, renderEyeHeight = 80;
		uint32_t displayEyeWidth = 100, displayEyeHeight = 100;
		uint32_t contractGeneration = 17, qualityMode = 4, dlssPreset = 10;
	} provider;
	struct Texture
	{
		struct Resource
		{
			void* get() { return this; }
		} resource;
	} textures[2];
	std::array<Texture*, 2> vrIntermediateColorIn{ &textures[0], &textures[1] };
	std::array<int, 4> sourceResources{};
	bool deferred = false, ordinarySave = false, advanceAfterSnapshot = false;
	bool dlssUpscaleOutputInSharpenerTexture = true;
	uint32_t textureGeneration = 17;
	unsigned snapshotReads = 0, generationReads = 0, compatibilityChecks = 0, viewportChecks = 0;
	int unavailableEye = -1;

	bool ShouldDeferVRVendorLifecycleMutation() const { return deferred; }
	bool ShouldReuseOrdinarySaveResources() const { return ordinarySave; }
	VRExistingVendorProviderSnapshot GetExistingVRVendorProviderSnapshot()
	{
		++snapshotReads;
		const auto captured = provider;
		if (advanceAfterSnapshot) {
			++provider.contractGeneration;
			++provider.qualityMode;
			++provider.dlssPreset;
		}
		return captured;
	}
	uint32_t GetVRVendorEvaluationContractGeneration(UpscaleMethod)
	{
		++generationReads;
		return provider.contractGeneration;
	}
	bool AreActiveVRIntermediateTexturesCompatible(UpscaleMethod method, uint32_t inputWidth, uint32_t inputHeight,
		uint32_t outputWidth, uint32_t outputHeight, void* color, void* motion, void* reactive, void* transparency,
		uint32_t generation, const UpscalingTextures::VRInputSourceDescriptions* descriptions)
	{
		++compatibilityChecks;
		Require(method == UpscaleMethod::kDLSS, "Compatibility must validate the DLSS provider");
		Require(inputWidth == 80 && inputHeight == 80 && outputWidth == 100 && outputHeight == 100,
			"Compatibility dimensions changed");
		Require(color == &sourceResources[0] && motion == &sourceResources[1] && reactive == &sourceResources[2] &&
					transparency == &sourceResources[3],
			"Compatibility must validate the original source resources");
		Require(descriptions && descriptions->matches, "Compatibility must receive valid source descriptions");
		return generation == textureGeneration;
	}
};

void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

bool Admit(Upscaling& upscaling, const UpscalingTextures::VRInputSourceDescriptions* a_sourceDescriptions)
{
	struct Size
	{
		float x, y;
	};
	const Size screenSize{ 200, 100 }, renderSize{ 160, 80 };
	void* a_upscalingTexture = &upscaling.sourceResources[0];
	void* a_motionVectors = &upscaling.sourceResources[1];
	void* a_reactiveMask = &upscaling.sourceResources[2];
	void* a_transparencyCompositionMask = &upscaling.sourceResources[3];
	const auto expectedQuality = upscaling.provider.qualityMode;
	const auto expectedPreset = upscaling.provider.dlssPreset;
	unsigned nextEye = 0;
	const auto TryResolveExistingVRDLSSViewport = [&](DLSSViewportRole role, uint32_t eye, uint32_t quality, uint32_t preset,
													  uint32_t width, uint32_t height, void* source, sl::ViewportHandle&) {
		++upscaling.viewportChecks;
		Require(role == DLSSViewportRole::FullEye && eye == nextEye++, "Each viewport must retain its eye ownership");
		Require(source == upscaling.vrIntermediateColorIn[eye]->resource.get(), "Viewport must use its eye's color input");
		Require(quality == expectedQuality && preset == expectedPreset, "Viewport profile must use the captured provider");
		Require(width == 100 && height == 100, "Viewport output dimensions changed");
		return static_cast<int>(eye) != upscaling.unavailableEye;
	};
	// Compile the production caller's admission block; replace only renderer dependencies.
#include "dlss_provider_reuse_under_test.h"
	const bool reuse = upscaling.deferred || upscaling.ordinarySave;
	Require(useAuthoritativeExistingProfile == reuse, "Only resource reuse may select the existing provider profile");
	Require(authoritativeQualityMode == (reuse ? expectedQuality : 0u), "Dispatch quality must retain provider selection");
	Require(authoritativeDLSSPreset == (reuse ? expectedPreset : Upscaling::kDLSSPresetK), "Dispatch preset must retain provider selection");
	return true;
}

int main()
{
	try {
		const UpscalingTextures::VRInputSourceDescriptions sources;
		unsigned cases = 0;
		for (bool deferred : { false, true }) {
			for (bool ordinarySave : { false, true }) {
				for (bool advance : { false, true }) {
					Upscaling up;
					up.deferred = deferred;
					up.ordinarySave = ordinarySave;
					up.advanceAfterSnapshot = advance;
					Require(Admit(up, &sources), "Matching provider must be admitted");
					const unsigned reuse = deferred || ordinarySave;
					Require(up.snapshotReads == reuse, "Only resource reuse may fetch one provider snapshot");
					Require(up.generationReads == 0, "A separate generation lookup must not run");
					Require(up.compatibilityChecks == reuse && up.viewportChecks == 2 * reuse, "Reuse checks must remain conditional");
					++cases;
				}
			}
		}
		for (unsigned failure = 0; failure < 9; ++failure) {
			Upscaling up;
			up.deferred = true;
			switch (failure) {
			case 0:
				up.provider.valid = false;
				break;
			case 1:
				up.provider.method = Upscaling::UpscaleMethod::kFSR;
				break;
			case 2:
				++up.provider.renderEyeWidth;
				break;
			case 3:
				++up.provider.renderEyeHeight;
				break;
			case 4:
				++up.provider.displayEyeWidth;
				break;
			case 5:
				++up.provider.displayEyeHeight;
				break;
			case 6:
				++up.textureGeneration;
				break;
			case 7:
				up.unavailableEye = 0;
				break;
			case 8:
				up.unavailableEye = 1;
				break;
			}
			Require(!Admit(up, &sources), "Invalid reuse must fail closed");
			Require(!up.dlssUpscaleOutputInSharpenerTexture, "Failed reuse must clear sharpener output");
			Require(up.snapshotReads == 1 && up.generationReads == 0, "Failed reuse must not fetch another generation");
			if (failure < 6)
				Require(up.compatibilityChecks == 0 && up.viewportChecks == 0, "Invalid provider must reject before resource reuse");
			if (failure == 6)
				Require(up.compatibilityChecks == 1 && up.viewportChecks == 0, "Generation mismatch must reject before viewport reuse");
			if (failure > 6)
				Require(up.viewportChecks == failure - 6, "Viewport rejection must stop at the failing eye");
			up.deferred = false;
			up.ordinarySave = true;
			up.dlssUpscaleOutputInSharpenerTexture = true;
			Require(!Admit(up, &sources), "Ordinary-save reuse must retain the same rejection checks");
			Require(!up.dlssUpscaleOutputInSharpenerTexture, "Ordinary-save failure must clear sharpener output");
			Require(up.snapshotReads == 2 && up.generationReads == 0, "Each reuse attempt must fetch one fresh provider");
			++cases;
		}
		{
			Upscaling up;
			up.provider.valid = false;
			up.provider.method = Upscaling::UpscaleMethod::kFSR;
			up.provider.contractGeneration = 0;
			Require(Admit(up, &sources), "Ordinary dispatch must not require a reusable provider");
			Require(up.snapshotReads == 0 && up.generationReads == 0 && up.compatibilityChecks == 0 && up.viewportChecks == 0,
				"Ordinary dispatch must not inspect the provider");
			++cases;
		}
		{
			Upscaling up;
			up.deferred = true;
			up.advanceAfterSnapshot = true;
			Require(Admit(up, &sources), "A captured provider must stay coherent within the operation");
			Require(!Admit(up, &sources), "A later operation must inspect the newer provider generation");
			Require(up.snapshotReads == 2 && up.generationReads == 0 && up.viewportChecks == 2,
				"A provider snapshot must never be cached across operations");
			++cases;
		}
		for (auto generation : { 0u, 1u, UINT32_MAX }) {
			Upscaling up;
			up.ordinarySave = true;
			up.provider.contractGeneration = generation;
			up.textureGeneration = generation;
			Require(Admit(up, &sources), "Preserve exact generation, including zero");
			++up.textureGeneration;
			Require(!Admit(up, &sources), "Generation mismatch must still reject");
			++cases;
		}
		for (bool captureSucceeds : { false, true }) {
			for (bool missing : { false, true }) {
				Upscaling up;
				up.deferred = true;
				UpscalingTextures::VRInputSourceDescriptions staleSources;
				staleSources.matches = false;
				UpscalingTextures::VRInputSourceDescriptions::captureSucceeds = captureSucceeds;
				Require(Admit(up, missing ? nullptr : &staleSources) == captureSucceeds, "Source recapture failure must still reject");
				Require(up.dlssUpscaleOutputInSharpenerTexture == captureSucceeds, "Source capture failure must clear sharpener output");
				Require(up.generationReads == 0 && up.snapshotReads == 1, "Source capture must not duplicate provider reads");
				Require(up.compatibilityChecks == static_cast<unsigned>(captureSucceeds), "Invalid source capture must stop compatibility checks");
				++cases;
			}
		}
		std::cout << "DLSS provider reuse: " << cases << " cases passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
