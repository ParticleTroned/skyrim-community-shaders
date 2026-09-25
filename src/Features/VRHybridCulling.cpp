#include "VRHybridCulling.h"

#include "GpuPass.h"
#include "State.h"
#include "Util.h"
#include "Utils/RendererContextAccess.h"
#include "VRDepthCullingTemporal.h"
#include "VRHybridCullingHistory.h"
#include "VRHybridCullingPolicy.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <d3d11_1.h>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <chrono>
#endif

namespace VRHybridCulling
{
	namespace
	{
		using namespace VRHybridCullingPolicy;
		using VRHybridCullingHistory::Batch;
		using VRHybridCullingHistory::EyePose;

		// These wrappers belong to Skyrim VR 1.4.15, whose callsites gate installation.
		struct NativeBuffer
		{
			ID3D11Buffer* buffer;
			ID3D11ShaderResourceView* srv;
			ID3D11UnorderedAccessView* uav;
			ID3D11Buffer* staging;
			std::uint64_t reserved;
			std::uint32_t count;
		};
		static_assert(offsetof(NativeBuffer, staging) == 0x18);
		static_assert(offsetof(NativeBuffer, count) == 0x28);
		static_assert(sizeof(NativeBuffer) == 0x30);

		template <class T>
		T ReadField(const void* a_object, std::size_t a_offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const std::byte*>(a_object) + a_offset, sizeof(value));
			return value;
		}

		struct Frame
		{
			BuildConstants build{};
			TestConstants test{};
			std::array<EyePose, kEyeCount> poses{};
			RE::NiTransform worldCamera{};
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint32_t number = 0;
			std::uint64_t epoch = 0;
			winrt::com_ptr<ID3D11ShaderResourceView> depth;
		};

		struct Resources
		{
			winrt::com_ptr<ID3D11DeviceContext1> context;
			winrt::com_ptr<ID3DDeviceContextState> isolatedState;
			winrt::com_ptr<ID3D11ComputeShader> build;
			winrt::com_ptr<ID3D11ComputeShader> reduce;
			winrt::com_ptr<ID3D11ComputeShader> test;
			winrt::com_ptr<ID3D11Buffer> constants;
			winrt::com_ptr<ID3D11Texture2D> pyramid;
			winrt::com_ptr<ID3D11ShaderResourceView> allMips;
			std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> mipSRVs;
			std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> mipUAVs;
			PyramidLayout layout{};
			bool failed = false;
		};

		struct History
		{
			Batch batch{};
			Frame frame{};
			std::array<OBBTransform, VRDepthCullingTemporalPolicy::kMaximumObjects> bounds{};
			bool pending = false;
		};

		Resources g_resources;
		Frame g_prepared;
		History g_history;
		bool g_ready = false;
		std::atomic_bool g_reloadRequested{ false };
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::atomic<const char*> g_status{ "idle" };
		std::atomic<const char*> g_backend{ "pending" }, g_fallbackReason{ "none" }, g_historyRejection{ "none" };
		std::atomic_uint64_t g_statusEpoch{ 0 };
		std::atomic_uint64_t g_historyEpoch{ 0 }, g_lastCountEpoch{ 0 };
		std::atomic_uint64_t g_submitted{ 0 }, g_accepted{ 0 }, g_invalidated{ 0 }, g_fallback{ 0 }, g_promoted{ 0 };
		std::atomic_uint64_t g_unreadable{ 0 };
		std::atomic_uint32_t g_lastCount{ 0 };

		struct TimingCounters
		{
			std::atomic_uint64_t samples{ 0 }, total{ 0 }, maximum{ 0 };
			StageTiming Read() const
			{
				return { samples.load(std::memory_order_relaxed), total.load(std::memory_order_relaxed), maximum.load(std::memory_order_relaxed) };
			}
			void Reset()
			{
				samples.store(0, std::memory_order_relaxed);
				total.store(0, std::memory_order_relaxed);
				maximum.store(0, std::memory_order_relaxed);
			}
		};
		TimingCounters g_prepareTiming, g_dispatchTiming, g_readbackTiming;

