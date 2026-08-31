#include "Hooks.h"

#include "ShaderTools/BSShaderHooks.h"
#include "Utils/ExternalEmittance.h"

#include "Feature.h"
#include "Globals.h"
#include "GpuPass.h"
#include "Menu.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "RenderMap/D3DContextHooks.h"
#	include "RenderMap/Runtime.h"
#endif
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/D3DTextureLifetimeTracker.h"
#endif

#include "Features/AdaptiveBrightness.h"
#include "Features/DynamicCubemaps.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/ScreenshotFeature.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"

#include "ShaderTools/BSShaderHooks.h"

#include <algorithm>
#include <array>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <bit>
#	include <bcrypt.h>
#endif
#include <cstring>
#include <intrin.h>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <limits>
#endif
#include <shared_mutex>
#include <string>
#include <string_view>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <vector>
#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
namespace
{
	void AppendMaterialTextureBinding(
		CSX::RenderMap::MaterialStateObservationInput& a_material,
		CSX::RenderMap::MaterialTextureRole a_role,
		std::uint32_t a_bindingIndex,
		RE::NiSourceTexture* a_texture) noexcept
	{
		if (!a_texture)
			return;
		if (a_material.textureBindingCount >= CSX::RenderMap::kMaximumMaterialTextureBindings) {
			a_material.textureBindingsTruncated = true;
			return;
		}
		auto& binding = a_material.textureBindings[a_material.textureBindingCount++];
		binding.role = a_role;
		binding.bindingIndex = a_bindingIndex;
		binding.niSourceTexture = reinterpret_cast<std::uintptr_t>(a_texture);
		if (a_texture->name.c_str())
			binding.path = a_texture->name.c_str();
		if (a_texture->rendererTexture && a_texture->rendererTexture->texture) {
			binding.resource = CSX::RenderMap::DescribeResource(a_texture->rendererTexture->texture);
		}
	}

	void PopulateMaterialTextureBindings(
		RE::BSShaderMaterial* a_material,
		CSX::RenderMap::MaterialStateObservationInput& a_observation) noexcept
	{
		if (!a_material)
			return;
		switch (a_material->GetType()) {
		case RE::BSShaderMaterial::Type::kLighting: {
			auto* lighting = static_cast<RE::BSLightingShaderMaterialBase*>(a_material);
			std::array<RE::NiSourceTexture*, 64> textures{};
			const auto textureCount = lighting->GetTextures(textures.data());
			const auto retainedCount = std::min<std::size_t>(textureCount, textures.size());
			for (std::size_t index = 0; index < retainedCount; ++index) {
				AppendMaterialTextureBinding(
					a_observation,
					CSX::RenderMap::MaterialTextureRole::kRuntimeMaterialList,
					static_cast<std::uint32_t>(index),
					textures[index]);
			}
			if (textureCount > textures.size())
				a_observation.textureBindingsTruncated = true;
			break;
		}
		case RE::BSShaderMaterial::Type::kEffect: {
			auto* effect = static_cast<RE::BSEffectShaderMaterial*>(a_material);
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kEffectSource, 0, effect->sourceTexture.get());
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kEffectGreyscale, 1, effect->greyscaleTexture.get());
			break;
		}
		case RE::BSShaderMaterial::Type::kWater: {
			auto* water = static_cast<RE::BSWaterShaderMaterial*>(a_material);
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kWaterStaticReflection, 0, water->staticReflectionTexture.get());
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kWaterNormal1, 1, water->normalTexture1.get());
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kWaterNormal2, 2, water->normalTexture2.get());
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kWaterNormal3, 3, water->normalTexture3.get());
			AppendMaterialTextureBinding(a_observation, CSX::RenderMap::MaterialTextureRole::kWaterNormal4, 4, water->normalTexture4.get());
			break;
		}
		default:
			break;
		}
	}

	CSX::RenderMap::GeometryBoundary BuildRenderMapGeometryBoundary(
		RE::BSShader* a_shader,
		RE::BSRenderPass* a_pass,
		std::uint32_t a_renderFlags,
		RE::BSShader::Type a_shaderType) noexcept
	{
		CSX::RenderMap::GeometryBoundary boundary{
			.shader = reinterpret_cast<std::uintptr_t>(a_shader),
			.renderPass = reinterpret_cast<std::uintptr_t>(a_pass),
			.geometry = reinterpret_cast<std::uintptr_t>(a_pass ? a_pass->geometry : nullptr),
			.shaderType = static_cast<std::uint32_t>(a_shaderType),
			.passEnum = a_pass ? a_pass->passEnum : 0,
			.renderFlags = a_renderFlags,
		};
		if (!a_pass || !a_pass->geometry)
			return boundary;

		auto* geometry = a_pass->geometry;
		if (auto* reference = geometry->GetUserData()) {
			boundary.sceneObject.reference = reinterpret_cast<std::uintptr_t>(reference);
			boundary.sceneObject.referenceFormId = reference->GetFormID();
			boundary.sceneObject.referenceFormDynamic = reference->IsDynamicForm();
			if (const auto* name = reference->GetName())
				boundary.sceneObject.referenceName = name;
			if (auto* base = reference->GetBaseObject()) {
				boundary.sceneObject.baseFormId = base->GetFormID();
				boundary.sceneObject.baseFormDynamic = base->IsDynamicForm();
				if (const auto* name = base->GetName())
					boundary.sceneObject.baseFormName = name;
			}
		}

		auto& geometryObservation = boundary.geometryObservation;
		geometryObservation.geometry = reinterpret_cast<std::uintptr_t>(geometry);
		if (const auto* rtti = geometry->GetRTTI(); rtti && rtti->GetName())
			geometryObservation.runtimeTypeName = rtti->GetName();
		if (geometry->name.c_str())
			geometryObservation.name = geometry->name.c_str();
		geometryObservation.geometryType = geometry->GetType().underlying();
		const auto& runtimeData = geometry->GetGeometryRuntimeData();
		geometryObservation.vertexDescriptor = std::bit_cast<std::uint64_t>(runtimeData.vertexDesc);
		std::size_t transformIndex = 0;
		for (const auto& row : geometry->world.rotate.entry) {
			for (const auto value : row)
				geometryObservation.worldTransform[transformIndex++] = value;
		}
		geometryObservation.worldTransform[9] = geometry->world.translate.x;
		geometryObservation.worldTransform[10] = geometry->world.translate.y;
		geometryObservation.worldTransform[11] = geometry->world.translate.z;
		geometryObservation.worldTransform[12] = geometry->world.scale;
		geometryObservation.worldTransformAvailable = true;
		geometryObservation.worldBound = {
			geometry->worldBound.center.x,
			geometry->worldBound.center.y,
			geometry->worldBound.center.z,
			geometry->worldBound.radius,
		};
		geometryObservation.worldBoundAvailable = true;

		if (auto* property = runtimeData.shaderProperty.get()) {
			auto& materialState = boundary.materialState;
			materialState.shaderProperty = reinterpret_cast<std::uintptr_t>(property);
			materialState.shaderPropertyAvailable = true;
			if (const auto* rtti = property->GetRTTI(); rtti && rtti->GetName())
				materialState.shaderPropertyRuntimeTypeName = rtti->GetName();
			materialState.shaderPropertyFlags = property->flags.underlying();
			materialState.alpha = property->alpha;
			materialState.engineMaterialType = static_cast<std::uint32_t>(property->GetMaterialType());
			if (auto* material = property->GetBaseMaterial()) {
				materialState.material = reinterpret_cast<std::uintptr_t>(material);
				materialState.materialAvailable = true;
				materialState.materialType = static_cast<std::uint32_t>(material->GetType());
				materialState.feature = static_cast<std::uint32_t>(material->GetFeature());
				materialState.hashKey = material->hashKey;
				PopulateMaterialTextureBindings(material, materialState);
			}
		}
		return boundary;
	}

	struct ShaderBytecodeRecord
	{
		std::vector<std::uint8_t> bytes;
		std::uint64_t bytecodeSize{ 0 };
		std::array<char, CSX::RenderMap::kSha256HexLength + 1> sha256{};
		bool hashAvailable{ false };
	};

	std::unordered_map<void*, ShaderBytecodeRecord> g_shaderBytecodeMap;
	std::shared_mutex g_shaderBytecodeMutex;

	std::string_view BoundedShaderString(const char* a_value) noexcept
	{
		if (!a_value)
			return {};
		std::size_t length = 0;
		while (length < 1024 && a_value[length] != '\0')
			++length;
		return { a_value, length };
	}

	std::string_view EffectiveShaderCompileSourceName(const RE::BSShader* a_shader) noexcept
	{
		if (!a_shader)
			return {};
		if (a_shader->shaderType.get() == RE::BSShader::Type::ImageSpace) {
			return BoundedShaderString(
				static_cast<const RE::BSImagespaceShader*>(a_shader)->originalShaderName);
		}
		return BoundedShaderString(a_shader->fxpFilename);
	}

	bool ComputeSha256Hex(
		const void* a_data,
		size_t a_size,
		std::array<char, CSX::RenderMap::kSha256HexLength + 1>& a_result)
	{
		if (!a_data || a_size > static_cast<size_t>(std::numeric_limits<ULONG>::max()))
			return false;
		struct AlgorithmProvider
		{
			AlgorithmProvider() noexcept
			{
				if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
					handle = nullptr;
			}
			~AlgorithmProvider()
			{
				if (handle)
					BCryptCloseAlgorithmProvider(handle, 0);
			}
			BCRYPT_ALG_HANDLE handle{ nullptr };
		};
		static AlgorithmProvider provider;
		if (!provider.handle)
			return false;

		DWORD objectBytes = 0;
		DWORD copiedBytes = 0;
		if (BCryptGetProperty(
				provider.handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
				sizeof(objectBytes), &copiedBytes, 0) < 0) {
			return false;
		}
		std::vector<UCHAR> hashObject(objectBytes);
		BCRYPT_HASH_HANDLE hash = nullptr;
		if (BCryptCreateHash(
				provider.handle, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()),
				nullptr, 0, 0) < 0) {
			return false;
		}
		std::array<UCHAR, 32> digest{};
		const auto hashStatus = BCryptHashData(
			hash, reinterpret_cast<PUCHAR>(const_cast<void*>(a_data)), static_cast<ULONG>(a_size), 0);
		const auto finishStatus = hashStatus < 0 ? hashStatus :
			BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
		BCryptDestroyHash(hash);
		if (finishStatus < 0) {
			return false;
		}
		constexpr char hex[] = "0123456789abcdef";
		for (size_t index = 0; index < digest.size(); ++index) {
			a_result[index * 2] = hex[digest[index] >> 4u];
			a_result[index * 2 + 1] = hex[digest[index] & 0x0Fu];
		}
		a_result.back() = '\0';
		return true;
	}

	struct BoundStageSelection
	{
		std::uintptr_t wrapper{ 0 };
		std::uintptr_t d3dObject{ 0 };
		std::uint32_t wrapperDescriptor{ 0 };
		CSX::RenderMap::ShaderSelectionRoute route{ CSX::RenderMap::ShaderSelectionRoute::kUnknown };
	};

	struct TechniqueSelectionContext
	{
		BoundStageSelection vertex;
		BoundStageSelection pixel;
	};

	thread_local TechniqueSelectionContext* g_techniqueSelectionContext = nullptr;

	class TechniqueSelectionGuard
	{
	public:
		explicit TechniqueSelectionGuard(TechniqueSelectionContext* a_context) noexcept :
			previous(g_techniqueSelectionContext)
		{
			if (a_context) {
				active = true;
				g_techniqueSelectionContext = a_context;
			}
		}
		~TechniqueSelectionGuard()
		{
			if (active)
				g_techniqueSelectionContext = previous;
		}
		TechniqueSelectionGuard(const TechniqueSelectionGuard&) = delete;
		TechniqueSelectionGuard& operator=(const TechniqueSelectionGuard&) = delete;

	private:
		TechniqueSelectionContext* previous{ nullptr };
		bool active{ false };
	};

	template <class T>
	void CaptureStageSelection(
		T* a_wrapper,
		CSX::RenderMap::ShaderStage a_stage,
		CSX::RenderMap::ShaderSelectionRoute a_route) noexcept
	{
		if (!g_techniqueSelectionContext)
			return;
		auto& selected = a_stage == CSX::RenderMap::ShaderStage::kPixel ?
			g_techniqueSelectionContext->pixel : g_techniqueSelectionContext->vertex;
		selected = {
			.wrapper = reinterpret_cast<std::uintptr_t>(a_wrapper),
			.d3dObject = reinterpret_cast<std::uintptr_t>(a_wrapper ? a_wrapper->shader : nullptr),
			.wrapperDescriptor = a_wrapper ? a_wrapper->id : 0,
			.route = a_route,
		};
	}

	bool GetShaderBytecodeIdentity(
		void* a_shader,
		std::uint64_t& a_size,
		std::array<char, CSX::RenderMap::kSha256HexLength + 1>& a_sha256) noexcept
	{
		std::shared_lock lock(g_shaderBytecodeMutex);
		const auto found = g_shaderBytecodeMap.find(a_shader);
		if (found == g_shaderBytecodeMap.end())
			return false;
		a_size = found->second.bytecodeSize;
		a_sha256 = found->second.hashAvailable ? found->second.sha256 :
			std::array<char, CSX::RenderMap::kSha256HexLength + 1>{};
		return true;
	}
}
#else
std::unordered_map<void*, std::pair<std::unique_ptr<uint8_t[]>, size_t>> ShaderBytecodeMap;
#endif

