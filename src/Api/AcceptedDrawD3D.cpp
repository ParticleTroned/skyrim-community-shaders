#include "Api/AcceptedDrawD3D.h"

#include "Api/AcceptedDrawService.h"
#include "Utils/VirtualFunctionHook.h"
#include <array>
#include <mutex>
#include <utility>

namespace CSX::Api
{
	namespace
	{
		constexpr std::size_t kMaximumTargets = 8;
		struct HookTarget
		{
			void* address = nullptr;
			void* observer = nullptr;
			bool installed = false;
			Util::VirtualHookMethod method = Util::VirtualHookMethod::Detour;
		};
		std::mutex installMutex;
		std::array<HookTarget, kMaximumTargets> indexedTargets, instancedTargets;

		template <std::size_t Index>
		struct SubmissionHooks
		{
			using Indexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
			using Instanced = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
			static inline Indexed originalIndexed = nullptr;
			static inline Instanced originalInstanced = nullptr;

			static void ReplayIndexed(ID3D11DeviceContext* context, const CSXAcceptedDrawAPI::Arguments& args)
			{
				originalIndexed(context, args.indexCount, args.startIndex, args.baseVertex);
			}
			static void ReplayInstanced(ID3D11DeviceContext* context, const CSXAcceptedDrawAPI::Arguments& args)
			{
				originalInstanced(context, args.indexCount, args.instanceCount, args.startIndex, args.baseVertex, args.startInstance);
			}
			static void STDMETHODCALLTYPE DrawIndexed(ID3D11DeviceContext* context, UINT count, UINT start, INT base)
			{
				originalIndexed(context, count, start, base);
				PublishAcceptedDraw(context, { CSXAcceptedDrawAPI::Indexed, count, 1, start, base, 0 }, ReplayIndexed);
			}
			static void STDMETHODCALLTYPE DrawIndexedInstanced(ID3D11DeviceContext* context, UINT count, UINT instances, UINT start, INT base, UINT firstInstance)
			{
				originalInstanced(context, count, instances, start, base, firstInstance);
				PublishAcceptedDraw(context, { CSXAcceptedDrawAPI::IndexedInstanced, count, instances, start, base, firstInstance }, ReplayInstanced);
			}
			static bool Install(ID3D11DeviceContext* context, std::size_t slot, HookTarget& target, void** original, void* observer)
			{
				// A failed publication is never retried with another trampoline. Some failure
				// paths can have published a table entry before restoring protection failed.
				if (target.address && !target.installed)
					return false;
				if (!target.address) {
					target.address = (*reinterpret_cast<void***>(context))[slot];
					target.observer = observer;
				}
				const auto result = Util::InstallVirtualFunctionHook(context, slot, original, observer);
				if (result.error != NO_ERROR) {
					logger::error("Accepted draw observer at D3D11 slot {} unavailable: {}", slot, result.error);
					return false;
				}
				target.installed = true;
				target.method = result.method;
				return true;
			}
			static bool InstallIndexed(ID3D11DeviceContext* context)
			{
				return Install(context, 12, indexedTargets[Index], reinterpret_cast<void**>(&originalIndexed), reinterpret_cast<void*>(&DrawIndexed));
			}
			static bool InstallInstanced(ID3D11DeviceContext* context)
			{
				return Install(context, 20, instancedTargets[Index], reinterpret_cast<void**>(&originalInstanced), reinterpret_cast<void*>(&DrawIndexedInstanced));
			}
		};

		template <std::size_t... Index>
		constexpr auto IndexedInstallers(std::index_sequence<Index...>)
		{
			return std::array{ &SubmissionHooks<Index>::InstallIndexed... };
		}
		template <std::size_t... Index>
		constexpr auto InstancedInstallers(std::index_sequence<Index...>)
		{
			return std::array{ &SubmissionHooks<Index>::InstallInstanced... };
		}
		using Installer = bool (*)(ID3D11DeviceContext*);

		bool CoverTarget(ID3D11DeviceContext* context, std::size_t slot,
			std::array<HookTarget, kMaximumTargets>& targets, const std::array<Installer, kMaximumTargets>& installers)
		{
			const auto address = (*reinterpret_cast<void***>(context))[slot];
			if (!address)
				return false;
			for (std::size_t index = 0; index < targets.size(); ++index) {
				const auto& target = targets[index];
				if (address != target.address && address != target.observer)
					continue;
				if (!target.installed)
					return false;
				if (address == target.observer || target.method == Util::VirtualHookMethod::Detour)
					return true;
				// A table/clone hook covers one table. Reuse its original storage for
				// another table with the same target; installing a new detour would
				// put two observers in the first table's call chain.
				return installers[index](context);
			}
			for (std::size_t index = 0; index < targets.size(); ++index)
				if (!targets[index].address)
					return installers[index](context);
			logger::error("Accepted draw observer target capacity exhausted at D3D11 slot {}; API registration is unavailable", slot);
			return false;
		}
	}

	bool InstallAcceptedDrawD3DHooks(ID3D11DeviceContext* context)
	{
		if (!context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return false;
		std::scoped_lock lock(installMutex);
		static constexpr auto indexed = IndexedInstallers(std::make_index_sequence<kMaximumTargets>{});
		static constexpr auto instanced = InstancedInstallers(std::make_index_sequence<kMaximumTargets>{});
		return CoverTarget(context, 12, indexedTargets, indexed) && CoverTarget(context, 20, instancedTargets, instanced);
	}
}
