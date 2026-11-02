#include "Game.h"
#include <RE/B/BSParticleShaderProperty.h>
#include <RE/B/BSParticleShaderRainEmitter.h>
#include <cfloat>
#include <cmath>
#include <limits>

#include <atomic>

#include "Globals.h"
#include "State.h"

namespace
{
	constexpr std::uint32_t timeJumpTransitionBit = 1u << 0;
	constexpr std::uint32_t gameLoadTransitionBit = 1u << 1;

	std::atomic_bool celestialTransitionHandlerAvailable{ false };
	std::atomic_uint32_t pendingCelestialTransitions{ 0 };
	std::atomic_uint32_t completedCelestialTransitionGeneration{ 0 };

	void MarkCelestialTransitionComplete()
	{
		completedCelestialTransitionGeneration.fetch_add(1, std::memory_order_release);
	}

	void RequestCelestialTransition(std::uint32_t a_transitionBit)
	{
		pendingCelestialTransitions.fetch_or(a_transitionBit, std::memory_order_release);
		if (!celestialTransitionHandlerAvailable.load(std::memory_order_acquire) &&
			pendingCelestialTransitions.exchange(0, std::memory_order_acq_rel) != 0) {
			MarkCelestialTransitionComplete();
		}
	}
}

namespace Util
{
	bool HasDirectionalLightDiscontinuity(const RE::NiPoint3& a_currentDirection, const RE::NiPoint3& a_previousDirection) noexcept
	{
		const auto isFinite = [](const RE::NiPoint3& direction) {
			return std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z);
		};
		if (!isFinite(a_currentDirection) || !isFinite(a_previousDirection))
			return false;
		auto current = a_currentDirection;
		auto previous = a_previousDirection;
		const float currentLength = current.Unitize();
		const float previousLength = previous.Unitize();
		if (!std::isfinite(currentLength) || !std::isfinite(previousLength) || currentLength <= FLT_EPSILON || previousLength <= FLT_EPSILON)
			return false;
		constexpr float threshold = RE::NI_PI / 180.0f;
		const auto difference = current - previous;
		return difference.Dot(difference) >= threshold * threshold;
	}

	RE::TESLandTexture* GetDefaultLandTexture()
	{
		static const REL::Relocation<RE::TESLandTexture**> defaultTexture{ RELOCATION_ID(514783, 400936) };
		return *defaultTexture;
	}

	void SetCelestialTransitionHandlerAvailable(bool a_available)
	{
		celestialTransitionHandlerAvailable.store(a_available, std::memory_order_release);
		if (a_available)
			return;

		if (pendingCelestialTransitions.exchange(0, std::memory_order_acq_rel) != 0)
			MarkCelestialTransitionComplete();
	}

	void RequestTimeJumpTransition()
	{
		RequestCelestialTransition(timeJumpTransitionBit);
	}

	void RequestGameLoadTransition()
	{
		RequestCelestialTransition(gameLoadTransitionBit);
	}

	CelestialTransitionRequest ConsumeCelestialTransitionRequest()
	{
		if (!celestialTransitionHandlerAvailable.load(std::memory_order_acquire))
			return {};

		const auto pending = pendingCelestialTransitions.exchange(0, std::memory_order_acq_rel);
		return {
			.timeJump = (pending & timeJumpTransitionBit) != 0,
			.gameLoad = (pending & gameLoadTransitionBit) != 0,
		};
	}

	void CompleteCelestialTransition()
	{
		MarkCelestialTransitionComplete();
	}

	std::uint32_t GetCompletedCelestialTransitionGeneration()
	{
		return completedCelestialTransitionGeneration.load(std::memory_order_acquire);
	}

	namespace
	{
		RE::ImageSpaceManager::UNK_BSImagespaceShaderISTemporalAA* GetTemporalAAShader()
		{
			auto* imageSpaceManager = globals::game::imageSpaceManager ? globals::game::imageSpaceManager : RE::ImageSpaceManager::GetSingleton();
			if (!imageSpaceManager)
				return nullptr;

			return imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA;
		}
	}

	float4 TryGetWaterData(float offsetX, float offsetY)
	{
		if (globals::game::shadowState) {
			if (auto tes = RE::TES::GetSingleton()) {
				auto position = GetEyePosition();
				position.x += offsetX;
				position.y += offsetY;
				if (auto cell = tes->GetCell(position)) {
					float4 data = float4(1.0f, 1.0f, 1.0f, -FLT_MAX);

					bool extraCellWater = false;

					if (auto extraCellWaterType = cell->extraList.GetByType<RE::ExtraCellWaterType>()) {
						if (auto water = extraCellWaterType->water) {
							{
								data = { float(water->data.deepWaterColor.red) + float(water->data.shallowWaterColor.red),
									float(water->data.deepWaterColor.green) + float(water->data.shallowWaterColor.green),
									float(water->data.deepWaterColor.blue) + float(water->data.shallowWaterColor.blue) };

								data.x /= 255.0f;
								data.y /= 255.0f;
								data.z /= 255.0f;

								data.x *= 0.5;
								data.y *= 0.5;
								data.z *= 0.5;
								extraCellWater = true;
							}
						}
					}

					if (!extraCellWater) {
						if (auto worldSpace = tes->GetRuntimeData2().worldSpace) {
							if (auto water = worldSpace->worldWater) {
								data = { float(water->data.deepWaterColor.red) + float(water->data.shallowWaterColor.red),
									float(water->data.deepWaterColor.green) + float(water->data.shallowWaterColor.green),
									float(water->data.deepWaterColor.blue) + float(water->data.shallowWaterColor.blue) };

								data.x /= 255.0f;
								data.y /= 255.0f;
								data.z /= 255.0f;

								data.x *= 0.5;
								data.y *= 0.5;
								data.z *= 0.5;
							}
						}
					}

					if (auto sky = globals::game::sky) {
						const auto& color = sky->skyColor[RE::TESWeather::ColorTypes::kWaterMultiplier];
						data.x *= color.red;
						data.y *= color.green;
						data.z *= color.blue;
					}

					data.w = cell->GetExteriorWaterHeight() - position.z;

					return data;
				}
			}
		}
		return float4(1.0f, 1.0f, 1.0f, -FLT_MAX);
	}

	RE::NiPoint3 GetEyePosition()
	{
		auto shadowState = globals::game::shadowState;
		return shadowState->GetRuntimeData().posAdjust.getEye();
	}

	float4 GetCameraData()
	{
		static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
		static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));

		float4 cameraData{};
		cameraData.x = cameraFar;
		cameraData.y = cameraNear;
		cameraData.z = cameraFar - cameraNear;
		cameraData.w = cameraFar * cameraNear;

		return cameraData;
	}

	bool GetTemporal()
	{
		auto* taaShader = GetTemporalAAShader();
		return taaShader && taaShader->taaEnabled;
	}

	void SetTemporal(bool enabled)
	{
		if (auto* taaShader = GetTemporalAAShader())
			taaShader->taaEnabled = enabled;
	}

	void DisableVanillaTAA()
	{
		if (auto* setting = RE::GetINISetting("bUseTAA:Display"))
			setting->data.b = false;
	}

	float GetVerticalFOVRad()
	{
		static float& cameraFOVDeg = (*(float*)(REL::RelocationID(513786, 388785).address()));  // FOV degrees
		auto viewport = globals::game::graphicsState;
		if (!viewport || viewport->screenWidth <= 0 || viewport->screenHeight <= 0) {
			return 0.0f;
		}

		float hFOVRad = cameraFOVDeg * (3.14159265359f / 180.0f);
		float unitHalfWidth = tan(hFOVRad / 2);                                                                                           // This is same as camera frustum RL
		float unitHalfHeight = unitHalfWidth / (static_cast<float>(viewport->screenWidth) / static_cast<float>(viewport->screenHeight));  // frustum TB
		float vFOVRad = 2.0f * atan(unitHalfHeight);
		return vFOVRad;
	}

	float2 ConvertToDynamic(float2 a_size, bool a_ignoreLock)
	{
		auto viewport = globals::game::graphicsState;
		auto& runtimeData = viewport->GetRuntimeData();

		if (runtimeData.dynamicResolutionLock && !a_ignoreLock)
			return a_size;

		return float2(
			a_size.x * runtimeData.dynamicResolutionWidthRatio,
			a_size.y * runtimeData.dynamicResolutionHeightRatio);
	}

	DispatchCount GetScreenDispatchCount(bool a_dynamic)
	{
		auto viewport = globals::game::graphicsState;
		if (!viewport) {
			return { 0, 0 };
		}

		float2 resolution{
			static_cast<float>(viewport->screenWidth),
			static_cast<float>(viewport->screenHeight)
		};

		// Feature passes use the scaled render area even when the vanilla dynamic-resolution lock is set.
		if (a_dynamic)
			resolution = ConvertToDynamic(resolution, true);

		if (resolution.x <= 0.0f || resolution.y <= 0.0f) {
			return { 0, 0 };
		}

		uint dispatchX = (uint)std::ceil(resolution.x / 8.0f);
		uint dispatchY = (uint)std::ceil(resolution.y / 8.0f);

		return { dispatchX, dispatchY };
	}

	bool IsDynamicResolution()
	{
		const static auto address = REL::RelocationID{ 508794, 380760 }.address();
		bool* bDynamicResolution = reinterpret_cast<bool*>(address);
		return *bDynamicResolution;
	}

	std::string FormatTESForm(const RE::TESForm* form)
	{
		if (!form) {
			return "nullptr";
		}

		// Get name and editor ID
		const char* rawName = form->GetName();
		const char* rawEditorID = form->GetFormEditorID();

		std::string name;
		std::string editorID = rawEditorID ? rawEditorID : "Unknown";

		// Check if name exists and is not just whitespace
		if (rawName && strlen(rawName) > 0) {
			std::string tempName(rawName);
			// Check if name is only whitespace
			bool isOnlyWhitespace = std::all_of(tempName.begin(), tempName.end(),
				[](unsigned char c) { return std::isspace(c); });

			if (!isOnlyWhitespace) {
				name = tempName;
			}
		}
		// Format the FormID part once
		std::string formIDStr = " - 0x" + std::format("{:08X}", form->GetFormID());

		// If no valid name, use editor ID as name and don't show it twice
		if (name.empty()) {
			return editorID + formIDStr;
		} else {
			return name + " " + editorID + formIDStr;
		}
	}
	std::string FormatWeather(const RE::TESWeather* weather)
	{
		if (!weather) {
			return "nullptr";
		}

		std::string baseFormat = FormatTESForm(weather);

		// Get all flag names for this weather using magic_enum
		std::vector<std::string> flagNames;
		uint32_t flags = weather->data.flags.underlying();

		if (flags == 0) {
			flagNames.push_back("None");
		} else {
			// Use magic_enum to iterate through all weather flags
			for (auto flagValue : magic_enum::enum_values<RE::TESWeather::WeatherDataFlag>()) {
				if (flagValue != RE::TESWeather::WeatherDataFlag::kNone &&
					weather->data.flags.any(flagValue)) {
					// Convert enum name to human-readable format
					std::string flagName = std::string(magic_enum::enum_name(flagValue));

					// Remove 'k' prefix and convert to readable format
					if (flagName.starts_with("k")) {
						flagName = flagName.substr(1);
					}

					// Convert specific cases to more readable names
					if (flagName == "PermAurora") {
						flagName = "Aurora";
					} else if (flagName == "AuroraFollowsSun") {
						flagName = "Aurora Sun";
					}

					flagNames.push_back(flagName);
				}
			}

			// Check for any unknown flags (flags not covered by the enum)
			uint32_t knownFlags = 0;
			for (auto flagValue : magic_enum::enum_values<RE::TESWeather::WeatherDataFlag>()) {
				if (flagValue != RE::TESWeather::WeatherDataFlag::kNone) {
					knownFlags |= static_cast<uint32_t>(flagValue);
				}
			}

			uint32_t unknownFlags = flags & ~knownFlags;
			if (unknownFlags != 0) {
				flagNames.push_back("Unknown(" + std::to_string(unknownFlags) + ")");
			}
		}

		// Join flag names with commas
		std::string flagsStr;
		for (size_t i = 0; i < flagNames.size(); ++i) {
			if (i > 0) {
				flagsStr += ", ";
			}
			flagsStr += flagNames[i];
		}

		return baseFormat + " [" + flagsStr + "]";
	}

	bool FrameChecker::IsNewFrame()
	{
		return IsNewFrame(globals::state->frameCount);
	}

	RE::BGSTextureSet* GetSeasonalSwap(RE::BGSTextureSet* textureSet)
	{
		if (textureSet == nullptr) {
			return nullptr;
		}

		if (textureSet->pad12C > 0) {
			if (auto* form = RE::TESForm::LookupByID<RE::BGSTextureSet>(textureSet->pad12C)) {
				return form;
			}
		}

		return textureSet;
	}

	bool IsInterior()
	{
		auto tes = RE::TES::GetSingleton();
		if (tes && !tes->interiorCell) {
			if (auto worldSpace = tes->GetRuntimeData2().worldSpace) {
				if (!worldSpace->flags.any(RE::TESWorldSpace::Flag::kNoSky, RE::TESWorldSpace::Flag::kFixedDimensions)) {
					return false;
				}
			}
		}
		return true;
	}

	void WorldToCell(const RE::NiPoint2& worldPos, int32_t& x, int32_t& y)
	{
		x = static_cast<int32_t>(floor(worldPos.x / 4096.0f));
		y = static_cast<int32_t>(floor(worldPos.y / 4096.0f));
	}

	void WorldToCell(const RE::NiPoint3& worldPos, int32_t& x, int32_t& y)
	{
		WorldToCell(RE::NiPoint2(worldPos.x, worldPos.y), x, y);
	}
}  // namespace Util