		class TelemetryScope
		{
		public:
			explicit TelemetryScope(TimingCounters& a_counters) : writer(VRDepthCullingTemporal::GetTelemetryGate()), counters(a_counters)
			{
				if (writer)
					started = std::chrono::steady_clock::now();
			}
			~TelemetryScope()
			{
				if (!writer)
					return;
				const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started)
						.count());
				counters.samples.fetch_add(1, std::memory_order_relaxed);
				counters.total.fetch_add(elapsed, std::memory_order_relaxed);
				VRDepthCullingTelemetryPolicy::UpdateMaximum(counters.maximum, elapsed);
			}
			explicit operator bool() const noexcept { return static_cast<bool>(writer); }

		private:
			VRDepthCullingTelemetryPolicy::WriterScope writer;
			TimingCounters& counters;
			std::chrono::steady_clock::time_point started{};
		};
#endif

		void PublishOutcome([[maybe_unused]] const char* a_state, [[maybe_unused]] const char* a_backend, [[maybe_unused]] std::uint64_t a_epoch)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			g_status.store(a_state, std::memory_order_relaxed);
			g_backend.store(a_backend, std::memory_order_relaxed);
			g_statusEpoch.store(a_epoch, std::memory_order_release);
#endif
		}

		bool PreparationFailed([[maybe_unused]] const char* a_reason, std::uint64_t a_epoch)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			g_fallbackReason.store(a_reason, std::memory_order_relaxed);
