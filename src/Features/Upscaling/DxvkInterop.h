#pragma once

// Bridge to DXVK's own Vulkan device.
//
// DXVK (the d3d11.dll/dxgi.dll proxy) renders everything on a single VkDevice.
// It exposes that device to D3D11 clients through a small set of COM interop
// interfaces (defined in DXVK's internal dxgi_interfaces.h). We re-declare the
// three we need here so CS can reach DXVK's VkInstance / VkPhysicalDevice /
// VkDevice / VkQueue and map any D3D11 texture to its backing VkImage — WITHOUT
// pulling in DXVK's internal headers and WITHOUT creating a second device.
//
// This is the foundation for running the FidelityFX Vulkan backend (FSR3 frame
// generation) on DXVK's single device with no interop: the VkImages handed to
// FFX are DXVK's own images, and FFX submits onto DXVK's own queue.
//
// The interfaces below are ABI-stable COM interfaces; the GUIDs match DXVK's
// dxgi_interfaces.h exactly. They are queried at runtime from the live device,
// so when running on the real (non-DXVK) D3D11 these QueryInterface calls simply
// fail and IsAvailable() returns false.

#include <d3d11.h>
#include <dxgi1_6.h>
#include <vector>
#include <vulkan/vulkan.h>
#include <winrt/base.h>

struct IDXGIVkInteropDevice;

// Vulkan interop surface — backing VkImage of a D3D11 texture.
MIDL_INTERFACE("5546cf8c-77e7-4341-b05d-8d4d5000e77d")
IDXGIVkInteropSurface : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDXGIVkInteropDevice** ppDevice) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(
		VkImage* pHandle,
		VkImageLayout* pLayout,
		VkImageCreateInfo* pInfo) = 0;
};

// Vulkan interop device — DXVK's VkInstance/PhysicalDevice/Device/Queue + sync.
MIDL_INTERFACE("e2ef5fa5-dc21-4af7-90c4-f67ef6a09323")
IDXGIVkInteropDevice : public IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanHandles(
		VkInstance* pInstance,
		VkPhysicalDevice* pPhysDev,
		VkDevice* pDevice) = 0;
	virtual void STDMETHODCALLTYPE GetSubmissionQueue(
		VkQueue* pQueue,
		uint32_t* pQueueFamilyIndex) = 0;
	virtual void STDMETHODCALLTYPE TransitionSurfaceLayout(
		IDXGIVkInteropSurface* pSurface,
		const VkImageSubresourceRange* pSubresources,
		VkImageLayout OldLayout,
		VkImageLayout NewLayout) = 0;
	virtual void STDMETHODCALLTYPE FlushRenderingCommands() = 0;
	virtual void STDMETHODCALLTYPE LockSubmissionQueue() = 0;
	virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue() = 0;
};

// Vulkan interop factory — the VkInstance + loader entry point used by DXVK.
MIDL_INTERFACE("4c5e1b0d-b0c8-4131-bfd8-9b2476f7f408")
IDXGIVkInteropFactory : public IUnknown
{
	virtual void STDMETHODCALLTYPE GetVulkanInstance(
		VkInstance* pInstance,
		PFN_vkGetInstanceProcAddr* ppfnVkGetInstanceProcAddr) = 0;
};

namespace SIE
{
	/**
	 * @brief Accessor for DXVK's single Vulkan device from the D3D11 proxy.
	 *
	 * Provides the Vulkan handles DXVK already uses (no second device, no DXGI
	 * shared-handle interop) plus helpers to expose CS's D3D11 textures as the
	 * VkImages that back them, and the submission-queue locking DXVK requires
	 * before foreign Vulkan work is submitted onto its queue.
	 */
	class DxvkInterop
	{
	public:
		static DxvkInterop* GetSingleton();

		/**
		 * @brief Query the interop interfaces from the live D3D11 device.
		 * @return true when running under DXVK and all handles resolved.
		 *
		 * Safe to call once after the device exists. On native D3D11 the
		 * QueryInterface fails and this returns false (available() stays false).
		 */
		bool Initialize();

		/** @brief Whether the DXVK Vulkan device was resolved successfully. */
		bool IsAvailable() const { return available; }

		VkInstance GetInstance() const { return instance; }
		VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
		VkDevice GetDevice() const { return device; }
		VkQueue GetQueue() const { return queue; }
		uint32_t GetQueueFamilyIndex() const { return queueFamilyIndex; }
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr() const { return vkGetInstanceProcAddr; }
		PFN_vkGetDeviceProcAddr GetDeviceProcAddr() const { return vkGetDeviceProcAddr; }

