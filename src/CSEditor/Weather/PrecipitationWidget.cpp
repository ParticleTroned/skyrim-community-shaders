#include "PrecipitationWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Globals.h"
#include "RE/B/BSShaderManager.h"
#include "RE/N/NiSourceTexture.h"
#include "Utils/Game.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
	using PrecipitationData = RE::BGSShaderParticleGeometryData;
	using DataID = PrecipitationData::DataID;
	using SettingValue = PrecipitationData::SETTING_VALUE;

	void SetSettingValue(PrecipitationData* precipitation, DataID id, SettingValue value)
	{
		precipitation->GetSettingRef(id) = value;
	}

	void SetSetting(PrecipitationData* precipitation, DataID id, float value)
	{
		SettingValue setting{};
		setting.f = value;
		SetSettingValue(precipitation, id, setting);
	}

	void SetSetting(PrecipitationData* precipitation, DataID id, uint32_t value)
	{
		SettingValue setting{};
		setting.i = value;
		SetSettingValue(precipitation, id, setting);
	}

	enum class BoxSizeDecodeResult
	{
		kInvalid,
		kDecoded,
		kMigratedLegacyFloat
	};

	BoxSizeDecodeResult DecodeBoxSize(const json& storedValue, uint32_t& boxSize)
	{
		if (storedValue.is_number_unsigned()) {
			const auto value = storedValue.get<std::uint64_t>();
			if (value <= std::numeric_limits<uint32_t>::max()) {
				boxSize = static_cast<uint32_t>(value);
				return BoxSizeDecodeResult::kDecoded;
			}
			return BoxSizeDecodeResult::kInvalid;
		}

		if (storedValue.is_number_integer()) {
			const auto value = storedValue.get<std::int64_t>();
			if (value >= 0 &&
				static_cast<std::uint64_t>(value) <= std::numeric_limits<uint32_t>::max()) {
				boxSize = static_cast<uint32_t>(value);
				return BoxSizeDecodeResult::kDecoded;
			}
			return BoxSizeDecodeResult::kInvalid;
		}

		if (!storedValue.is_number_float())
			return BoxSizeDecodeResult::kInvalid;

		const double value = storedValue.get<double>();
		if (!std::isfinite(value) ||
			value < 0.0 ||
			value > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
			return BoxSizeDecodeResult::kInvalid;
		}
		// A legacy floating-point zero is ambiguous: the original integer union
		// value may have been flushed to zero before it was serialized.
		if (value == 0.0)
			return BoxSizeDecodeResult::kInvalid;

		// Old builds read the integer union member through SETTING_VALUE::f.
		// Small box sizes were therefore serialized as exact float subnormals.
		if (value > 0.0 && value < std::numeric_limits<float>::min()) {
			const double subnormalUnit = std::ldexp(1.0, -149);
			const double encodedValue = value / subnormalUnit;
			const double roundedValue = std::round(encodedValue);
			if (std::abs(encodedValue - roundedValue) > 1.0e-6 ||
				roundedValue < 1.0 ||
				roundedValue > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
				return BoxSizeDecodeResult::kInvalid;
			}

			boxSize = static_cast<uint32_t>(roundedValue);
			return BoxSizeDecodeResult::kMigratedLegacyFloat;
		}

		if (std::trunc(value) != value)
			return BoxSizeDecodeResult::kInvalid;

		boxSize = static_cast<uint32_t>(value);
		return BoxSizeDecodeResult::kMigratedLegacyFloat;
	}

	bool DecodeUint32(
		const json& storedValue,
		uint32_t minimum,
		uint32_t maximum,
		uint32_t& value)
	{
		std::uint64_t decoded = 0;
		if (storedValue.is_number_unsigned()) {
			decoded = storedValue.get<std::uint64_t>();
		} else if (storedValue.is_number_integer()) {
			const auto signedValue = storedValue.get<std::int64_t>();
			if (signedValue < 0)
				return false;
			decoded = static_cast<std::uint64_t>(signedValue);
		} else if (storedValue.is_number_float()) {
			const double floatValue = storedValue.get<double>();
			if (!std::isfinite(floatValue) ||
				std::trunc(floatValue) != floatValue ||
				floatValue < 0.0 ||
				floatValue > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
				return false;
			}
			decoded = static_cast<std::uint64_t>(floatValue);
		} else {
			return false;
		}

		if (decoded < minimum || decoded > maximum)
			return false;
		value = static_cast<uint32_t>(decoded);
		return true;
	}

	namespace PrecipitationTab
	{
		constexpr const char* kParticle = "Particle";
		constexpr const char* kPosition = "Position";
		constexpr const char* kTexture = "Texture";
	}

	namespace PrecipitationSetting
	{
		constexpr const char* kType = "Type";
		constexpr const char* kSizeX = "Size X";
		constexpr const char* kSizeY = "Size Y";
		constexpr const char* kGravityVelocity = "Gravity Velocity";
		constexpr const char* kRotationVelocity = "Rotation Velocity";
		constexpr const char* kCenterOffsetMin = "Center Offset Min";
		constexpr const char* kCenterOffsetMax = "Center Offset Max";
		constexpr const char* kStartRotationRange = "Start Rotation Range";
		constexpr const char* kBoxSize = "Box Size";
		constexpr const char* kParticleDensity = "Particle Density";
		constexpr const char* kNumSubtexturesX = "Num Subtextures X";
		constexpr const char* kNumSubtexturesY = "Num Subtextures Y";
		constexpr const char* kParticleTexture = "Particle Texture";
	}
}

void PrecipitationWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##PrecipitationSearch", true, true);
		DrawSearchDropdown();

		if (ImGui::BeginTabBar("PrecipitationTabs")) {
			const ImGuiTabItemFlags particleFlags = GetTabFlagsForOverride(PrecipitationTab::kParticle);
			const ImGuiTabItemFlags positionFlags = GetTabFlagsForOverride(PrecipitationTab::kPosition);
			const ImGuiTabItemFlags textureFlags = GetTabFlagsForOverride(PrecipitationTab::kTexture);

			if (ImGui::BeginTabItem(PrecipitationTab::kParticle, nullptr, particleFlags)) {
				BeginScrollableContent("##ParticleScroll");
				DrawIfMatchesSearch(PrecipitationSetting::kType, [&](const char* label) {
					ImGui::SeparatorText("Particle Type");
					const char* types[] = { "Rain", "Snow" };
					int currentType = static_cast<int>(settings.particleType);
					bool comboChanged = DrawWithHighlight(label, [&]() {
						return ImGui::Combo(label, &currentType, types, IM_ARRAYSIZE(types));
					});
					if (comboChanged) {
						EditorWindow::GetSingleton()->PushUndoState(this);
						settings.particleType = static_cast<uint32_t>(currentType);
						return true;
					}
					return false;
				});
				if (MatchesAnySearch({ PrecipitationSetting::kSizeX, PrecipitationSetting::kSizeY })) {
					ImGui::SeparatorText("Particle Size");
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kSizeX, settings.particleSizeX, 0.0f, 200.0f);
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kSizeY, settings.particleSizeY, 0.0f, 200.0f);
				}
				if (MatchesAnySearch({ PrecipitationSetting::kGravityVelocity, PrecipitationSetting::kRotationVelocity })) {
					ImGui::SeparatorText("Velocity");
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kGravityVelocity, settings.gravityVelocity, 0.0f, 10000.0f);
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kRotationVelocity, settings.rotationVelocity, 0.0f, 10000.0f);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(PrecipitationTab::kPosition, nullptr, positionFlags)) {
				BeginScrollableContent("##PositionScroll");
				if (MatchesAnySearch({ PrecipitationSetting::kCenterOffsetMin, PrecipitationSetting::kCenterOffsetMax, PrecipitationSetting::kStartRotationRange })) {
					ImGui::SeparatorText("Offset");
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kCenterOffsetMin, settings.centerOffsetMin, 0.0f, 200.0f);
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kCenterOffsetMax, settings.centerOffsetMax, 0.0f, 200.0f);
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kStartRotationRange, settings.startRotationRange, 0.0f, 360.0f);
				}
				if (MatchesAnySearch({ PrecipitationSetting::kBoxSize, PrecipitationSetting::kParticleDensity })) {
					ImGui::SeparatorText("Volume");
					WeatherUtils::DrawSliderUint32(PrecipitationSetting::kBoxSize, settings.boxSize, 0, 1000);
					WeatherUtils::DrawSliderFloat(PrecipitationSetting::kParticleDensity, settings.particleDensity, 0.0f, 1000.0f);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(PrecipitationTab::kTexture, nullptr, textureFlags)) {
				BeginScrollableContent("##TextureScroll");
				if (MatchesAnySearch({ PrecipitationSetting::kNumSubtexturesX, PrecipitationSetting::kNumSubtexturesY })) {
					ImGui::SeparatorText("Subtextures");
					int numX = static_cast<int>(std::min(
						settings.numSubtexturesX,
						static_cast<uint32_t>(std::numeric_limits<int>::max())));
					int numY = static_cast<int>(std::min(
						settings.numSubtexturesY,
						static_cast<uint32_t>(std::numeric_limits<int>::max())));
					bool numXActivated = false;
					const bool numXChanged =
						DrawIfMatchesSearch(PrecipitationSetting::kNumSubtexturesX, [&](const char* label) {
							const bool changed = DrawWithHighlight(label, [&]() {
								return ImGui::InputInt(label, &numX);
							});
							numXActivated = ImGui::IsItemActivated();
							return changed;
						});
					if (numXActivated)
						EditorWindow::GetSingleton()->PushUndoState(this);
					if (numXChanged)
						settings.numSubtexturesX = std::max(1, numX);

					bool numYActivated = false;
					const bool numYChanged =
						DrawIfMatchesSearch(PrecipitationSetting::kNumSubtexturesY, [&](const char* label) {
							const bool changed = DrawWithHighlight(label, [&]() {
								return ImGui::InputInt(label, &numY);
							});
							numYActivated = ImGui::IsItemActivated();
							return changed;
						});
					if (numYActivated)
						EditorWindow::GetSingleton()->PushUndoState(this);
					if (numYChanged)
						settings.numSubtexturesY = std::max(1, numY);
				}
				DrawSearchSectionIfMatches(PrecipitationSetting::kParticleTexture, [&](const char* label) {
					ImGui::SeparatorText("Texture Path");
					const bool inputChanged = DrawWithHighlight(label, [&]() {
						return ImGui::InputText(label, textureBuffer, sizeof(textureBuffer));
					});
					if (ImGui::IsItemActivated())
						EditorWindow::GetSingleton()->PushUndoState(this);
					std::string_view buf(textureBuffer);
					if (buf != lastCheckedBuffer) {
						lastCheckedBuffer = std::string(buf);
						lastCheckedExists = WeatherUtils::TexturePath::ExistsOnDisk(buf);
					}
					if (inputChanged) {
						if (buf.empty()) {
							settings.particleTexture.clear();
							lastAppliedTexture.clear();
							lastAppliedPrecip.reset();
							lastInvalidTexture.clear();
						} else if (WeatherUtils::TexturePath::HasDdsExtension(buf) && lastCheckedExists) {
							settings.particleTexture = lastCheckedBuffer;
						}
					}
					if (settings.particleTexture != buf && !buf.empty()) {
						if (!WeatherUtils::TexturePath::HasDdsExtension(buf))
							ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "Path must end with '.dds'");
						else if (!lastCheckedExists)
							ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "Texture not found in loose files or archives.");
					}
				});

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void PrecipitationWidget::LoadSettings()
{
	if (!precipitation)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		auto readFloat = [&](const char* key, float minimum, float maximum, float& target) {
			const auto it = js.find(key);
			if (it == js.end())
				return;
			try {
				if (!it->is_number())
					throw std::runtime_error("value is not numeric");
				const double value = it->get<double>();
				if (!std::isfinite(value) || value < minimum || value > maximum)
					throw std::runtime_error("value is outside the supported range");
				target = static_cast<float>(value);
			} catch (const std::exception& e) {
				logger::warn(
					"Precipitation {}: invalid {} override ({}); keeping game value",
					GetEditorID(), key, e.what());
			}
		};
		auto readUint = [&](const char* key, uint32_t minimum, uint32_t maximum, uint32_t& target) {
			const auto it = js.find(key);
			if (it == js.end())
				return;
			try {
				uint32_t value = target;
				if (!DecodeUint32(*it, minimum, maximum, value))
					throw std::runtime_error("value is outside the supported integer range");
				target = value;
			} catch (const std::exception& e) {
				logger::warn(
					"Precipitation {}: invalid {} override ({}); keeping game value",
					GetEditorID(), key, e.what());
			}
		};

		readFloat("gravityVelocity", 0.0f, 10000.0f, settings.gravityVelocity);
		readFloat("rotationVelocity", 0.0f, 10000.0f, settings.rotationVelocity);
		readFloat("particleSizeX", 0.0f, 200.0f, settings.particleSizeX);
		readFloat("particleSizeY", 0.0f, 200.0f, settings.particleSizeY);
		readFloat("centerOffsetMin", 0.0f, 200.0f, settings.centerOffsetMin);
		readFloat("centerOffsetMax", 0.0f, 200.0f, settings.centerOffsetMax);
		readFloat("startRotationRange", 0.0f, 360.0f, settings.startRotationRange);
		readUint(
			"numSubtexturesX", 1, static_cast<uint32_t>(std::numeric_limits<int>::max()),
			settings.numSubtexturesX);
		readUint(
			"numSubtexturesY", 1, static_cast<uint32_t>(std::numeric_limits<int>::max()),
			settings.numSubtexturesY);
		readUint("particleType", 0, 1, settings.particleType);
		if (const auto boxSizeIt = js.find("boxSize"); boxSizeIt != js.end()) {
			uint32_t boxSize = settings.boxSize;
			const auto decodeResult = DecodeBoxSize(*boxSizeIt, boxSize);
			if (decodeResult != BoxSizeDecodeResult::kInvalid && boxSize <= 1000) {
				settings.boxSize = boxSize;
				if (decodeResult == BoxSizeDecodeResult::kMigratedLegacyFloat) {
					logger::info(
						"Precipitation {}: migrated legacy float boxSize to {}",
						GetEditorID(), boxSize);
				}
			} else {
				logger::warn(
					"Precipitation {}: invalid boxSize override; keeping game value {}",
					GetEditorID(), settings.boxSize);
			}
		}
		readFloat("particleDensity", 0.0f, 1000.0f, settings.particleDensity);

		if (const auto textureIt = js.find("particleTexture"); textureIt != js.end()) {
			if (!textureIt->is_string()) {
				logger::warn("Precipitation {}: particleTexture is not a string, skipping", GetEditorID());
			} else {
				const auto texPath = textureIt->get<std::string>();
				if (texPath.empty()) {
					settings.particleTexture.clear();
				} else if (!WeatherUtils::TexturePath::HasDdsExtension(texPath)) {
					logger::warn("Precipitation {}: ignoring malformed texture path '{}'", GetEditorID(), texPath);
				} else {
					settings.particleTexture = texPath;
					if (!WeatherUtils::TexturePath::ExistsOnDisk(texPath))
						logger::warn("Precipitation {}: saved texture path '{}' not found in game resources", GetEditorID(), texPath);
				}
			}
		}
	} else {
		settings = vanillaSettings;
	}

	originalSettings = settings;
	strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
	ApplyChanges();
}

