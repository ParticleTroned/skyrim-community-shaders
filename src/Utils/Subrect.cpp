#include "Utils/Subrect.h"
#include "Utils/NormalizedCoordinates.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace
{
	Util::Subrect::UVRegion ClampUV(Util::Subrect::UVRegion uv)
	{
		if (!std::isfinite(uv.x))
			uv.x = 0.0f;
		if (!std::isfinite(uv.y))
			uv.y = 0.0f;
		if (!std::isfinite(uv.w))
			uv.w = 1.0f;
		if (!std::isfinite(uv.h))
			uv.h = 1.0f;

		constexpr double minimumExtent = 0.01;
		const double x = std::clamp(static_cast<double>(uv.x), 0.0, 1.0 - minimumExtent);
		const double y = std::clamp(static_cast<double>(uv.y), 0.0, 1.0 - minimumExtent);
		uv.x = static_cast<float>(x);
		uv.y = static_cast<float>(y);
		uv.w = static_cast<float>(std::clamp(static_cast<double>(uv.w), minimumExtent, 1.0 - x));
		uv.h = static_cast<float>(std::clamp(static_cast<double>(uv.h), minimumExtent, 1.0 - y));

		return uv;
	}

	Util::Subrect::UVRegion DefaultUV()
	{
		return {};
	}

	Util::Subrect::UVRegion LoadUVArray(const json& arr)
	{
		Util::Subrect::UVRegion uv = DefaultUV();
		if (arr.is_array() && arr.size() == 4) {
			uv.x = arr[0];
			uv.y = arr[1];
			uv.w = arr[2];
			uv.h = arr[3];
		}
		return ClampUV(uv);
	}

	json SaveUVToJson(const Util::Subrect::UVRegion& uv)
	{
		return { uv.x, uv.y, uv.w, uv.h };
	}

	bool MatchesUV(const Util::Subrect::UVRegion& lhs, const Util::Subrect::UVRegion& rhs)
	{
		constexpr float epsilon = 1.0e-6f;
		return std::abs(lhs.x - rhs.x) <= epsilon &&
		       std::abs(lhs.y - rhs.y) <= epsilon &&
		       std::abs(lhs.w - rhs.w) <= epsilon &&
		       std::abs(lhs.h - rhs.h) <= epsilon;
	}

	bool IsFullFrame(const Util::Subrect::UVRegion& uv)
	{
		return MatchesUV(uv, Util::Subrect::UVRegion{});
	}

}

namespace Util::Subrect
{
	PixelRegion ResolvePixelRegion(const UVRegion& uv, uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) {
			return { 0, 0, 0, 0 };
		}

		const UVRegion normalized = ClampUV(uv);
		const uint32_t left = NormalizedCoordinates::ResolvePixelBoundary(normalized.x, width);
		const uint32_t top = NormalizedCoordinates::ResolvePixelBoundary(normalized.y, height);
		const uint32_t right = NormalizedCoordinates::ResolvePixelBoundary(normalized.x + normalized.w, width);
		const uint32_t bottom = NormalizedCoordinates::ResolvePixelBoundary(normalized.y + normalized.h, height);

