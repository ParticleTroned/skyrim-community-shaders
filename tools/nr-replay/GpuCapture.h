#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <renderdoc_app.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace NrReplay
{
	/** Explicit, single-sample capture for the offline replay; construct before D3D initialization. */
	class GpuCapture
	{
	public:
		GpuCapture(const std::filesystem::path& renderdocDll, const std::filesystem::path& outputPrefix)
		{
			if (renderdocDll.empty())
				return;
			requested_ = true;
			if (!std::filesystem::is_regular_file(renderdocDll) || outputPrefix.empty() || outputPrefix.filename().empty())
				Fail("RenderDoc capture requires an existing DLL and a nonempty output prefix");
			libraryPath_ = std::filesystem::canonical(renderdocDll);
			outputPrefix_ = std::filesystem::absolute(outputPrefix).lexically_normal();
			std::filesystem::create_directories(outputPrefix_.parent_path());
			HMODULE module = LoadLibraryExW(libraryPath_.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (!module)
				Fail("RenderDoc DLL load failed with Windows error " + std::to_string(GetLastError()));
			// Capture hooks can outlive local D3D owners; retain the injected library until process exit.
			HMODULE pinned = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
					reinterpret_cast<LPCWSTR>(module), &pinned))
				Fail("RenderDoc DLL pin failed with Windows error " + std::to_string(GetLastError()));
			std::vector<wchar_t> loadedPath(32768);
			const auto loadedLength = GetModuleFileNameW(module, loadedPath.data(), static_cast<DWORD>(loadedPath.size()));
			if (!loadedLength || loadedLength >= loadedPath.size() ||
				!std::filesystem::equivalent(libraryPath_, std::filesystem::path(loadedPath.data())))
				Fail("loaded RenderDoc DLL does not match the requested file");
			const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
			if (!getApi || getApi(eRENDERDOC_API_Version_1_5_0, reinterpret_cast<void**>(&api_)) != 1 || !api_)
				Fail("RenderDoc API 1.5 is unavailable");
			api_->GetAPIVersion(&apiMajor_, &apiMinor_, &apiPatch_);
			api_->SetCaptureKeys(nullptr, 0);
			api_->SetFocusToggleKeys(nullptr, 0);
			api_->MaskOverlayBits(0, 0);
			const auto prefix = Utf8(outputPrefix_);
			api_->SetCaptureFilePathTemplate(prefix.c_str());
		}

		GpuCapture(const GpuCapture&) = delete;
		GpuCapture& operator=(const GpuCapture&) = delete;

		~GpuCapture() noexcept
		{
			if (active_) {
				try {
					End();
				} catch (const std::exception& error) {
					std::fprintf(stderr, "RenderDoc capture finalization failed during cleanup: %s\n", error.what());
				} catch (...) {
					std::fputs("RenderDoc capture finalization failed during cleanup: unknown exception\n", stderr);
				}
			}
		}

		[[nodiscard]] bool Enabled() const noexcept { return requested_; }

		/** Starts one headless capture on the explicitly selected replay device. */
		void Begin(ID3D12Device* device)
		{
			if (!requested_)
				return;
			if (!api_ || !device || attempted_ || api_->IsFrameCapturing())
				Fail("RenderDoc capture cannot start: missing device/API, repeated capture, or another capture is active");
			device_ = device;
			initialCount_ = api_->GetNumCaptures();
			attempted_ = true;
			api_->StartFrameCapture(device_.Get(), nullptr);
			active_ = api_->IsFrameCapturing() != 0;
			if (!active_)
				Fail("RenderDoc did not start a capture for the replay device");
		}

		/** Finalizes after GPU retirement and verifies the capture file exists. */
		void End()
		{
			if (!requested_)
				return;
			if (!active_)
				Fail("RenderDoc capture is not active");
			auto device = std::move(device_);
			active_ = false;
			if (!api_->EndFrameCapture(device.Get(), nullptr))
				Fail("RenderDoc capture finalization failed");
			if (api_->GetNumCaptures() != initialCount_ + 1)
				Fail("RenderDoc did not publish exactly one captured sample");
			std::uint32_t length = 0;
			if (!api_->GetCapture(initialCount_, nullptr, &length, &timestamp_) || !length || length > 65536)
				Fail("RenderDoc capture path is unavailable or exceeds its size limit");
			std::vector<char> path(static_cast<std::size_t>(length) + 1, '\0');
			if (!api_->GetCapture(initialCount_, path.data(), &length, &timestamp_))
				Fail("RenderDoc capture path lookup failed");
			capturePath_ = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data())));
			if (!std::filesystem::is_regular_file(capturePath_) || std::filesystem::file_size(capturePath_) == 0)
				Fail("RenderDoc reported a missing or empty capture file");
			captured_ = true;
		}

		/** Reports capture evidence separately from timings affected by capture instrumentation. */
		[[nodiscard]] nlohmann::json Report() const
		{
			return {
				{ "requested", requested_ },
				{ "attempted", attempted_ },
				{ "captured", captured_ },
				{ "state", !requested_ ? "not_requested" : !error_.empty() ? "failed" :
													   captured_           ? "captured" :
													   active_             ? "capturing" :
																			 "ready" },
				{ "library", Utf8(libraryPath_) },
				{ "apiVersion", { apiMajor_, apiMinor_, apiPatch_ } },
				{ "outputPrefix", Utf8(outputPrefix_) },
				{ "capturePath", Utf8(capturePath_) },
				{ "captureTimestamp", timestamp_ },
				{ "error", error_ },
				{ "timingsInstrumented", requested_ },
			};
		}

	private:
		[[noreturn]] void Fail(std::string message)
		{
			error_ = std::move(message);
			throw std::runtime_error(error_);
		}

		static std::string Utf8(const std::filesystem::path& path)
		{
			const auto value = path.u8string();
			return { reinterpret_cast<const char*>(value.data()), value.size() };
		}

		RENDERDOC_API_1_5_0* api_ = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Device> device_;
		std::filesystem::path libraryPath_, outputPrefix_, capturePath_;
		std::string error_;
		int apiMajor_ = 0, apiMinor_ = 0, apiPatch_ = 0;
		std::uint32_t initialCount_ = 0;
		std::uint64_t timestamp_ = 0;
		bool requested_ = false, attempted_ = false, active_ = false, captured_ = false;
	};
}