void PrecipitationWidget::LoadFromGameSettings()
{
	if (!precipitation)
		return;

	settings.gravityVelocity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
	settings.rotationVelocity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kRotationVelocity).f;
	settings.particleSizeX = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleSizeX).f;
	settings.particleSizeY = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleSizeY).f;
	settings.centerOffsetMin = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMin).f;
	settings.centerOffsetMax = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMax).f;
	settings.startRotationRange = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kStartRotationRange).f;
	settings.numSubtexturesX = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesX).i;
	settings.numSubtexturesY = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesY).i;
	settings.particleType = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleType).i;
	settings.boxSize = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kBoxSize).i;
	settings.particleDensity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	GET_INSTANCE_MEMBER(particleTexture, precipitation)
	settings.particleTexture = particleTexture.textureName.c_str();
}

void PrecipitationWidget::SaveSettings()
{
	js["gravityVelocity"] = settings.gravityVelocity;
	js["rotationVelocity"] = settings.rotationVelocity;
	js["particleSizeX"] = settings.particleSizeX;
	js["particleSizeY"] = settings.particleSizeY;
	js["centerOffsetMin"] = settings.centerOffsetMin;
	js["centerOffsetMax"] = settings.centerOffsetMax;
	js["startRotationRange"] = settings.startRotationRange;
	js["numSubtexturesX"] = settings.numSubtexturesX;
	js["numSubtexturesY"] = settings.numSubtexturesY;
	js["particleType"] = settings.particleType;
	js["boxSize"] = settings.boxSize;
	js["particleDensity"] = settings.particleDensity;
	js["particleTexture"] = settings.particleTexture;
}

