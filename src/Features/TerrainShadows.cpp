#include "TerrainShadows.h"

#include <bit>
#include <cctype>
#include <ranges>

#include <DirectXTex.h>
#include <pystring/pystring.h>

#include "Globals.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Util.h"

#define I18N_KEY_PREFIX "feature.terrain_shadows."

namespace
{
	RE::BSTEventSource<RE::BGSActorCellEvent>* GetPlayerCellEventSource(RE::PlayerCharacter* a_player)
	{
		return a_player ? a_player->AsBGSActorCellEventSource() : nullptr;
	}

	void RequestTimeJumpSynchronization()
	{
		Util::RequestTimeJumpTransition();
	}

	class TimeJumpEventHandler :
		public RE::BSTEventSink<RE::TESWaitStopEvent>,
		public RE::BSTEventSink<RE::TESSleepStopEvent>,
		public RE::BSTEventSink<RE::TESFastTravelEndEvent>,
		public RE::BSTEventSink<RE::BGSActorCellEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent*, RE::BSTEventSource<RE::TESWaitStopEvent>*) override
		{
			RequestTimeJumpSynchronization();
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
		{
			RequestTimeJumpSynchronization();
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESFastTravelEndEvent*, RE::BSTEventSource<RE::TESFastTravelEndEvent>*) override
		{
			RequestTimeJumpSynchronization();
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*) override
		{
			if (!a_event || a_event->flags != RE::BGSActorCellEvent::CellFlag::kLeave)
				return RE::BSEventNotifyControl::kContinue;

			const auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_event->cellID);
			if (cell && cell->IsInteriorCell())
				RequestTimeJumpSynchronization();
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	struct ConsoleCommandHook
	{
		static bool IsGameHourSetCommand(std::string_view a_command) noexcept
		{
			auto consumeToken = [&](std::string_view a_token) {
				while (!a_command.empty() && std::isspace(static_cast<unsigned char>(a_command.front())))
					a_command.remove_prefix(1);

				const auto tokenEnd = a_command.find_first_of(" \t\r\n");
				const auto token = a_command.substr(0, tokenEnd);
				if (!std::ranges::equal(token, a_token, [](char a_lhs, char a_rhs) {
						return std::tolower(static_cast<unsigned char>(a_lhs)) == std::tolower(static_cast<unsigned char>(a_rhs));
					})) {
					return false;
				}

				a_command.remove_prefix(token.size());
				return true;
			};

			if (!consumeToken("set") || !consumeToken("gamehour") || !consumeToken("to"))
				return false;

			while (!a_command.empty() && std::isspace(static_cast<unsigned char>(a_command.front())))
				a_command.remove_prefix(1);
			return !a_command.empty();
		}

		static void thunk(RE::FxDelegateArgs* a_args)
		{
			bool changesGameHour = false;
			if (a_args && a_args->GetArgCount() > 0) {
				const auto& commandValue = (*a_args)[0];
				if (commandValue.IsString()) {
					const auto command = commandValue.GetString();
					changesGameHour = command && IsGameHourSetCommand(command);
				}
			}

			func(a_args);
			if (changesGameHour)
				RequestTimeJumpSynchronization();
		}

		static void Install()
		{
			const auto result = stl::detour_thunk<ConsoleCommandHook>(REL::RelocationID(50157, 51084));
			if (result == NO_ERROR)
				logger::info("[Terrain Shadows] Installed console time-change hook");
			else
				logger::error("[Terrain Shadows] Failed to install console time-change hook: {}", result);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct PapyrusSetGlobalValueHook
	{
		static void thunk(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::TESGlobal* a_global, float a_value)
		{
			const auto calendar = globals::game::calendar;
			const bool changesGameHour = calendar && calendar->gameHour && a_global == calendar->gameHour;
			const float previousValue = changesGameHour ? a_global->value : 0.0f;

			func(a_vm, a_stackID, a_global, a_value);
			if (changesGameHour && a_global->value != previousValue)
				RequestTimeJumpSynchronization();
		}

		static void Install()
		{
			const auto result = stl::detour_thunk<PapyrusSetGlobalValueHook>(REL::RelocationID(55352, 55923));
			if (result == NO_ERROR)
				logger::info("[Terrain Shadows] Installed Papyrus GameHour change hook");
			else
				logger::error("[Terrain Shadows] Failed to install Papyrus GameHour change hook: {}", result);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainShadows::Settings,
	EnableTerrainShadow)

void TerrainShadows::PostPostLoad()
{
	static bool installed = false;
	if (installed)
		return;
	installed = true;

	ConsoleCommandHook::Install();
	PapyrusSetGlobalValueHook::Install();
}

void TerrainShadows::DataLoaded()
{
	static bool registered = false;
	if (registered)
		return;

	const auto eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
	if (!eventSourceHolder) {
		logger::error("[Terrain Shadows] Script event source holder not found");
		return;
	}

	static TimeJumpEventHandler eventHandler;
	if (const auto waitEvents = eventSourceHolder->GetEventSource<RE::TESWaitStopEvent>())
		waitEvents->AddEventSink(&eventHandler);
	if (const auto sleepEvents = eventSourceHolder->GetEventSource<RE::TESSleepStopEvent>())
		sleepEvents->AddEventSink(&eventHandler);
	if (const auto fastTravelEvents = eventSourceHolder->GetEventSource<RE::TESFastTravelEndEvent>())
		fastTravelEvents->AddEventSink(&eventHandler);

	if (const auto cellEvents = GetPlayerCellEventSource(RE::PlayerCharacter::GetSingleton()))
		cellEvents->AddEventSink(&eventHandler);
	else
		logger::warn("[Terrain Shadows] Player cell event source not found");

	registered = true;
}

void TerrainShadows::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainShadows::DrawSettings()
{
	ImGui::Checkbox("Enable", &settings.EnableTerrainShadow);

	if (ImGui::CollapsingHeader(T(TKEY("debug"), "Debug"))) {
		std::string curr_worldspace = "N/A";
		std::string curr_worldspace_name = "N/A";
		auto tes = RE::TES::GetSingleton();
		if (tes) {
			auto worldspace = tes->GetRuntimeData2().worldSpace;
			if (worldspace) {
				curr_worldspace = worldspace->GetFormEditorID();
				curr_worldspace_name = worldspace->GetName();
			}
		}
		ImGui::Text(fmt::format("Current worldspace: {} ({})", curr_worldspace, curr_worldspace_name).c_str());
		ImGui::Text(fmt::format("Has height map: {}", heightmaps.contains(curr_worldspace)).c_str());

		ImGui::Separator();

		ImGui::BulletText("shadowUpdateCBData");
		ImGui::Indent();
		{
			ImGui::Text(fmt::format("LightPxDir: ({}, {})", shadowUpdateCBData.LightPxDir.x, shadowUpdateCBData.LightPxDir.y).c_str());
			ImGui::Text(fmt::format("LightDeltaZ: ({}, {})", shadowUpdateCBData.LightDeltaZ.x, shadowUpdateCBData.LightDeltaZ.y).c_str());
			ImGui::Text(fmt::format("StartPxCoord: {}", shadowUpdateCBData.StartPxCoord).c_str());
			ImGui::Text(fmt::format("PxSize: ({}, {})", shadowUpdateCBData.PxSize.x, shadowUpdateCBData.PxSize.y).c_str());
		}
		ImGui::Unindent();

		if (ImGui::TreeNode(T(TKEY("buffer_viewer"), "Buffer Viewer"))) {
			static float debugRescale = .1f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

			if (texShadowHeight) {
				BUFFER_VIEWER_NODE_BULLET(texShadowHeight, debugRescale)
			}
			ImGui::TreePop();
		}
	}
}

void TerrainShadows::DrawEssentialSettings()
{
	ImGui::Checkbox("Enable", &settings.EnableTerrainShadow);
}

void TerrainShadows::ClearShaderCache()
{
	shadowUpdateProgram.Reset();

	CompileComputeShaders();
}

void TerrainShadows::ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style)
{
	auto filename = p.filename();
	if (filename.extension() != ".dds")
		return;
	logger::debug("Found dds: {}", filename.string());

	auto splitstr = pystring::split(filename.stem().string(), ".");
	if (splitstr.size() != (xlodgen_style ? 9 : 10)) {
		logger::debug("{} has incorrect number ({}) of fields", filename.string(), splitstr.size());
		return;
	}

	bool middle_check = xlodgen_style ? ((splitstr[1] == "Terrain") && (splitstr[2] == "HeightMap")) : (splitstr[1] == "HeightMap");
	if (middle_check) {
		HeightMapMetadata metadata;
		try {
			if (xlodgen_style) {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[4]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[6]) + 1) * 4096.f;
				metadata.pos0.z = -32767 * 8.f;
				metadata.pos1.z = 32767 * 8.f;
				metadata.zRange.x = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[8]) * 8.f;
			} else {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[2]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[4]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.z = std::stoi(splitstr[6]) * 8.f;
				metadata.pos1.z = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.x = std::stoi(splitstr[8]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[9]) * 8.f;
			}
		} catch (std::exception& e) {
			logger::debug("Failed to parse {}. Error: {}", filename.string(), e.what());
			return;
		}

		metadata.dir = p.parent_path().wstring();
		metadata.filename = filename.string();

		if (heightmaps.contains(metadata.worldspace))
			logger::warn("{} has more than one height maps!", metadata.worldspace);
		heightmaps[metadata.worldspace] = metadata;

		logger::info("{} loaded.", filename.string());
	} else
		logger::debug("{} has unknown type ({})", filename.string(), splitstr[1]);
}