namespace
{
	std::shared_mutex g_renderTargetRecreationMutex;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
void RegisterShaderBytecode(
	CSX::RenderMap::ShaderStage a_stage,
	void* Shader,
	const void* Bytecode,
	size_t BytecodeLength)
{
	ShaderBytecodeRecord record;
	record.bytecodeSize = BytecodeLength;
	if (globals::shaderCache && globals::shaderCache->IsDump()) {
		record.bytes.resize(BytecodeLength);
		memcpy(record.bytes.data(), Bytecode, BytecodeLength);
	}
	record.hashAvailable = ComputeSha256Hex(Bytecode, BytecodeLength, record.sha256);
	CSX::RenderMap::GetRuntime().RegisterCreatedStageShader(
		a_stage,
		reinterpret_cast<std::uintptr_t>(Shader),
		BytecodeLength,
		record.hashAvailable ? std::string_view(record.sha256.data()) : std::string_view{});
	logger::debug(fmt::runtime("Saving shader at index {:x} with {} bytes:\t{:x}"), (std::uintptr_t)Shader, BytecodeLength, (std::uintptr_t)Bytecode);
	std::unique_lock lock(g_shaderBytecodeMutex);
	g_shaderBytecodeMap.insert_or_assign(Shader, std::move(record));
}

std::vector<std::uint8_t> GetShaderBytecode(void* Shader)
{
	logger::debug(fmt::runtime("Loading shader at index {:x}"), (std::uintptr_t)Shader);
	std::shared_lock lock(g_shaderBytecodeMutex);
	return g_shaderBytecodeMap.at(Shader).bytes;
}
#else
void RegisterShaderBytecode(void* Shader, const void* Bytecode, size_t BytecodeLength)
{
	auto codeCopy = std::make_unique<uint8_t[]>(BytecodeLength);
	memcpy(codeCopy.get(), Bytecode, BytecodeLength);
	logger::debug(fmt::runtime("Saving shader at index {:x} with {} bytes:\t{:x}"), (std::uintptr_t)Shader, BytecodeLength, (std::uintptr_t)Bytecode);
	ShaderBytecodeMap.emplace(Shader, std::make_pair(std::move(codeCopy), BytecodeLength));
}

const std::pair<std::unique_ptr<uint8_t[]>, size_t>& GetShaderBytecode(void* Shader)
{
	logger::debug(fmt::runtime("Loading shader at index {:x}"), (std::uintptr_t)Shader);
	return ShaderBytecodeMap.at(Shader);
}
#endif

namespace
{
	enum class InputHookSafeguardReason : uint32_t
	{
		kSwallow = 1u << 0,
		kInvalidHead = 1u << 1,
		kProcessInputEventsException = 1u << 2,
		kGetDeviceException = 1u << 3,
		kMouseWheelReadException = 1u << 4
	};

	const char* ToString(InputHookSafeguardReason a_reason)
	{
		switch (a_reason) {
		case InputHookSafeguardReason::kSwallow:
			return "swallow";
		case InputHookSafeguardReason::kInvalidHead:
			return "invalid_head";
		case InputHookSafeguardReason::kProcessInputEventsException:
			return "process_input_events_exception";
		case InputHookSafeguardReason::kGetDeviceException:
			return "get_device_exception";
		case InputHookSafeguardReason::kMouseWheelReadException:
			return "mouse_wheel_read_exception";
		default:
			return "unknown";
		}
	}

	const char* BoolText(bool a_value)
	{
		return a_value ? "yes" : "no";
	}

	constexpr double kCSFrameIntervalSpikeThresholdMs = 12.0;
	constexpr double kCSFrameIntervalSevereThresholdMs = 18.0;
	constexpr double kCSFrameHookPhaseDiagThresholdUs = 500.0;
	constexpr double kCSFrameHookPhaseDiagSevereThresholdUs = 3000.0;
	constexpr bool kCSFrameDiagnosticsEnabled = false;

	uint64_t ReadFrameDiagCounterTicks()
	{
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		return static_cast<uint64_t>(counter.QuadPart);
	}

	double ConvertFrameDiagTicksToMilliseconds(uint64_t a_ticks)
	{
		static const uint64_t frequency = []() {
			LARGE_INTEGER counterFrequency{};
			QueryPerformanceFrequency(&counterFrequency);
			return static_cast<uint64_t>(std::max<LONGLONG>(counterFrequency.QuadPart, 1));
		}();
		return static_cast<double>(a_ticks) * 1000.0 / static_cast<double>(frequency);
	}

	double ConvertFrameDiagTicksToMicroseconds(uint64_t a_ticks)
	{
		return ConvertFrameDiagTicksToMilliseconds(a_ticks) * 1000.0;
	}

	uint64_t ConvertFrameDiagMicrosecondsToTicks(double a_microseconds)
	{
		static const uint64_t frequency = []() {
			LARGE_INTEGER counterFrequency{};
			QueryPerformanceFrequency(&counterFrequency);
			return static_cast<uint64_t>(std::max<LONGLONG>(counterFrequency.QuadPart, 1));
		}();
		return static_cast<uint64_t>(
			std::max(
				1.0,
				a_microseconds * static_cast<double>(frequency) / 1000000.0));
	}

	std::string FormatFrameDiagMenus()
	{
		auto* ui = globals::game::ui;
		if (!ui)
			return "none";

		static constexpr std::array<std::string_view, 12> kMenuNames{ {
			"Main Menu",
			"Loading Menu",
			"MapMenu",
			"Journal Menu",
			"StatsMenu",
			"InventoryMenu",
			"MagicMenu",
			"TweenMenu",
			"Dialogue Menu",
			"BarterMenu",
			"ContainerMenu",
			"Crafting Menu",
		} };

		std::string result;
		for (const auto menuName : kMenuNames) {
			if (!ui->IsMenuOpen(menuName.data()))
				continue;

			if (!result.empty())
				result += "|";
			result += menuName;
		}

		return result.empty() ? "none" : result;
	}

	enum class CSFrameHookPhase : size_t
	{
		SetDirtyStatesTotal,
		SetDirtyStatesOriginal,
		TerrainOnSetDirtyStates,
		StateDraw,
		BeginTechniqueTotal,
		BeginTechniqueTerrainOnBegin,
		BeginTechniqueModifyShaderLookup,
		BeginTechniqueOriginal,
		BeginTechniqueShaderCacheLookup,
		BeginTechniqueBindCustomShader,
		Count
	};

	struct CSFrameHookPhaseStats
	{
		uint64_t totalTicks = 0;
		uint64_t maxTicks = 0;
		uint32_t calls = 0;
	};

	struct CSFrameHookPhaseFrameStats
	{
		uint32_t frame = 0;
		std::array<CSFrameHookPhaseStats, static_cast<size_t>(CSFrameHookPhase::Count)> phases{};
		uint32_t computeCalls = 0;
		uint32_t graphicsCalls = 0;

		void Reset(uint32_t a_frame)
		{
			frame = a_frame;
			phases = {};
			computeCalls = 0;
			graphicsCalls = 0;
		}
	};

	CSFrameHookPhaseFrameStats g_csFrameHookPhaseDiag;

	bool ShouldRecordCSFramePhaseDiag()
	{
		if constexpr (!kCSFrameDiagnosticsEnabled) {
			return false;
		} else {
			auto* state = globals::state;
			return globals::game::isVR && state && state->IsDeveloperMode();
		}
	}

	CSFrameHookPhaseStats& GetCSFrameHookPhaseStats(CSFrameHookPhase a_phase)
	{
		return g_csFrameHookPhaseDiag.phases[static_cast<size_t>(a_phase)];
	}

	void RecordCSFrameHookPhase(CSFrameHookPhase a_phase, uint32_t a_frame, uint64_t a_elapsedTicks)
	{
		if (!a_elapsedTicks)
			return;

		if (g_csFrameHookPhaseDiag.frame != a_frame)
			g_csFrameHookPhaseDiag.Reset(a_frame);

		auto& stats = GetCSFrameHookPhaseStats(a_phase);
		stats.totalTicks += a_elapsedTicks;
		stats.maxTicks = std::max(stats.maxTicks, a_elapsedTicks);
		stats.calls++;
	}

	void RecordCSFrameHookCall(uint32_t a_frame, bool a_isCompute)
	{
		if (g_csFrameHookPhaseDiag.frame != a_frame)
			g_csFrameHookPhaseDiag.Reset(a_frame);

		if (a_isCompute)
			g_csFrameHookPhaseDiag.computeCalls++;
		else
			g_csFrameHookPhaseDiag.graphicsCalls++;
	}

	void FlushCSFrameHookPhaseDiag(uint32_t a_frame, double a_intervalMs)
	{
		if (!ShouldRecordCSFramePhaseDiag() || g_csFrameHookPhaseDiag.frame != a_frame)
			return;

		const auto& total = GetCSFrameHookPhaseStats(CSFrameHookPhase::SetDirtyStatesTotal);
		const auto& beginTechnique = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueTotal);
		if (!total.calls && !beginTechnique.calls) {
			g_csFrameHookPhaseDiag.Reset(0);
			return;
		}

		const uint64_t logThresholdTicks = ConvertFrameDiagMicrosecondsToTicks(kCSFrameHookPhaseDiagThresholdUs);
		const uint64_t severeThresholdTicks = ConvertFrameDiagMicrosecondsToTicks(kCSFrameHookPhaseDiagSevereThresholdUs);
		const bool frameIntervalSpike = a_intervalMs >= kCSFrameIntervalSpikeThresholdMs;
		const bool phaseSpike =
			total.totalTicks >= logThresholdTicks ||
			total.maxTicks >= logThresholdTicks ||
			beginTechnique.totalTicks >= logThresholdTicks ||
			beginTechnique.maxTicks >= logThresholdTicks;
		if (!frameIntervalSpike && !phaseSpike) {
			g_csFrameHookPhaseDiag.Reset(0);
			return;
		}

		static uint32_t loggedFrameCount = 0;
		const bool severe =
			a_intervalMs >= kCSFrameIntervalSevereThresholdMs ||
			total.totalTicks >= severeThresholdTicks ||
			total.maxTicks >= severeThresholdTicks ||
			beginTechnique.totalTicks >= severeThresholdTicks ||
			beginTechnique.maxTicks >= severeThresholdTicks;
		if (loggedFrameCount >= 256 && !severe) {
			g_csFrameHookPhaseDiag.Reset(0);
			return;
		}
		loggedFrameCount++;