void PrecipitationWidget::ApplyChanges()
{
	if (!CanApplyPersistentChanges())
		return;

	if (!precipitation)
		return;

	SetSetting(precipitation, DataID::kGravityVelocity, settings.gravityVelocity);
	SetSetting(precipitation, DataID::kRotationVelocity, settings.rotationVelocity);
	SetSetting(precipitation, DataID::kParticleSizeX, settings.particleSizeX);
	SetSetting(precipitation, DataID::kParticleSizeY, settings.particleSizeY);
	SetSetting(precipitation, DataID::kCenterOffsetMin, settings.centerOffsetMin);
	SetSetting(precipitation, DataID::kCenterOffsetMax, settings.centerOffsetMax);
	SetSetting(precipitation, DataID::kStartRotationRange, settings.startRotationRange);
	SetSetting(precipitation, DataID::kNumSubtexturesX, settings.numSubtexturesX);
	SetSetting(precipitation, DataID::kNumSubtexturesY, settings.numSubtexturesY);
	SetSetting(precipitation, DataID::kParticleType, settings.particleType);
	SetSetting(precipitation, DataID::kBoxSize, settings.boxSize);
	SetSetting(precipitation, DataID::kParticleDensity, settings.particleDensity);
	GET_INSTANCE_MEMBER(particleTexture, precipitation)
	const std::string resourcePath = WeatherUtils::TexturePath::BuildResourcePath(settings.particleTexture);
	bool applyLiveTexture = false;
	if (settings.particleTexture.empty()) {
		particleTexture.textureName = "";
		lastInvalidTexture.clear();
	} else if (!resourcePath.empty() && WeatherUtils::TexturePath::ExistsOnDisk(resourcePath)) {
		// SPGD stores paths relative to Textures, while the resource system needs the prefix.
		const std::string recordPath =
			resourcePath.substr(WeatherUtils::TexturePath::kResourcePrefix.size());
		particleTexture.textureName = recordPath.c_str();
		lastInvalidTexture.clear();
		applyLiveTexture = true;
	} else {
		if (settings.particleTexture != lastInvalidTexture) {
			logger::warn(
				"Precipitation {}: texture '{}' is invalid or unavailable",
				GetEditorID(), settings.particleTexture);
			lastInvalidTexture = settings.particleTexture;
		}
	}
	if (applyLiveTexture)
		ApplyLiveParticleTexture(resourcePath);
	Widget::ForceCurrentWeatherReinit();
}