#endif
			PublishOutcome("native_fallback", "native", a_epoch);
			return false;
		}

		bool CaptureFrame(Frame& a_frame, std::uint64_t a_epoch)
		{
			a_frame = {};
			auto* renderer = globals::game::renderer;
			auto* graphics = globals::game::graphicsState;
			auto* camera = RE::Main::WorldRootCamera();
			if (!renderer || !graphics || !camera || !globals::state ||
				globals::state->IsMainOrLoadingMenuOpen() || globals::state->IsSaveLoadSafeModeActive())
				return false;
			const auto& dynamic = graphics->GetRuntimeData();
			// Packed subrects require their own source contract; physical render-scale targets remain supported.
			if (!dynamic.dynamicResolutionLock &&
				(dynamic.dynamicResolutionWidthRatio != 1.0f || dynamic.dynamicResolutionHeightRatio != 1.0f))
				return false;
			auto* depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
			D3D11_TEXTURE2D_DESC texture{};
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			if (!depth || !Util::GetTexture2DDesc(depth, texture))
				return false;
			depth->GetDesc(&view);
			if (texture.ArraySize != 1 || texture.SampleDesc.Count != 1 || texture.Width % 2 != 0 ||
				view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view.Texture2D.MostDetailedMip != 0 ||
				(view.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && view.Format != DXGI_FORMAT_R32_FLOAT))
				return false;
			using FindCamera = void* (*)(RE::BSGraphics::State*, RE::NiCamera*, bool);
			static REL::Relocation<FindCamera> findCamera{ REL::Offset(0xDD1AD0) };
			const auto* cache = findCamera(graphics, camera, false);
			if (!cache || ReadField<std::uint32_t>(cache, 0x6C) != kEyeCount ||
				ReadField<std::uint32_t>(cache, 0x18) != kEyeCount || ReadField<std::uint32_t>(cache, 0x30) != kEyeCount)
				return false;
			const auto* cameras = ReadField<const RE::BSGraphics::ViewData*>(cache, 0x08);
			const auto* positions = ReadField<const RE::NiPoint3*>(cache, 0x20);
			if (!cameras || !positions)
				return false;
			for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
				const auto& data = cameras[eye];
				if (data.viewPort[0] != 0.0f || data.viewPort[1] != 1.0f || data.viewPort[2] != 1.0f || data.viewPort[3] != 0.0f ||
					data.viewDepthRange.x != 0.0f || data.viewDepthRange.y != 1.0f)
					return false;
				const auto matrix = data.viewProjMat.Transpose();
				std::memcpy(a_frame.test.viewProjection[eye], &matrix, sizeof(matrix));
				a_frame.test.cameraAdjust[eye][0] = positions[eye].x;
				a_frame.test.cameraAdjust[eye][1] = positions[eye].y;
				a_frame.test.cameraAdjust[eye][2] = positions[eye].z;
				std::memcpy(a_frame.poses[eye].position, &positions[eye], sizeof(RE::NiPoint3));
				std::memcpy(a_frame.poses[eye].projection, &data.projMatrixUnjittered, sizeof(Matrix));
				for (std::uint32_t row = 0; row < 3; ++row)
					for (std::uint32_t column = 0; column < 3; ++column)
						a_frame.poses[eye].rotation[row][column] = data.viewMat.m[row][column];
				a_frame.test.eyes[eye] = { eye * (texture.Width / 2), 0, texture.Width / 2, texture.Height };
			}
			if (!TryMakeBuildConstants(a_frame.test.eyes, texture.Width, texture.Height, kDefaultSourceReduction,
					a_frame.build, a_frame.test.pyramid))
				return false;
			a_frame.test.objectCount = 1;
			a_frame.test.pixelGuardBand = 2.0f;
			if (!IsValidTestConstants(a_frame.test, texture.Width, texture.Height))
				return false;
			a_frame.depth.copy_from(depth);
			a_frame.width = texture.Width;
			a_frame.height = texture.Height;
			a_frame.number = globals::state->frameCount;
			a_frame.epoch = a_epoch;
			a_frame.worldCamera = camera->world;
			return true;
		}

		bool IsWorldCameraCoherent(const RE::NiTransform& a_previous, const RE::NiTransform& a_current)
		{
			EyePose previous{}, current{};
			std::memcpy(previous.rotation, a_previous.rotate.entry, sizeof(previous.rotation));
			std::memcpy(current.rotation, a_current.rotate.entry, sizeof(current.rotation));
			std::memcpy(previous.position, &a_previous.translate, sizeof(previous.position));
			std::memcpy(current.position, &a_current.translate, sizeof(current.position));
			return std::isfinite(a_previous.scale) && a_previous.scale > 0.0f && a_previous.scale == a_current.scale &&
			       VRHybridCullingHistory::IsCoherent(previous, current);
		}

		bool ReadBatch(void* a_culler, std::uint64_t a_epoch, Batch& a_batch)
		{
			a_batch = {};
			if (!a_culler || !globals::state)
				return false;
			const auto count = ReadField<std::uint32_t>(a_culler, 0xB0);
			const auto selector = ReadField<std::uint32_t>(a_culler, 0xC0);
			if (count == 0 || count > VRDepthCullingTemporalPolicy::kMaximumObjects || selector > 1)
				return false;
			a_batch = { reinterpret_cast<std::uintptr_t>(a_culler), ReadField<std::uintptr_t>(a_culler, 0xB8),
				ReadField<std::uintptr_t>(a_culler, 0xD0 + selector * sizeof(void*)), a_epoch,
				globals::state->frameCount, count, selector };
			return a_batch.transforms != 0 && a_batch.results != 0;
		}

		bool CheckNativeBuffers(void* a_culler, std::uint32_t a_count, NativeBuffer*& a_bounds, NativeBuffer*& a_results)
		{
			a_bounds = ReadField<NativeBuffer*>(a_culler, 0xF8);
			a_results = ReadField<NativeBuffer*>(a_culler, 0x100);
			if (ReadField<std::uint8_t>(a_culler, 0xC9) != 1 || !a_bounds || !a_results ||
				!a_bounds->buffer || !a_bounds->srv || !a_results->buffer || !a_results->uav || !a_results->staging ||
				a_bounds->count < a_count || a_results->count < a_count)
				return false;
			D3D11_BUFFER_DESC bounds{}, results{}, staging{};
			a_bounds->buffer->GetDesc(&bounds);
			a_results->buffer->GetDesc(&results);
			a_results->staging->GetDesc(&staging);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			a_bounds->srv->GetDesc(&srv);
			a_results->uav->GetDesc(&uav);
			winrt::com_ptr<ID3D11Resource> boundsResource, resultsResource;
			a_bounds->srv->GetResource(boundsResource.put());
			a_results->uav->GetResource(resultsResource.put());
			if (boundsResource.get() != a_bounds->buffer || resultsResource.get() != a_results->buffer)
				return false;
			return bounds.StructureByteStride == sizeof(OBBTransform) && results.StructureByteStride == sizeof(std::uint32_t) &&
			       (bounds.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && (results.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
			       (bounds.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			       (results.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			       bounds.ByteWidth >= a_count * sizeof(OBBTransform) && results.ByteWidth >= a_count * sizeof(std::uint32_t) &&
			       staging.ByteWidth == results.ByteWidth && staging.Usage == D3D11_USAGE_STAGING &&
			       (staging.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0 &&
			       srv.ViewDimension == D3D11_SRV_DIMENSION_BUFFER && srv.Format == DXGI_FORMAT_UNKNOWN &&
			       srv.Buffer.FirstElement == 0 && srv.Buffer.NumElements >= a_count &&
			       uav.ViewDimension == D3D11_UAV_DIMENSION_BUFFER && uav.Format == DXGI_FORMAT_UNKNOWN &&
			       uav.Buffer.FirstElement == 0 && uav.Buffer.NumElements >= a_count && uav.Buffer.Flags == 0;
		}

		bool EnsurePipeline()
		{
			auto& resources = g_resources;
			if (resources.failed)
				return false;
			if (resources.test)
				return true;
			auto* device = globals::d3d::device;
			winrt::com_ptr<ID3D11Device1> device1;
			DX::ThrowIfFailed(device->QueryInterface(__uuidof(ID3D11Device1), device1.put_void()));
			DX::ThrowIfFailed(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), resources.context.put_void()));
			const auto featureLevel = device->GetFeatureLevel();
			DX::ThrowIfFailed(device1->CreateDeviceContextState(device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED,
				&featureLevel, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, resources.isolatedState.put()));
			Util::SetResourceName(resources.isolatedState.get(), "VRHybridCulling::ContextState");
			resources.build.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\BuildDepthCS.hlsl", {}, "cs_5_0")));
			resources.reduce.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\ReduceDepthCS.hlsl", {}, "cs_5_0")));
			resources.test.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VRHybridCulling\\TestBoundsCS.hlsl", {}, "cs_5_0")));
			if (!resources.build || !resources.reduce || !resources.test) {
				resources.failed = true;
				logger::warn("VR: Hybrid Hi-Z shaders unavailable; using native depth culling");
				return false;
			}
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = sizeof(TestConstants);
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			DX::ThrowIfFailed(device->CreateBuffer(&description, nullptr, resources.constants.put()));
			Util::SetResourceName(resources.constants.get(), "VRHybridCulling::Constants");
			return true;
		}

		void EnsurePyramid(const PyramidLayout& a_layout)
		{
			auto& resources = g_resources;
			if (resources.pyramid && resources.layout.width == a_layout.width && resources.layout.height == a_layout.height)
				return;
			auto* device = globals::d3d::device;
			winrt::com_ptr<ID3D11Texture2D> pyramid;
			winrt::com_ptr<ID3D11ShaderResourceView> allMips;
			std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs(a_layout.mipCount);
			std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs(a_layout.mipCount);
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = a_layout.width;
			texture.Height = a_layout.height;
			texture.MipLevels = a_layout.mipCount;
			texture.ArraySize = kEyeCount;
			texture.Format = DXGI_FORMAT_R32_FLOAT;
			texture.SampleDesc.Count = 1;
			texture.Usage = D3D11_USAGE_DEFAULT;
			texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			DX::ThrowIfFailed(device->CreateTexture2D(&texture, nullptr, pyramid.put()));
			Util::SetResourceName(pyramid.get(), "VRHybridCulling::DepthPyramid");
			DX::ThrowIfFailed(device->CreateShaderResourceView(pyramid.get(), nullptr, allMips.put()));
			Util::SetResourceName(allMips.get(), "VRHybridCulling::DepthPyramid SRV");
			for (std::uint32_t mip = 0; mip < a_layout.mipCount; ++mip) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format = texture.Format;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				srv.Texture2DArray.MostDetailedMip = mip;
				srv.Texture2DArray.MipLevels = 1;
				srv.Texture2DArray.ArraySize = kEyeCount;
				DX::ThrowIfFailed(device->CreateShaderResourceView(pyramid.get(), &srv, srvs[mip].put()));
				Util::SetResourceName(srvs[mip].get(), "VRHybridCulling::Mip%u SRV", mip);
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = texture.Format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
				uav.Texture2DArray.MipSlice = mip;
				uav.Texture2DArray.ArraySize = kEyeCount;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(pyramid.get(), &uav, uavs[mip].put()));
				Util::SetResourceName(uavs[mip].get(), "VRHybridCulling::Mip%u UAV", mip);
			}
			resources.pyramid = std::move(pyramid);
			resources.allMips = std::move(allMips);
			resources.mipSRVs = std::move(srvs);
			resources.mipUAVs = std::move(uavs);
			resources.layout = a_layout;
		}

		template <class T>
		void UploadConstants(const T& a_value)
		{
			static_assert(sizeof(T) <= sizeof(TestConstants));
			std::array<std::byte, sizeof(TestConstants)> data{};
			std::memcpy(data.data(), &a_value, sizeof(T));
			auto* context = g_resources.context.get();
			context->UpdateSubresource(g_resources.constants.get(), 0, nullptr, data.data(), 0, 0);
			auto* constants = g_resources.constants.get();
			context->CSSetConstantBuffers(0, 1, &constants);
		}

		void UnbindCompute()
		{
			ID3D11ShaderResourceView* srvs[2]{};
			ID3D11UnorderedAccessView* uav = nullptr;
			g_resources.context->CSSetShaderResources(0, 2, srvs);
			g_resources.context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}

		void BuildPyramid()
		{
			CS_GPU_PASS("VRHybridCulling::BuildHierarchy");
			auto* context = g_resources.context.get();
			UploadConstants(g_prepared.build);
			auto* source = g_prepared.depth.get();
			auto* destination = g_resources.mipUAVs[0].get();
			context->CSSetShader(g_resources.build.get(), nullptr, 0);
			context->CSSetShaderResources(0, 1, &source);
			context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
			context->Dispatch((g_prepared.build.outputWidth + 7) / 8, (g_prepared.build.outputHeight + 7) / 8, kEyeCount);
			UnbindCompute();
			context->CSSetShader(g_resources.reduce.get(), nullptr, 0);
			for (std::uint32_t mip = 1; mip < g_resources.layout.mipCount; ++mip) {
				const ReduceConstants constants{ std::max(1u, g_resources.layout.width >> mip), std::max(1u, g_resources.layout.height >> mip), {} };
				UploadConstants(constants);
				source = g_resources.mipSRVs[mip - 1].get();
				destination = g_resources.mipUAVs[mip].get();
				context->CSSetShaderResources(0, 1, &source);
				context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
				context->Dispatch((constants.outputWidth + 7) / 8, (constants.outputHeight + 7) / 8, kEyeCount);
				UnbindCompute();
			}
		}
	}

	bool Prepare(std::uint64_t a_epoch)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		const TelemetryScope telemetry(g_prepareTiming);
		g_fallbackReason.store("none", std::memory_order_relaxed);