void TerrainShadows::SetupResources()
{
	logger::debug("Listing xLODGen height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\Terrain\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec }) {
			auto dir_path = dir_entry.path();
			if (!std::filesystem::is_directory(dir_path))
				continue;

			for (auto const& sub_dir_entry : std::filesystem::directory_iterator{ dir_path })
				ParseHeightmapPath(sub_dir_entry.path(), true);
		}
	}

	logger::debug("Listing height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\heightmaps\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec })
			ParseHeightmapPath(dir_entry.path(), false);
	}

	logger::debug("Creating constant buffers...");
	{
		shadowUpdateCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<ShadowUpdateCB>(), "TerrainShadows::UpdateCB");
	}

	CompileComputeShaders();
}

void TerrainShadows::CompileComputeShaders()
{
	GetShadowUpdateProgram();
}

ID3D11ComputeShader* TerrainShadows::GetShadowUpdateProgram()
{
	return shadowUpdateProgram.Get(
		L"Data\\Shaders\\TerrainShadows\\ShadowUpdate.cs.hlsl",
		{},
		"cs_5_0",
		"main",
		"TerrainShadows::ShadowUpdateProgram");
}

bool TerrainShadows::IsHeightMapReady() const
{
	if (auto tes = RE::TES::GetSingleton())
		if (auto worldspace = tes->GetRuntimeData2().worldSpace)
			return cachedHeightmap && cachedHeightmap->worldspace == worldspace->GetFormEditorID();
	return false;
}