void PrecipitationWidget::ApplyLiveParticleTexture(const std::string& resourcePath)
{
	if (resourcePath.empty())
		return;

	auto* sky = globals::game::sky;
	if (!sky || !sky->precip)
		return;

	if (resourcePath == lastAppliedTexture &&
		sky->precip->currentPrecip == lastAppliedPrecip &&
		sky->precip->lastPrecip == lastAppliedPrecip)
		return;

	RE::NiPointer<RE::NiTexture> tex;
	RE::BSShaderManager::GetTexture(resourcePath.c_str(), true, tex, false);
	if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get())
		return;

	auto* sourceTex = static_cast<RE::NiSourceTexture*>(tex.get());
	if (!sourceTex->rendererTexture || !sourceTex->rendererTexture->texture)
		return;

	RE::BSGeometry* precipObjects[] = { sky->precip->currentPrecip.get(), sky->precip->lastPrecip.get() };
	for (auto* precipObject : precipObjects) {
		if (!precipObject)
			continue;
		if (auto* shaderProp = netimmerse_cast<RE::BSParticleShaderProperty*>(precipObject->GetGeometryRuntimeData().shaderProperty.get()))
			shaderProp->particleShaderTexture = RE::NiPointer(sourceTex);
	}

	lastAppliedTexture = resourcePath;
	lastInvalidTexture.clear();
	lastAppliedPrecip = sky->precip->currentPrecip;
}

