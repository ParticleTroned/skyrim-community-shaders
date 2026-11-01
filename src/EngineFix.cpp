#include "EngineFix.h"
#include "EngineFixes/SunShadowReceiverFix.h"

#include "EngineFixes/EffectShaderNoDecalsFix.h"
#include "EngineFixes/LODBlendRingFix.h"
#include "EngineFixes/ShadowmapCascadeCullingFix.h"
#include "EngineFixes/ShadowmapCascadeRasterizerFix.h"
#include "EngineFixes/SunOcclusionPollFix.h"

const std::vector<EngineFix*>& EngineFix::GetOnPostPostLoadFixesList()
{
	static SunShadowReceiverFix sunShadowReceiverFix;
	static EffectShaderNoDecalsFix effectShaderNoDecalsFix;
	static ShadowmapCascadeCullingFix shadowmapCascadeCullingFix;
	static ShadowmapRasterizerFix shadowmapRasterizerFix;
	static SunOcclusionPollFix sunOcclusionPollFix;

	static std::vector<EngineFix*> fixes = {
		&sunShadowReceiverFix,
		&effectShaderNoDecalsFix,
		&LODBlendRingFix::GetInstance(),
		&shadowmapCascadeCullingFix,
		&shadowmapRasterizerFix,
		&sunOcclusionPollFix,
	};

	return fixes;
}

const std::vector<EngineFix*>& EngineFix::GetOnDataLoadedFixesList()
{
	static std::vector<EngineFix*> fixes = {};

	return fixes;
}

void EngineFix::InstallFixes(const std::vector<EngineFix*>& fixes)
{
	for (const auto fix : fixes) {
		if (IsInstalledByEngineFixes(fix->GetEngineFixesName())) {
			logger::info("[Engine Fixes] Skipped {} (already installed by Engine Fixes)", fix->GetName());
			continue;
		}
		if (fix->TryInstall())
			logger::info("[Engine Fixes] Installed {}", fix->GetName());
		else
			logger::warn("[Engine Fixes] {} was not installed", fix->GetName());
	}
}

bool EngineFix::IsInstalledByEngineFixes(const char* a_name)
{
	if (!a_name)
		return false;
	using Query = bool (*)(const char*);
	for (const auto* moduleName : { L"EngineFixes.dll" }) {
		const auto module = GetModuleHandleW(moduleName);
		if (!module)
			continue;
		const auto query = reinterpret_cast<Query>(GetProcAddress(module, "EngineFixes_IsFixInstalled"));
		if (query && query(a_name))
			return true;
	}
	return false;
}

void EngineFix::InstallOnPostPostLoadFixes()
{
	InstallFixes(GetOnPostPostLoadFixesList());
}

void EngineFix::InstallOnDataLoadedFixes()
{
	InstallFixes(GetOnDataLoadedFixesList());
}
