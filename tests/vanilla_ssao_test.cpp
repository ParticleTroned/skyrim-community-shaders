#include <iostream>
#include <stdexcept>

namespace globals
{
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	} cache;
	ShaderCache* shaderCache = &cache;
}
namespace globals::features
{
	struct ReverseZ
	{
		bool active = false;
		bool IsActive() const { return active; }
	} reverseZ;
}
namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
namespace RE
{
	struct Setting
	{
		struct
		{
			bool b = true;
		} data;
	};
	Setting setting;
	bool hasSetting = true;
	Setting* GetINISetting(const char*) { return hasSetting ? &setting : nullptr; }
	struct SSAO
	{
		bool enableSAO = true;
	} sao;
	struct ImageSpaceManager
	{
		SSAO* blur = &sao;
		static ImageSpaceManager* GetSingleton();
	} manager;
	bool hasManager = true;
	ImageSpaceManager* ImageSpaceManager::GetSingleton() { return hasManager ? &manager : nullptr; }
}
#define GET_INSTANCE_MEMBER(member, manager) auto* member = manager->blur
struct ScreenSpaceGI
{
	bool loaded = true;
	struct
	{
		bool EnableVanillaSSAO = true;
	} settings;
	void SyncVanillaSSAO();
};
#include "vanilla_ssao.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	try {
		for (bool enabled : { false, true }) {
			globals::cache.enabled = enabled;
			for (bool vr : { false, true })
				for (bool reversed : { false, true })
					for (bool loaded : { false, true })
						for (bool requested : { false, true }) {
							REL::Module::vr = vr;
							globals::features::reverseZ.active = reversed;
							ScreenSpaceGI feature;
							feature.loaded = loaded;
							feature.settings.EnableVanillaSSAO = requested;
							RE::setting.data.b = RE::sao.enableSAO = true;
							feature.SyncVanillaSSAO();
							const bool unmanaged = (!loaded || !enabled) && !reversed;
							const bool expected = unmanaged || (!vr && !reversed && requested);
							Require(RE::setting.data.b == expected && RE::sao.enableSAO == expected,
								"Native SSAO did not follow the effective depth convention");
							Require(vr || feature.settings.EnableVanillaSSAO == requested,
								"Reverse-depth compatibility overwrote the saved desktop preference");
							feature.SyncVanillaSSAO();
							Require(RE::setting.data.b == expected && RE::sao.enableSAO == expected,
								"Repeated SSAO synchronization changed the result");
						}
		}
		REL::Module::vr = false;
		globals::cache.enabled = true;
		globals::features::reverseZ.active = true;
		ScreenSpaceGI desktop;
		desktop.SyncVanillaSSAO();
		globals::features::reverseZ.active = false;
		desktop.SyncVanillaSSAO();
		Require(RE::setting.data.b && RE::sao.enableSAO && desktop.settings.EnableVanillaSSAO,
			"Standard depth did not retain the requested native SSAO preference");
		globals::shaderCache = nullptr;
		globals::features::reverseZ.active = true;
		ScreenSpaceGI feature;
		feature.loaded = false;
		RE::hasManager = false;
		RE::setting.data.b = true;
		feature.SyncVanillaSSAO();
		Require(!RE::setting.data.b, "Missing image-space manager bypassed the INI guard");
		RE::hasSetting = false;
		RE::hasManager = true;
		RE::sao.enableSAO = true;
		feature.SyncVanillaSSAO();
		Require(!RE::sao.enableSAO, "Missing INI setting bypassed the live SSAO guard");
		RE::manager.blur = nullptr;
		feature.SyncVanillaSSAO();
		RE::manager.blur = &RE::sao;
		RE::sao.enableSAO = true;
		feature.SyncVanillaSSAO();
		Require(!RE::sao.enableSAO, "Recreated native SSAO state escaped the guard");
		std::cout << "Vanilla SSAO depth compatibility and unavailable-engine-state cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