		const auto& original = GetCSFrameHookPhaseStats(CSFrameHookPhase::SetDirtyStatesOriginal);
		const auto& onSetDirty = GetCSFrameHookPhaseStats(CSFrameHookPhase::TerrainOnSetDirtyStates);
		const auto& stateDraw = GetCSFrameHookPhaseStats(CSFrameHookPhase::StateDraw);
		const auto& beginTerrain = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueTerrainOnBegin);
		const auto& beginModifyLookup = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueModifyShaderLookup);
		const auto& beginOriginal = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueOriginal);
		const auto& beginShaderLookup = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueShaderCacheLookup);
		const auto& beginBind = GetCSFrameHookPhaseStats(CSFrameHookPhase::BeginTechniqueBindCustomShader);
		logger::debug(
			"[CSFramePhase][Hook] frame={} intervalMs={:.2f} menus={} setDirtyCalls={} graphicsCalls={} computeCalls={} setDirtyUs={:.2f} setDirtyMaxUs={:.2f} originalUs={:.2f} originalMaxUs={:.2f} onSetDirtyUs={:.2f} onSetDirtyMaxUs={:.2f} stateDrawUs={:.2f} stateDrawMaxUs={:.2f} beginTechniqueCalls={} beginTechniqueUs={:.2f} beginTechniqueMaxUs={:.2f} beginTerrainUs={:.2f} beginModifyLookupUs={:.2f} beginOriginalUs={:.2f} beginShaderLookupUs={:.2f} beginBindUs={:.2f}",
			a_frame,
			a_intervalMs,
			FormatFrameDiagMenus(),
			total.calls,
			g_csFrameHookPhaseDiag.graphicsCalls,
			g_csFrameHookPhaseDiag.computeCalls,
			ConvertFrameDiagTicksToMicroseconds(total.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(total.maxTicks),
			ConvertFrameDiagTicksToMicroseconds(original.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(original.maxTicks),
			ConvertFrameDiagTicksToMicroseconds(onSetDirty.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(onSetDirty.maxTicks),
			ConvertFrameDiagTicksToMicroseconds(stateDraw.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(stateDraw.maxTicks),
			beginTechnique.calls,
			ConvertFrameDiagTicksToMicroseconds(beginTechnique.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(beginTechnique.maxTicks),
			ConvertFrameDiagTicksToMicroseconds(beginTerrain.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(beginModifyLookup.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(beginOriginal.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(beginShaderLookup.totalTicks),
			ConvertFrameDiagTicksToMicroseconds(beginBind.totalTicks));

		g_csFrameHookPhaseDiag.Reset(0);
	}

	void LogFrameIntervalSpikeIfNeeded(
		[[maybe_unused]] uint64_t a_presentBeginTicks,
		[[maybe_unused]] uint64_t a_previousPresentBeginTicks,
		[[maybe_unused]] uint64_t a_beforePresentTicks,
		[[maybe_unused]] uint64_t a_afterPresentTicks,
		[[maybe_unused]] HRESULT a_presentResult)
	{
		if constexpr (!kCSFrameDiagnosticsEnabled) {
			return;
		} else {
			auto* state = globals::state;
			if (!globals::game::isVR || !state || !state->IsDeveloperMode() || a_previousPresentBeginTicks == 0)
				return;

			const double intervalMs = ConvertFrameDiagTicksToMilliseconds(a_presentBeginTicks - a_previousPresentBeginTicks);
			if (intervalMs < kCSFrameIntervalSpikeThresholdMs)
				return;

			static uint32_t loggedSpikeCount = 0;
			if (loggedSpikeCount >= 256 && intervalMs < kCSFrameIntervalSevereThresholdMs)
				return;
			++loggedSpikeCount;

			const double prePresentMs = ConvertFrameDiagTicksToMilliseconds(a_beforePresentTicks - a_presentBeginTicks);
			const double presentCallMs = ConvertFrameDiagTicksToMilliseconds(a_afterPresentTicks - a_beforePresentTicks);
			auto& upscaling = globals::features::upscaling;
			logger::debug(
				"[CSFrameSpike] frame={} intervalMs={:.2f} prePresentMs={:.2f} presentCallMs={:.2f} menus={} paused={} renderScaleLatched={} perfActive={} presentationActive={} pendingRelatch={} pendingTransition={} profilerCpuMs={:.2f} profilerGpuMs={:.2f} hr=0x{:08X}",
				state->frameCount,
				intervalMs,
				prePresentMs,
				presentCallMs,
				FormatFrameDiagMenus(),
				BoolText(globals::game::ui && globals::game::ui->GameIsPaused()),
				BoolText(upscaling.IsVRRenderScaleModeLatched()),
				BoolText(upscaling.IsPerfModeActive()),
				BoolText(upscaling.IsPresentationUpscalingActive()),
				BoolText(upscaling.pendingPerfModeRenderTargetRecreate.load(std::memory_order_acquire)),
				BoolText(upscaling.HasPendingVRUpscalingTransition()),
				globals::profiler ? globals::profiler->GetCpuTotalTimeMs() : 0.0f,
				globals::profiler ? globals::profiler->GetTotalTimeMs() : 0.0f,
				static_cast<uint32_t>(a_presentResult));
		}
	}

	HMODULE GetModuleHandleFromAddress(const void* a_address)
	{
		if (!a_address) {
			return nullptr;
		}

		HMODULE moduleHandle = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(a_address),
				&moduleHandle) ||
			!moduleHandle) {
			MEMORY_BASIC_INFORMATION memoryInfo{};
			if (VirtualQuery(a_address, &memoryInfo, sizeof(memoryInfo)) == 0 || !memoryInfo.AllocationBase) {
				return nullptr;
			}
			moduleHandle = static_cast<HMODULE>(memoryInfo.AllocationBase);
		}

		return moduleHandle;
	}

	std::string GetModuleName(HMODULE a_moduleHandle)
	{
		if (!a_moduleHandle) {
			return {};
		}

		char modulePath[MAX_PATH]{};
		const auto length = GetModuleFileNameA(a_moduleHandle, modulePath, static_cast<DWORD>(std::size(modulePath)));
		if (length == 0) {
			return {};
		}

		return std::filesystem::path(std::string_view(modulePath, length)).filename().string();
	}

	const void* TryGetObjectVtable(const void* a_object)
	{
		if (!a_object) {
			return nullptr;
		}

		__try {
			return *reinterpret_cast<const void* const*>(a_object);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	std::vector<std::string> CollectExternalInputSinkModules(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher)
	{
		std::vector<std::string> modules;
		if (!a_dispatcher) {
			return modules;
		}

		const auto selfModule = GetModuleHandleFromAddress(&CollectExternalInputSinkModules);

		std::vector<const void*> sinkVtables;
		{
			RE::BSSpinLockGuard locker(a_dispatcher->lock);
			sinkVtables.reserve(a_dispatcher->sinks.size());
			for (auto* sink : a_dispatcher->sinks) {
				if (!sink) {
					continue;
				}

				const auto vtable = TryGetObjectVtable(sink);
				if (vtable) {
					sinkVtables.push_back(vtable);
				}
			}
		}

		std::unordered_set<HMODULE> seenModules;
		seenModules.reserve(sinkVtables.size());
		for (const auto* sinkVtable : sinkVtables) {
			const auto moduleHandle = GetModuleHandleFromAddress(sinkVtable);
			if (!moduleHandle || moduleHandle == selfModule || !seenModules.emplace(moduleHandle).second) {
				continue;
			}

			auto moduleName = GetModuleName(moduleHandle);
			if (moduleName.empty()) {
				continue;
			}

			std::string moduleNameLower = moduleName;
			std::ranges::transform(moduleNameLower, moduleNameLower.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});

			if (moduleNameLower.ends_with(".exe")) {
				continue;
			}

			modules.push_back(std::move(moduleName));
		}

		std::ranges::sort(modules);
		return modules;
	}

	std::string JoinModules(const std::vector<std::string>& a_modules)
	{
		if (a_modules.empty()) {
			return "<none>";
		}

		std::string result;
		for (size_t i = 0; i < a_modules.size(); ++i) {
			if (i != 0) {
				result += ", ";
			}
			result += a_modules[i];
		}
		return result;
	}

	bool IsVRControllerInputDevice(RE::INPUT_DEVICES::INPUT_DEVICE a_device)
	{
		return a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kVivePrimary ||
		       a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kViveSecondary ||
		       a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusPrimary ||
		       a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusSecondary ||
		       a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRPrimary ||
		       a_device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRSecondary;
	}

	enum class MenuInputBlockDecision
	{
		kAllow,
		kBlock,
		kInvalidHead,
		kGetDeviceException
	};

	const RE::InputEvent* TryGetInputEventHead(RE::InputEvent* const* a_events)
	{
		if (!a_events) {
			return nullptr;
		}

		__try {
			return *a_events;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	MenuInputBlockDecision GetMenuInputBlockDecision(RE::InputEvent* const* a_events, bool a_blockAllDevices)
	{
		if (a_blockAllDevices) {
			return MenuInputBlockDecision::kBlock;
		}

		if (!a_events) {
			return MenuInputBlockDecision::kBlock;
		}

		RE::InputEvent* event = nullptr;
		__try {
			event = *a_events;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return MenuInputBlockDecision::kInvalidHead;
		}

		if (!event) {
			return MenuInputBlockDecision::kBlock;
		}

		while (event) {
			RE::INPUT_DEVICES::INPUT_DEVICE device{};
			RE::InputEvent* next = nullptr;
			__try {
				device = event->GetDevice();
				next = event->next;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return MenuInputBlockDecision::kGetDeviceException;
			}

			if (globals::game::isVR) {
				if (device == RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad) {
					event = next;
				} else if (IsVRControllerInputDevice(device) && !globals::features::vr.IsOpenVRCompatible()) {
					event = next;
				} else {
					return MenuInputBlockDecision::kBlock;
				}
			} else if (device != RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad) {
				return MenuInputBlockDecision::kBlock;
			} else {
				event = next;
			}
		}

		return MenuInputBlockDecision::kAllow;
	}

	void LogInputHookSafeguardOnce(
		InputHookSafeguardReason a_reason,
		RE::BSTEventSource<RE::InputEvent*>* a_dispatcher,
		const RE::InputEvent* a_originalHead,
		bool a_substitutedEmptyList)
	{
		static std::atomic<uint32_t> loggedReasons = 0;
		const auto reasonBit = static_cast<uint32_t>(a_reason);
		if ((loggedReasons.load(std::memory_order_relaxed) & reasonBit) != 0) {
			return;
		}
		if ((loggedReasons.fetch_or(reasonBit, std::memory_order_relaxed) & reasonBit) != 0) {
			return;
		}

		const auto externalModules = CollectExternalInputSinkModules(a_dispatcher);
		logger::warn("[InputHook] safeguard engaged: reason={} original_head=0x{:X} substituted_empty_list={} external_sinks=[{}]",
			ToString(a_reason),
			reinterpret_cast<std::uintptr_t>(a_originalHead),
			a_substitutedEmptyList ? "yes" : "no",
			JoinModules(externalModules));
	}
}

template <class ShaderType>
void DumpShader(
	const REX::BSShader* thisClass,
	const ShaderType* shader,
#ifdef DEVBENCH_BRIDGE_ENABLED
	const std::vector<std::uint8_t>& bytecode)
#else
	const std::pair<std::unique_ptr<uint8_t[]>, size_t>& bytecode)
#endif
{
	static_assert(std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> || std::is_same_v<ShaderType, RE::BSGraphics::PixelShader>);

#ifdef DEVBENCH_BRIDGE_ENABLED
	uint8_t* dxbcData = new uint8_t[bytecode.size()];
	size_t dxbcLen = bytecode.size();
	memcpy(dxbcData, bytecode.data(), bytecode.size());
#else
	uint8_t* dxbcData = new uint8_t[bytecode.second];
	size_t dxbcLen = bytecode.second;
	memcpy(dxbcData, bytecode.first.get(), bytecode.second);
#endif

	constexpr auto shaderExtStr = std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> ? "vs" : "ps";
	constexpr auto shaderTypeStr = std::is_same_v<ShaderType, RE::BSGraphics::VertexShader> ? "vertex" : "pixel";

	std::string dumpDir = std::format("Data\\ShaderDump\\{}\\{:X}.{}.bin", thisClass->m_LoaderType, shader->id, shaderExtStr);
	auto directoryPath = std::format("Data\\ShaderDump\\{}", thisClass->m_LoaderType);
	logger::debug(fmt::runtime("Dumping {} shader {} with id {:x} at {}"), shaderTypeStr, thisClass->m_LoaderType, shader->id, dumpDir);

	if (!std::filesystem::is_directory(directoryPath)) {
		try {
			std::filesystem::create_directories(directoryPath);
		} catch (std::filesystem::filesystem_error const& ex) {
			logger::error("Failed to create folder: {}", ex.what());
		}
	}

	if (FILE* file; fopen_s(&file, dumpDir.c_str(), "wb") == 0) {
		fwrite(dxbcData, 1, dxbcLen, file);
		fclose(file);
	}

	delete[] dxbcData;
}

struct BSShader_LoadShaders
{
	static void thunk(RE::BSShader* shader, std::uintptr_t stream)
	{
		func(shader, stream);

		auto state = globals::state;
		auto shaderCache = globals::shaderCache;
#ifdef DEVBENCH_BRIDGE_ENABLED
		auto& renderMap = CSX::RenderMap::GetRuntime();
		const auto* shaderToolsView = reinterpret_cast<const REX::BSShader*>(shader);
		const std::string_view loaderType = shaderToolsView->m_LoaderType ? shaderToolsView->m_LoaderType : "";
		const auto compileSourceName = EffectiveShaderCompileSourceName(shader);
#endif

		if (shaderCache->IsDiskCache() || shaderCache->IsDump()) {
			if (shaderCache->IsDiskCache()) {
				Feature::ForEachLoadedFeature("GenerateShaderPermutations", [shader](Feature* feature) {
					feature->GenerateShaderPermutations(shader);
				});
			}

			for (const auto& entry : shader->vertexShaders) {
				if (entry->shader && shaderCache->IsDump()) {
					const auto& bytecode = GetShaderBytecode(entry->shader);
					DumpShader((REX::BSShader*)shader, entry, bytecode);
				}
				auto vertexShaderDesriptor = entry->id;
				auto pixelShaderDescriptor = entry->id;
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				shaderCache->GetVertexShader(*shader, vertexShaderDesriptor);
			}
			for (const auto& entry : shader->pixelShaders) {
				if (entry->shader && shaderCache->IsDump()) {
					const auto& bytecode = GetShaderBytecode(entry->shader);
					DumpShader((REX::BSShader*)shader, entry, bytecode);
				}
				auto vertexShaderDesriptor = entry->id;
				auto pixelShaderDescriptor = entry->id;
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
				state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor, true);
				shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
			}
		}
		BSShaderHooks::hk_LoadShaders((REX::BSShader*)shader, stream);

#ifdef DEVBENCH_BRIDGE_ENABLED
		// Record the entries only after every cache and loose-shader replacement
		// has completed. Registering immediately after the engine load associates
		// aliases with the displaced vanilla D3D objects instead of the objects
		// that subsequent draws actually bind.
		for (const auto& entry : shader->vertexShaders) {
			if (entry->shader) {
				renderMap.RegisterEngineStageShader(
					CSX::RenderMap::ShaderStage::kVertex,
					reinterpret_cast<std::uintptr_t>(entry->shader), loaderType, entry->id,
					compileSourceName);
			}
		}
		for (const auto& entry : shader->pixelShaders) {
			if (entry->shader) {
				renderMap.RegisterEngineStageShader(
					CSX::RenderMap::ShaderStage::kPixel,
					reinterpret_cast<std::uintptr_t>(entry->shader), loaderType, entry->id,
					compileSourceName);
			}
		}
#endif
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

bool Hooks::BSShader_BeginTechnique::thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader)
{
	auto state = globals::state;
	auto shaderCache = globals::shaderCache;
	const auto callerRva = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - REL::Module::get().base());
#ifdef DEVBENCH_BRIDGE_ENABLED
	auto& renderMap = CSX::RenderMap::GetRuntime();
	[[maybe_unused]] CSX::RenderMap::Collector::ScopeGuard renderMapScope;
	std::string_view renderMapLoaderType;
	std::string_view renderMapCompileSourceName;
	if (renderMap.IsCapturing()) {
		if (state)
			renderMap.SetCpuFrame(state->frameCount);
		std::string_view imageSpaceName;
		if (shader->shaderType.get() == RE::BSShader::Type::ImageSpace) {
			const auto* imageSpaceShader = static_cast<const RE::BSImagespaceShader*>(shader);
			imageSpaceName = BoundedShaderString(imageSpaceShader->name);
		}
		renderMapLoaderType = BoundedShaderString(shader->fxpFilename);
		renderMapCompileSourceName = EffectiveShaderCompileSourceName(shader);
		renderMapScope = renderMap.EnterTechnique({
			.shader = reinterpret_cast<std::uintptr_t>(shader),
			.shaderType = static_cast<std::uint32_t>(shader->shaderType.get()),
			.vertexDescriptor = vertexDescriptor,
			.pixelDescriptor = pixelDescriptor,
			.callerRva = callerRva,
			.skipPixelShader = skipPixelShader,
			.fxpFilename = renderMapLoaderType,
			.imageSpaceName = imageSpaceName,
			.compileSourceName = renderMapCompileSourceName,
		});
	}
#endif
	const bool phaseDiagActive = ShouldRecordCSFramePhaseDiag();
	const uint32_t phaseDiagFrame = phaseDiagActive && state ? state->frameCount : 0;
	const uint64_t totalStartTicks = phaseDiagActive ? ReadFrameDiagCounterTicks() : 0;
	uint64_t phaseStartTicks = totalStartTicks;

	state->updateShader = true;
	state->currentShader = shader;

	state->currentVertexDescriptor = vertexDescriptor;
	state->currentPixelDescriptor = pixelDescriptor;

	globals::features::terrainBlending.OnBeginTechnique(shader, pixelDescriptor, callerRva);
	if (phaseDiagActive) {
		const uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
		RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueTerrainOnBegin, phaseDiagFrame, phaseEndTicks - phaseStartTicks);
		phaseStartTicks = phaseEndTicks;
	}

	state->permutationData.VertexShaderDescriptor = vertexDescriptor;
	state->permutationData.PixelShaderDescriptor = pixelDescriptor;

	state->modifiedVertexDescriptor = vertexDescriptor;
	state->modifiedPixelDescriptor = pixelDescriptor;

	state->ModifyShaderLookup(*shader, state->modifiedVertexDescriptor, state->modifiedPixelDescriptor);
	if (phaseDiagActive) {
		const uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
		RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueModifyShaderLookup, phaseDiagFrame, phaseEndTicks - phaseStartTicks);
		phaseStartTicks = phaseEndTicks;
	}

	// Only check against non-shader bits
	state->permutationData.PixelShaderDescriptor &= ~state->modifiedPixelDescriptor;

#ifdef DEVBENCH_BRIDGE_ENABLED
	TechniqueSelectionContext selectedStages;
	// The selection context feeds persistent stage identity as well as the
	// optional technique-resolved event. Keep it active for every render-map
	// capture, even when technique events themselves were filtered out.
	TechniqueSelectionGuard selectedStagesGuard(renderMap.IsCapturing() ? std::addressof(selectedStages) : nullptr);
#endif
	bool shaderFound = func(shader, vertexDescriptor, pixelDescriptor, skipPixelShader);
	if (phaseDiagActive) {
		const uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
		RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueOriginal, phaseDiagFrame, phaseEndTicks - phaseStartTicks);
		phaseStartTicks = phaseEndTicks;
	}

	if (!shaderFound && shader->shaderType.get() != RE::BSShader::Type::Effect) {
		RE::BSGraphics::VertexShader* vertexShader = shaderCache->GetVertexShader(*shader, state->modifiedVertexDescriptor);
		RE::BSGraphics::PixelShader* pixelShader = shaderCache->GetPixelShader(*shader, state->modifiedPixelDescriptor);
		if (phaseDiagActive) {
			const uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
			RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueShaderCacheLookup, phaseDiagFrame, phaseEndTicks - phaseStartTicks);
			phaseStartTicks = phaseEndTicks;
		}
		if (vertexShader == nullptr || (!skipPixelShader && pixelShader == nullptr)) {
			shaderFound = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			CaptureStageSelection<RE::BSGraphics::VertexShader>(
				nullptr, CSX::RenderMap::ShaderStage::kVertex, CSX::RenderMap::ShaderSelectionRoute::kMissing);
			CaptureStageSelection<RE::BSGraphics::PixelShader>(
				nullptr, CSX::RenderMap::ShaderStage::kPixel,
				skipPixelShader ? CSX::RenderMap::ShaderSelectionRoute::kSkipped :
					CSX::RenderMap::ShaderSelectionRoute::kMissing);
#endif
		} else {
			state->settingCustomShader = true;
			globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader), NULL, NULL);
			*globals::game::currentVertexShader = vertexShader;
			globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
			if (skipPixelShader) {
				pixelShader = nullptr;
			}
			*globals::game::currentPixelShader = pixelShader;
			if (pixelShader)
				globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader), NULL, NULL);
			state->settingCustomShader = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			CaptureStageSelection(vertexShader, CSX::RenderMap::ShaderStage::kVertex,
				CSX::RenderMap::ShaderSelectionRoute::kCSXFallback);
			CaptureStageSelection(pixelShader, CSX::RenderMap::ShaderStage::kPixel,
				skipPixelShader ? CSX::RenderMap::ShaderSelectionRoute::kSkipped :
					CSX::RenderMap::ShaderSelectionRoute::kCSXFallback);