void PrecipitationWidget::RevertChanges()
{
	settings = vanillaSettings;
	strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
	lastAppliedTexture.clear();
	lastAppliedPrecip.reset();
	lastInvalidTexture.clear();
	lastCheckedBuffer.clear();
	lastCheckedExists = false;
	ApplyChanges();
}

Widget::UndoRestoreAction PrecipitationWidget::CaptureUndoState() const
{
	const auto snapshot = settings;
	return [snapshot](Widget& widget) {
		auto& self = static_cast<PrecipitationWidget&>(widget);
		self.settings = snapshot;
		strncpy_s(self.textureBuffer, sizeof(self.textureBuffer), self.settings.particleTexture.c_str(), _TRUNCATE);
		self.lastAppliedTexture.clear();
		self.lastAppliedPrecip.reset();
	};
}

Widget::UndoRestoreAction PrecipitationWidget::CaptureBaselineState() const
{
	const auto snapshot = vanillaSettings;
	return [snapshot](Widget& widget) {
		static_cast<PrecipitationWidget&>(widget).vanillaSettings = snapshot;
	};
}

bool PrecipitationWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

std::vector<Widget::SearchResult> PrecipitationWidget::CollectSearchableSettings() const
{
	const std::vector<std::pair<std::string, std::vector<std::string>>> entries = {
		{ PrecipitationTab::kParticle, { PrecipitationSetting::kType, PrecipitationSetting::kSizeX, PrecipitationSetting::kSizeY, PrecipitationSetting::kGravityVelocity, PrecipitationSetting::kRotationVelocity } },
		{ PrecipitationTab::kPosition, { PrecipitationSetting::kCenterOffsetMin, PrecipitationSetting::kCenterOffsetMax, PrecipitationSetting::kStartRotationRange, PrecipitationSetting::kBoxSize, PrecipitationSetting::kParticleDensity } },
		{ PrecipitationTab::kTexture, { PrecipitationSetting::kNumSubtexturesX, PrecipitationSetting::kNumSubtexturesY, PrecipitationSetting::kParticleTexture } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ name, tab, name });
		}
	}
	return results;
}
