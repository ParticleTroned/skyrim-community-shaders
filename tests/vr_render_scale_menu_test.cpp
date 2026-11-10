#include "Features/Upscaling/VRRenderScaleModePolicy.h"
#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>

namespace ImGui
{
	bool disabled = false;
	bool checked = false;
	std::optional<bool> edit;
	void TextUnformatted(const char*) {}
}

namespace Util
{
	struct DisableGuard
	{
		bool previous = ImGui::disabled;
		explicit DisableGuard(bool disabled) { ImGui::disabled = previous || disabled; }
		~DisableGuard() { ImGui::disabled = previous; }
	};
	bool HoverTooltipWrapper() { return false; }
	namespace Widgets
	{
		bool Checkbox(const char*, bool* value)
		{
			ImGui::checked = *value;
			if (ImGui::disabled || !ImGui::edit || *ImGui::edit == *value)
				return false;
			*value = *ImGui::edit;
			return true;
		}
	}
}

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class VRUpscalingTransitionOrigin
	{
		CSMenu
	};
	enum class UpscalingTransitionApplyDisposition
	{
		Applied,
		Rejected
	};
	struct VRRenderScaleDesiredProfile
	{
		UpscaleMethod method = UpscaleMethod::kDLSS;
		unsigned qualityMode = 3;
		unsigned dlssPreset = 5;
		bool renderScaleModePreference = true;
		bool fsr4RuntimeEnabled = true;
	};
	struct ApplyResult
	{
		UpscalingTransitionApplyDisposition disposition;
	};
	bool fallback = false, savedIntent = true, blocked = false, nrRequired = false;
	bool reject = false, linked = true;
	unsigned requests = 0;
	VRRenderScaleDesiredProfile submitted;
	bool direct = false;
	VRVendorRelatchPolicy::StartupNativeFallbackControl control{};
	bool IsVRStartupNativeFallbackRestartRequired() const { return fallback; }
	bool IsVRStartupNativeFallbackSavedIntentActive() const { return savedIntent; }
	bool IsOpenCompositeUpscalingBlocked() const { return blocked; }
	bool IsNeuralRenderingRenderScaleRequired() const { return nrRequired; }
	void SetRenderScaleLinkedToUpscaling(bool value) { linked = value; }
	ApplyResult ApplyCSMenuUpscalingTransition(
		UpscaleMethod method, bool enabled, unsigned quality, unsigned preset,
		const char*, VRUpscalingTransitionOrigin, unsigned, std::optional<bool> fsr4,
		VRVendorRelatchPolicy::StartupNativeFallbackControl fallbackControl, bool directMenuEdit)
	{
		++requests;
		submitted = { method, quality, preset, enabled, fsr4.value_or(false) };
		control = fallbackControl;
		direct = directMenuEdit;
		return { reject ? UpscalingTransitionApplyDisposition::Rejected : UpscalingTransitionApplyDisposition::Applied };
	}
};

bool IsRenderScaleMethodEligible(Upscaling::UpscaleMethod method)
{
	return method == Upscaling::UpscaleMethod::kFSR || method == Upscaling::UpscaleMethod::kDLSS;
}
bool IsRenderScaleQualityMode(unsigned quality) { return quality != 0; }

#include "render_scale_menu_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

int main()
{
	using Method = Upscaling::UpscaleMethod;
	for (auto method : { Method::kNONE, Method::kTAA, Method::kFSR, Method::kDLSS }) {
		for (unsigned quality = 0; quality <= 6; ++quality) {
			for (bool preference : { false, true }) {
				Upscaling upscaling;
				const Upscaling::VRRenderScaleDesiredProfile profile{ method, quality, 5, preference, true };
				const bool available = (method == Method::kFSR || method == Method::kDLSS) && quality != 0;
				const bool enabled = available && preference;
				ImGui::edit = !enabled;
				DrawVRRenderScaleModeSetting(upscaling, profile);
				Require(ImGui::checked == enabled, "Native AA and ineligible methods must display Render Scale off");
				Require(upscaling.requests == (available ? 1u : 0u), "Unavailable controls must not publish edits");
				if (available) {
					Require(upscaling.submitted.method == method && upscaling.submitted.qualityMode == quality &&
								upscaling.submitted.dlssPreset == 5 && upscaling.submitted.fsr4RuntimeEnabled,
						"Edits must retain the complete displayed pending profile");
					Require(upscaling.submitted.renderScaleModePreference == !enabled && upscaling.direct,
						"Discrete checkbox edits must be submitted immediately");
					Require(upscaling.linked == !enabled, "Accepted manual off must unlink; enabling must retain the link");
				}
			}
		}
	}
	const Upscaling::VRRenderScaleDesiredProfile profile{};
	{
		Upscaling upscaling;
		ImGui::edit.reset();
		DrawVRRenderScaleModeSetting(upscaling, profile);
		Require(upscaling.requests == 0 && upscaling.linked,
			"Drawing without an edit must preserve settings");
	}
	for (bool blocked : { false, true }) {
		for (bool nrRequired : { false, true }) {
			Upscaling upscaling;
			upscaling.blocked = blocked;
			upscaling.nrRequired = nrRequired;
			ImGui::edit = false;
			DrawVRRenderScaleModeSetting(upscaling, profile);
			Require(upscaling.requests == (!blocked && !nrRequired ? 1u : 0u),
				"OpenComposite and required NR must lock the control");
		}
	}
	for (bool reject : { false, true }) {
		Upscaling upscaling;
		upscaling.fallback = true;
		upscaling.reject = reject;
		ImGui::edit = false;
		DrawVRRenderScaleModeSetting(upscaling, profile);
		Require(upscaling.requests == 1 &&
					upscaling.control == VRVendorRelatchPolicy::StartupNativeFallbackControl::DisableSavedProfile,
			"Startup fallback must retain the explicit saved-profile disable action");
		Require(upscaling.linked == reject, "Rejected manual off must preserve linking");
	}
	Require(!ImGui::disabled, "The disabled state must be restored after drawing");
}