#endif
		g_ready = false;
		g_prepared.depth = nullptr;
		if (g_reloadRequested.exchange(false, std::memory_order_acq_rel))
			g_resources = {};
		if (g_resources.failed)
			return PreparationFailed("pipeline_unavailable", a_epoch);
		if (!REL::Module::IsVR() || !globals::d3d::device || !globals::d3d::context || !CaptureFrame(g_prepared, a_epoch))
			return PreparationFailed("unsupported_frame", a_epoch);
		try {
			if (!EnsurePipeline())
				return PreparationFailed("pipeline_unavailable", a_epoch);
			EnsurePyramid(g_prepared.test.pyramid);
			g_ready = true;
			return true;
		} catch (const std::exception& error) {
			g_resources.failed = true;
			logger::warn("VR: Hybrid Hi-Z resource setup failed: {}", error.what());
			return PreparationFailed("resource_setup_failed", a_epoch);
		}
	}

	bool Dispatch(void* a_culler, std::uint64_t a_epoch)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		const TelemetryScope telemetry(g_dispatchTiming);
#endif
		Batch batch;
		NativeBuffer* bounds = nullptr;
		NativeBuffer* results = nullptr;
		if (!g_ready)
			return PreparationFailed("not_prepared", a_epoch);
		if (!ReadBatch(a_culler, a_epoch, batch))
			return PreparationFailed("empty_or_invalid_batch", a_epoch);
		if (g_prepared.epoch != a_epoch || g_prepared.number != batch.frame)
			return PreparationFailed("preparation_expired", a_epoch);
		if (!CheckNativeBuffers(a_culler, batch.count, bounds, results))
			return PreparationFailed("native_buffers_invalid", a_epoch);
		g_ready = false;
		g_prepared.test.objectCount = batch.count;
		const Util::RendererOwnership ownership(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context), true);
		if (!ownership)
			return PreparationFailed("renderer_unavailable", a_epoch);
		CS_GPU_PASS("VRHybridCulling::Visibility");
		auto* context = g_resources.context.get();
		winrt::com_ptr<ID3DDeviceContextState> previous;
		context->SwapDeviceContextState(g_resources.isolatedState.get(), previous.put());
		const SKSE::stl::scope_exit restore([&]() noexcept {
			UnbindCompute();
			context->SwapDeviceContextState(previous.get(), nullptr);
		});
		BuildPyramid();
		UploadConstants(g_prepared.test);
		ID3D11ShaderResourceView* srvs[]{ bounds->srv, g_resources.allMips.get() };
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, &results->uav, nullptr);
		context->CSSetShader(g_resources.test.get(), nullptr, 0);
		context->Dispatch((batch.count + 63) / 64, 1, 1);
		UnbindCompute();
		context->CopyResource(results->staging, results->buffer);
		g_history.batch = batch;
		g_history.frame = g_prepared;
		g_prepared.depth = nullptr;
		std::memcpy(g_history.bounds.data(), reinterpret_cast<const void*>(batch.transforms), batch.count * sizeof(OBBTransform));
		g_history.pending = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (telemetry) {
			g_lastCount.store(batch.count, std::memory_order_relaxed);
			g_lastCountEpoch.store(a_epoch, std::memory_order_release);
			g_submitted.fetch_add(1, std::memory_order_relaxed);
		}
		g_fallbackReason.store("none", std::memory_order_relaxed);
