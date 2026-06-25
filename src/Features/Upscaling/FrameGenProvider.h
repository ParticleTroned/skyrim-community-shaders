#pragma once

#include <vulkan/vulkan.h>

#include <vector>

// Provider-agnostic frame-generation abstraction.
//
// Frame generation on DXVK requires owning the Vulkan presentation path (both AMD FFX's FG
// swapchain and NVIDIA DLSS-G intercept vkQueuePresentKHR/vkAcquireNextImageKHR), plus extra
// queues (and, for DLSS-G, an optical-flow queue + VK_NV_optical_flow) requested at
// vkCreateDevice. DxvkWsiHook is the SINGLE interposer: it injects each active provider's
// device-creation needs and routes DXVK's five WSI swapchain functions to the active provider.
// FFX FSR3 and (later) DLSS-G are interchangeable IFrameGenProvider backends, so adding DLSS-G
// is a new backend rather than a re-plumbing of the interception layer.
// What a provider needs DXVK's vkCreateDevice to add. The hook aggregates and injects these
// before the real vkCreateDevice; DXVK keeps using its own queues.
struct FrameGenDeviceRequirements
{
	// Additional queues to claim from a present-capable family DISTINCT from DXVK's graphics
	// (game) family. FFX's FG swapchain needs 2 (present + image-acquire).
	uint32_t presentCapableQueues = 0;
	// An exclusive optical-flow queue family (DLSS-G). Not used by FFX.
	bool needsOpticalFlowQueue = false;
	// Device extensions to enable (e.g. VK_NV_optical_flow for DLSS-G).
	std::vector<const char*> deviceExtensions;
	// Optional feature-struct chain to merge into VkDeviceCreateInfo::pNext. Provider-owned;
	// must remain valid through device creation (e.g. VkPhysicalDeviceOpticalFlowFeaturesNV).
	void* featuresPNext = nullptr;
};

// The queues the hook resolved for the provider after device creation.
struct FrameGenQueues
{
	VkQueue gameQueue = VK_NULL_HANDLE;  // DXVK's graphics queue (shared; serialize via submitFunc)
	uint32_t gameFamily = 0;
	VkQueue presentQueue = VK_NULL_HANDLE;  // injected, present-capable, exclusive to the provider
	uint32_t presentFamily = 0;
	VkQueue imageAcquireQueue = VK_NULL_HANDLE;  // injected, exclusive to the provider
	uint32_t imageAcquireFamily = 0;
	VkQueue opticalFlowQueue = VK_NULL_HANDLE;  // injected for DLSS-G (else null)
	uint32_t opticalFlowFamily = 0;
};

// A frame-generation backend that owns DXVK's swapchain while active. The hook calls these.
class IFrameGenProvider
{
public:
	virtual ~IFrameGenProvider() = default;

	// Short name for logging.
	virtual const char* Name() const = 0;

	// Should this provider OWN presentation right now (FG enabled in settings AND supported)?
	// Evaluated at swapchain (re)creation to decide between wrapping and native pass-through, so
	// that disabling FG costs nothing (native DXVK present, no provider overhead).
	virtual bool WantsToWrap() const = 0;

	// --- Device creation (from the vkCreateDevice interception) -------------------------
	virtual FrameGenDeviceRequirements GetDeviceRequirements(VkPhysicalDevice physicalDevice) = 0;
	// Hand back the queues the hook claimed (game = DXVK graphics; the rest = injected).
	virtual void OnDeviceCreated(VkPhysicalDevice physicalDevice, VkDevice device, const FrameGenQueues& queues) = 0;

	// --- Swapchain ownership (from the WSI wrappers, only while WantsToWrap()) -----------
	// Create the wrapped swapchain. On VK_SUCCESS *pSwapchain is the provider's (opaque to DXVK)
	// handle; any non-success makes the hook fall back to a native swapchain (pass-through).
	virtual VkResult CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) = 0;
	virtual VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain,
		uint32_t* pCount, VkImage* pImages) = 0;
	virtual VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
		VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) = 0;
	virtual VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) = 0;
	virtual void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) = 0;
};