namespace
{
	void ResetModelHandle(RE::ModelDBHandle& a_handle)
	{
		if (!a_handle)
			return;

		if (REL::Module::IsAE()) {
			auto* entry = a_handle.get();
			a_handle = {};
			static REL::Relocation<void (*)(RE::ModelDBHandle::U_Entry*)> release{ REL::ID(15443) };
			release(entry);
		} else {
			using Reset = RE::ModelDBHandle* (*)(RE::ModelDBHandle*, RE::ModelDBHandle::U_Entry*);
			static REL::Relocation<Reset> reset{ REL::ID(25746) };
			reset(&a_handle, nullptr);
		}
	}
}

namespace Util
{
	RE::TESObjectREFR* GetOwnUserData(const RE::NiAVObject* object)
	{
		return object ? object->userData : nullptr;
	}

	RE::BSParticleShaderRainEmitter* GetRainEmitterFromPrecipGeometry(RE::BSGeometry* a_precipObject)
	{
		if (!a_precipObject)
			return nullptr;

		auto* shaderProperty = a_precipObject->GetGeometryRuntimeData().shaderProperty.get();
		auto* particleProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProperty);
		if (!particleProperty || !particleProperty->particleEmitter)
			return nullptr;

		auto* emitter = particleProperty->particleEmitter;
		if (!emitter->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain))
			return nullptr;

		return static_cast<RE::BSParticleShaderRainEmitter*>(emitter);
	}

	void RefreshForcedWeatherSky(RE::Sky* a_sky)
	{
		if (!a_sky)
			return;

		if (a_sky->auroraRoot) {
			if (a_sky->root)
				a_sky->root->DetachChild(a_sky->auroraRoot.get());
			a_sky->auroraRoot.reset();
		}
		ResetModelHandle(a_sky->auroraModel);

		// Defer cloud-pass rebuilding until accumulation; current queues may still borrow these passes.
		if (a_sky->clouds) {
			for (const auto& cloud : a_sky->clouds->clouds) {
				if (cloud) {
					if (auto* property = skyrim_cast<RE::BSSkyShaderProperty*>(cloud->GetGeometryRuntimeData().shaderProperty.get()))
						property->lastRenderPassState = (std::numeric_limits<std::int32_t>::max)();
				}
			}
		}
	}
}