#endif
		PublishOutcome("hybrid_submitted", "hybrid", a_epoch);
		return true;
	}

	bool CompleteReadback(void* a_culler, std::uint64_t a_epoch, bool a_selected)
	{
		if (!g_history.pending)
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		const TelemetryScope telemetry(g_readbackTiming);
#endif
		Batch batch;
		if (!ReadBatch(a_culler, a_epoch, batch)) {
			g_history.pending = false;
			g_history.frame.depth = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
			g_historyRejection.store("unreadable_batch", std::memory_order_relaxed);
			g_historyEpoch.store(a_epoch, std::memory_order_release);
			if (telemetry)
				g_unreadable.fetch_add(1, std::memory_order_relaxed);
#endif
			PublishOutcome("history_unreadable", "hybrid", g_history.batch.epoch);
			return true;
		}
		Frame current;
		const char* rejection = nullptr;
		if (!a_selected || !VRHybridCullingHistory::Matches(g_history.batch, batch))
			rejection = "batch_mismatch";
		else if (std::memcmp(g_history.bounds.data(), reinterpret_cast<const void*>(batch.transforms), batch.count * sizeof(OBBTransform)) != 0)
			rejection = "bounds_changed";
		else if (!CaptureFrame(current, a_epoch))
			rejection = "frame_unavailable";
		else if (current.depth.get() != g_history.frame.depth.get() ||
				 current.width != g_history.frame.width || current.height != g_history.frame.height ||
				 std::memcmp(current.build.eyes.data(), g_history.frame.build.eyes.data(), sizeof(current.build.eyes)) != 0)
			rejection = "depth_changed";
		else if (!IsWorldCameraCoherent(g_history.frame.worldCamera, current.worldCamera) ||
				 !VRHybridCullingHistory::IsStereoCoherent(g_history.frame.poses, current.poses))
			rejection = "view_changed";
		g_history.pending = false;
		g_history.frame.depth = nullptr;
#ifdef DEVBENCH_BRIDGE_ENABLED
		g_historyRejection.store(rejection ? rejection : "none", std::memory_order_relaxed);
		g_historyEpoch.store(a_epoch, std::memory_order_release);
#endif
		if (!rejection) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (telemetry)
				g_accepted.fetch_add(1, std::memory_order_relaxed);
#endif
			PublishOutcome("history_accepted", "hybrid", g_history.batch.epoch);
			return true;
		}
		auto* results = reinterpret_cast<std::uint32_t*>(batch.results);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (telemetry) {
			g_invalidated.fetch_add(1, std::memory_order_relaxed);
			g_promoted.fetch_add(std::count(results, results + batch.count, 0u), std::memory_order_relaxed);
		}