bool TerrainShadows::HasShadowUpdateResources() const
{
	return IsHeightMapReady() &&
	       texHeightMap && texHeightMap->srv &&
	       texShadowHeight && texShadowHeight->srv && texShadowHeight->uav &&
	       shadowUpdateCB && shadowUpdateCB->CB() &&
	       shadowUpdateProgram &&
	       globals::d3d::context &&
	       globals::state &&
	       globals::profiler;
}

RE::NiDirectionalLight* TerrainShadows::GetShadowUpdateSunLight() const
{
	if (!HasShadowUpdateResources())
		return nullptr;

	auto** accumulatorSlot = globals::game::currentAccumulator.get();
	if (!accumulatorSlot || !*accumulatorSlot)
		return nullptr;

	auto* shadowSceneNode = (*accumulatorSlot)->GetRuntimeData().activeShadowSceneNode;
	if (!shadowSceneNode)
		return nullptr;

	const auto& sunLight = shadowSceneNode->GetRuntimeData().sunLight;
	if (!sunLight || !sunLight->light)
		return nullptr;

	return skyrim_cast<RE::NiDirectionalLight*>(sunLight->light.get());
}

bool TerrainShadows::IsShadowMapRefreshPending() const
{
	return !shadowMapValid ||
	       fullRefreshActive ||
	       Util::GetCompletedCelestialTransitionGeneration() != handledTimeJumpRefreshGeneration;
}

