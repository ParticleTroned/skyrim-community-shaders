#include "EmptyOverlayCullFix.h"

#include "Utils/DdsResource.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/StringUtils.h"
#include "Utils/TextureAlpha.h"
#include "Utils/TexturePath.h"
#include "Utils/VirtualFunctionHook.h"

#include <array>
#include <stdexcept>
#include <string>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#	include <nlohmann/json.hpp>
#endif

namespace
{
	bool installed = false;
	std::string emptyTexture;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool enabled{ true };
	std::atomic_uint64_t culledPassBuilds{ 0 };
	std::string installationError = "Not initialized";
#endif

	bool Decline(const std::string& a_reason)
	{
		logger::info("[Empty Overlay Cull] {}; retaining native rendering", a_reason);
#ifdef DEVBENCH_BRIDGE_ENABLED
		installationError = a_reason;
#endif
		return false;
	}

	bool IsEmptyOverlay(RE::BSLightingShaderProperty* a_property, RE::BSGeometry* a_geometry)
	{
		using enum RE::BSShaderProperty::EShaderPropertyFlag;
		if (!a_property || !a_geometry || !a_property->flags.any(kSkinned) || a_property->flags.any(kZBufferWrite))
			return false;
		const auto* alpha = a_geometry->GetGeometryRuntimeData().alphaProperty.get();
		using enum RE::NiAlphaProperty::AlphaFunction;
		if (!alpha || !alpha->GetAlphaBlending() || alpha->GetSrcBlendMode() != kSrcAlpha || alpha->GetDestBlendMode() != kInvSrcAlpha)
			return false;
		const auto* material = a_property->GetBaseMaterial();
		if (!material || material->GetType() != RE::BSShaderMaterial::Type::kLighting || material->GetFeature() != RE::BSShaderMaterial::Feature::kDefault)
			return false;
		const auto* diffuse = static_cast<const RE::BSLightingShaderMaterialBase*>(material)->diffuseTexture.get();
		const auto* path = diffuse ? diffuse->name.c_str() : nullptr;
		return path && Util::IEquals(path, emptyTexture);
	}

	struct GetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* a_property, RE::BSGeometry* a_geometry, uint32_t a_mode, RE::BSShaderAccumulator* a_accumulator)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (!enabled.load(std::memory_order_relaxed))
				return func(a_property, a_geometry, a_mode, a_accumulator);
#endif
			if (IsEmptyOverlay(a_property, a_geometry)) {
				// Return cached passes to their pool before publishing the empty list.
				a_property->renderPassList.Clear();
#ifdef DEVBENCH_BRIDGE_ENABLED
				culledPassBuilds.fetch_add(1, std::memory_order_relaxed);
#endif
				return &a_property->renderPassList;
			}
			return func(a_property, a_geometry, a_mode, a_accumulator);
		}
		static inline decltype(&thunk) func = nullptr;
	};
}

bool EmptyOverlayCullFix::TryInstall()
{
	if (installed)
		return true;
	const auto iniPath = Util::PathHelpers::GetDataPath() / "SKSE" / "skse.ini";
	std::array<wchar_t, MAX_PATH> value{};
	const auto length = GetPrivateProfileStringW(L"Overlays/Data", L"sDefaultTexture",
		L"textures\\actors\\character\\overlays\\default.dds", value.data(), static_cast<DWORD>(value.size()), iniPath.c_str());
	if (length == 0 || length >= value.size() - 1)
		return Decline("Default texture path is empty or truncated");
	emptyTexture = Util::NormalizeTexturePath(Util::WStringToString(value.data()));
	try {
		std::ignore = Util::JoinRelativePath({}, emptyTexture);
	} catch (const std::invalid_argument&) {
		return Decline("Default texture path is not a relative resource");
	}
	DirectX::ScratchImage image;
	if (FAILED(Util::LoadDDSImageFromResource(emptyTexture.c_str(), image)) || !Util::HasOnlyZeroAlpha(image))
		return Decline("Default texture is unavailable or its complete alpha is not zero");
	const REL::Relocation<std::uintptr_t> table{ RE::VTABLE_BSLightingShaderProperty[0] };
	const auto result = Util::InstallNativeVirtualFunctionHook(table.address(), 0x2A,
		reinterpret_cast<void**>(&GetRenderPasses::func), reinterpret_cast<void*>(&GetRenderPasses::thunk));
	if (result.error != NO_ERROR)
		return Decline("Render-pass hook installation failed (" + std::to_string(result.error) + ")");
	installed = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
	installationError.clear();
#endif
	return true;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
nlohmann::json EmptyOverlayCull::Control(const nlohmann::json& a_args)
{
	for (const auto& [name, value] : a_args.items()) {
		(void)value;
		if (name != "action" && name != "contractMajor" && name != "clientId" && name != "commandId" &&
			name != "expectedBuildId" && name != "emptyOverlayCull")
			throw std::invalid_argument("Unknown empty_overlay_cull argument");
	}
	if (const auto settings = a_args.find("emptyOverlayCull"); settings != a_args.end()) {
		if (!settings->is_object() || settings->size() != 1 || !settings->contains("enabled") || !settings->at("enabled").is_boolean())
			throw std::invalid_argument("emptyOverlayCull requires exactly one boolean enabled field");
		if (!installed)
			throw std::invalid_argument("Empty overlay cull hook is unavailable");
		enabled.store(settings->at("enabled").get<bool>(), std::memory_order_relaxed);
	}
	return { { "available", installed }, { "enabled", installed && enabled.load(std::memory_order_relaxed) },
		{ "verifiedTexture", installed ? emptyTexture : "" }, { "installationError", installationError },
		{ "culledPassBuilds", culledPassBuilds.load(std::memory_order_relaxed) }, { "sessionOnly", true } };
}
#endif
