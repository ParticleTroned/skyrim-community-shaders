#pragma once

#include "FrameGenProvider.h"

#include <vulkan/vulkan.h>

// Externally hooks DXVK's Vulkan WSI (swapchain) entry points WITHOUT modifying the DXVK
// DLL. DXVK resolves every Vulkan function through vkGetInstanceProcAddr -> vkGetDeviceProcAddr
// (it dynamically loads vulkan-1.dll and does a single GetProcAddress for vkGetInstanceProcAddr).
// We inline-detour vulkan-1.dll!vkGetInstanceProcAddr and wrap the proc-addr chain so DXVK
// resolves OUR replacements for the five swapchain device functions:
//   vkCreateSwapchainKHR / vkDestroySwapchainKHR / vkGetSwapchainImagesKHR /
//   vkAcquireNextImageKHR / vkQueuePresentKHR
// This is the hook point used to insert the AMD FidelityFX frame-generation swapchain into
// DXVK's presentation while keeping the stock DXVK DLL.
//
// MILESTONE 1 (current): the wrappers pass straight through to the real driver functions, so
// runtime behaviour is identical to no hook. This validates the interception mechanism and the
// install timing (DXVK must resolve our wrappers, and pass-through must not perturb present).
// The FFX FG-swapchain routing is layered on top of this foundation in a later step.
namespace DxvkWsiHook
{
	// Install the vkGetInstanceProcAddr detour. MUST be called from the
	// D3D11CreateDeviceAndSwapChain hook BEFORE the real call, so it is in place before DXVK
	// creates its VkInstance/VkDevice and resolves its swapchain functions. Idempotent;
	// returns true if the hook is (now) active.
	bool Install();

	// Remove the detour. Best-effort; intended for shutdown.
	void Uninstall();

	// The genuine vkGetDeviceProcAddr, captured the first time DXVK resolves it through our
	// detour (valid only after DXVK has created its VkDevice). Consumers that must reach the
	// REAL swapchain functions (e.g. the FFX VK backend, which itself creates and presents the
	// underlying swapchain) resolve through this rather than the wrapped chain, to avoid
	// recursing back into our wrappers. Returns nullptr before DXVK device creation.
	PFN_vkGetDeviceProcAddr GetRealDeviceProcAddr();

	// True once DXVK has resolved its device proc addr through our hook (i.e. a DXVK VkDevice
	// exists and the WSI wrappers are live).
	bool IsActive();

	// Register the frame-generation provider the interposer delegates to (FFX now, DLSS-G later).
	// The provider drives device-creation augmentation and the five WSI swapchain functions. Call
	// before DXVK creates its device (e.g. when the FFX provider is constructed at plugin load).
	void SetProvider(IFrameGenProvider* provider);
	IFrameGenProvider* GetProvider();

	// Ask DXVK to recreate its swapchain so the wrap decision is re-evaluated (used to switch
	// between native and provider-wrapped presentation when FG is toggled, or once FG becomes
	// ready after the initial native swapchain). Implemented by returning VK_ERROR_OUT_OF_DATE_KHR
	// from the next present, which DXVK handles with its own robust swapchain-recreate path — so no
	// DXGI ResizeBuffers and no back-buffer-reference juggling on the CS side. Thread-safe to set.
	void RequestSwapchainRecreate();

	// True while a swapchain is currently provider-wrapped (FG swapchain active). The present hook
	// uses this to skip the legacy dispatch-only double-present when FFX owns presentation.
	bool IsSwapchainWrapped();

	// Extra queues we injected into DXVK's vkCreateDevice so the FFX VK frame-generation
	// swapchain can be given the THREE distinct queues it requires. DXVK keeps using its own
	// graphics queue as the FFX gameQueue; these supply the present + image-acquire queues
	// (both from a present-capable family distinct from DXVK's graphics family). All
	// VK_NULL_HANDLE if a suitable family was not found (then the FG swapchain is not used and
	// presentation falls back to the dispatch-only path).
	struct InjectedQueues
	{
		VkQueue present = VK_NULL_HANDLE;
		uint32_t presentFamily = 0;
		VkQueue imageAcquire = VK_NULL_HANDLE;
		uint32_t imageAcquireFamily = 0;
	};
	InjectedQueues GetInjectedQueues();

	// The VkPhysicalDevice and VkDevice DXVK created (captured at vkCreateDevice). Needed by the
	// FG-swapchain wiring to query surface present support and create the FFX context.
	VkPhysicalDevice GetPhysicalDevice();
	VkDevice GetDevice();
}
