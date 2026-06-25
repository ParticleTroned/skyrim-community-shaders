#include "DxvkWsiHook.h"

#include "Globals.h"

#include <atomic>
#include <cstring>
#include <detours/detours.h>
#include <vector>

namespace SIE::DxvkWsiHook
{
	namespace
	{
		// Real vulkan-1.dll!vkGetInstanceProcAddr. After DetourAttach this pointer is the
		// trampoline that reaches the genuine function.
		PFN_vkGetInstanceProcAddr g_realGetInstanceProcAddr = nullptr;

		// Real vkGetDeviceProcAddr, captured the first time DXVK resolves it (see the detour).
		PFN_vkGetDeviceProcAddr g_realGetDeviceProcAddr = nullptr;

		// The VkInstance DXVK created, captured from the first instance-bearing proc-addr lookup.
		// Needed to resolve vkCreateDevice and the physical-device queries inside the create-device
		// wrapper (which only receives a VkPhysicalDevice).
		VkInstance g_instance = VK_NULL_HANDLE;

		// Physical/logical device DXVK created (captured at vkCreateDevice).
		VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;
		VkDevice g_device = VK_NULL_HANDLE;

		// Queues we injected into DXVK's vkCreateDevice for the FFX FG swapchain.
		DxvkWsiHook::InjectedQueues g_injected;

		// The active frame-generation provider (FFX now, DLSS-G later). The interposer delegates
		// device-creation augmentation and the five WSI functions to it. Null => pure pass-through.
		IFrameGenProvider* g_provider = nullptr;

		// The wrapped swapchain handle the provider returned from CreateSwapchain (Skyrim has a
		// single swapchain). WSI calls naming this handle route to the provider; others pass through.
		VkSwapchainKHR g_providerSwapchain = VK_NULL_HANDLE;

		// Set to force DXVK to recreate its swapchain (re-evaluate wrap) via an OUT_OF_DATE present.
		std::atomic<bool> g_pendingRecreate{ false };

		bool g_installed = false;

		// One-shot logging so we can confirm in the log that DXVK actually resolved each of our
		// wrappers (i.e. the interception is live) without spamming per-present.
		bool g_loggedCreate = false;
		bool g_loggedPresent = false;

		// Genuine driver WSI entry points, captured once a DXVK VkDevice is known (at swapchain
		// creation). vkQueuePresentKHR has no device parameter, so we cannot resolve it inside the
		// present wrapper — caching all five here from the device-bearing create wrapper solves it.
		VkDevice g_dxvkDevice = VK_NULL_HANDLE;
		PFN_vkCreateSwapchainKHR g_realCreate = nullptr;
		PFN_vkDestroySwapchainKHR g_realDestroy = nullptr;
		PFN_vkGetSwapchainImagesKHR g_realGetImages = nullptr;
		PFN_vkAcquireNextImageKHR g_realAcquire = nullptr;
		PFN_vkQueuePresentKHR g_realPresent = nullptr;

