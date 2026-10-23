#define NOMINMAX
#include "Features/VR/ViewRefresh.h"
#include "Features/VR/ViewRefreshPolicy.h"
#include "Utils/DeviceContextState.h"
#include "d3d11_shader_test.h"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <stdexcept>

namespace globals
{
	struct State
	{
		uint32_t frameCount = 40;
		uint64_t generation = 8;
		uint64_t GetCompletedRenderTargetResourcePublicationGeneration() const { return generation; }
	} stateStorage;
	State* state = &stateStorage;
	namespace features
	{
		struct Upscaling
		{
			uint64_t GetHistoryResetRevision() const { return 77; }
		} upscaling;
	}
	namespace d3d
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(77);
		ID3D11DeviceContext* context = nullptr;
	}
}
uint32_t currentThread = 99;
uint32_t ViewRefreshTestThreadId() { return currentThread; }
namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}

namespace Hooks
{
	std::shared_mutex& GetRenderTargetRecreationMutex()
	{
		static std::shared_mutex mutex;
		return mutex;
	}
}
namespace Util
{
	vr::HmdMatrix34_t MatrixToHmdMatrix34(const DirectX::SimpleMath::Matrix& value)
	{
		vr::HmdMatrix34_t pose{};
		pose.m[0][3] = value._41;
		return pose;
	}
}
#define CS_GPU_PASS(name)
namespace CSX::ViewRefresh
{
	using json = nlohmann::json;
	using Clock = std::chrono::steady_clock;
	struct Output
	{
		struct Handle
		{
			void* get() const { return reinterpret_cast<void*>(123); }
		} resource;
	};
	template <class T>
	struct Handle : Microsoft::WRL::ComPtr<T>
	{
		T* get() const { return this->Get(); }
	};
	struct Texture2D
	{
		D3D11_TEXTURE2D_DESC desc{};
		Handle<ID3D11Texture2D> resource;
	};
	struct Runtime
	{
		std::mutex mutex;
		Settings settings;
		std::atomic<bool> serviceRequired{ false }, pendingPair{ false };
		uint64_t revision = 1, poseCycle = 100;
		SceneIdentity source, prepared, pairIdentity;
		ID3D11Device* device = nullptr;
		const char* reason = "Disabled";
		bool pairActive = false, pairRefreshed = false, reused = false;
		bool graphicsFailed = false, retryGraphics = false;
		uint8_t capturedEyes = 0, preparedEyes = 0;
		uint64_t historyRevision = 0;
		bool latePoseSelected = false;
		Clock::time_point latePoseSelectedAt;
		Clock::time_point capturedAt;
		Matrix lateTarget;
		std::unique_ptr<Output> depth;
		winrt::com_ptr<ID3D11DeviceContext1> context;
		winrt::com_ptr<ID3DDeviceContextState> isolatedState;
		StereoReceipt receipt;
		vr::HmdMatrix34_t displayedPose{};
		struct Eye
		{
			std::unique_ptr<Output> output;
			std::unique_ptr<Texture2D> color;
			vr::EColorSpace colorSpace = vr::ColorSpace_Gamma;
		};
		std::array<Eye, 2> eyes;
		uint64_t renderedPairs = 0, reusedPairs = 0, correctedPairs = 0, failedPairs = 0;
	} runtime;
	uint32_t releases = 0;
	void ReleaseGraphics()
	{
		if (runtime.pairActive || runtime.pendingPair.load())
			throw std::runtime_error("Released graphics inside a prepared stereo transaction");
		++releases;
		runtime.source = runtime.prepared = {};
		runtime.device = nullptr;
		runtime.eyes = {};
		runtime.depth.reset();
		runtime.context = nullptr;
		runtime.isolatedState = nullptr;
	}
	bool Eligible() { return runtime.settings.Active(); }
	bool BelongsToCurrentDevice(ID3D11DeviceChild* resource)
	{
		Microsoft::WRL::ComPtr<ID3D11Device> owner;
		resource->GetDevice(owner.GetAddressOf());
		return owner.Get() == globals::d3d::device;
	}
	bool ValidSize(const D3D11_TEXTURE2D_DESC& desc) { return desc.ArraySize == 1 && desc.SampleDesc.Count == 1; }
	std::unique_ptr<Texture2D> MakeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, bool, const char* name)
	{
		auto texture = std::make_unique<Texture2D>();
		auto& desc = texture->desc;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = format;
		D3D11ShaderTest::Check(globals::d3d::device->CreateTexture2D(&desc, nullptr, texture->resource.GetAddressOf()));
		Util::SetResourceName(texture->resource.get(), "%s", name);
		return texture;
	}
	int failEye = -1, expireEye = -1;
	uint32_t poseQueries = 0;
	std::array<Matrix, 2> preparedPoses{};
	bool LatePose(Matrix& target)
	{
		++poseQueries;
		target = Matrix::CreateTranslation(0.001f, 0, 0);
		return true;
	}
	bool EnsureContext();
	bool PrepareEye(uint32_t eye, const Matrix& target)
	{
		preparedPoses[eye] = target;
		if (static_cast<int>(eye) == expireEye)
			runtime.latePoseSelectedAt -= std::chrono::seconds(1);
		return EnsureContext() && static_cast<int>(eye) != failEye;
	}
	void Failure(const char*, const char*)
	{
		runtime.graphicsFailed = true;
		runtime.source.complete = false;
	}
