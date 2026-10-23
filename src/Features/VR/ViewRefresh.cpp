#include "ViewRefresh.h"

#include "Buffer.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Globals.h"
#include "GpuPass.h"
#include "Hooks.h"
#include "OpenVRDetection.h"
#include "State.h"
#include "Utils/DeviceContextState.h"
#include "Utils/LazyShader.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"
#include "ViewRefreshPolicy.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace CSX::ViewRefresh
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using DirectX::SimpleMath::Vector2;
		struct alignas(16) Constants
		{
			EyeTransform transform;
			uint32_t width, height, eye, reverseDepth;
			Vector2 depthUvOffset;
			Vector2 padding{};
		};
		static_assert(sizeof(Constants) == 288);

		struct EyeResources
		{
			std::unique_ptr<Texture2D> distance;
			std::unique_ptr<Texture2D> owner;
			std::unique_ptr<Texture2D> output;
		};

		struct Runtime
		{
			std::mutex mutex;
			Settings settings;
			std::atomic<bool> serviceRequired{ false };
			std::atomic<bool> pendingPair{ false };
			uint64_t revision = 1;
			const char* reason = "Disabled";
			SceneIdentity source;
			SceneIdentity prepared;
			SceneIdentity pairIdentity;
			uint64_t poseCycle = 0;
			Matrix renderPose;
			bool poseValid = false;
			vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseStanding;
			Clock::time_point capturedAt;
			Clock::time_point poseReceivedAt;
			std::array<Matrix, 2> projection;
			std::array<Vector2, 2> depthUvOffset;
			Vector3 playerPosition;
			Vector3 playerAngles;
			float worldScale = 0;
			Matrix sourcePose;
			std::unique_ptr<Texture2D> depth;
			std::unique_ptr<Texture2D> color;
			std::array<EyeResources, 2> eyes;
			std::unique_ptr<ConstantBuffer> constants;
			Util::LazyShader<ID3D11ComputeShader> selectDepth, selectSource, resolveColor;
			winrt::com_ptr<ID3D11DeviceContext1> context;
			winrt::com_ptr<ID3DDeviceContextState> isolatedState;
			ID3D11Device* device = nullptr;
			vr::HmdMatrix34_t displayedPose{};
			bool reused = false;
			bool pairActive = false;
			bool pairRefreshed = false;
			bool graphicsFailed = false;
			bool retryGraphics = false;
			StereoReceipt receipt;
			uint64_t renderedPairs = 0, reusedPairs = 0, correctedPairs = 0, failedPairs = 0;
		};
		Runtime runtime;
		void Failure(const char* operation, const char* message);

		SceneIdentity CurrentIdentity()
		{
			if (!globals::state)
				return {};
			return { globals::state->frameCount, runtime.poseCycle,
				globals::state->GetCompletedRenderTargetResourcePublicationGeneration(),
				runtime.revision, GetCurrentThreadId(), false, reinterpret_cast<uintptr_t>(globals::d3d::device) };
		}

		bool SupportsLatePose()
		{
			return globals::features::vr.openVRInfo.runtimeType == VRDetection::RuntimeType::SteamVR;
		}

		bool EngineTaaActive()
		{
			const auto method = globals::features::upscaling.GetUpscaleMethod();
			if (method == Upscaling::UpscaleMethod::kTAA)
				return true;
			if (method != Upscaling::UpscaleMethod::kNONE)
				return false;
			auto* preferences = globals::game::iniPrefSettingCollection;
			auto* setting = preferences ? preferences->GetSetting("bUseTAA:Display") : nullptr;
			return !setting || setting->GetBool();
		}

		bool Eligible(bool forReuse = false)
		{
			if (!globals::game::isVR || !runtime.settings.Active() || !globals::state ||
				!globals::d3d::device || !globals::d3d::context) {
				runtime.reason = "Disabled";
				return false;
			}
			const auto* state = globals::state;
			if (runtime.graphicsFailed)
				return false;
			auto* ui = globals::game::ui;
			if (!ui || ui->GameIsPaused() || state->IsMainOrLoadingMenuOpen() || state->isMapMenuOpen ||
				ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME) ||
				globals::features::vr.ShouldPresentOverlayInHeadset()) {
				runtime.reason = "Menu or overlay needs fresh rendering";
				return false;
			}
			auto& upscaling = globals::features::upscaling;
			const auto& plan = upscaling.GetRuntimeResolutionPlan();
			if (plan.menuContextActive || plan.loadingMenuActive) {
				runtime.reason = "Menu presentation requires fresh rendering";
				return false;
			}
			if (upscaling.IsVRRenderScaleModeLatched() || upscaling.IsPresentationUpscalingActive() ||
				upscaling.IsVRPostLoadCompositorHoldActive() || upscaling.IsVRInitialLoadPresentationProtectionActive() ||
				upscaling.GetVRNativeRestorePresentationGuardActiveEpoch() != 0 || upscaling.IsNeuralRenderingRequested()) {
				runtime.reason = "Requires stable native-resolution presentation";
				return false;
			}
			// Engine TAA has no public skipped-frame history reset contract.
			const bool canReuse = runtime.settings.reuseScenes && !EngineTaaActive();
			if (forReuse && !runtime.settings.reuseScenes)
				return false;
			if ((forReuse && !canReuse) || (!canReuse && !(runtime.settings.latePose && SupportsLatePose()))) {
				runtime.reason = runtime.settings.reuseScenes ? "Scene Reuse requires engine TAA to be off" : "Late Pose Correction requires SteamVR";
				return false;
			}
			if (!runtime.poseValid || runtime.poseCycle == 0 ||
				state->GetRenderTargetResourcePublicationGeneration() != state->GetCompletedRenderTargetResourcePublicationGeneration()) {
				runtime.reason = "Waiting for a valid pose and stable render targets";
				return false;
			}
			return true;
		}

		void ReleaseGraphics()
		{
			runtime.source = {};
			runtime.prepared = {};
			runtime.pairIdentity = {};
			runtime.reused = runtime.pairActive = runtime.pairRefreshed = false;
			runtime.pendingPair.store(false, std::memory_order_release);
			runtime.depth.reset();
			runtime.color.reset();
			runtime.eyes = {};
			runtime.constants.reset();
			runtime.selectDepth.Reset();
			runtime.selectSource.Reset();
			runtime.resolveColor.Reset();
			runtime.context = nullptr;
			runtime.isolatedState = nullptr;
			runtime.device = nullptr;
		}

		bool ServiceDisabled()
		{
			if (runtime.settings.Active())
				return false;
			if (!runtime.pairActive && !SameFrame(runtime.prepared, CurrentIdentity()))
				runtime.pendingPair.store(false, std::memory_order_release);
			if (!runtime.pendingPair.load(std::memory_order_acquire)) {
				ReleaseGraphics();
				runtime.retryGraphics = false;
				runtime.serviceRequired.store(false, std::memory_order_release);
			}
			return true;
		}

		bool EnsureContext()
		{
			if (runtime.device != globals::d3d::device || runtime.retryGraphics) {
				ReleaseGraphics();
				runtime.retryGraphics = false;
			}
			if (!runtime.context && !Util::CreateIsolatedDeviceContextState(globals::d3d::device, globals::d3d::context,
										runtime.context, runtime.isolatedState, "ViewRefresh::IsolatedState")) {
				Failure("context initialization", "D3D11 isolated context state is unavailable");
				return false;
			}
			runtime.device = globals::d3d::device;
			return true;
		}

		bool ValidSize(const D3D11_TEXTURE2D_DESC& desc)
		{
			return desc.Width >= 2 && desc.Width <= 8192 && desc.Width % 2 == 0 &&
			       desc.Height >= 1 && desc.Height <= 4096 && desc.ArraySize == 1 &&
			       desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
		}

		bool BelongsToCurrentDevice(ID3D11DeviceChild* resource)
		{
			winrt::com_ptr<ID3D11Device> device;
			if (resource)
				resource->GetDevice(device.put());
			return device && device.get() == globals::d3d::device;
		}

		std::unique_ptr<Texture2D> MakeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, bool writable, const char* name)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			auto result = std::make_unique<Texture2D>(desc, name);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			result->CreateSRV(srv);
			if (writable) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				result->CreateUAV(uav);
			}
			return result;
		}

		bool EnsureOutputs()
		{
			if (!EnsureContext() || !runtime.color || !runtime.depth)
				return false;
			if (!runtime.selectDepth.Get(L"Data/Shaders/VR/ViewRefresh.hlsl", {}, "cs_5_0", "SelectDepth", "ViewRefresh::SelectDepth") ||
				!runtime.selectSource.Get(L"Data/Shaders/VR/ViewRefresh.hlsl", {}, "cs_5_0", "SelectSource", "ViewRefresh::SelectSource") ||
				!runtime.resolveColor.Get(L"Data/Shaders/VR/ViewRefresh.hlsl", {}, "cs_5_0", "ResolveColor", "ViewRefresh::ResolveColor")) {
				Failure("shader preparation", "A required compute shader could not be compiled");
				return false;
			}
			const auto width = runtime.color->desc.Width / 2;
			const auto height = runtime.color->desc.Height;
			for (uint32_t eye = 0; eye < 2; ++eye) {
				auto& resources = runtime.eyes[eye];
				if (resources.output && resources.output->desc.Width == width && resources.output->desc.Height == height)
					continue;
				EyeResources candidate;
				candidate.distance = MakeTexture(width, height, DXGI_FORMAT_R32_UINT, true, eye ? "ViewRefresh::RightDistance" : "ViewRefresh::LeftDistance");
				candidate.owner = MakeTexture(width, height, DXGI_FORMAT_R32_UINT, true, eye ? "ViewRefresh::RightOwner" : "ViewRefresh::LeftOwner");
				candidate.output = MakeTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true, eye ? "ViewRefresh::RightColor" : "ViewRefresh::LeftColor");
				resources = std::move(candidate);
			}
			if (!runtime.constants) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(Constants);
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				runtime.constants = std::make_unique<ConstantBuffer>(desc, "ViewRefresh::Constants");
			}
			return true;
		}

		bool PreparePair(const Matrix& target, uint64_t cycle)
		{
			Util::OpenVRContext openvr;
			if (!openvr.IsValid() || !runtime.color || !runtime.depth || !BoundedMotion(runtime.sourcePose, target))
				return false;
			std::array<Constants, 2> data{};
			for (uint32_t eye = 0; eye < 2; ++eye) {
				auto& cb = data[eye];
				const auto eyeToHead = Util::HmdMatrix34ToMatrix(openvr.system->GetEyeToHeadTransform(static_cast<vr::EVREye>(eye)));
				if (!BuildEyeTransform(runtime.projection[eye], eyeToHead, runtime.sourcePose, target, runtime.worldScale, cb.transform))
					return false;
				cb.width = runtime.color->desc.Width / 2;
				cb.height = runtime.color->desc.Height;
				cb.eye = eye;
				cb.reverseDepth = runtime.projection[eye]._43 > 0;
				cb.depthUvOffset = runtime.depthUvOffset[eye];
			}
			if (!EnsureOutputs())
				return false;
			// Shader compilation or allocation may outlive the admission checked by the caller.
			if (std::chrono::duration<double>(Clock::now() - runtime.capturedAt).count() > MaximumSceneAgeSeconds)
				return false;
			CS_GPU_PASS("ViewRefresh::ReprojectStereo");
			Util::ScopedDeviceContextState isolated(runtime.context.get(), runtime.isolatedState.get());
			if (!isolated)
				return false;
			auto* context = globals::d3d::context;
			ID3D11ShaderResourceView* inputs[] = { runtime.color->srv.get(), runtime.depth->srv.get() };
			context->CSSetShaderResources(0, 2, inputs);
			auto* cb = runtime.constants->CB();
			context->CSSetConstantBuffers(0, 1, &cb);
			constexpr UINT empty[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
			for (uint32_t eye = 0; eye < 2; ++eye) {
				auto& resources = runtime.eyes[eye];
				runtime.constants->Update(data[eye]);
				context->ClearUnorderedAccessViewUint(resources.distance->uav.get(), empty);
				context->ClearUnorderedAccessViewUint(resources.owner->uav.get(), empty);
				ID3D11UnorderedAccessView* outputs[] = { resources.distance->uav.get(), resources.owner->uav.get(), resources.output->uav.get() };
				context->CSSetUnorderedAccessViews(0, 3, outputs, nullptr);
				for (auto* shader : { runtime.selectDepth.get(), runtime.selectSource.get(), runtime.resolveColor.get() }) {
					context->CSSetShader(shader, nullptr, 0);
					context->Dispatch((data[eye].width + 7) / 8, (data[eye].height + 7) / 8, 1);
				}
			}
			ID3D11UnorderedAccessView* noOutputs[3]{};
			ID3D11ShaderResourceView* noInputs[2]{};
			context->CSSetUnorderedAccessViews(0, 3, noOutputs, nullptr);
			context->CSSetShaderResources(0, 2, noInputs);
			if (FAILED(globals::d3d::device->GetDeviceRemovedReason())) {
				Failure("reprojection", "The D3D11 device is unavailable");
				return false;
			}
			runtime.displayedPose = Util::MatrixToHmdMatrix34(target);
			runtime.prepared = CurrentIdentity();
			runtime.prepared.cycle = cycle;
			runtime.pendingPair.store(true, std::memory_order_release);
			return true;
		}

		bool LatePose(Matrix& target)
		{
			// OpenComposite does not guarantee this query; its reuse path uses the game's pose wait.
			Util::OpenVRContext openvr;
			if (!SupportsLatePose() || !openvr.IsValid())
				return false;
			float sinceVsync = 0;
			uint64_t frame = 0;
			vr::ETrackedPropertyError error = vr::TrackedProp_Success;
			const float frequency = openvr.system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error);
			if (error != vr::TrackedProp_Success || !std::isfinite(frequency) || frequency < 30 || frequency > 240 ||
				!openvr.system->GetTimeSinceLastVsync(&sinceVsync, &frame) || !std::isfinite(sinceVsync) || sinceVsync < 0)
				return false;
			const float photons = openvr.system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float, &error);
			const float prediction = 2.0f / frequency - sinceVsync + photons;
			if (error != vr::TrackedProp_Success || !std::isfinite(prediction) || prediction < 0 || prediction > 0.04f)
				return false;
			vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
			openvr.system->GetDeviceToAbsoluteTrackingPose(runtime.origin, prediction, poses, vr::k_unMaxTrackedDeviceCount);
			const auto& pose = poses[vr::k_unTrackedDeviceIndex_Hmd];
			if (!pose.bPoseIsValid || !pose.bDeviceIsConnected || pose.eTrackingResult != vr::TrackingResult_Running_OK)
				return false;
			target = Util::HmdMatrix34ToMatrix(pose.mDeviceToAbsoluteTracking);
			return BoundedMotion(runtime.sourcePose, target);
		}

		void Failure(const char* operation, const char* message)
		{
			runtime.source.complete = false;
			runtime.graphicsFailed = true;
			runtime.reason = "Graphics preparation failed; rendering fresh scenes";
			logger::warn("View Refresh {}: {}", operation, message);
		}
	}

	void Configure(Settings settings)
	{
		const std::scoped_lock lock(runtime.mutex);
		if (runtime.settings == settings && !runtime.graphicsFailed)
			return;
		runtime.settings = settings;
		++runtime.revision;
		runtime.source.complete = false;
		runtime.graphicsFailed = false;
		runtime.retryGraphics = true;
		const bool active = settings.Active();
		runtime.serviceRequired.store(active || runtime.device || runtime.pendingPair.load(std::memory_order_acquire), std::memory_order_release);
		runtime.reason = active ? "Waiting for a complete rendered stereo scene" : "Disabled";
	}

	Settings GetSettings()
	{
		const std::scoped_lock lock(runtime.mutex);
		return runtime.settings;
	}

	void Load(const nlohmann::json& settings)
	{
		Settings loaded;
		if (const auto found = settings.find("ViewRefresh"); found != settings.end() && found->is_object()) {
			const auto read = [&](const char* name, bool fallback) {
				const auto value = found->find(name);
				return value != found->end() && value->is_boolean() ? value->get<bool>() : fallback;
			};
			loaded = { read("Enabled", false), read("SceneReuse", true), read("LatePose", false) };
		}
		Configure(loaded);
	}

	void Save(nlohmann::json& settings)
	{
		const auto current = GetSettings();
		settings["ViewRefresh"] = { { "Enabled", current.enabled }, { "SceneReuse", current.reuseScenes }, { "LatePose", current.latePose } };
	}

	void OnRenderPose(uint64_t cycle, vr::IVRCompositor* compositor, const vr::TrackedDevicePose_t* pose)
	{
		if (!runtime.serviceRequired.load(std::memory_order_acquire))
			return;
		const std::scoped_lock lock(runtime.mutex);
		runtime.poseCycle = cycle;
		runtime.poseReceivedAt = Clock::now();
		runtime.poseValid = pose && pose->bPoseIsValid && pose->bDeviceIsConnected && pose->eTrackingResult == vr::TrackingResult_Running_OK;
		if (!runtime.poseValid || !compositor) {
			runtime.source.complete = false;
			return;
		}
		const auto origin = compositor->GetTrackingSpace();
		if (origin != runtime.origin)
			runtime.source.complete = false;
		runtime.origin = origin;
		runtime.renderPose = Util::HmdMatrix34ToMatrix(pose->mDeviceToAbsoluteTracking);
		runtime.poseValid = RigidPose(runtime.renderPose);
	}

	bool TryReuseWorld()
	{
		if (!runtime.serviceRequired.load(std::memory_order_acquire) || !globals::game::isVR)
			return false;
		if (!globals::features::vr.InstallSubmitHook())
			return false;
		const std::shared_lock resources(Hooks::GetRenderTargetRecreationMutex());
		const std::scoped_lock lock(runtime.mutex);
		if (ServiceDisabled() || runtime.pairActive)
			return false;
		if (runtime.reused && SameFrame(runtime.prepared, CurrentIdentity()))
			return true;
		runtime.pendingPair.store(false, std::memory_order_release);
		runtime.prepared = {};
		runtime.reused = false;
		if (!Eligible(true) || !runtime.settings.reuseScenes || !CanReuse(runtime.source, CurrentIdentity(), std::chrono::duration<double>(Clock::now() - runtime.capturedAt).count()))
			return false;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player)
			return false;
		const auto position = player->GetPosition();
		const auto angles = player->GetAngle();
		if (!StationaryPlayer(runtime.playerPosition, runtime.playerAngles,
				Vector3(position.x, position.y, position.z), Vector3(angles.x, angles.y, angles.z))) {
			runtime.reason = "Locomotion requires a fresh scene";
			return false;
		}
		try {
			if (!PreparePair(runtime.renderPose, runtime.poseCycle)) {
				if (!runtime.graphicsFailed)
					runtime.reason = "Pose or graphics inputs require a fresh scene";
				return false;
			}
			runtime.reused = true;
			runtime.reason = "Reusing one scene with current stereo tracking";
			globals::features::upscaling.RequestHistoryReset();
			return true;
		} catch (const std::exception& error) {
			Failure("scene preparation", error.what());
			return false;
		}
	}

	void CaptureWorld()
	{
		if (!runtime.serviceRequired.load(std::memory_order_acquire) || !globals::game::isVR)
			return;
		const std::shared_lock resources(Hooks::GetRenderTargetRecreationMutex());
		const std::scoped_lock lock(runtime.mutex);
		if (ServiceDisabled() || runtime.pairActive)
			return;
		runtime.source = {};
		if (!Eligible())
			return;
		try {
			if (!EnsureContext())
				return;
			auto* depthView = Util::GetCurrentSceneDepthSRV();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* scaleSetting = globals::game::iniSettingCollection ? globals::game::iniSettingCollection->GetSetting("fVrScale:VR") : nullptr;
			if (!depthView || !player || !scaleSetting)
				return;
			const float scale = scaleSetting->GetFloat();
			if (!std::isfinite(scale) || scale < 1 || scale > 1000)
				return;
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			depthView->GetDesc(&viewDesc);
			if (viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || viewDesc.Texture2D.MostDetailedMip != 0)
				return;
			winrt::com_ptr<ID3D11Resource> resource;
			depthView->GetResource(resource.put());
			auto depthTexture = resource.try_as<ID3D11Texture2D>();
			if (!depthTexture || !BelongsToCurrentDevice(depthTexture.get()))
				return;
			D3D11_TEXTURE2D_DESC desc{};
			depthTexture->GetDesc(&desc);
			if (!ValidSize(desc))
				return;
			const auto activeSize = Util::ConvertToDynamic(globals::state->screenSize);
			if (activeSize.x != static_cast<float>(desc.Width) || activeSize.y != static_cast<float>(desc.Height)) {
				runtime.reason = "Dynamic resolution requires a fresh scene";
				return;
			}
			const auto& camera = globals::game::frameBufferCached;
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const auto projection = camera.GetCameraProjUnjittered(eye).Transpose();
				const auto jittered = camera.GetCameraProj(eye).Transpose();
				if (!Finite(projection) || !Finite(jittered) || std::abs(projection._34) < 0.99f)
					return;
				runtime.projection[eye] = projection;
				runtime.depthUvOffset[eye] = { (jittered._31 - projection._31) / projection._34 * 0.5f,
					(jittered._32 - projection._32) / projection._34 * -0.5f };
				const auto offset = runtime.depthUvOffset[eye];
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || std::abs(offset.x) > 1 || std::abs(offset.y) > 1)
					return;
			}
			if (!runtime.depth || runtime.depth->desc.Width != desc.Width || runtime.depth->desc.Height != desc.Height || runtime.depth->desc.Format != desc.Format) {
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.CPUAccessFlags = desc.MiscFlags = 0;
				auto candidate = std::make_unique<Texture2D>(desc, "ViewRefresh::SceneDepth");
				candidate->CreateSRV(viewDesc);
				runtime.depth = std::move(candidate);
			}
			CS_GPU_PASS("ViewRefresh::CaptureDepth");
			Util::ScopedDeviceContextState isolated(runtime.context.get(), runtime.isolatedState.get());
			globals::d3d::context->CopyResource(runtime.depth->resource.get(), depthTexture.get());
			runtime.source = CurrentIdentity();
			runtime.sourcePose = runtime.renderPose;
			runtime.worldScale = scale;
			runtime.capturedAt = runtime.poseReceivedAt;
			const auto position = player->GetPosition();
			const auto angles = player->GetAngle();
			runtime.playerPosition = { position.x, position.y, position.z };
			runtime.playerAngles = { angles.x, angles.y, angles.z };
			if (!StationaryPlayer(runtime.playerPosition, runtime.playerAngles, runtime.playerPosition, runtime.playerAngles))
				runtime.source = {};
		} catch (const std::exception& error) {
			Failure("world capture", error.what());
		}
	}

	void BeginSubmitPair(ID3D11Texture2D* color, uint64_t cycle)
	{
		if (!runtime.serviceRequired.load(std::memory_order_acquire) && !runtime.pendingPair.load(std::memory_order_acquire))
			return;
		const std::shared_lock resources(Hooks::GetRenderTargetRecreationMutex());
		const std::scoped_lock lock(runtime.mutex);
		runtime.pairActive = false;
		runtime.pairRefreshed = false;
		runtime.pairIdentity = CurrentIdentity();
		runtime.receipt = { cycle };
		if (runtime.reused && SameFrame(runtime.prepared, runtime.pairIdentity) && runtime.prepared.cycle == cycle) {
			runtime.pairActive = true;
			runtime.pairRefreshed = true;
			return;
		}
		if (!color || !Eligible() || !BelongsToCurrentDevice(color) || !SameFrame(runtime.source, runtime.pairIdentity) || runtime.source.cycle != cycle ||
			runtime.source.settingsRevision != runtime.revision ||
			!runtime.depth)
			return;
		try {
			D3D11_TEXTURE2D_DESC desc{};
			color->GetDesc(&desc);
			if (!ValidSize(desc) || desc.Width != runtime.depth->desc.Width || desc.Height != runtime.depth->desc.Height)
				return;
			DXGI_FORMAT sampledFormat = desc.Format;
			if (sampledFormat == DXGI_FORMAT_R8G8B8A8_TYPELESS || sampledFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
				sampledFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (sampledFormat != DXGI_FORMAT_R8G8B8A8_UNORM && sampledFormat != DXGI_FORMAT_R16G16B16A16_FLOAT)
				return;
			if (!runtime.color || runtime.color->desc.Width != desc.Width || runtime.color->desc.Height != desc.Height || runtime.color->desc.Format != sampledFormat)
				runtime.color = MakeTexture(desc.Width, desc.Height, sampledFormat, false, "ViewRefresh::SceneColor");
			{
				CS_GPU_PASS("ViewRefresh::CaptureColor");
				Util::ScopedDeviceContextState isolated(runtime.context.get(), runtime.isolatedState.get());
				globals::d3d::context->CopyResource(runtime.color->resource.get(), color);
			}
			runtime.pairActive = true;
			runtime.pendingPair.store(true, std::memory_order_release);
			runtime.reason = "Fresh stereo scene captured";
			if (runtime.settings.latePose) {
				Matrix target;
				if (LatePose(target) && PreparePair(target, cycle)) {
					runtime.pairRefreshed = true;
					runtime.reason = "Correcting rendered stereo pose";
				}
			}
		} catch (const std::exception& error) {
			Failure("stereo capture", error.what());
			runtime.pairActive = false;
		}
	}

	bool GetSubmission(vr::EVREye eye, uint64_t cycle, vr::VRTextureWithPose_t& texture)
	{
		if (!runtime.pendingPair.load(std::memory_order_acquire))
			return false;
		const std::scoped_lock lock(runtime.mutex);
		if (eye != vr::Eye_Left && eye != vr::Eye_Right)
			return false;
		if (!runtime.pairActive || !runtime.pairRefreshed || !SameFrame(runtime.prepared, CurrentIdentity()) ||
			runtime.prepared.cycle != cycle || !runtime.eyes[eye].output)
			return false;
		texture.handle = runtime.eyes[eye].output->resource.get();
		texture.eType = vr::TextureType_DirectX;
		texture.eColorSpace = vr::ColorSpace_Gamma;
		texture.mDeviceToAbsoluteTracking = runtime.displayedPose;
		return true;
	}

	void RecordSubmission(vr::EVREye eye, uint64_t cycle, bool accepted, bool refreshed)
	{
		if (!runtime.pendingPair.load(std::memory_order_acquire))
			return;
		const std::scoped_lock lock(runtime.mutex);
		if (!runtime.pairActive)
			return;
		runtime.receipt.Record(static_cast<uint32_t>(eye), cycle,
			accepted && SameFrame(runtime.pairIdentity, CurrentIdentity()) && refreshed == runtime.pairRefreshed);
	}

	void EndSubmitPair(uint64_t cycle)
	{
		if (!runtime.pendingPair.exchange(false, std::memory_order_acq_rel))
			return;
		const std::scoped_lock lock(runtime.mutex);
		if (!runtime.pairActive)
			return;
		const auto current = CurrentIdentity();
		const bool complete = SameFrame(runtime.pairIdentity, current) && cycle == current.cycle && runtime.receipt.Complete(cycle);
		runtime.source.complete = complete && !runtime.graphicsFailed && !runtime.reused && SameFrame(runtime.source, runtime.pairIdentity) && runtime.source.settingsRevision == runtime.revision;
		if (!complete) {
			++runtime.failedPairs;
			runtime.reason = "Stereo submission failed; rendering fresh scenes";
		} else if (runtime.reused) {
			++runtime.reusedPairs;
		} else {
			++runtime.renderedPairs;
			if (runtime.pairRefreshed)
				++runtime.correctedPairs;
		}
		runtime.pairActive = false;
		ServiceDisabled();
	}

	nlohmann::json Status()
	{
		const std::scoped_lock lock(runtime.mutex);
		return { { "enabled", runtime.settings.enabled }, { "sceneReuse", runtime.settings.reuseScenes },
			{ "latePose", runtime.settings.latePose }, { "reason", runtime.reason },
			{ "sourceReady", runtime.source.complete }, { "sourceFrame", runtime.source.frame },
			{ "sourceCycle", runtime.source.cycle }, { "poseCycle", runtime.poseCycle },
			{ "renderedPairs", runtime.renderedPairs }, { "reusedPairs", runtime.reusedPairs },
			{ "correctedPairs", runtime.correctedPairs }, { "failedPairs", runtime.failedPairs } };
	}

	void DrawSettings()
	{
		ImGui::SeparatorText("View Refresh (experimental)");
		auto settings = GetSettings();
		bool changed = ImGui::Checkbox("Enable View Refresh", &settings.enabled);
		{
			auto disabled = Util::DisableGuard(!settings.enabled);
			changed |= ImGui::Checkbox("Scene Reuse", &settings.reuseScenes);
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted("Render a fresh world every other display frame while stationary. Head tracking continues; hands and world animation update at scene cadence. Requires native resolution without engine TAA or Neural Rendering.");
			changed |= ImGui::Checkbox("Late Pose Correction", &settings.latePose);
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted("Refresh the pose of freshly rendered frames on SteamVR. Adds a reprojection pass. Scene Reuse also works with the game's normal OpenComposite pose cycle.");
		}
		if (changed)
			Configure(settings);
		const char* reason;
		{
			const std::scoped_lock lock(runtime.mutex);
			reason = runtime.reason;
		}
		ImGui::TextWrapped("%s", reason);
	}
}