#endif
		std::fill_n(results, batch.count, 1u);
		PublishOutcome("history_invalid_visible", "hybrid", g_history.batch.epoch);
		return true;
	}

	void CancelPreparation(bool a_hybridSelected, std::uint64_t a_epoch)
	{
		g_ready = false;
		g_prepared.depth = nullptr;
		g_history.pending = false;
		g_history.frame.depth = nullptr;
		if (a_hybridSelected) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			const VRDepthCullingTelemetryPolicy::WriterScope telemetry(VRDepthCullingTemporal::GetTelemetryGate());
			if (telemetry)
				g_fallback.fetch_add(1, std::memory_order_relaxed);
			if (g_statusEpoch.load(std::memory_order_acquire) != a_epoch ||
				std::strcmp(g_fallbackReason.load(std::memory_order_relaxed), "none") == 0)
				g_fallbackReason.store("replacement_not_prepared", std::memory_order_relaxed);
#endif
			PublishOutcome("native_fallback", "native", a_epoch);
		}
	}

	void ClearShaderCache()
	{
		g_reloadRequested.store(true, std::memory_order_release);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	Status GetStatus(std::uint64_t a_epoch, bool a_selected, bool a_enabled, bool a_installed)
	{
		const bool current = a_selected && a_enabled && a_installed && g_statusEpoch.load(std::memory_order_acquire) == a_epoch;
		return {
			.state = current ? g_status.load(std::memory_order_relaxed) : (a_selected && a_enabled ? (a_installed ? "pending" : "native_fallback") : "inactive"),
			.effectiveBackend = !a_enabled ? "disabled" : (!a_selected || !a_installed ? "native" : (current ? g_backend.load(std::memory_order_relaxed) : "pending")),
			.fallbackReason = a_selected && a_enabled && !a_installed ? "hooks_unavailable" : (current ? g_fallbackReason.load(std::memory_order_relaxed) : "none"),
			.historyRejectionReason = current && g_historyEpoch.load(std::memory_order_acquire) == a_epoch ? g_historyRejection.load(std::memory_order_relaxed) : "none",
			.submittedBatches = g_submitted.load(std::memory_order_relaxed),
			.acceptedBatches = g_accepted.load(std::memory_order_relaxed),
			.invalidatedBatches = g_invalidated.load(std::memory_order_relaxed),
			.fallbackBatches = g_fallback.load(std::memory_order_relaxed),
			.promotedObjects = g_promoted.load(std::memory_order_relaxed),
			.unreadableBatches = g_unreadable.load(std::memory_order_relaxed),
			.lastObjectCount = current && g_lastCountEpoch.load(std::memory_order_acquire) == a_epoch ? g_lastCount.load(std::memory_order_relaxed) : 0,
			.prepare = g_prepareTiming.Read(),
			.dispatch = g_dispatchTiming.Read(),
			.readback = g_readbackTiming.Read()
		};
	}

	void ResetTelemetryUnderLock() noexcept
	{
		g_submitted.store(0, std::memory_order_relaxed);
		g_accepted.store(0, std::memory_order_relaxed);
		g_invalidated.store(0, std::memory_order_relaxed);
		g_fallback.store(0, std::memory_order_relaxed);
		g_promoted.store(0, std::memory_order_relaxed);
		g_unreadable.store(0, std::memory_order_relaxed);
		g_lastCount.store(0, std::memory_order_relaxed);
		g_lastCountEpoch.store(0, std::memory_order_release);
		g_prepareTiming.Reset();
		g_dispatchTiming.Reset();
		g_readbackTiming.Reset();
	}
#endif
}