#define GetCurrentThreadId ViewRefreshTestThreadId
#include "view_refresh_identity.h"
#undef GetCurrentThreadId
#include "view_refresh_configure.h"
#include "view_refresh_context.h"
#include "view_refresh_disabled.h"
#include "view_refresh_presentation.h"
#include "view_refresh_reuse.h"
#include "view_refresh_submission.h"
	json Status() { return { { "enabled", GetSettings().enabled } }; }
#include "view_refresh_request.h"
}

namespace
{
	using namespace CSX::ViewRefresh;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	vr::EVRCompositorError RouteSubmission(uint32_t& vendorWork)
	{
		struct
		{
			uint64_t matchedToken = 1;
		} submitBoundaryIdentity;
		const bool nativeRestoreGuardActive = false;
		struct
		{
			bool IsVRPostLoadCompositorHoldActive() const { return false; }
		} upscaling;
		const auto eEye = vr::Eye_Left;
		const auto compositorCycleToken = runtime.poseCycle;
		const auto submit = [](const char*, const vr::Texture_t*, const vr::VRTextureBounds_t*, vr::EVRSubmitFlags) { return vr::VRCompositorError_None; };
#include "view_refresh_submit_route.h"
		++vendorWork;
		return vr::VRCompositorError_None;
	}
	void Prepare(bool refreshed = false, bool reused = false)
	{
		globals::state = &globals::stateStorage;
		globals::stateStorage = {};
		globals::d3d::device = reinterpret_cast<ID3D11Device*>(77);
		currentThread = 99;
		runtime.settings = { true, true, refreshed };
		runtime.revision = 1;
		runtime.poseCycle = 100;
		runtime.source = runtime.prepared = runtime.pairIdentity = CurrentIdentity();
		runtime.device = globals::d3d::device;
		runtime.pairActive = true;
		runtime.pairRefreshed = refreshed;
		runtime.reused = reused;
		runtime.graphicsFailed = runtime.retryGraphics = false;
		runtime.serviceRequired = runtime.pendingPair = true;
		runtime.receipt = { 100 };
		runtime.capturedEyes = runtime.preparedEyes = 3;
		runtime.capturedAt = Clock::now();
		runtime.renderedPairs = runtime.reusedPairs = runtime.correctedPairs = runtime.failedPairs = 0;
		for (auto& eye : runtime.eyes)
			eye.output = std::make_unique<Output>();
		releases = 0;
	}
	void AcceptedEyes(bool refreshed)
	{
		RecordSubmission(vr::Eye_Right, 100, true, refreshed);
		RecordSubmission(vr::Eye_Left, 100, true, refreshed);
	}
	void Completion()
	{
		for (bool refreshed : { false, true }) {
			Prepare(refreshed);
			runtime.eyes[0].colorSpace = runtime.eyes[1].colorSpace = vr::ColorSpace_Linear;
			vr::VRTextureWithPose_t submission{};
			if (refreshed)
				Require(GetSubmission(vr::Eye_Right, 100, submission) && submission.eColorSpace == vr::ColorSpace_Linear,
					"Linear final output was relabeled gamma");
			AcceptedEyes(refreshed);
			EndSubmitPair(100);
			Require(runtime.source.complete && runtime.renderedPairs == 1 && runtime.correctedPairs == (refreshed ? 1u : 0u) && runtime.failedPairs == 0,
				"Actual accepted route did not determine fresh-pair completion");
		}
		Prepare();
		runtime.capturedEyes = 1;
		AcceptedEyes(false);
		EndSubmitPair(100);
		Require(!runtime.source.complete, "A missing final processed eye became reusable history");
		Prepare(true, true);
		AcceptedEyes(true);
		EndSubmitPair(100);
		Require(!runtime.source.complete && runtime.reusedPairs == 1, "A reused image became source history");
		Prepare(true);
		RecordSubmission(vr::Eye_Left, 100, true, true);
		RecordSubmission(vr::Eye_Right, 100, true, false);
		EndSubmitPair(100);
		Require(!runtime.source.complete && runtime.failedPairs == 1 && runtime.correctedPairs == 0,
			"A fallback route counted as a corrected stereo pair");
		for (uint32_t changed = 0; changed < 6; ++changed) {
			Prepare(true);
			AcceptedEyes(true);
			switch (changed) {
			case 0:
				++globals::state->frameCount;
				break;
			case 1:
				++globals::state->generation;
				break;
			case 2:
				++currentThread;
				break;
			case 3:
				++runtime.poseCycle;
				break;
			case 4:
				globals::d3d::device = reinterpret_cast<ID3D11Device*>(78);
				break;
			case 5:
				globals::state = nullptr;
				break;
			}
			vr::VRTextureWithPose_t eye{};
			Require(!GetSubmission(vr::Eye_Left, 100, eye), "Prepared image survived a changed frame owner");
			EndSubmitPair(100);
			Require(!runtime.source.complete && runtime.failedPairs == 1, "Source completion survived a changed frame owner");
		}
	}
	void SettingsAndCleanup()
	{
		Prepare(true);
		Configure(GetSettings());
		Require(runtime.revision == 1 && !runtime.retryGraphics && releases == 0, "Unchanged settings invalidated graphics");
		Require(releases == 0 && !ServiceDisabled(), "Disabled service misclassified active work");
	}
	void DisablePreparedPair()
	{
		Prepare(true);
		Configure({});
		Require(ServiceDisabled() && releases == 0, "Disabling released an in-flight stereo pair");
		vr::VRTextureWithPose_t eye{};
		Require(GetSubmission(vr::Eye_Left, 100, eye) && eye.handle, "A toggle interrupted a prepared eye");
		AcceptedEyes(true);
		EndSubmitPair(100);
		Require(releases == 1 && !runtime.serviceRequired && !runtime.pendingPair && !runtime.source.complete,
			"Disabled resources were retained after the native pair completed");
		Prepare();
		runtime.pairActive = false;
		runtime.pendingPair = false;
		Configure({ true, false, false });
		Require(ServiceDisabled() && releases == 1 && !runtime.serviceRequired, "No selected modes retained graphics");
		Prepare(true);
		runtime.pairActive = false;
		Configure({});
		++globals::state->frameCount;
		Require(ServiceDisabled() && releases == 1 && !runtime.pendingPair,
			"An abandoned prepared pair retained disabled resources");
		Prepare();
		runtime.graphicsFailed = true;
		Configure(GetSettings());
		Require(runtime.retryGraphics && !runtime.graphicsFailed && runtime.revision == 2, "Failed graphics could not be retried");
	}
	void FinalPresentation()
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D11ShaderTest::Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		const auto start = [&](bool late) {
			Prepare(late);
			globals::d3d::device = device.Get();
			globals::d3d::context = context.Get();
			runtime.device = device.Get();
			Require(Util::CreateIsolatedDeviceContextState(device.Get(), context.Get(), runtime.context,
						runtime.isolatedState, "ViewRefreshTest::IsolatedState"),
				"Isolated context creation failed");
			runtime.source = CurrentIdentity();
			runtime.depth = std::make_unique<Output>();
			poseQueries = 0;
			failEye = -1;
			expireEye = -1;
			BeginSubmitPair(100);
		};
		start(true);
		auto combined = MakeTexture(16, 4, DXGI_FORMAT_R8G8B8A8_UNORM, false, "ViewRefreshTest::FinalStereo");
		std::array<uint32_t, 64> pixels;
		for (uint32_t i = 0; i < pixels.size(); ++i) pixels[i] = 0xff000000u | i;
		context->UpdateSubresource(combined->resource.get(), 0, nullptr, pixels.data(), 16 * sizeof(uint32_t), 0);
		vr::Texture_t input{ combined->resource.get(), vr::TextureType_DirectX, vr::ColorSpace_Linear };
		const vr::VRTextureBounds_t bounds[] = { { 0, 0, .5f, 1 }, { .5f, 0, 1, 1 } };
		vr::VRTextureWithPose_t corrected[2]{};
		for (auto eye : { vr::Eye_Right, vr::Eye_Left }) {
			Require(PrepareSubmission(eye, 100, &input, &bounds[eye], vr::Submit_Default, corrected[eye]) == Submission::Refreshed,
				"Completed stereo eye did not reach late correction");
			RecordSubmission(eye, 100, true, true);
		}
		Require(poseQueries == 1 && preparedPoses[0] == preparedPoses[1] && corrected[0].eColorSpace == vr::ColorSpace_Linear && corrected[1].eColorSpace == vr::ColorSpace_Linear,
			"Late eyes did not share one pose and preserve transfer");
		EndSubmitPair(100);
		Require(runtime.source.complete && runtime.capturedEyes == 3, "Final processed stereo was not retained");
		auto desc = runtime.eyes[1].color->desc;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
		D3D11ShaderTest::Check(device->CreateTexture2D(&desc, nullptr, readback.GetAddressOf()));
		Util::SetResourceName(readback.Get(), "ViewRefreshTest::CaptureReadback");
		context->CopyResource(readback.Get(), runtime.eyes[1].color->resource.get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		D3D11ShaderTest::Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		for (uint32_t y = 0; y < 4; ++y) {
			const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
			for (uint32_t x = 0; x < 8; ++x) Require(row[x] == pixels[y * 16 + x + 8], "Final eye copy crossed native stereo bounds");
		}
		context->Unmap(readback.Get(), 0);
		for (bool late : { false, true }) {
			start(late);
			auto finalEye = MakeTexture(8, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "ViewRefreshTest::NeuralOutput");
			input = { finalEye->resource.get(), vr::TextureType_DirectX, vr::ColorSpace_Auto };
			for (auto eye : { vr::Eye_Left, vr::Eye_Right }) {
				const auto route = PrepareSubmission(eye, 100, &input, nullptr, vr::Submit_Default, corrected[eye]);
				Require(route == (late ? Submission::Refreshed : Submission::Original), "Per-eye vendor output was not captured");
				RecordSubmission(eye, 100, true, late);
			}
			EndSubmitPair(100);
			Require(runtime.source.complete && runtime.eyes[0].colorSpace == vr::ColorSpace_Linear, "Float final output lost its color contract");
		}
		input = { combined->resource.get(), vr::TextureType_DirectX, vr::ColorSpace_Gamma };
		for (bool disable : { false, true }) {
			start(true);
			Require(PrepareSubmission(vr::Eye_Left, 100, &input, &bounds[0], vr::Submit_Default, corrected[0]) == Submission::Refreshed,
				"First late eye failed before a configuration change");
			RecordSubmission(vr::Eye_Left, 100, true, true);
			Configure(disable ? Settings{} : Settings{ true, true, false });
			Require(PrepareSubmission(vr::Eye_Right, 100, &input, &bounds[1], vr::Submit_Default, corrected[1]) == Submission::Refreshed && releases == 0,
				"A mid-pair configuration change reset resources or mixed stereo poses");
			RecordSubmission(vr::Eye_Right, 100, true, true);
			EndSubmitPair(100);
			Require(!runtime.source.complete, "Changed settings retained reusable scene history");
			if (disable)
				Require(releases == 1 && !runtime.context, "Disabled pair did not release resources after submission");
			else
				Require(EnsureContext() && releases == 1 && !runtime.retryGraphics, "Deferred graphics retry did not run after the pair");
		}
		for (int expired : { 0, 1 }) {
			start(true);
			expireEye = expired;
			Require(PrepareSubmission(vr::Eye_Left, 100, &input, &bounds[0], vr::Submit_Default, corrected[0]) ==
						(expired == 0 ? Submission::Original : Submission::Refreshed),
				"Expired first pose did not fall back safely");
			RecordSubmission(vr::Eye_Left, 100, true, expired != 0);
			Require(PrepareSubmission(vr::Eye_Right, 100, &input, &bounds[1], vr::Submit_Default, corrected[1]) ==
						(expired == 0 ? Submission::Original : Submission::Reject),
				"Expired peer pose mixed corrected and original eyes");
			EndSubmitPair(100);
			Require(!runtime.source.complete && !runtime.graphicsFailed, "A slow pose created reusable history or latched a graphics failure");
		}
		for (bool missing : { false, true }) {
			start(true);
			Require(PrepareSubmission(vr::Eye_Left, 100, &input, &bounds[0], vr::Submit_Default, corrected[0]) == Submission::Refreshed, "First late eye failed");
			RecordSubmission(vr::Eye_Left, 100, true, true);
			failEye = missing ? -1 : 1;
			Require(PrepareSubmission(vr::Eye_Right, 100, missing ? nullptr : &input, &bounds[1], vr::Submit_Default, corrected[1]) == Submission::Reject,
				"A failed peer mixed original and corrected stereo poses");
			EndSubmitPair(100);
			Require(!runtime.source.complete && runtime.failedPairs == 1, "Failed correction became reusable history");
		}
		start(false);
		Require(PrepareSubmission(vr::Eye_Left, 100, nullptr, nullptr, vr::Submit_Default, corrected[0]) == Submission::Original, "First-eye fallback failed closed");
		Require(PrepareSubmission(vr::Eye_Right, 100, &input, &bounds[1], vr::Submit_Default, corrected[1]) == Submission::Original, "A malformed pair resumed correction");
		EndSubmitPair(100);
		Prepare(true, true);
		uint32_t vendorWork = 0;
		Require(IsReusingScene(), "Prepared reuse did not bypass postprocessing");
		Require(RouteSubmission(vendorWork) == vr::VRCompositorError_None && vendorWork == 0, "Prepared reuse fell through to vendor work");
		Configure({});
		Require(IsReusingScene(), "A mid-pair toggle resumed stale vendor processing");
		++globals::state->generation;
		Require(IsReusingScene() && !GetSubmission(vr::Eye_Left, 100, corrected[0]),
			"Invalidated reuse resumed postprocessing or submitted stale resources");
		Require(RouteSubmission(vendorWork) == vr::VRCompositorError_RequestFailed && vendorWork == 0, "Invalidated reuse fell through the submit hook into vendor work");
		++globals::state->frameCount;
		Require(!IsReusingScene(), "Reuse bypass survived into a fresh frame");
		Prepare(true, true);
		runtime.capturedAt -= std::chrono::seconds(1);
		Require(IsReusingScene() && !GetSubmission(vr::Eye_Left, 100, corrected[0]), "Expired reuse was presented or resumed vendor work");
		Require(RouteSubmission(vendorWork) == vr::VRCompositorError_RequestFailed && vendorWork == 0, "Expired reuse fell through the submit hook into vendor work");
		EndSubmitPair(100);
		Require(!runtime.source.complete && runtime.failedPairs == 1, "Expired reuse counted as a successful pair");
		Prepare();
		Require(RouteSubmission(vendorWork) == vr::VRCompositorError_None && vendorWork == 1, "A fresh frame did not retain its normal vendor route");
	}
	void Requests()
	{
		Prepare();
		const auto original = GetSettings();
		for (const auto& request : { json::array(), json{ { "action", 1 } }, json{ { "action", "configure" }, { "enabled", 1 } },
				 json{ { "action", "status" }, { "enabled", false } }, json{ { "unknown", true } } }) {
			Require(Execute(request).contains("error") && GetSettings() == original, "Malformed request changed settings");
		}
		Require(!Execute({ { "action", "status" } }).contains("error"), "Read-only status request failed");
		REL::Module::vr = false;
		Require(Execute({ { "action", "configure" }, { "enabled", false } }).contains("error") && GetSettings() == original,
			"A flat-runtime request configured VR rendering");
		REL::Module::vr = true;
		Require(!Execute({ { "action", "configure" }, { "latePose", true } }).contains("error") && GetSettings().latePose,
			"Valid VR configuration failed");
	}
}

int main()
{
	try {
		Completion();
		SettingsAndCleanup();
		DisablePreparedPair();
		Requests();
		FinalPresentation();
		std::cout << "View Refresh real lifecycle: identity, routes, toggles, cleanup, retry and requests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