bool TerrainShadows::IsPerformanceTuningApplicable() const
{
	return GetShadowUpdateSunLight() != nullptr;
}

const char* TerrainShadows::GetPerformanceTuningApplicabilityReason() const
{
	if (IsPerformanceTuningApplicable())
		return nullptr;
	if (!IsHeightMapReady() || !texHeightMap) {
		return T(
			"menu.performance_tuning.feature.terrain_shadows.no_heightmap",
			"Terrain Shadows have no loaded heightmap for the current worldspace, so there is no runtime work to measure.");
	}
	if (!HasShadowUpdateResources()) {
		return T(
			"menu.performance_tuning.feature.terrain_shadows.no_runtime_resources",
			"Terrain Shadows cannot be measured because their runtime update resources are unavailable.");
	}
	return T(
		"menu.performance_tuning.feature.terrain_shadows.no_active_sun",
		"Terrain Shadows cannot be measured because the current scene has no active directional sunlight.");
}

void TerrainShadows::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	if (settings.EnableTerrainShadow == a_enabled)
		return;

	settings.EnableTerrainShadow = a_enabled;
	InvalidateShadowMap();
}

bool TerrainShadows::IsPerformanceCostMeasurementReady() const
{
	return !settings.EnableTerrainShadow ||
	       (!IsShadowMapRefreshPending() && GetShadowUpdateSunLight() != nullptr);
}

const char* TerrainShadows::GetPerformanceCostMeasurementWaitText() const
{
	return T(
		"menu.performance_tuning.feature.terrain_shadows.wait",
		"Waiting for Terrain Shadows to refresh");
}

TerrainShadows::PerFrame TerrainShadows::GetCommonBufferData()
{
	bool isHeightmapReady = IsHeightMapReady();

	PerFrame data = {
		.EnableTerrainShadow = settings.EnableTerrainShadow && isHeightmapReady && !IsShadowMapRefreshPending(),
	};

	if (isHeightmapReady) {
		auto invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		data.Scale = float3(1.f, 1.f, 1.f) / invScale;
		data.Offset = -cachedHeightmap->pos0 * float2{ data.Scale.x, data.Scale.y };
		data.ZRange = cachedHeightmap->zRange;
	}

	return data;
}

void TerrainShadows::LoadHeightmap()
{
	auto tes = globals::game::tes;
	if (!tes)
		return;

	auto worldspace = tes->GetRuntimeData2().worldSpace;
	while (worldspace && worldspace->parentWorld && worldspace->parentUseFlags.any(RE::TESWorldSpace::ParentUseFlag::kUseLandData))
		worldspace = worldspace->parentWorld;

	if (!worldspace)
		return;

	std::string worldspace_name = worldspace->GetFormEditorID();
	if (!heightmaps.contains(worldspace_name))  // no height map for that, but we don't remove cache
		return;

	if (cachedHeightmap && cachedHeightmap->worldspace == worldspace_name)  // already cached
		return;

	auto device = globals::d3d::device;

	logger::debug("Loading height map...");
	{
		auto& target_heightmap = heightmaps[worldspace_name];

		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ target_heightmap.dir };
			path /= target_heightmap.filename;

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		auto loadedHeightMap = std::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "TerrainShadows::HeightMap");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = loadedHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		loadedHeightMap->CreateSRV(srvDesc);

		// Publish only a complete replacement. unique_ptr assignment destroys the
		// old wrapper and releases its texture and views instead of leaking them.
		texHeightMap = std::move(loadedHeightMap);

		cachedHeightmap = &heightmaps[worldspace_name];
	}

	shadowUpdateIdx = 0;
	needPrecompute = true;
	fullRefreshActive = false;
	shadowMapValid = false;
}

void TerrainShadows::Precompute()
{
	if (!cachedHeightmap)
		return;

	logger::info("Creating shadow texture...");
	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = texHeightMap->desc.Width,
			.Height = texHeightMap->desc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_UNORM,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto loadedShadowHeight = std::make_unique<Texture2D>(texDesc, "TerrainShadows::ShadowHeight");
		loadedShadowHeight->CreateSRV(srvDesc);
		loadedShadowHeight->CreateUAV(uavDesc);

		if (texShadowHeight) {
			auto context = globals::d3d::context;

			std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
			context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
			context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		}

		// Keep the current map alive if creation fails; swap only after the new
		// texture and both required views are ready.
		texShadowHeight = std::move(loadedShadowHeight);
	}