		/**
		 * @brief Map a D3D11 resource to its backing VkImage on DXVK's device.
		 * @param a_resource A D3D11 texture created by the DXVK device.
		 * @param a_outImage Receives the VkImage handle (may be null to skip).
		 * @param a_outLayout Receives the image layout after a flush (may be null).
		 * @param a_outInfo Receives the VkImageCreateInfo (sType must be preset; may be null).
		 * @return true on success.
		 */
		bool GetVkImage(ID3D11Resource* a_resource, VkImage* a_outImage,
			VkImageLayout* a_outLayout = nullptr, VkImageCreateInfo* a_outInfo = nullptr) const;

		/** @brief Flush outstanding D3D11 rendering before foreign Vulkan submits. */
		void FlushRenderingCommands() const;
		/** @brief Lock DXVK's submission queue around a foreign Vulkan submit. */
		void LockSubmissionQueue() const;
		/** @brief Release DXVK's submission queue after a foreign Vulkan submit. */
		void ReleaseSubmissionQueue() const;

		IDXGIVkInteropDevice* GetInteropDevice() const { return interopDevice.get(); }

		// --- Command-buffer ring for recording foreign Vulkan (FFX) work ---

		/**
		 * @brief Creates the command pool + per-frame command-buffer/fence ring on
		 *        DXVK's queue family. Required before BeginFrameCommandBuffer().
		 * @param a_framesInFlight Ring depth (number of in-flight command buffers).
		 * @return true on success.
		 */
		bool CreateCommandResources(uint32_t a_framesInFlight = 3);

		/** @brief Destroys the command pool, command buffers and fences. */
		void DestroyCommandResources();

		/** @brief Whether the command ring is ready (CreateCommandResources succeeded). */
		bool CommandResourcesReady() const { return commandPool != VK_NULL_HANDLE; }

		/**
		 * @brief Acquires the next ring command buffer (waits its fence), begins it
		 *        (ONE_TIME_SUBMIT) and returns it for ffxGetCommandListVK.
		 * @return The VkCommandBuffer, or VK_NULL_HANDLE on failure.
		 */
		VkCommandBuffer BeginFrameCommandBuffer();

		/**
		 * @brief Ends + submits the current ring command buffer on DXVK's queue.
		 *        Flushes DXVK's pending D3D11 work first, then submits under the
		 *        submission-queue lock with the ring fence. Optionally blocks until done.
		 * @param a_commandBuffer The buffer returned by BeginFrameCommandBuffer().
		 * @param a_waitIdle If true, waits the fence before returning (needed when the
		 *        result is consumed immediately, e.g. presenting an interpolated frame).
		 * @return true on success.
		 */
		bool SubmitFrameCommandBuffer(VkCommandBuffer a_commandBuffer, bool a_waitIdle = false);

		/**
		 * @brief Transitions an interop image's layout via DXVK's own tracker
		 *        (IDXGIVkInteropDevice::TransitionSurfaceLayout) so DXVK stays synced.
		 *        Never issue a raw vkCmdPipelineBarrier on an interop image.
		 * @param a_resource The D3D11 texture.
		 * @param a_oldLayout Current layout (read it from GetVkImage's pLayout).
		 * @param a_newLayout Desired layout.
		 * @param a_aspect Image aspect (color/depth).
		 */
		void TransitionImageLayout(ID3D11Resource* a_resource, VkImageLayout a_oldLayout,
			VkImageLayout a_newLayout, VkImageAspectFlags a_aspect = VK_IMAGE_ASPECT_COLOR_BIT) const;

		/** @brief Queries the IDXGIVkInteropSurface for a D3D11 resource. */
		bool GetInteropSurface(ID3D11Resource* a_resource, IDXGIVkInteropSurface** a_outSurface) const;

	private:
		DxvkInterop() = default;

		bool available = false;

		winrt::com_ptr<IDXGIVkInteropDevice> interopDevice;

		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;
		uint32_t queueFamilyIndex = 0;

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;

		// Command-buffer ring (recorded on DXVK's queue family, submitted on its queue).
		VkCommandPool commandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> commandBuffers;
		std::vector<VkFence> commandFences;
		uint32_t framesInFlight = 0;
		uint32_t commandFrameIndex = 0;
	};
}
