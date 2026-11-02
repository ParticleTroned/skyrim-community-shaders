#pragma once

#include <Windows.h>
#include <array>
#include <filesystem>
#include <nvsdk_ngx.h>
#include <string>

namespace NeuralRendering
{
	/** Owns the provider and its retained parameter allocator through complete retirement. */
	class DirectProvider
	{
	public:
		DirectProvider() = default;
		DirectProvider(const DirectProvider&) = delete;
		DirectProvider& operator=(const DirectProvider&) = delete;
		~DirectProvider();

		/** Opens only the admitted file, checks loaded identity, and calls its exported initializer. */
		NVSDK_NGX_Result Initialize(const std::filesystem::path&, HANDLE verifiedFile,
			ID3D12Device*, const std::filesystem::path& dataPath, std::string& detail);
		/** Allocates through the admitted provider or a verified, already loaded driver core. */
		NVSDK_NGX_Result AllocateParameters(NVSDK_NGX_Parameter**, std::string& detail);
		/** Destroys through the same allocator; a failed destruction retains ownership. */
		NVSDK_NGX_Result DestroyParameters(NVSDK_NGX_Parameter*);
		/** Restores provider defaults and callbacks after resetting a parameter map. */
		NVSDK_NGX_Result PopulateParameters(NVSDK_NGX_Parameter*);
		/** Retires provider device state after all parameter maps have been destroyed. */
		NVSDK_NGX_Result ShutdownDevice(ID3D12Device*);
		/** Restores the provider-local import before releasing the module reference. */
		bool Unload();
		[[nodiscard]] bool Active() const noexcept { return initialized_; }
		/** Whether the native initializer was reached after the latest binding checks. */
		[[nodiscard]] bool InitializationAttempted() const noexcept { return initializationAttempted_; }

		using ScratchSize = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, size_t*);
		using Create = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
		using Evaluate = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
		using Release = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);

		ScratchSize scratchSize = nullptr;
		Create create = nullptr;
		Evaluate evaluate = nullptr;
		Release release = nullptr;

	private:
		HMODULE module_ = nullptr;
		HMODULE parameterCore_ = nullptr;
		HANDLE parameterCoreFile_ = INVALID_HANDLE_VALUE;
		using Allocate = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
		using Destroy = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
		using Populate = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
		Allocate allocate_ = nullptr;
		Destroy destroy_ = nullptr;
		Populate populate_ = nullptr;
		// Capability callbacks outlive resets of the reusable feature parameter map.
		std::array<NVSDK_NGX_Parameter*, 2> parameters_{};
		bool BindParameterCore(std::string& detail);
		using Shutdown = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
		Shutdown shutdown_ = nullptr;
		bool initialized_ = false;
		bool initializationAttempted_ = false;
	};
}
