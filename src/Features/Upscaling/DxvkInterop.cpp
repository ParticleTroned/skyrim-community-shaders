#include "DxvkInterop.h"

#include "Globals.h"

namespace SIE
{
	DxvkInterop* DxvkInterop::GetSingleton()
	{
		static DxvkInterop singleton;
		return &singleton;
	}

	bool DxvkInterop::Initialize()
	{
		if (available)
			return true;

		auto d3dDevice = globals::d3d::device;
		if (!d3dDevice) {
			logger::warn("[DxvkInterop] No D3D11 device available yet");
			return false;
		}

		// IDXGIVkInteropDevice is only implemented by DXVK. On native D3D11 this
		// fails cleanly and we simply report unavailable.
		winrt::com_ptr<IDXGIVkInteropDevice> dev;
		if (FAILED(d3dDevice->QueryInterface(__uuidof(IDXGIVkInteropDevice), dev.put_void()))) {
			logger::info("[DxvkInterop] IDXGIVkInteropDevice not present — not running under DXVK");
			return false;
		}

		interopDevice = dev;
		interopDevice->GetVulkanHandles(&instance, &physicalDevice, &device);
		interopDevice->GetSubmissionQueue(&queue, &queueFamilyIndex);

		if (!instance || !physicalDevice || !device || !queue) {
			logger::error("[DxvkInterop] DXVK returned null Vulkan handles");
			interopDevice = nullptr;
			return false;
		}

		// vulkan-1.dll is already loaded in-process by DXVK; grab the loader entry.
		if (HMODULE vk = GetModuleHandleW(L"vulkan-1.dll")) {
			vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
				reinterpret_cast<void*>(GetProcAddress(vk, "vkGetInstanceProcAddr")));
		}
		if (!vkGetInstanceProcAddr) {
			logger::error("[DxvkInterop] Could not resolve vkGetInstanceProcAddr from vulkan-1.dll");
			interopDevice = nullptr;
			return false;
		}

		vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
			vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
		if (!vkGetDeviceProcAddr) {
			logger::error("[DxvkInterop] Could not resolve vkGetDeviceProcAddr");
			interopDevice = nullptr;
			return false;
		}

		// Log the physical device for confirmation (single shared device).
		if (auto pfnProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
				vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"))) {
			VkPhysicalDeviceProperties props{};
			pfnProps(physicalDevice, &props);
			logger::info("[DxvkInterop] Bridged to DXVK Vulkan device: '{}' (API {}.{}.{}), queueFamily {}",
				props.deviceName,
				VK_API_VERSION_MAJOR(props.apiVersion),
				VK_API_VERSION_MINOR(props.apiVersion),
				VK_API_VERSION_PATCH(props.apiVersion),
				queueFamilyIndex);
		} else {
			logger::info("[DxvkInterop] Bridged to DXVK Vulkan device (queueFamily {})", queueFamilyIndex);
		}

		available = true;
		return true;
	}

	bool DxvkInterop::GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
		VkImageLayout* a_outLayout, VkImageCreateInfo* a_outInfo) const
	{
		if (!available || !a_resource)
			return false;

		winrt::com_ptr<IDXGIVkInteropSurface> surface;
		if (FAILED(a_resource->QueryInterface(__uuidof(IDXGIVkInteropSurface), surface.put_void())))
			return false;

		VkImageCreateInfo localInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		VkImageCreateInfo* info = a_outInfo ? a_outInfo : &localInfo;
		info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info->pNext = nullptr;
		info->queueFamilyIndexCount = 0;
		info->pQueueFamilyIndices = nullptr;

		return SUCCEEDED(surface->GetVulkanImageInfo(a_outImage, a_outLayout, info));
	}

	void DxvkInterop::FlushRenderingCommands() const
	{
		if (interopDevice)
			interopDevice->FlushRenderingCommands();
	}

	void DxvkInterop::LockSubmissionQueue() const
	{
		if (interopDevice)
			interopDevice->LockSubmissionQueue();
	}

	void DxvkInterop::ReleaseSubmissionQueue() const
	{
		if (interopDevice)
			interopDevice->ReleaseSubmissionQueue();
	}
}