#endif
			shaderFound = true;
		}
		if (phaseDiagActive) {
			const uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
			RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueBindCustomShader, phaseDiagFrame, phaseEndTicks - phaseStartTicks);
			phaseStartTicks = phaseEndTicks;
		}
	}

	state->lastModifiedVertexDescriptor = state->modifiedVertexDescriptor;
	state->lastModifiedPixelDescriptor = state->modifiedPixelDescriptor;

#ifdef DEVBENCH_BRIDGE_ENABLED
	// Technique scope events are optional capture output. Selected-stage
	// identity is state required by draw observations, so it must still be
	// resolved when technique events themselves were filtered out.
	if (renderMap.IsCapturing()) {
		if (!shaderFound) {
			selectedStages.vertex = { .route = CSX::RenderMap::ShaderSelectionRoute::kMissing };
			selectedStages.pixel = {
				.route = skipPixelShader ? CSX::RenderMap::ShaderSelectionRoute::kSkipped :
					CSX::RenderMap::ShaderSelectionRoute::kMissing,
			};
		} else if (skipPixelShader) {
			selectedStages.pixel = { .route = CSX::RenderMap::ShaderSelectionRoute::kSkipped };
		}

		// CSX cache selections are distinct D3D objects from the entries owned by
		// Skyrim's engine shader table. The resolved descriptors at this point are
		// the authoritative semantic identity for both routes, so register the
		// selected objects before their stage observations are materialized.
		if (selectedStages.vertex.d3dObject != 0) {
			renderMap.RegisterEngineStageShader(
				CSX::RenderMap::ShaderStage::kVertex,
				selectedStages.vertex.d3dObject,
				renderMapLoaderType,
				state->modifiedVertexDescriptor,
				renderMapCompileSourceName);
		}
		if (selectedStages.pixel.d3dObject != 0) {
			renderMap.RegisterEngineStageShader(
				CSX::RenderMap::ShaderStage::kPixel,
				selectedStages.pixel.d3dObject,
				renderMapLoaderType,
				state->modifiedPixelDescriptor,
				renderMapCompileSourceName);
		}

		std::array<char, CSX::RenderMap::kSha256HexLength + 1> vertexSha256{};
		std::array<char, CSX::RenderMap::kSha256HexLength + 1> pixelSha256{};
		std::uint64_t vertexBytecodeSize = 0;
		std::uint64_t pixelBytecodeSize = 0;
		GetShaderBytecodeIdentity(
			reinterpret_cast<void*>(selectedStages.vertex.d3dObject), vertexBytecodeSize, vertexSha256);
		GetShaderBytecodeIdentity(
			reinterpret_cast<void*>(selectedStages.pixel.d3dObject), pixelBytecodeSize, pixelSha256);
		renderMap.RecordTechniqueResolution({
			.inputVertexDescriptor = vertexDescriptor,
			.inputPixelDescriptor = pixelDescriptor,
			.resolvedVertexDescriptor = state->modifiedVertexDescriptor,
			.resolvedPixelDescriptor = state->modifiedPixelDescriptor,
			.shaderFound = shaderFound,
			.skipPixelShader = skipPixelShader,
			.vertex = {
				.route = selectedStages.vertex.route,
				.shader = {
					.stage = CSX::RenderMap::ShaderStage::kVertex,
					.wrapper = selectedStages.vertex.wrapper,
					.d3dObject = selectedStages.vertex.d3dObject,
					.wrapperDescriptor = selectedStages.vertex.wrapperDescriptor,
					.bytecodeSize = vertexBytecodeSize,
					.bytecodeSha256 = vertexSha256.data(),
					.cachePath = {},
				},
			},
			.pixel = {
				.route = selectedStages.pixel.route,
				.shader = {
					.stage = CSX::RenderMap::ShaderStage::kPixel,
					.wrapper = selectedStages.pixel.wrapper,
					.d3dObject = selectedStages.pixel.d3dObject,
					.wrapperDescriptor = selectedStages.pixel.wrapperDescriptor,
					.bytecodeSize = pixelBytecodeSize,
					.bytecodeSha256 = pixelSha256.data(),
					.cachePath = {},
				},
			},
		});
	}