		// Resolve and cache the genuine WSI functions for DXVK's device (idempotent).
		void CaptureReals(VkDevice device)
		{
			if (g_dxvkDevice != VK_NULL_HANDLE || device == VK_NULL_HANDLE || !g_realGetDeviceProcAddr)
				return;
			g_dxvkDevice = device;
			g_realCreate = reinterpret_cast<PFN_vkCreateSwapchainKHR>(g_realGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
			g_realDestroy = reinterpret_cast<PFN_vkDestroySwapchainKHR>(g_realGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
			g_realGetImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(g_realGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
			g_realAcquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(g_realGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
			g_realPresent = reinterpret_cast<PFN_vkQueuePresentKHR>(g_realGetDeviceProcAddr(device, "vkQueuePresentKHR"));
		}

		// ---- The five WSI device-function wrappers (MILESTONE 1: pass-through) ---------------
		// Each resolves the genuine driver function via the captured real vkGetDeviceProcAddr and
		// forwards unchanged. The later milestone replaces these bodies with routing into the
		// FFX frame-generation swapchain.

		// True if this VkSwapchainKHR is the one the provider currently owns.
		bool IsProviderSwapchain(VkSwapchainKHR sc)
		{
			return g_provider && sc != VK_NULL_HANDLE && sc == g_providerSwapchain;
		}

		VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateSwapchainKHR(
			VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
			const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
		{
			CaptureReals(device);
			if (!g_loggedCreate) {
				g_loggedCreate = true;
				logger::info("[DxvkWsiHook] DXVK creating swapchain through our wrapper ({}x{}, fmt {})",
					pCreateInfo ? pCreateInfo->imageExtent.width : 0,
					pCreateInfo ? pCreateInfo->imageExtent.height : 0,
					pCreateInfo ? (int)pCreateInfo->imageFormat : -1);
			}

			// Conditional wrap: only hand the swapchain to the provider when frame generation is
			// actually enabled. When off, DXVK gets a native swapchain and presents with zero
			// frame-gen overhead. (Runtime toggle recreates the swapchain to switch paths.)
			if (g_provider && g_provider->WantsToWrap()) {
				VkResult r = g_provider->CreateSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
				if (r == VK_SUCCESS) {
					g_providerSwapchain = *pSwapchain;
					logger::info("[DxvkWsiHook] swapchain owned by provider '{}'", g_provider->Name());
					return r;
				}
				logger::warn("[DxvkWsiHook] provider '{}' CreateSwapchain failed ({}); falling back to native",
					g_provider->Name(), (int)r);
			}
			return g_realCreate(device, pCreateInfo, pAllocator, pSwapchain);
		}

		VKAPI_ATTR void VKAPI_CALL Wrap_vkDestroySwapchainKHR(
			VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
		{
			CaptureReals(device);
			if (IsProviderSwapchain(swapchain)) {
				g_provider->DestroySwapchain(device, swapchain, pAllocator);
				g_providerSwapchain = VK_NULL_HANDLE;
				return;
			}
			g_realDestroy(device, swapchain, pAllocator);
		}

		VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkGetSwapchainImagesKHR(
			VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
		{
			CaptureReals(device);
			if (IsProviderSwapchain(swapchain))
				return g_provider->GetSwapchainImages(device, swapchain, pSwapchainImageCount, pSwapchainImages);
			return g_realGetImages(device, swapchain, pSwapchainImageCount, pSwapchainImages);
		}

		VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkAcquireNextImageKHR(
			VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
			VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
		{
			CaptureReals(device);
			if (IsProviderSwapchain(swapchain))
				return g_provider->AcquireNextImage(device, swapchain, timeout, semaphore, fence, pImageIndex);
			return g_realAcquire(device, swapchain, timeout, semaphore, fence, pImageIndex);
		}

		VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkQueuePresentKHR(
			VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
		{
			if (!g_loggedPresent) {
				g_loggedPresent = true;
				logger::info("[DxvkWsiHook] DXVK presents through our wrapper (interception live)");
			}
			// A toggle (or FG becoming ready) asked us to switch wrap state: drop this present so
			// DXVK recreates the swapchain through our create wrapper, which re-decides wrap vs
			// native. DXVK's own recreate path handles in-flight frames and image lifetime.
			if (g_pendingRecreate.exchange(false)) {
				logger::info("[DxvkWsiHook] forcing swapchain recreate (FG wrap state change)");
				return VK_ERROR_OUT_OF_DATE_KHR;
			}
			// Route to the provider if any of the presented swapchains is the one it owns.
			if (g_provider && pPresentInfo) {
				for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
					if (pPresentInfo->pSwapchains[i] == g_providerSwapchain && g_providerSwapchain != VK_NULL_HANDLE)
						return g_provider->QueuePresent(queue, pPresentInfo);
				}
			}
			// vkQueuePresentKHR has no device parameter; use the pointer captured at swapchain
			// creation (the create wrapper always runs first under DXVK's single device).
			return g_realPresent ? g_realPresent(queue, pPresentInfo) : VK_ERROR_INITIALIZATION_FAILED;
		}

		// ---- vkCreateDevice wrapper: inject the active provider's device-creation needs --------
		// Frame-gen providers need queues (and, for DLSS-G, an optical-flow queue + VK_NV_optical_flow)
		// requested at device creation, which DXVK owns. We intercept DXVK's vkCreateDevice and add
		// the active provider's requirements (extra present-capable queues from a family distinct from
		// DXVK's graphics family, plus any device extensions / pNext feature structs), then claim the
		// queues and hand them to the provider. DXVK keeps using its own queues; the change is
		// transparent to it.
		VKAPI_ATTR VkResult VKAPI_CALL Wrap_vkCreateDevice(
			VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
			const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
		{
			auto realCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
				g_realGetInstanceProcAddr(g_instance, "vkCreateDevice"));
			auto getQFP = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
				g_realGetInstanceProcAddr(g_instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
			if (!realCreateDevice)
				return VK_ERROR_INITIALIZATION_FAILED;

			g_physicalDevice = physicalDevice;

			FrameGenDeviceRequirements req;
			if (g_provider)
				req = g_provider->GetDeviceRequirements(physicalDevice);

			const VkDeviceCreateInfo* infoToUse = pCreateInfo;
			VkDeviceCreateInfo modified = *pCreateInfo;
			std::vector<VkDeviceQueueCreateInfo> queues(
				pCreateInfo->pQueueCreateInfos,
				pCreateInfo->pQueueCreateInfos + pCreateInfo->queueCreateInfoCount);
			std::vector<const char*> extensions(
				pCreateInfo->ppEnabledExtensionNames,
				pCreateInfo->ppEnabledExtensionNames + pCreateInfo->enabledExtensionCount);
			static const float priorities[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			uint32_t gameFamily = UINT32_MAX;
			uint32_t presentFamily = UINT32_MAX;
			bool modify = false;

			if (g_provider && getQFP) {
				uint32_t famCount = 0;
				getQFP(physicalDevice, &famCount, nullptr);
				std::vector<VkQueueFamilyProperties> fams(famCount);
				getQFP(physicalDevice, &famCount, fams.data());

				// DXVK's game (graphics+compute) family — the one we must NOT reuse for present.
				for (const auto& q : queues) {
					if (q.queueFamilyIndex < famCount &&
						(fams[q.queueFamilyIndex].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
							(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
						gameFamily = q.queueFamilyIndex;
						break;
					}
				}

				// Present/acquire queues: a compute-capable family (virtually always present-capable
				// on real GPUs; re-validated against the surface at swapchain creation), distinct from
				// the game family, with enough queues for the provider's request.
				if (req.presentCapableQueues > 0) {
					for (uint32_t f = 0; f < famCount; ++f) {
						if (f == gameFamily)
							continue;
						if ((fams[f].queueFlags & VK_QUEUE_COMPUTE_BIT) && fams[f].queueCount >= req.presentCapableQueues) {
							presentFamily = f;
							break;
						}
					}
					if (presentFamily != UINT32_MAX) {
						bool bumped = false;
						for (auto& q : queues) {
							if (q.queueFamilyIndex == presentFamily) {
								q.queueCount = req.presentCapableQueues;
								q.pQueuePriorities = priorities;
								bumped = true;
								break;
							}
						}
						if (!bumped) {
							VkDeviceQueueCreateInfo extra{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
							extra.queueFamilyIndex = presentFamily;
							extra.queueCount = req.presentCapableQueues;
							extra.pQueuePriorities = priorities;
							queues.push_back(extra);
						}
						modify = true;
						logger::info("[DxvkWsiHook] injecting {} queues from family {} for provider '{}' (game family {})",
							req.presentCapableQueues, presentFamily, g_provider->Name(), gameFamily);
					} else {
						logger::warn("[DxvkWsiHook] no spare present-capable family for provider '{}'; FG unavailable", g_provider->Name());
					}
				}

				// Provider device extensions (e.g. VK_NV_optical_flow for DLSS-G).
				for (const char* ext : req.deviceExtensions) {
					extensions.push_back(ext);
					modify = true;
				}
			}

			if (modify) {
				modified.queueCreateInfoCount = (uint32_t)queues.size();
				modified.pQueueCreateInfos = queues.data();
				modified.enabledExtensionCount = (uint32_t)extensions.size();
				modified.ppEnabledExtensionNames = extensions.data();
				// Prepend the provider's feature-struct chain to pNext, preserving DXVK's chain.
				if (req.featuresPNext) {
					auto* tail = reinterpret_cast<VkBaseOutStructure*>(req.featuresPNext);
					while (tail->pNext)
						tail = tail->pNext;
					tail->pNext = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(modified.pNext));
					modified.pNext = req.featuresPNext;
				}
				infoToUse = &modified;
			}

			VkResult res = realCreateDevice(physicalDevice, infoToUse, pAllocator, pDevice);
			if (res != VK_SUCCESS || !pDevice)
				return res;
			g_device = *pDevice;

			if (g_provider && presentFamily != UINT32_MAX) {
				auto realGDPA = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
					g_realGetInstanceProcAddr(g_instance, "vkGetDeviceProcAddr"));
				auto getDevQ = realGDPA ? reinterpret_cast<PFN_vkGetDeviceQueue>(realGDPA(*pDevice, "vkGetDeviceQueue")) : nullptr;
				if (getDevQ) {
					FrameGenQueues fq;
					if (gameFamily != UINT32_MAX)
						getDevQ(*pDevice, gameFamily, 0, &fq.gameQueue);
					fq.gameFamily = gameFamily;
					getDevQ(*pDevice, presentFamily, 0, &fq.presentQueue);
					getDevQ(*pDevice, presentFamily, 1, &fq.imageAcquireQueue);
					fq.presentFamily = presentFamily;
					fq.imageAcquireFamily = presentFamily;
					g_injected.present = fq.presentQueue;
					g_injected.imageAcquire = fq.imageAcquireQueue;
					g_injected.presentFamily = presentFamily;
					g_injected.imageAcquireFamily = presentFamily;
					logger::info("[DxvkWsiHook] claimed provider queues game=0x{:X} present=0x{:X} acquire=0x{:X}",
						(uintptr_t)fq.gameQueue, (uintptr_t)fq.presentQueue, (uintptr_t)fq.imageAcquireQueue);
					g_provider->OnDeviceCreated(physicalDevice, *pDevice, fq);
				}
			}
			return res;
		}

		// ---- Wrapped vkGetDeviceProcAddr: hands DXVK our WSI wrappers ------------------------
		VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Wrapped_GetDeviceProcAddr(VkDevice device, const char* pName)
		{
			if (pName) {
				if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0)
					return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkCreateSwapchainKHR);
				if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
					return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkDestroySwapchainKHR);
				if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0)
					return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkGetSwapchainImagesKHR);
				if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0)
					return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkAcquireNextImageKHR);
				if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
					return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkQueuePresentKHR);
			}
			return g_realGetDeviceProcAddr ? g_realGetDeviceProcAddr(device, pName) : nullptr;
		}

		// ---- The detour on vulkan-1.dll!vkGetInstanceProcAddr -------------------------------
		VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Detour_GetInstanceProcAddr(VkInstance instance, const char* pName)
		{
			// Capture the VkInstance so the create-device wrapper can resolve physical-device queries.
			if (instance != VK_NULL_HANDLE)
				g_instance = instance;

			// Inject the FFX FG-swapchain queues by wrapping the device creation DXVK is about to do.
			if (pName && std::strcmp(pName, "vkCreateDevice") == 0)
				return reinterpret_cast<PFN_vkVoidFunction>(Wrap_vkCreateDevice);

			if (pName && std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
				// Capture the genuine device proc addr the first time it is resolved, then hand the
				// caller (DXVK) our wrapped version so its swapchain-function lookups reach our
				// wrappers. FFX uses the captured real one (GetRealDeviceProcAddr) and so bypasses
				// this wrap, avoiding recursion.
				if (!g_realGetDeviceProcAddr && g_realGetInstanceProcAddr)
					g_realGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
						g_realGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
				return reinterpret_cast<PFN_vkVoidFunction>(Wrapped_GetDeviceProcAddr);
			}
			return g_realGetInstanceProcAddr ? g_realGetInstanceProcAddr(instance, pName) : nullptr;
		}
	}

	bool Install()
	{
		if (g_installed)
			return true;

		HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
		if (!vk)
			vk = LoadLibraryW(L"vulkan-1.dll");
		if (!vk) {
			logger::error("[DxvkWsiHook] vulkan-1.dll not present; cannot hook DXVK WSI");
			return false;
		}

		g_realGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			reinterpret_cast<void*>(GetProcAddress(vk, "vkGetInstanceProcAddr")));
		if (!g_realGetInstanceProcAddr) {
			logger::error("[DxvkWsiHook] could not resolve vulkan-1.dll!vkGetInstanceProcAddr");
			return false;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&reinterpret_cast<PVOID&>(g_realGetInstanceProcAddr), Detour_GetInstanceProcAddr);
		const LONG err = DetourTransactionCommit();
		if (err != NO_ERROR) {
			logger::error("[DxvkWsiHook] DetourAttach(vkGetInstanceProcAddr) failed ({})", (int)err);
			return false;
		}

		g_installed = true;
		logger::info("[DxvkWsiHook] vkGetInstanceProcAddr detour installed (WSI interception armed)");
		return true;
	}

	void Uninstall()
	{
		if (!g_installed)
			return;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&reinterpret_cast<PVOID&>(g_realGetInstanceProcAddr), Detour_GetInstanceProcAddr);
		DetourTransactionCommit();
		g_installed = false;
	}

	PFN_vkGetDeviceProcAddr GetRealDeviceProcAddr()
	{
		return g_realGetDeviceProcAddr;
	}

	bool IsActive()
	{
		return g_realGetDeviceProcAddr != nullptr;
	}

	void SetProvider(IFrameGenProvider* provider)
	{
		g_provider = provider;
	}

	IFrameGenProvider* GetProvider()
	{
		return g_provider;
	}

	void RequestSwapchainRecreate()
	{
		g_pendingRecreate.store(true);
	}

	bool IsSwapchainWrapped()
	{
		return g_providerSwapchain != VK_NULL_HANDLE;
	}

	InjectedQueues GetInjectedQueues()
	{
		return g_injected;
	}

	VkPhysicalDevice GetPhysicalDevice()
	{
		return g_physicalDevice;
	}

	VkDevice GetDevice()
	{
		return g_device;
	}
}