#undef I18N_KEY_PREFIX

	needPrecompute = false;
}

void TerrainShadows::BeginFullRefresh(std::uint32_t a_generation)
{
	activeRefreshGeneration = a_generation;
	shadowUpdateIdx = 0;
	fullRefreshActive = true;
	shadowMapValid = false;
}

void TerrainShadows::InvalidateShadowMap()
{
	shadowUpdateIdx = 0;
	fullRefreshActive = false;
	shadowMapValid = false;
}

TerrainShadows::ShadowUpdateResult TerrainShadows::UpdateShadow(bool a_fullRefresh)
{
	ZoneScoped;

	auto* sunLight = GetShadowUpdateSunLight();
	if (!sunLight)
		return ShadowUpdateResult::kUnavailable;

	// don't forget to change NTHREADS in shader!
	constexpr uint updateLength = 128u;
	constexpr uint logUpdateLength = std::bit_width(128u) - 1;  // integer log2, https://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c

	auto context = globals::d3d::context;

	std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
	context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());

	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Occlusion - Update Shadows");

	/* ---- UPDATE CB ---- */
	uint width = texHeightMap->desc.Width;
	uint height = texHeightMap->desc.Height;
	if (width == 0 || height == 0)
		return ShadowUpdateResult::kUnavailable;

	// only update direction at the start of each cycle
	static uint edgePxCoord;
	static int signDir;
	static uint maxUpdates;
	if (shadowUpdateIdx == 0) {
		auto direction = sunLight->GetWorldDirection();
		const auto isFiniteDirection = [](const RE::NiPoint3& a_direction) {
			return std::isfinite(a_direction.x) && std::isfinite(a_direction.y) && std::isfinite(a_direction.z);
		};
		if (!isFiniteDirection(direction))
			return ShadowUpdateResult::kUnavailable;

		const float directionLength = direction.Unitize();
		if (!std::isfinite(directionLength) || directionLength <= FLT_EPSILON || !isFiniteDirection(direction))
			return ShadowUpdateResult::kUnavailable;

		float3 dirLightDir = { direction.x, direction.y, direction.z };
		if (dirLightDir.z > 0)
			dirLightDir = -dirLightDir;

		// in UV
		float3 invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		invScale.z = cachedHeightmap->zRange.y - cachedHeightmap->zRange.x;
		if (!std::isfinite(invScale.x) || !std::isfinite(invScale.y) || !std::isfinite(invScale.z) ||
			std::abs(invScale.x) <= FLT_EPSILON || std::abs(invScale.y) <= FLT_EPSILON || std::abs(invScale.z) <= FLT_EPSILON) {
			return ShadowUpdateResult::kUnavailable;
		}

		float3 dirLightPxDir = dirLightDir / invScale;
		dirLightPxDir.x *= width;
		dirLightPxDir.y *= height;
		if (!std::isfinite(dirLightPxDir.x) || !std::isfinite(dirLightPxDir.y) ||
			(std::abs(dirLightPxDir.x) <= FLT_EPSILON && std::abs(dirLightPxDir.y) <= FLT_EPSILON)) {
			return ShadowUpdateResult::kUnavailable;
		}

		float stepMult;
		if (abs(dirLightPxDir.x) >= abs(dirLightPxDir.y)) {
			stepMult = 1.f / abs(dirLightPxDir.x);
			edgePxCoord = dirLightPxDir.x > 0 ? 0 : (width - 1);
			signDir = dirLightPxDir.x > 0 ? 1 : -1;
			maxUpdates = (width + updateLength - 1) >> logUpdateLength;
		} else {
			stepMult = 1.f / abs(dirLightPxDir.y);
			edgePxCoord = dirLightPxDir.y > 0 ? 0 : height - 1;
			signDir = dirLightPxDir.y > 0 ? 1 : -1;
			maxUpdates = (height + updateLength - 1) >> logUpdateLength;
		}
		dirLightPxDir *= stepMult;

		shadowUpdateCBData.LightPxDir = { dirLightPxDir.x, dirLightPxDir.y };

		// soft shadow angles
		float lenUV = float2{ dirLightDir.x, dirLightDir.y }.Length();
		float dirLightAngle = atan2(-dirLightDir.z, lenUV);
		float shadowSofteningRadiusAngle = RE::NI_PI / 180.f;
		float upperAngle = std::max(0.f, dirLightAngle - shadowSofteningRadiusAngle);
		float lowerAngle = std::min(RE::NI_HALF_PI - 1e-2f, dirLightAngle + shadowSofteningRadiusAngle);

		shadowUpdateCBData.LightDeltaZ = -(lenUV / invScale.z * stepMult) * float2{ std::tan(upperAngle), std::tan(lowerAngle) };
	}

	shadowUpdateCBData.StartPxCoord = edgePxCoord + signDir * shadowUpdateIdx * updateLength;
	shadowUpdateCBData.PxSize = { 1.f / texHeightMap->desc.Width, 1.f / texHeightMap->desc.Height };
	shadowUpdateCBData.BlendWeight = a_fullRefresh ? 1.0f : 0.5f;
	shadowUpdateCBData.PosRange = { cachedHeightmap->pos0.z, cachedHeightmap->pos1.z };
	shadowUpdateCBData.ZRange = cachedHeightmap->zRange;

	shadowUpdateCB->Update(shadowUpdateCBData);

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[1] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11Buffer* buffer = nullptr;
	} old, newer;

	/* ---- DISPATCH ---- */

	newer.srvs[0] = texHeightMap->srv.get();
	newer.uavs[0] = texShadowHeight->uav.get();
	newer.buffer = shadowUpdateCB->CB();

	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &newer.buffer);
	context->CSSetShader(GetShadowUpdateProgram(), nullptr, 0);
	globals::profiler->BeginPass("TerrainShadows::ShadowUpdate");
	context->Dispatch(abs(shadowUpdateCBData.LightPxDir.x) >= abs(shadowUpdateCBData.LightPxDir.y) ? height : width, 1, 1);
	globals::profiler->EndPass();

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, nullptr, 0);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &old.buffer);

	shadowUpdateIdx = (shadowUpdateIdx + 1) % maxUpdates;
	return shadowUpdateIdx == 0 ? ShadowUpdateResult::kComplete : ShadowUpdateResult::kIncomplete;
}