#endif

	if (phaseDiagActive) {
		const uint64_t totalEndTicks = ReadFrameDiagCounterTicks();
		RecordCSFrameHookPhase(CSFrameHookPhase::BeginTechniqueTotal, phaseDiagFrame, totalEndTicks - totalStartTicks);
	}

	return shaderFound;
}

namespace EffectExtensions
{
	struct BSEffectShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			auto& renderMap = CSX::RenderMap::GetRuntime();
			[[maybe_unused]] CSX::RenderMap::Collector::ScopeGuard renderMapScope;
			if (renderMap.IsCapturing()) {
				if (globals::state)
					renderMap.SetCpuFrame(globals::state->frameCount);
				renderMapScope = renderMap.EnterGeometry(
					BuildRenderMapGeometryBoundary(shader, pass, renderFlags, RE::BSShader::Type::Effect));
			}
#endif
			func(shader, pass, renderFlags);

			auto state = globals::state;
			ExternalEmittance::UpdatePermutation(pass);

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::EffectShadows);

			if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(pass->geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale)) {
					state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::EffectShadows);
				}
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace LightingExtensions
{
	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			globals::state->UpdateLightingShaderPermutation(pass);

#ifdef DEVBENCH_BRIDGE_ENABLED
			auto& renderMap = CSX::RenderMap::GetRuntime();
			[[maybe_unused]] CSX::RenderMap::Collector::ScopeGuard renderMapScope;
			if (renderMap.IsCapturing()) {
				if (globals::state)
					renderMap.SetCpuFrame(globals::state->frameCount);
				renderMapScope = renderMap.EnterGeometry(
					BuildRenderMapGeometryBoundary(shader, pass, renderFlags, RE::BSShader::Type::Lighting));
			}
#endif
			func(shader, pass, renderFlags);

			auto state = globals::state;

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsTree);

			if (auto userData = pass->geometry->GetUserData())
				if (auto baseObject = userData->GetBaseObject())
					if (baseObject->As<RE::TESObjectTREE>())
						state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsTree);

		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace GrassExtensions
{
	struct BSGrassShaderProperty_ctor
	{
		static RE::BSLightingShaderProperty* thunk(RE::BSLightingShaderProperty* property)
		{
			const uint64_t stackPointer = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
			const uint64_t lightingPropertyAddress = stackPointer + (REL::Module::IsAE() ? 0x68 : 0x70);
			auto* lightingProperty = *reinterpret_cast<RE::BSLightingShaderProperty**>(lightingPropertyAddress);

			RE::BSLightingShaderProperty* grassProperty = func(property);

			if (lightingProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting)) {
				grassProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kEffectLighting, true);
			}

			return grassProperty;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			auto& renderMap = CSX::RenderMap::GetRuntime();
			[[maybe_unused]] CSX::RenderMap::Collector::ScopeGuard renderMapScope;
			if (renderMap.IsCapturing()) {
				if (globals::state)
					renderMap.SetCpuFrame(globals::state->frameCount);
				renderMapScope = renderMap.EnterGeometry(
					BuildRenderMapGeometryBoundary(shader, pass, renderFlags, RE::BSShader::Type::Grass));
			}
#endif
			func(shader, pass, renderFlags);

			auto state = globals::state;

			state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::GrassSphereNormal);

			if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(pass->geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting)) {
					state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::GrassSphereNormal);
				}
			}

		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace WaterBlendHistory
{
	struct BSImagespaceShader_Render
	{
		static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
		{
			if (const auto shadowState = globals::game::shadowState; shadowState && globals::game::renderer && globals::d3d::context) {
				GET_INSTANCE_MEMBER(renderTargets, shadowState)

				const auto target = renderTargets[1];
				if (target != RE::RENDER_TARGET::kNONE) {
					const auto rtv = globals::game::renderer->GetRuntimeData().renderTargets[target].RTV;
					if (rtv) {
						// Clear stale coverage left by discarded non-water pixels.
						constexpr float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
						globals::d3d::context->ClearRenderTargetView(rtv, clearColor);
					}
				}
			}

			func(imageSpaceShader, shape, param);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

struct IDXGISwapChain_Present
{
	static HRESULT WINAPI thunk(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
	{
		auto state = globals::state;
		const bool armStartupMenuBlurSource =
			!state->startupMenuBlurSourceReady &&
			state->startupMenuInitializationComplete.load(std::memory_order_acquire);

		const bool frameDiagActive = ShouldRecordCSFramePhaseDiag();
		const uint64_t presentBeginTicks = frameDiagActive ? ReadFrameDiagCounterTicks() : 0;
		static uint64_t previousPresentBeginTicks = 0;
		const uint64_t previousTicks = frameDiagActive ? previousPresentBeginTicks : 0;
		if (frameDiagActive)
			previousPresentBeginTicks = presentBeginTicks;

		auto menu = globals::menu;
		if (frameDiagActive) {
			const uint32_t completedFrame = state ? state->frameCount : 0;
			const double intervalMs = previousTicks != 0 ? ConvertFrameDiagTicksToMilliseconds(presentBeginTicks - previousTicks) : 0.0;
			FlushCSFrameHookPhaseDiag(completedFrame, intervalMs);
		}
		globals::features::upscaling.PresentVRMenuDesktopMirror(This);
		state->Reset();
		menu->DrawOverlay();
		globals::features::screenshotFeature.OnBeforePresent(This);
		globals::features::screenshotFeature.DrawPostCaptureIndicator();

		const uint64_t beforePresentTicks = frameDiagActive ? ReadFrameDiagCounterTicks() : 0;
		HRESULT retval = func(This, SyncInterval, Flags);
		const uint64_t afterPresentTicks = frameDiagActive ? ReadFrameDiagCounterTicks() : 0;
		if (SUCCEEDED(retval) && armStartupMenuBlurSource)
			state->startupMenuBlurSourceReady = true;

		TracyD3D11Collect(state->tracyCtx);
		if (frameDiagActive)
			LogFrameIntervalSpikeIfNeeded(presentBeginTicks, previousTicks, beforePresentTicks, afterPresentTicks, retval);

		return retval;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

decltype(&CreateDXGIFactory) ptrCreateDXGIFactory;

HRESULT WINAPI hk_CreateDXGIFactory(REFIID, void** ppFactory)
{
	return ptrCreateDXGIFactory(__uuidof(IDXGIFactory4), ppFactory);
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	auto ret = ptrD3D11CreateDeviceAndSwapChain(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	return ret;
}

void Hooks::BSGraphics_SetDirtyStates::thunk(bool isCompute)
{
	const auto callerRva = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - REL::Module::get().base());
	if (!ShouldRecordCSFramePhaseDiag()) {
		func(isCompute);
		globals::features::terrainBlending.OnSetDirtyStates(isCompute, callerRva);
		globals::state->Draw();
		return;
	}

	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	RecordCSFrameHookCall(frame, isCompute);

	const uint64_t totalStartTicks = ReadFrameDiagCounterTicks();
	uint64_t phaseStartTicks = totalStartTicks;
	func(isCompute);
	uint64_t phaseEndTicks = ReadFrameDiagCounterTicks();
	RecordCSFrameHookPhase(CSFrameHookPhase::SetDirtyStatesOriginal, frame, phaseEndTicks - phaseStartTicks);

	phaseStartTicks = phaseEndTicks;
	globals::features::terrainBlending.OnSetDirtyStates(isCompute, callerRva);
	phaseEndTicks = ReadFrameDiagCounterTicks();
	RecordCSFrameHookPhase(CSFrameHookPhase::TerrainOnSetDirtyStates, frame, phaseEndTicks - phaseStartTicks);

	phaseStartTicks = phaseEndTicks;
	globals::state->Draw();
	phaseEndTicks = ReadFrameDiagCounterTicks();
	RecordCSFrameHookPhase(CSFrameHookPhase::StateDraw, frame, phaseEndTicks - phaseStartTicks);
	RecordCSFrameHookPhase(CSFrameHookPhase::SetDirtyStatesTotal, frame, phaseEndTicks - totalStartTicks);
}

struct ID3D11Device_CreateVertexShader
{
	static HRESULT thunk(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader)
	{
		HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);

		if (SUCCEEDED(hr) && ppVertexShader && *ppVertexShader) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			RegisterShaderBytecode(
				CSX::RenderMap::ShaderStage::kVertex, *ppVertexShader, pShaderBytecode, BytecodeLength);
#else
			RegisterShaderBytecode(*ppVertexShader, pShaderBytecode, BytecodeLength);
#endif
		}

		return hr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

#ifdef DEVBENCH_BRIDGE_ENABLED
struct ID3D11Device_CreateTexture2D
{
	static HRESULT STDMETHODCALLTYPE thunk(
		ID3D11Device* a_device,
		const D3D11_TEXTURE2D_DESC* a_desc,
		const D3D11_SUBRESOURCE_DATA* a_initialData,
		ID3D11Texture2D** a_texture)
	{
		const HRESULT result = func(a_device, a_desc, a_initialData, a_texture);
		if (SUCCEEDED(result) && a_desc && a_texture && *a_texture) {
			Diagnostics::D3DTextureLifetimeTracker::OnTextureCreated(
				*a_texture,
				*a_desc,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
		}
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
#endif

struct ID3D11Device_CreatePixelShader
{
	static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader)
	{
		HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);

		if (SUCCEEDED(hr) && ppPixelShader && *ppPixelShader) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			RegisterShaderBytecode(
				CSX::RenderMap::ShaderStage::kPixel, *ppPixelShader, pShaderBytecode, BytecodeLength);
#else
			RegisterShaderBytecode(*ppPixelShader, pShaderBytecode, BytecodeLength);
#endif
		}

		return hr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

#ifdef DEVBENCH_BRIDGE_ENABLED
struct ID3D11Device_CreateComputeShader
{
	static HRESULT STDMETHODCALLTYPE thunk(
		ID3D11Device* This,
		const void* pShaderBytecode,
		SIZE_T BytecodeLength,
		ID3D11ClassLinkage* pClassLinkage,
		ID3D11ComputeShader** ppComputeShader)
	{
		const HRESULT hr = func(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
		if (SUCCEEDED(hr) && ppComputeShader && *ppComputeShader)
			RegisterShaderBytecode(
				CSX::RenderMap::ShaderStage::kCompute, *ppComputeShader, pShaderBytecode, BytecodeLength);
		return hr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
#endif

struct ID3D11Device_CreateSamplerState
{
	static HRESULT STDMETHODCALLTYPE thunk(ID3D11Device* This, D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState)
	{
		// Limit Anisotropy to 8x for performance
		D3D11_SAMPLER_DESC descCopy = *pSamplerDesc;  // make a copy, pSamplerDesc is supposed to be immutable
		descCopy.MaxAnisotropy = std::min(descCopy.MaxAnisotropy, 8u);
		return func(This, &descCopy, ppSamplerState);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSShaderRenderTargets_Create
{
	/**
	 * @brief Calls the original render target creation function and reinitializes global rendering state.
	 *
	 * Invokes the original function, then reinitializes global state and performs necessary setup for rendering targets.
	 */
	static void thunk()
	{
		RecreateAndSetupFull();
	}

	static bool CanSetupRenderingResources()
	{
		return globals::game::renderer &&
		       globals::state &&
		       globals::deferred &&
		       globals::d3d::device &&
		       globals::d3d::context;
	}

	static bool RecreateAndSetupFull()
	{
		{
			const std::unique_lock recreateLock(
				g_renderTargetRecreationMutex);
			if (globals::state)
				globals::state->InvalidateRenderTargetResourcePublication();
			func();
		}
		globals::ReInit();
		if (!CanSetupRenderingResources())
			return false;
		globals::state->Setup();
		return true;
	}

	static bool RecreateAndSetupRenderTargetResources(
		Hooks::VRRenderTargetRecreatePreparation a_beforeEngineCreate,
		Hooks::VRRenderTargetRecreateMutationEntered a_onEngineCreateEntered,
		Hooks::VRRenderTargetRecreateCheckpoint a_afterEngineCreate,
		void* a_context,
		bool* a_engineCreateEntered)
	{
		{
			const std::unique_lock recreateLock(
				g_renderTargetRecreationMutex);
			try {
				if (a_beforeEngineCreate)
					a_beforeEngineCreate(a_context);
				if (globals::state)
					globals::state->InvalidateRenderTargetResourcePublication();
				if (a_onEngineCreateEntered)
					a_onEngineCreateEntered(a_context);
				if (a_engineCreateEntered)
					*a_engineCreateEntered = true;
				func();
			} catch (...) {
				// Offered resources must be reclaimed while recreation still owns
				// the unique table lock. Reachability is untrusted on this path.
				if (a_afterEngineCreate)
					(void)a_afterEngineCreate(a_context, true);
				throw;
			}
			if (a_afterEngineCreate &&
				!a_afterEngineCreate(a_context, false)) {
				return false;
			}
		}
		globals::ReInit();
		if (!CanSetupRenderingResources())
			return false;
		globals::state->SetupRenderTargetResources();
		return true;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSInputDeviceManager_PollInputDevices
{
	static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
	{
		// Run Reflex frame pacing as early as possible in the frame loop.
		globals::features::upscaling.streamline.UpdateReflex();

		auto menu = globals::menu;
		const bool shouldSwallowInput = menu->ShouldSwallowInput();
		const bool blockAllDevices = menu->ShouldBlockAllGameInput();

		if (a_events) {
			__try {
				if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton()) {
					if (const auto* mouse = inputManager->GetMouse())
						menu->RecordDirectInputWheelDelta(mouse->GetRuntimeData().dInputNextState.z);
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				LogInputHookSafeguardOnce(InputHookSafeguardReason::kMouseWheelReadException, a_dispatcher, TryGetInputEventHead(a_events), false);
			}
			__try {
				menu->ProcessInputEvents(a_events);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				LogInputHookSafeguardOnce(InputHookSafeguardReason::kProcessInputEventsException, a_dispatcher, TryGetInputEventHead(a_events), false);
			}
		}

		// Block all devices while first-time setup is modal. For the normal menu,
		// inspect the whole event list so a leading gamepad/controller event cannot
		// let keyboard or mouse input pass through to Skyrim.
		const auto blockDecision = GetMenuInputBlockDecision(a_events, blockAllDevices);
		if (blockDecision == MenuInputBlockDecision::kInvalidHead) {
			LogInputHookSafeguardOnce(InputHookSafeguardReason::kInvalidHead, a_dispatcher, nullptr, false);
		} else if (blockDecision == MenuInputBlockDecision::kGetDeviceException) {
			LogInputHookSafeguardOnce(InputHookSafeguardReason::kGetDeviceException, a_dispatcher, TryGetInputEventHead(a_events), false);
		}
		const bool blockedDevice = blockDecision != MenuInputBlockDecision::kAllow;

		if (blockedDevice && shouldSwallowInput) {  //the menu is open, eat all keypresses
			// During active flying preview, let input reach the game for movement/camera.
			if (menu->IsPreviewFlying()) {
				func(a_dispatcher, a_events);
				return;
			}
			LogInputHookSafeguardOnce(InputHookSafeguardReason::kSwallow, a_dispatcher, TryGetInputEventHead(a_events), true);
			constexpr RE::InputEvent* const dummy[] = { nullptr };
			func(a_dispatcher, dummy);
			return;
		}

		func(a_dispatcher, a_events);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

namespace Hooks
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	class VRFaceGenTintAssignmentBridge : public Xbyak::CodeGenerator
	{
	public:
		VRFaceGenTintAssignmentBridge(std::uintptr_t a_callback, std::uintptr_t a_originalAssignment)
		{
			// At the native call site RCX is the material's tintTexture slot and
			// RDX is the generated NiSourceTexture. The containing FaceGen function
			// keeps the node, head part, and NPC in RSI, R13, and RDI respectively.
			// Preserve the original two arguments across the diagnostic callback,
			// then tail-call Skyrim's unmodified smart-pointer assignment helper.
			sub(rsp, 0x38);
			mov(qword[rsp + 0x28], rcx);
			mov(qword[rsp + 0x30], rdx);
			mov(r8, rsi);
			mov(r9, r13);
			mov(qword[rsp + 0x20], rdi);
			mov(rax, a_callback);
			call(rax);
			mov(rcx, qword[rsp + 0x28]);
			mov(rdx, qword[rsp + 0x30]);
			add(rsp, 0x38);
			mov(rax, a_originalAssignment);
			jmp(rax);
		}
	};

	void InstallVRFaceGenTintAssignmentDiagnostic()
	{
		if (!REL::Module::IsVR())
			return;
		if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
			logger::error(
				"[Texture lifetime] FaceGen owner hook not installed: unsupported Skyrim VR runtime {}",
				REL::Module::get().version().string());
			return;
		}

		constexpr std::uintptr_t kTintAssignmentCallOffset = 0x3CE;
		const auto prepareHeadPart = REL::RelocationID(26259, 26838).address();
		const auto callsite = prepareHeadPart + kTintAssignmentCallOffset;
		if (*reinterpret_cast<const std::uint8_t*>(callsite) != 0xE8) {
			logger::error(
				"[Texture lifetime] FaceGen owner hook not installed: expected CALL at SkyrimVR+0x{:x}",
				callsite - REL::Module::get().base());
			return;
		}

		std::int32_t displacement{};
		std::memcpy(&displacement, reinterpret_cast<const void*>(callsite + 1), sizeof(displacement));
		const auto originalAssignment = callsite + 5 + displacement;
		const auto expectedAssignment = REL::Offset(0x3E3320).address();
		if (originalAssignment != expectedAssignment) {
			logger::error(
				"[Texture lifetime] FaceGen owner hook not installed: unexpected target SkyrimVR+0x{:x}",
				originalAssignment - REL::Module::get().base());
			return;
		}

		VRFaceGenTintAssignmentBridge code(
			reinterpret_cast<std::uintptr_t>(&Diagnostics::D3DTextureLifetimeTracker::OnFaceGenTintAssigned),
			originalAssignment);
		code.ready();

		auto& trampoline = SKSE::GetTrampoline();
		const auto bridge = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
		trampoline.write_call<5>(callsite, bridge);
		logger::info(
			"[Texture lifetime] Installed FaceGen tint owner correlation at SkyrimVR+0x{:x}",
			callsite - REL::Module::get().base());
	}
#endif

	std::shared_mutex& GetRenderTargetRecreationMutex()
	{
		return g_renderTargetRecreationMutex;
	}

	bool RecreateRenderTargets()
	{
		if (!globals::game::renderer || !globals::state || !globals::d3d::device || !globals::d3d::context)
			return false;

		return BSShaderRenderTargets_Create::RecreateAndSetupFull();
	}

	bool RecreateRenderTargetsForVRRenderScale(
		VRRenderTargetRecreatePreparation a_beforeEngineCreate,
		VRRenderTargetRecreateMutationEntered a_onEngineCreateEntered,
		VRRenderTargetRecreateCheckpoint a_afterEngineCreate,
		void* a_context,
		bool* a_engineCreateEntered)
	{
		if (a_engineCreateEntered)
			*a_engineCreateEntered = false;
		if (!globals::game::renderer || !globals::state || !globals::deferred || !globals::d3d::device || !globals::d3d::context)
			return false;

		return BSShaderRenderTargets_Create::RecreateAndSetupRenderTargetResources(
			a_beforeEngineCreate,
			a_onEngineCreateEntered,
			a_afterEngineCreate,
			a_context,
			a_engineCreateEntered);
	}

	struct BSGraphics_Renderer_Init_InitD3D
	{
		static void thunk()
		{
			logger::info("Calling original Init3D");

			func();

			logger::info("Accessing render device information");
			globals::ReInit();

			logger::info("Detouring virtual function tables");
			stl::detour_vfunc<8, IDXGISwapChain_Present>(globals::d3d::swapChain);
#ifdef DEVBENCH_BRIDGE_ENABLED
			stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(globals::d3d::device);
#endif

#ifdef DEVBENCH_BRIDGE_ENABLED
			// Creation-time bytecode identity is lightweight render-map provenance.
			// Full bytecode is retained only when Dump Shaders is enabled.
			stl::detour_vfunc<12, ID3D11Device_CreateVertexShader>(globals::d3d::device);
			stl::detour_vfunc<15, ID3D11Device_CreatePixelShader>(globals::d3d::device);
			stl::detour_vfunc<18, ID3D11Device_CreateComputeShader>(globals::d3d::device);
#else
			auto shaderCache = globals::shaderCache;
			if (shaderCache->IsDump()) {
				stl::detour_vfunc<12, ID3D11Device_CreateVertexShader>(globals::d3d::device);
				stl::detour_vfunc<15, ID3D11Device_CreatePixelShader>(globals::d3d::device);
			}
#endif

			stl::detour_vfunc<23, ID3D11Device_CreateSamplerState>(globals::d3d::device);

			globals::InstallD3DHooks(globals::d3d::context);

			globals::menu->Init();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct WndProcHandler_Hook
	{
		static LRESULT thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			auto menu = globals::menu;
			if ((a_msg == WM_KILLFOCUS || a_msg == WM_SETFOCUS) && menu->initialized) {
				menu->focusChanged = true;
			}
			if (a_msg == WM_CLOSE) {
				globals::OnGameWindowClose();
			}
			return func(a_hwnd, a_msg, a_wParam, a_lParam);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RegisterClassA_Hook
	{
		static ATOM thunk(WNDCLASSA* a_wndClass)
		{
			WndProcHandler_Hook::func = reinterpret_cast<uintptr_t>(a_wndClass->lpfnWndProc);
			a_wndClass->lpfnWndProc = &WndProcHandler_Hook::thunk;

			return func(a_wndClass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Main
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			// Modify in place and restore so chained hooks keep a stable pointer.
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Normals
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_NormalsSwap
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_MotionVectors
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_RefractionNormals
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			a_properties->copyable = true;
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_UnderwaterMask
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			const auto saved = *a_properties;
			globals::state->ModifyRenderTarget(a_target, a_properties);
			a_properties->copyable = true;
			func(This, a_target, a_properties);
			*a_properties = saved;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShader__BeginTechnique_SetVertexShader
	{
		static void thunk(RE::BSGraphics::Renderer*, RE::BSGraphics::VertexShader* a_vertexShader)
		{
			auto state = globals::state;
			auto shaderCache = globals::shaderCache;

			if (!state->settingCustomShader) {
				if (shaderCache->IsEnabled()) {
					auto currentShader = state->currentShader;
					auto type = currentShader->shaderType.get();
					if (type > 0 && type < RE::BSShader::Type::Total) {
						if (state->enabledClasses[type - 1]) {
							RE::BSGraphics::VertexShader* vertexShader = shaderCache->GetVertexShader(*currentShader, state->modifiedVertexDescriptor);
							if (vertexShader) {
#ifdef DEVBENCH_BRIDGE_ENABLED
								CaptureStageSelection(vertexShader, CSX::RenderMap::ShaderStage::kVertex,
									CSX::RenderMap::ShaderSelectionRoute::kCSXCache);
#endif
								globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(vertexShader->shader), NULL, NULL);
								*globals::game::currentVertexShader = a_vertexShader;
								globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);
								return;
							}
						}
					}
				}
			}

			globals::game::stateUpdateFlags->set(RE::BSGraphics::DIRTY_VERTEX_DESC);

#ifdef DEVBENCH_BRIDGE_ENABLED
			CaptureStageSelection(a_vertexShader, CSX::RenderMap::ShaderStage::kVertex,
				a_vertexShader ? CSX::RenderMap::ShaderSelectionRoute::kEngine :
					CSX::RenderMap::ShaderSelectionRoute::kMissing);
#endif
			*globals::game::currentVertexShader = a_vertexShader;
			globals::d3d::context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(a_vertexShader->shader), NULL, NULL);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSShader__BeginTechnique_SetPixelShader
	{
		static void thunk(RE::BSGraphics::Renderer*, RE::BSGraphics::PixelShader* a_pixelShader)
		{
			auto state = globals::state;
			auto shaderCache = globals::shaderCache;

			if (!state->settingCustomShader) {
				if (shaderCache->IsEnabled()) {
					auto currentShader = state->currentShader;
					auto type = currentShader->shaderType.get();
					if (type > 0 && type < RE::BSShader::Type::Total) {
						if (state->enabledClasses[type - 1]) {
							RE::BSGraphics::PixelShader* pixelShader = shaderCache->GetPixelShader(*currentShader, state->modifiedPixelDescriptor);
							if (pixelShader) {
#ifdef DEVBENCH_BRIDGE_ENABLED
								CaptureStageSelection(pixelShader, CSX::RenderMap::ShaderStage::kPixel,
									CSX::RenderMap::ShaderSelectionRoute::kCSXCache);
#endif
								globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader), NULL, NULL);
								*globals::game::currentPixelShader = a_pixelShader;
								return;
							}
						}
					}
				}
			}

#ifdef DEVBENCH_BRIDGE_ENABLED
			CaptureStageSelection(a_pixelShader, CSX::RenderMap::ShaderStage::kPixel,
				a_pixelShader ? CSX::RenderMap::ShaderSelectionRoute::kEngine :
					CSX::RenderMap::ShaderSelectionRoute::kMissing);
#endif
			*globals::game::currentPixelShader = a_pixelShader;

			if (a_pixelShader)
				globals::d3d::context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(a_pixelShader->shader), NULL, NULL);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil_PrecipitationMask
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
		{
			a_properties->use16BitsDepth = true;
			a_properties->stencil = false;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateCubemapRenderTarget_Reflections
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::CubeMapRenderTargetProperties* a_properties)
		{
			const auto resolution = globals::features::dynamicCubemaps.GetCubemapResolutionForResourceCreation();
			a_properties->height = resolution;
			a_properties->width = resolution;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateDepthStencil_Reflections
	{
		static void thunk(RE::BSGraphics::Renderer* This, uint32_t a_target, RE::BSGraphics::DepthStencilTargetProperties* a_properties)
		{
			const auto resolution = globals::features::dynamicCubemaps.GetCubemapResolutionForResourceCreation();
			a_properties->height = resolution;
			a_properties->width = resolution;
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Sky Reflection Fix
	struct TESWaterReflections_Update_Actor_GetLOSPosition
	{
		static RE::NiPoint3* thunk(RE::PlayerCharacter* a_player, RE::NiPoint3* a_target, int unk1, float unk2)
		{
			auto ret = func(a_player, a_target, unk1, unk2);

			auto camera = RE::PlayerCamera::GetSingleton();
			ret->x = camera->cameraRoot->world.translate.x;
			ret->y = camera->cameraRoot->world.translate.y;
			ret->z = camera->cameraRoot->world.translate.z;

			return ret;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESObjectLAND_SetupMaterial
	{
		static bool thunk(RE::TESObjectLAND* land)
		{
			bool vanillaResult = func(land);

			// TerrainHelper must see the vanilla material hash before TruePBR replaces land materials.
			auto& terrainHelper = globals::features::terrainHelper;
			if (vanillaResult && terrainHelper.loaded) {
				terrainHelper.TESObjectLAND_SetupMaterial(land);
			}

			// setup material for PBR
			auto& truePBR = globals::features::truePBR;
			if (truePBR.loaded && truePBR.TESObjectLAND_SetupMaterial(land)) {
				// if PBR, we are done
				return true;
			}

			return vanillaResult;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_SetupMaterial
	{
		static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
		{
			// A geometry with no resolvable material (for example, a decal whose
			// textures failed to load) can reach this hook with a null material.
			// Guard before TruePBR and the original engine call, which both dereference it.
			if (!material)
				return;

			// setup material for PBR
			auto& truePBR = globals::features::truePBR;
			if (truePBR.loaded && truePBR.BSLightingShader_SetupMaterial(shader, material)) {
				// if PBR, we are done
				return;
			}

			// vanilla
			func(shader, material);

			// terrain helper
			auto& terrainHelper = globals::features::terrainHelper;
			if (terrainHelper.loaded) {
				terrainHelper.BSLightingShader_SetupMaterial(material);
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};
	bool ShouldSkipRenderPassForParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique)
	{
#if defined(_MSC_VER)
		__try
#endif
		{
			return globals::features::lightLimitFix.loaded &&
			       !globals::features::lightLimitFix.CheckParticleLights(a_pass, a_technique);
		}
#if defined(_MSC_VER)
		__except (1) {
			// Fail open on transient invalid render-pass data to avoid crashing render-thread hooks.
			return false;
		}
#endif
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	CSX::RenderMap::Collector::ScopeGuard EnterRenderPassBoundary(
		RE::BSRenderPass* a_pass,
		uint32_t a_technique,
		bool a_alphaTest,
		uint32_t a_renderFlags)
	{
		auto& renderMap = CSX::RenderMap::GetRuntime();
		if (!renderMap.IsCapturing())
			return {};
		if (globals::state)
			renderMap.SetCpuFrame(globals::state->frameCount);
		return renderMap.EnterRenderPass({
			.renderPass = reinterpret_cast<std::uintptr_t>(a_pass),
			.geometry = reinterpret_cast<std::uintptr_t>(a_pass ? a_pass->geometry : nullptr),
			.technique = a_technique,
			.passEnum = a_pass ? a_pass->passEnum : 0,
			.renderFlags = a_renderFlags,
			.alphaTest = a_alphaTest,
		});
	}
#endif

	// This is from 1.4.0 but absent in 1.4.6
	void BSBatchRenderer_RenderPassImmediately1::thunk(
		RE::BSRenderPass* a_pass,
		uint32_t a_technique,
		bool a_alphaTest,
		uint32_t a_renderFlags)
	{
		if (ShouldSkipRenderPassForParticleLights(a_pass, a_technique)) {
			return;
		}

		// Original call from 1.4.0
#ifdef DEVBENCH_BRIDGE_ENABLED
		[[maybe_unused]] const auto renderMapScope = EnterRenderPassBoundary(a_pass, a_technique, a_alphaTest, a_renderFlags);
#endif
		func(a_pass, a_technique, a_alphaTest, a_renderFlags);
	}

	struct BSBatchRenderer_RenderPassImmediately2  // This is from 1.4.0 but absent in 1.4.6
	{
		static void thunk(RE::BSRenderPass* a_pass,
			uint32_t a_technique,
			bool a_alphaTest,
			uint32_t a_renderFlags)
		{
			if (ShouldSkipRenderPassForParticleLights(a_pass, a_technique)) {
				return;
			}

			if (globals::features::terrainBlending.loaded) {
				const auto action = globals::features::terrainBlending.OnRenderPassImmediately(a_pass, a_technique, a_alphaTest, a_renderFlags);
				if (action == TerrainBlending::RenderPassImmediatelyAction::Skip) {
					return;
				}
				if (action == TerrainBlending::RenderPassImmediatelyAction::DrawTwice) {
					DrawRenderPassImmediately(a_pass, a_technique, a_alphaTest, a_renderFlags);
				}
			}

			DrawRenderPassImmediately(a_pass, a_technique, a_alphaTest, a_renderFlags);
		}

		// This is from 1.4.0 but absent in 1.4.6
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSBatchRenderer_RenderPassImmediately3  // This is from 1.4.0 but absent in 1.4.6
	{
		static void thunk(RE::BSRenderPass* a_pass,
			uint32_t a_technique,
			bool a_alphaTest,
			uint32_t a_renderFlags)
		{
			if (ShouldSkipRenderPassForParticleLights(a_pass, a_technique)) {
				return;
			}

			// Original call
#ifdef DEVBENCH_BRIDGE_ENABLED
			[[maybe_unused]] const auto renderMapScope = EnterRenderPassBoundary(a_pass, a_technique, a_alphaTest, a_renderFlags);
#endif
			func(a_pass, a_technique, a_alphaTest, a_renderFlags);
		}

		static inline REL::Relocation<decltype(thunk)> func;  // This is from 1.4.0 but absent in 1.4.6
	};

	void DrawRenderPassImmediately(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		[[maybe_unused]] const auto renderMapScope = EnterRenderPassBoundary(a_pass, a_technique, a_alphaTest, a_renderFlags);
#endif
		if (globals::features::interiorSun.loaded) {
			globals::features::interiorSun.UpdateRasterStateCullMode(a_pass, a_technique);
		}

		BSBatchRenderer_RenderPassImmediately2::func(a_pass, a_technique, a_alphaTest, a_renderFlags);
	}

#ifdef TRACY_ENABLE
	struct Main_Update
	{
		static void thunk(RE::Main* a_this, float a2)
		{
			func(a_this, a2);
			FrameMark;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
#endif

	namespace CSShadersSupport
	{
		RE::BSImagespaceShader* CurrentlyDispatchedShader = nullptr;
		RE::BSComputeShader* CurrentlyDispatchedComputeShader = nullptr;
		uint32_t CurrentComputeShaderTechniqueId = 0;

		struct BSImagespaceShader_DispatchComputeShader
		{
			static void thunk(RE::BSImagespaceShader* shader, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				CurrentlyDispatchedShader = shader;
				func(shader, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				CurrentlyDispatchedShader = nullptr;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSComputeShader_Dispatch
		{
			static void thunk(RE::BSComputeShader* shader, uint32_t techniqueId, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				CurrentlyDispatchedComputeShader = shader;
				CurrentComputeShaderTechniqueId = techniqueId;
				func(shader, techniqueId, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				CurrentlyDispatchedComputeShader = nullptr;
				CurrentComputeShaderTechniqueId = 0;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		const char* GetVolumetricLightingProfileName(RE::BSComputeShader* shader)
		{
			if (!shader)
				return nullptr;

			if (shader->name == "ISVolumetricLightingGenerateCS"sv)
				return "VolumetricLighting::Generate";
			if (shader->name == "ISVolumetricLightingRaymarchCS"sv)
				return "VolumetricLighting::Raymarch";
			if (shader->name == "ISVolumetricLightingBlurHCS"sv)
				return "VolumetricLighting::BlurH";
			if (shader->name == "ISVolumetricLightingBlurVCS"sv)
				return "VolumetricLighting::BlurV";

			return nullptr;
		}

		struct Renderer_DispatchCSShader
		{
			static void thunk(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::ComputeShader* shader, uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
			{
				auto state = globals::state;
				auto shaderCache = globals::shaderCache;
				auto& vl = globals::features::volumetricLighting;
				const char* profileName = nullptr;

				if (state->enabledClasses[RE::BSShader::Type::ImageSpace]) {
					RE::BSImagespaceShader* isShader = CurrentlyDispatchedShader;
					uint32_t techniqueId = CurrentComputeShaderTechniqueId;
					if (vl.loaded && CurrentlyDispatchedComputeShader) {
						profileName = GetVolumetricLightingProfileName(CurrentlyDispatchedComputeShader);

						if (CurrentlyDispatchedShader == nullptr) {
							techniqueId = 0;
							if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingGenerateCS"sv) {
								isShader = vl.GetOrCreateGenerateCS(CurrentlyDispatchedComputeShader);
							} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingRaymarchCS"sv) {
								isShader = vl.GetOrCreateRaymarchCS(CurrentlyDispatchedComputeShader);
							}
						} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingBlurHCS"sv) {
							techniqueId = 0;
							isShader = vl.GetOrCreateBlurHCS(CurrentlyDispatchedComputeShader);
							vl.SetDimensionsCB();
							vl.SetGroupCountsHCS(threadGroupCountX);
						} else if (CurrentlyDispatchedComputeShader->name == "ISVolumetricLightingBlurVCS"sv) {
							techniqueId = 0;
							isShader = vl.GetOrCreateBlurVCS(CurrentlyDispatchedComputeShader);
							vl.SetDimensionsCB();
							vl.SetGroupCountsVCS(threadGroupCountY);
						}
					}
					if (isShader != nullptr) {
						if (auto* computeShader = shaderCache->GetComputeShader(*isShader, techniqueId)) {
							shader = computeShader;
						}
					}
				}

				if (profileName) {
					CS_GPU_PASS_DYNAMIC(profileName);
					func(renderer, shader, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				} else {
					func(renderer, shader, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void PatchMemory(uintptr_t Address, const uint8_t* Data, size_t Size)
	{
		DWORD d = 0;
		VirtualProtect(reinterpret_cast<LPVOID>(Address), Size, PAGE_EXECUTE_READWRITE, &d);

		for (uintptr_t i = Address; i < (Address + Size); i++) {
			*reinterpret_cast<volatile uint8_t*>(i) = *Data++;
		}

		VirtualProtect(reinterpret_cast<LPVOID>(Address), Size, d, &d);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(Address), Size);
	}

	void PatchMemory(uintptr_t Address, std::initializer_list<uint8_t> Data)
	{
		PatchMemory(Address, Data.begin(), Data.size());
	}

	struct BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights
	{
		static void thunk(RE::BSGraphics::PixelShader* PixelShader, RE::BSRenderPass* Pass, DirectX::XMMATRIX& Transform, uint32_t LightCount, uint32_t ShadowLightCount, float WorldScale, uint32_t)
		{
			if (globals::features::lightLimitFix.loaded) {
				globals::features::lightLimitFix.BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(Pass);
			} else {
				func(PixelShader, Pass, Transform, LightCount, ShadowLightCount, WorldScale, 0);
				if (globals::features::adaptiveBrightness.NeedsVanillaPointLightData())
					globals::features::adaptiveBrightness.UpdateVanillaPointLightData(Pass, LightCount, AdaptiveBrightness::kLightingPointLightCBRegister);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImageSpace_Init_IBLF
	{
		static void thunk(char* a1,
			void* a2,
			void* a3,
			void* a4,
			void* a5,
			void* a6,
			void* a7)
		{
			auto enableIBLF = (float*)(REL::RelocationID(513510, 391362).address());
			*enableIBLF = false;

			func(a1, a2, a3, a4, a5, a6, a7);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	/**
	 * @brief Installs hooks, detours, and memory patches for graphics, input, and rendering subsystems.
	 *
	 * Sets up function hooks and virtual method overrides for shader management, input polling, rendering pipeline stages, compute shader dispatch, material setup, batch rendering, and window procedure handling. Applies memory patches to adjust render pass cache sizes and offsets. Installs additional update hooks for frame timing and Reflex marker integration when not in VR mode.
	 */
	void Install()
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		// Construct the inert collector away from render-thread first use. Capture
		// remains disabled until an explicit controller starts a bounded session.
		(void)CSX::RenderMap::GetRuntime();
		InstallVRFaceGenTintAssignmentDiagnostic();
#endif

		if (!REL::Module::IsVR()) {
			logger::info("Hooking BSImageSpace::Init::IBLF");
			stl::detour_thunk<BSImageSpace_Init_IBLF>(REL::RelocationID(100480, 107198));
		}

		logger::info("Hooking BSInputDeviceManager::PollInputDevices");
		stl::write_thunk_call<BSInputDeviceManager_PollInputDevices>(REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B, 0x81));

		logger::info("Hooking BSShader::LoadShaders");
		stl::detour_thunk<BSShader_LoadShaders>(REL::RelocationID(101339, 108326));

		logger::info("Hooking BSShader::BeginTechnique");
		stl::detour_thunk<BSShader_BeginTechnique>(REL::RelocationID(101341, 108328));

		stl::write_thunk_call<BSShader__BeginTechnique_SetVertexShader>(REL::RelocationID(101341, 108328).address() + REL::Relocate(0xC3, 0xD5));
		stl::write_thunk_call<BSShader__BeginTechnique_SetPixelShader>(REL::RelocationID(101341, 108328).address() + REL::Relocate(0xD7, 0xEB));

		logger::info("Hooking BSGraphics::SetDirtyStates");
		stl::detour_thunk<BSGraphics_SetDirtyStates>(REL::RelocationID(75580, 77386));

		logger::info("Hooking BSGraphics::Renderer::InitD3D");
		stl::write_thunk_call<BSGraphics_Renderer_Init_InitD3D>(REL::RelocationID(75595, 77226).address() + REL::Relocate(0x50, 0x2BC));

		logger::info("Hooking WndProcHandler");
		stl::write_thunk_call<RegisterClassA_Hook, 6>(REL::VariantID(75591, 77226, 0xDC4B90).address() + REL::VariantOffset(0x8E, 0x15C, 0x99).offset());

		logger::info("Hooking BSShaderRenderTargets::Create");
		stl::detour_thunk<BSShaderRenderTargets_Create>(REL::RelocationID(100458, 107175));

		logger::info("Hooking BSShaderRenderTargets::Create::CreateRenderTarget(s)");
		stl::write_thunk_call<CreateRenderTarget_Main>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x3F0, 0x3F3, 0x548));
		stl::write_thunk_call<CreateRenderTarget_Normals>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x458, 0x45B, 0x5B0));
		stl::write_thunk_call<CreateRenderTarget_NormalsSwap>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x46B, 0x46E, 0x5C3));
		stl::write_thunk_call<CreateRenderTarget_MotionVectors>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x4F0, 0x4EF, 0x64E));

		stl::write_thunk_call<CreateRenderTarget_RefractionNormals>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x503, 0x502, 0x661));
		stl::write_thunk_call<CreateRenderTarget_UnderwaterMask>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xB19, 0xB19, 0xE06));

		stl::write_thunk_call<CreateDepthStencil_PrecipitationMask>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x1245, 0x123B, 0x1917));
		stl::write_thunk_call<CreateCubemapRenderTarget_Reflections>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xA25, 0xA25, 0xCD2));
		stl::write_thunk_call<CreateDepthStencil_Reflections>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0xA59, 0xA59, 0xD13));

#ifdef TRACY_ENABLE
		stl::write_thunk_call<Main_Update>(REL::RelocationID(35551, 36544).address() + REL::Relocate(0x11F, 0x160));
#endif

		logger::info("Hooking BSImagespaceShader");
		stl::detour_thunk<CSShadersSupport::BSImagespaceShader_DispatchComputeShader>(REL::RelocationID(100952, 107734));
		stl::write_vfunc<0x1, WaterBlendHistory::BSImagespaceShader_Render>(RE::VTABLE_BSImagespaceShaderISWaterBlend[3]);

		logger::info("Hooking BSComputeShader");
		stl::write_vfunc<0x02, CSShadersSupport::BSComputeShader_Dispatch>(RE::VTABLE_BSComputeShader[0]);

		logger::info("Hooking Renderer::DispatchCSShader");
		stl::detour_thunk<CSShadersSupport::Renderer_DispatchCSShader>(REL::RelocationID(75532, 77329));

		logger::info("Hooking TESWaterReflections::Update_Actor::GetLOSPosition for Sky Reflection Fix");
		stl::write_thunk_call<TESWaterReflections_Update_Actor_GetLOSPosition>(REL::RelocationID(31373, 32160).address() + REL::Relocate(0x1AD, 0x1CA, 0x1ed));

		logger::info("Installing SetupGeometry hooks");
		stl::write_vfunc<0x6, EffectExtensions::BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
		stl::write_vfunc<0x6, LightingExtensions::BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		stl::write_thunk_call<GrassExtensions::BSGrassShaderProperty_ctor>(REL::RelocationID(15214, 15383).address() + REL::Relocate(0x45B, 0x4F5));
		stl::write_vfunc<0x6, GrassExtensions::BSGrassShader_SetupGeometry>(RE::VTABLE_BSGrassShader[0]);

		logger::info("Hooking TESObjectLAND");
		stl::detour_thunk<TESObjectLAND_SetupMaterial>(REL::RelocationID(18368, 18791));

		logger::info("Hooking BSLightingShader");
		stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);

		logger::info("Hooking BSBatchRenderer::RenderPassImmediately");
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately1>(
			REL::RelocationID(100877, 107673).address() + REL::Relocate(0x1E5, 0x1EE));
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately2>(
			REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
		stl::write_thunk_call<BSBatchRenderer_RenderPassImmediately3>(
			REL::RelocationID(100871, 107667).address() + REL::Relocate(0xEE, 0xED));

		// Patch render space in BSLightingShader::SetupGeometry to always use world space
		// The variable updateEyePosition is set to 1 when not skinned. By patching to be 0 it will always use world space
		// We offset from the base address of the containing function to the start of the patch
		{
			logger::info("Patching BSLightingShader::SetupGeometry::updateEyePosition");
			auto setupGeometryUpdateRenderSpace = REL::RelocationID(100565, 107300).address();

			if (REL::Module::IsAE()) {
				std::uint8_t patch[] = { 0x41, 0x83, 0xE7, 0x00 };  // and r15d, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x71, patch, sizeof(patch));
			} else if (REL::Module::IsVR()) {
				std::uint8_t patch[] = { 0x41, 0x83, 0xE4, 0x00 };  // and r12d, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x65, patch, sizeof(patch));
			} else {
				std::uint8_t patch1[] = { 0xB8, 0x00, 0x00 };  // mov eax, 0
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x73, patch1, sizeof(patch1));

				std::uint8_t patch2[] = { 0x45, 0x31, 0xC9 };  // xor r9d, r9d (zeros r9d)
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x36D, patch2, sizeof(patch2));

				std::uint8_t patch3[] = { 0x45, 0x31, 0xC0 };  // xor r8d, r8d (zeros r8d)
				REL::safe_write(setupGeometryUpdateRenderSpace + 0x378, patch3, sizeof(patch3));
			}
		}

		stl::write_thunk_call<BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights>(REL::RelocationID(100565, 107300).address() + REL::Relocate(0x523, 0xB0E, 0x5FE));
	}

	void InstallEarlyHooks()
	{
		if (!globals::features::upscaling.loaded) {
			logger::info("Hooking D3D11CreateDeviceAndSwapChain");
			*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		}

		logger::info("Hooking CreateDXGIFactory");
		*(uintptr_t*)&ptrCreateDXGIFactory = SKSE::PatchIAT(hk_CreateDXGIFactory, "dxgi.dll", !REL::Module::IsVR() ? "CreateDXGIFactory" : "CreateDXGIFactory1");
	}
}
