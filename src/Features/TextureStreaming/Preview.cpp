#include "Preview.h"
#include "Globals.h"
#include "LoadHooks.h"
#include "Utils/Game.h"
#include "Utils/VirtualFunctionHook.h"
#include <RE/I/Inventory3DManager.h>
#include <RE/M/MistMenu.h>
#include <array>
#include <atomic>

namespace StreamingTextures::Preview
{
	namespace
	{
		constexpr std::array menus{ RE::InventoryMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME,
			RE::GiftMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME, RE::MagicMenu::MENU_NAME };
		std::atomic_bool hooksReady{ false };
		bool listenerReady = false;
		struct Events : RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (event)
					for (unsigned i = 0; i < menus.size(); ++i)
						if (event->menuName == menus[i]) {
							LoadHooks::SetPreviewActive(event->opening, 2u << i);
							break;
						}
				return RE::BSEventNotifyControl::kContinue;
			}
		} events;
		using BeginFunction = void (*)(RE::Inventory3DManager*, RE::INTERFACE_LIGHT_SCHEME);
		using EndFunction = void (*)(RE::Inventory3DManager*);
		BeginFunction beginOriginal = nullptr;
		EndFunction endOriginal = nullptr;
		void Begin(RE::Inventory3DManager* manager, RE::INTERFACE_LIGHT_SCHEME scheme)
		{
			if (hooksReady.load()) {
				LoadHooks::SetPreviewActive(true);
			}
			beginOriginal(manager, scheme);
		}
		void End(RE::Inventory3DManager* manager)
		{
			endOriginal(manager);
			LoadHooks::SetPreviewActive(false);
		}
		void Listen(RE::UI* ui)
		{
			if (ui && !listenerReady) {
				ui->AddEventSink<RE::MenuOpenCloseEvent>(&events);
				listenerReady = true;
				for (unsigned i = 0; i < menus.size(); ++i)
					LoadHooks::SetPreviewActive(ui->IsMenuOpen(menus[i]), 2u << i);
			}
		}
		void AddRoot(RE::NiAVObject* root, RootSnapshot& output)
		{
			if (!output.complete || !root)
				return;
			if (!Util::IsLikelyValidPointer(root)) {
				output.complete = false;
				return;
			}
			output.roots.emplace_back(root);
		}
		template <class Data>
		void AddModels(const Data* data, RootSnapshot& output)
		{
			if (!data)
				return;
			if (data->loadedModels.size() > 32) {
				output.complete = false;
				return;
			}
			for (const auto& model : data->loadedModels) {
				AddRoot(model.spModel.get(), output);
				if (!output.complete)
					return;
			}
		}
	}
	void Install()
	{
		if (beginOriginal || endOriginal)
			return;
		try {
			beginOriginal = reinterpret_cast<BeginFunction>(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(50881, 51754) }.address());
			endOriginal = reinterpret_cast<EndFunction>(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(50883, 51756) }.address());
			const auto begin = Util::AttachDetour(reinterpret_cast<void**>(&beginOriginal), reinterpret_cast<void*>(&Begin));
			if (begin.error != NO_ERROR) {
				logger::warn("[TextureStreaming] Preview begin hook unavailable ({}); menu-event protection retained", begin.error);
				return;
			}
			const auto end = Util::AttachDetour(reinterpret_cast<void**>(&endOriginal), reinterpret_cast<void*>(&End));
			hooksReady = end.error == NO_ERROR;
			if (end.error != NO_ERROR)
				logger::warn("[TextureStreaming] Preview end hook unavailable ({}); menu-event protection retained", end.error);
		} catch (const std::exception& error) {
			logger::warn("[TextureStreaming] Native preview tracking unavailable: {}", error.what());
		}
	}
	bool Active() { return LoadHooks::IsPreviewActive(); }
	RootSnapshot Roots()
	{
		RootSnapshot output;
		auto* ui = globals::game::ui;
		Listen(ui);
		if (!ui)
			return output;
		// Retained models cover native previews even when their menu has no standard event name.
		if (const auto* manager = RE::Inventory3DManager::GetSingleton()) {
			if (globals::game::isVR)
				AddModels(manager->GetVRRuntimeData(), output);
			else
				AddModels(manager->GetRuntimeData(), output);
		}
		if (!output.complete)
			return output;
		if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))
			if (const auto mist = ui->GetMenu<RE::MistMenu>()) {
				if (globals::game::isVR) {
					if (const auto* data = mist->GetVRRuntimeData())
						AddRoot(data->loadScreen3DModelNode.get(), output);
				} else {
					AddRoot(mist->GetRuntimeData().loadScreenModel.get(), output);
				}
			}
		return output;
	}
}