void TerrainShadows::ReflectionsPrepass()
{
	auto context = globals::d3d::context;
	if (!context)
		return;
	std::array<ID3D11ShaderResourceView*, 1> srvs = {
		texShadowHeight && shadowMapValid ? texShadowHeight->srv.get() : nullptr
	};
	context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
}

void TerrainShadows::EarlyPrepass()
{
	LoadHeightmap();

	const auto requestedRefreshGeneration = Util::GetCompletedCelestialTransitionGeneration();
	if (!settings.EnableTerrainShadow)
		return;

	const bool recreateShadowMap = needPrecompute;
	if (recreateShadowMap)
		Precompute();

	const bool transitionChanged = fullRefreshActive ?
	                                   requestedRefreshGeneration != activeRefreshGeneration :
	                                   requestedRefreshGeneration != handledTimeJumpRefreshGeneration;
	if (recreateShadowMap || (!shadowMapValid && !fullRefreshActive) || transitionChanged)
		BeginFullRefresh(requestedRefreshGeneration);

	if (fullRefreshActive) {
		const auto updateResult = UpdateShadow(true);
		if (updateResult == ShadowUpdateResult::kComplete) {
			const auto latestRefreshGeneration = Util::GetCompletedCelestialTransitionGeneration();
			if (latestRefreshGeneration == activeRefreshGeneration) {
				handledTimeJumpRefreshGeneration = activeRefreshGeneration;
				fullRefreshActive = false;
				shadowMapValid = true;
			} else {
				BeginFullRefresh(latestRefreshGeneration);
			}
		}
	} else {
		UpdateShadow(false);
	}

	auto context = globals::d3d::context;
	if (!context)
		return;
	std::array<ID3D11ShaderResourceView*, 1> srvs = {
		texShadowHeight && shadowMapValid ? texShadowHeight->srv.get() : nullptr
	};
	context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
}