		PixelRegion result;
		result.x = std::min(width - 1, left);
		result.y = std::min(height - 1, top);
		result.w = std::clamp(right, result.x + 1, width) - result.x;
		result.h = std::clamp(bottom, result.y + 1, height) - result.y;
		return result;
	}

	void Controller::LoadSettings(const json& a_json)
	{
		if (a_json.contains("CropX"))
			currentUV.x = a_json["CropX"];
		if (a_json.contains("CropY"))
			currentUV.y = a_json["CropY"];
		if (a_json.contains("CropW"))
			currentUV.w = a_json["CropW"];
		if (a_json.contains("CropH"))
			currentUV.h = a_json["CropH"];

		if (a_json.contains("CropPresets") && a_json["CropPresets"].is_array()) {
			presets.clear();
			for (auto& entry : a_json["CropPresets"]) {
				Preset preset;
				preset.name = entry.value("name", "Unknown");
				if (entry.contains("uv")) {
					preset.uv = LoadUVArray(entry["uv"]);
				}
				presets.push_back(std::move(preset));
			}
		}

		EnsureDefaultPreset();
		ClampCurrentUV();

		if (a_json.contains("SelectedPresetIndex")) {
			selectedPresetIndex = a_json["SelectedPresetIndex"];
			if (selectedPresetIndex >= 0 && selectedPresetIndex < static_cast<int>(presets.size())) {
				ApplyPreset(selectedPresetIndex);
			} else {
				selectedPresetIndex = -1;
			}
		}

		ReconcileSeededDefaults();
	}

	void Controller::SaveSettings(json& a_json) const
	{
		a_json["CropX"] = currentUV.x;
		a_json["CropY"] = currentUV.y;
		a_json["CropW"] = currentUV.w;
		a_json["CropH"] = currentUV.h;

		json presetsJson = json::array();
		for (const auto& preset : presets) {
			json entry;
			entry["name"] = preset.name;
			entry["uv"] = SaveUVToJson(preset.uv);
			presetsJson.push_back(std::move(entry));
		}
		a_json["CropPresets"] = presetsJson;
		a_json["SelectedPresetIndex"] = selectedPresetIndex;
	}

	void Controller::SeedDefaultPresets(std::vector<Preset> defaults)
	{
		seededDefaults = std::move(defaults);
		ReconcileSeededDefaults();
	}

	void Controller::ReconcileSeededDefaults()
	{
		if (seededDefaults.empty()) {
			return;
		}

		const bool hasLegacyFullFramePlaceholder =
			presets.size() == 1 &&
			presets.front().name == "Full Frame" &&
			IsFullFrame(presets.front().uv);
		if (!presets.empty() && !hasLegacyFullFramePlaceholder) {
			MigrateLegacySeededPresetNames();
			return;
		}

		const UVRegion previousUV = currentUV;
		const int previousPresetIndex = selectedPresetIndex;
		presets = seededDefaults;
		if (!hasLegacyFullFramePlaceholder) {
			currentUV = presets.front().uv;
			selectedPresetIndex = 0;
		} else if (previousPresetIndex == 0 && IsFullFrame(previousUV)) {
			const auto fullFrame = std::find_if(presets.begin(), presets.end(), [](const Preset& preset) {
				return IsFullFrame(preset.uv);
			});
			selectedPresetIndex = fullFrame != presets.end() ?
			                          static_cast<int>(fullFrame - presets.begin()) :
			                          -1;
			currentUV = previousUV;
		} else {
			currentUV = previousUV;
			selectedPresetIndex = -1;
		}
		ClampCurrentUV();
	}

	void Controller::DrawEditor(ID3D11ShaderResourceView* previewSrv, ID3D11Texture2D* previewTexture, float uvVisibleWidth, float uvStartX, ImDrawCallback imageRenderCallback)
	{
		// Hosts that render without first calling LoadSettings would otherwise
		// see an empty presets vector and the combo would mislabel as "(Custom)".
		EnsureDefaultPreset();
		if (selectedPresetIndex < -1 || selectedPresetIndex >= static_cast<int>(presets.size())) {
			selectedPresetIndex = -1;
		}

		std::string currentPreview =
			(selectedPresetIndex >= 0 && selectedPresetIndex < static_cast<int>(presets.size())) ? presets[selectedPresetIndex].name : "(Custom)";

		if (ImGui::BeginCombo("Crop Preset", currentPreview.c_str())) {
			for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
				const bool isSelected = selectedPresetIndex == i;
				if (ImGui::Selectable(presets[i].name.c_str(), isSelected)) {
					ApplyPreset(i);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::InputText("Save As", newPresetName, sizeof(newPresetName));
		ImGui::SameLine();
		if (ImGui::Button("Save Preset")) {
			std::string presetName = newPresetName;
			if (!presetName.empty()) {
				presets.push_back(Preset{ .name = presetName, .uv = currentUV });
				selectedPresetIndex = static_cast<int>(presets.size()) - 1;
				newPresetName[0] = '\0';
			}
		}

		if (selectedPresetIndex > 0) {
			ImGui::SameLine();
			if (ImGui::Button("Delete Preset")) {
				presets.erase(presets.begin() + selectedPresetIndex);
				ApplyPreset(0);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Reset Crop")) {
			ApplyPreset(0);
		}

		ImGui::Spacing();
		ImGui::PushItemWidth(250.0f);
		bool changed = false;
		changed |= ImGui::SliderFloat2("Position UV (X, Y)", &currentUV.x, 0.0f, 1.0f, "%.3f");
		changed |= ImGui::SliderFloat2("Size UV (W, H)", &currentUV.w, 0.01f, 1.0f, "%.3f");
		ImGui::PopItemWidth();

		if (changed) {
			selectedPresetIndex = -1;
			ClampCurrentUV();
		}

		ImGui::Spacing();
		ImGui::Text("Interactive Cropping (Drag on the image to select)");

		if (!previewSrv || !previewTexture) {
			ImGui::TextDisabled("Preview unavailable.");
			return;
		}

		D3D11_TEXTURE2D_DESC desc{};
		previewTexture->GetDesc(&desc);
		float maxWidth = std::min(400.0f, ImGui::GetContentRegionAvail().x);
		float aspectRatio = (static_cast<float>(desc.Width) * uvVisibleWidth) / static_cast<float>(desc.Height);
		ImVec2 imageSize(maxWidth, maxWidth / aspectRatio);
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		ImDrawList* hostDrawList = ImGui::GetWindowDrawList();
		if (imageRenderCallback) {
			hostDrawList->AddCallback(imageRenderCallback, nullptr);
		}
		ImGui::Image(reinterpret_cast<ImTextureID>(previewSrv), imageSize,
			ImVec2(uvStartX, 0.0f), ImVec2(uvStartX + uvVisibleWidth, 1.0f));
		if (imageRenderCallback) {
			hostDrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}

		ImGui::SetCursorScreenPos(cursorPos);
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton("##subrectCanvas", imageSize);

		ImVec2 mousePos = ImGui::GetIO().MousePos;
		ImVec2 relativeMouseP(mousePos.x - cursorPos.x, mousePos.y - cursorPos.y);
		float mouseUVX = std::clamp(relativeMouseP.x / imageSize.x, 0.0f, 1.0f);
		float mouseUVY = std::clamp(relativeMouseP.y / imageSize.y, 0.0f, 1.0f);

		if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			isDraggingCrop = true;
			selectedPresetIndex = -1;
			dragStartUV[0] = mouseUVX;
			dragStartUV[1] = mouseUVY;
			currentUV.x = mouseUVX;
			currentUV.y = mouseUVY;
			currentUV.w = 0.0f;
			currentUV.h = 0.0f;
		}

		if (isDraggingCrop) {
			float minX = std::min(dragStartUV[0], mouseUVX);
			float minY = std::min(dragStartUV[1], mouseUVY);
			float maxX = std::max(dragStartUV[0], mouseUVX);
			float maxY = std::max(dragStartUV[1], mouseUVY);

			currentUV.x = minX;
			currentUV.y = minY;
			currentUV.w = maxX - minX;
			currentUV.h = maxY - minY;
			ClampCurrentUV();

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				isDraggingCrop = false;
			}
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 pMin(cursorPos.x + currentUV.x * imageSize.x, cursorPos.y + currentUV.y * imageSize.y);
		ImVec2 pMax(cursorPos.x + (currentUV.x + currentUV.w) * imageSize.x,
			cursorPos.y + (currentUV.y + currentUV.h) * imageSize.y);
		drawList->AddRect(pMin, pMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
	}

	PixelRegion Controller::GetPixelRegion(uint32_t width, uint32_t height) const
	{
		return ResolvePixelRegion(currentUV, width, height);
	}

	void Controller::EnsureDefaultPreset()
	{
		if (!presets.empty()) {
			return;
		}
		if (!seededDefaults.empty()) {
			presets = seededDefaults;
			// currentUV must match what the combo shows as selected; otherwise
			// the first preset appears chosen but the crop region stays full-frame.
			currentUV = presets[0].uv;
			selectedPresetIndex = 0;
		} else {
			presets.push_back(Preset{ .name = "Full Frame", .uv = DefaultUV() });
		}
	}

	void Controller::MigrateLegacySeededPresetNames()
	{
		if (presets.size() < 3 || seededDefaults.size() < 3) {
			return;
		}

		const bool hasLegacyVRStereoDefaults =
			seededDefaults[0].name == "Left Eye" &&
			seededDefaults[1].name == "Right Eye" &&
			presets[0].name == "Left Eye" &&
			presets[1].name == "Right Eye" &&
			presets[2].name == "Full Frame" &&
			MatchesUV(presets[0].uv, seededDefaults[0].uv) &&
			MatchesUV(presets[1].uv, seededDefaults[1].uv) &&
			IsFullFrame(presets[2].uv) &&
			IsFullFrame(seededDefaults[2].uv);
		if (hasLegacyVRStereoDefaults) {
			presets[2].name = seededDefaults[2].name;
		}
	}

	void Controller::ClampCurrentUV()
	{
		currentUV = ClampUV(currentUV);
	}

	void Controller::ApplyPreset(int index)
	{
		EnsureDefaultPreset();
		selectedPresetIndex = std::clamp(index, 0, static_cast<int>(presets.size()) - 1);
		currentUV = presets[selectedPresetIndex].uv;
		ClampCurrentUV();
	}
}  // namespace Util::Subrect
