#pragma once

#include <Windows.h>
#include <filesystem>
#include <nvsdk_ngx.h>
#include <string>

namespace NeuralRendering
{
	/** Owns the optional provider route while SDK parameter maps retain SDK ownership. */
	class DirectProvider
	{
	public:
		DirectProvider() = default;
		DirectProvider(const DirectProvider&) = delete;
		DirectProvider& operator=(const DirectProvider&) = delete;
		~DirectProvider();

		/** Opens only the admitted file, checks loaded identity, and calls its exported initializer. */
		NVSDK_NGX_Result Initialize(const std::filesystem::path&, HANDLE verifiedFile,
			ID3D12Device*, const std::filesystem::path& dataPath, NVSDK_NGX_Parameter* capabilities,
			std::string& detail);
		/** Retires provider device state before SDK maps are destroyed. Failure retains ownership. */
		NVSDK_NGX_Result ShutdownDevice(ID3D12Device*);
		/** Restores the provider-local import before releasing the module reference. */
		bool Unload();
		[[nodiscard]] bool Active() const noexcept { return initialized_; }

		using ScratchSize = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, size_t*);
		using Create = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, const NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
		using Evaluate = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
		using Release = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);

		ScratchSize scratchSize = &NVSDK_NGX_D3D12_GetScratchBufferSize;
		Create create = [](ID3D12GraphicsCommandList* list, NVSDK_NGX_Feature feature, const NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle) {
			return NVSDK_NGX_D3D12_CreateFeature(list, feature, const_cast<NVSDK_NGX_Parameter*>(parameters), handle);
		};
		Evaluate evaluate = &NVSDK_NGX_D3D12_EvaluateFeature;
		Release release = &NVSDK_NGX_D3D12_ReleaseFeature;

	private:
		HMODULE module_ = nullptr;
		using Shutdown = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
		Shutdown shutdown_ = nullptr;
		bool initialized_ = false;
	};
}
