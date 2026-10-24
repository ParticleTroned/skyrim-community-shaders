#include "ColourPipelineProbe.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "ColourPipelineProbePolicy.h"
#	include "ColourPipelineSceneObservation.h"
#	include "Globals.h"
#	include "GpuPass.h"
#	include "State.h"
#	include "Utils/D3D.h"

#	include <magic_enum/magic_enum.hpp>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <chrono>
#	include <cstring>
#	include <format>
#	include <limits>
#	include <mutex>
#	include <string_view>
#	include <unordered_map>
#	include <vector>

namespace CSX::Diagnostics::ColourPipelineProbe
{
	namespace
	{
		using json = nlohmann::json;
		constexpr std::uint32_t kSchemaVersion = 3;
		constexpr std::uint32_t kGridSize = 17;
		constexpr auto kEyeCount = Policy::kEyeCount;
		constexpr auto kStageCount = Policy::kStageCount;
		constexpr auto kSlotCount = Policy::kSlotCount;
		constexpr std::uint32_t kCaptureTimeoutFrames = 120;

		enum class ProbeState : std::uint8_t
		{
			Idle,
			Armed,
			Capturing,
			ReadbackPending,
			Complete,
			Failed
		};

		struct Slot
		{
			bool queued = false;
			bool mapped = false;
			Stage stage = Stage::FsrInput;
			std::uint32_t eye = 0;
			std::uint32_t sourceSubresource = 0;
			std::uint32_t sourceX = 0;
			std::uint32_t sourceY = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t queuedQpc = 0;
			std::uint64_t mappedQpc = 0;
			std::uint64_t writeEpoch = 0;
			bool hasEngineTarget = false;
			std::uint32_t engineTarget = 0;
			bool matchesMainTarget = false;
			std::string symbol;
			std::string callsite;
			std::string resourceObservationId;
			std::string resourceVersionObservationId;
			std::string stagingResourceObservationId;
			std::string sourcePointer;
			DispatchMetadata dispatch{};
			ColourPipelineSceneObservation::Snapshot sceneObservation{};
			D3D11_TEXTURE2D_DESC sourceDesc{};
			DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT rtvFormat = DXGI_FORMAT_UNKNOWN;
			DXGI_FORMAT uavFormat = DXGI_FORMAT_UNKNOWN;
			winrt::com_ptr<ID3D11Texture2D> staging;
			winrt::com_ptr<ID3D11Texture2D> sourceOwner;
			json result = json::object();
		};

		struct State
		{
			std::mutex mutex;
			ProbeState state = ProbeState::Idle;
			std::uint64_t generation = 0;
			std::uint64_t expectedColourContractRevision = 0;
			std::uint64_t nextWriteEpoch = 1;
			std::uint64_t armedQpc = 0;
			std::uint64_t queryQueuedQpc = 0;
			std::uint64_t completedQpc = 0;
			std::uint32_t cpuFrame = 0;
			std::uint32_t sceneEpoch = 0;
			std::uint32_t submissionEpoch = 0;
			std::uint64_t stagingPayloadBytes = 0;
			std::chrono::steady_clock::time_point deadline{};
			std::string captureId;
			std::string error;
			json metadata = json::object();
			DispatchMetadata dispatch{};
			ID3D11DeviceContext* context = nullptr;
			winrt::com_ptr<ID3D11Query> completionQuery;
			std::array<Slot, kSlotCount> slots{};
			std::unordered_map<std::uintptr_t, std::string> resourceIds;
			json flows = json::array();
		};

		State g_state;
		std::atomic_bool g_captureActive{ false };

		void ClearStateLocked()
		{
			g_state.state = ProbeState::Idle;
			g_state.expectedColourContractRevision = 0;
			g_state.nextWriteEpoch = 1;
			g_state.armedQpc = 0;
			g_state.queryQueuedQpc = 0;
			g_state.completedQpc = 0;
			g_state.cpuFrame = 0;
			g_state.sceneEpoch = 0;
			g_state.submissionEpoch = 0;
			g_state.stagingPayloadBytes = 0;
			g_state.deadline = {};
			g_state.captureId.clear();
			g_state.error.clear();
			g_state.metadata = json::object();
			g_state.dispatch = {};
			g_state.context = nullptr;
			g_state.completionQuery = nullptr;
			for (auto& slot : g_state.slots)
				slot = {};
			g_state.resourceIds.clear();
			g_state.flows = json::array();
		}

		std::uint64_t QueryQpc() noexcept
		{
			LARGE_INTEGER value{};
			return ::QueryPerformanceCounter(&value) && value.QuadPart > 0 ?
			           static_cast<std::uint64_t>(value.QuadPart) :
			           0;
		}

		const char* StateName(ProbeState a_state) noexcept
		{
			switch (a_state) {
			case ProbeState::Idle:
				return "idle";
			case ProbeState::Armed:
				return "armed";
			case ProbeState::Capturing:
				return "capturing";
			case ProbeState::ReadbackPending:
				return "readback_pending";
			case ProbeState::Complete:
				return "complete";
			case ProbeState::Failed:
				return "failed";
			}
			return "unknown";
		}

		const char* StageName(Stage a_stage) noexcept
		{
			switch (a_stage) {
			case Stage::FsrInput:
				return "fsr_input";
			case Stage::FsrOutput:
				return "fsr_output";
			case Stage::CombinedMain:
				return "combined_main";
			case Stage::ImageSpaceInput:
				return "imagespace_input";
			case Stage::ImageSpaceOutput:
				return "imagespace_output";
			}
			return "unknown";
		}

		std::size_t SlotIndex(Stage a_stage, std::uint32_t a_eye) noexcept
		{
			return static_cast<std::size_t>(a_stage) * kEyeCount + a_eye;
		}

		json Format(DXGI_FORMAT a_format)
		{
			return {
				{ "value", static_cast<std::uint32_t>(a_format) },
				{ "name", std::string(magic_enum::enum_name(a_format)) },
			};
		}

		std::string Pointer(const void* a_pointer)
		{
			return a_pointer ? std::format("0x{:X}", reinterpret_cast<std::uintptr_t>(a_pointer)) : std::string{};
		}

		std::uint32_t BytesPerPixel(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return 4;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
				return 8;
			case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				return 16;
			default:
				return 0;
			}
		}

		float DecodeHalf(std::uint16_t a_value) noexcept
		{
			const bool negative = (a_value & 0x8000u) != 0;
			const std::uint32_t exponent = (a_value >> 10u) & 0x1Fu;
			const std::uint32_t mantissa = a_value & 0x3FFu;
			float result = 0.0f;
			if (exponent == 0)
				result = std::ldexp(static_cast<float>(mantissa), -24);
			else if (exponent == 0x1Fu)
				result = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
			else
				result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
			return negative ? -result : result;
		}

		float DecodeUnsignedFloat(std::uint32_t a_value, std::uint32_t a_mantissaBits) noexcept
		{
			const std::uint32_t mantissaMask = (1u << a_mantissaBits) - 1u;
			const std::uint32_t exponent = (a_value >> a_mantissaBits) & 0x1Fu;
			const std::uint32_t mantissa = a_value & mantissaMask;
			if (exponent == 0)
				return std::ldexp(static_cast<float>(mantissa), 1 - 15 - static_cast<int>(a_mantissaBits));
			if (exponent == 0x1Fu)
				return mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
			return std::ldexp(1.0f + static_cast<float>(mantissa) / static_cast<float>(1u << a_mantissaBits), static_cast<int>(exponent) - 15);
		}

		bool DecodePixel(DXGI_FORMAT a_format, const std::uint8_t* a_pixel, std::array<float, 4>& a_value) noexcept
		{
			a_value = { 0.0f, 0.0f, 0.0f, 1.0f };
			if (!a_pixel)
				return false;
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				a_value = { a_pixel[0] / 255.0f, a_pixel[1] / 255.0f, a_pixel[2] / 255.0f, a_pixel[3] / 255.0f };
				return true;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				a_value = { a_pixel[2] / 255.0f, a_pixel[1] / 255.0f, a_pixel[0] / 255.0f,
					a_format == DXGI_FORMAT_B8G8R8X8_UNORM || a_format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ? 1.0f : a_pixel[3] / 255.0f };
				return true;
			case DXGI_FORMAT_R10G10B10A2_UNORM:
				{
					std::uint32_t packed = 0;
					std::memcpy(&packed, a_pixel, sizeof(packed));
					a_value = { static_cast<float>(packed & 0x3FFu) / 1023.0f,
						static_cast<float>((packed >> 10u) & 0x3FFu) / 1023.0f,
						static_cast<float>((packed >> 20u) & 0x3FFu) / 1023.0f,
						static_cast<float>((packed >> 30u) & 0x3u) / 3.0f };
					return true;
				}
			case DXGI_FORMAT_R11G11B10_FLOAT:
				{
					std::uint32_t packed = 0;
					std::memcpy(&packed, a_pixel, sizeof(packed));
					a_value = { DecodeUnsignedFloat(packed & 0x7FFu, 6u), DecodeUnsignedFloat((packed >> 11u) & 0x7FFu, 6u), DecodeUnsignedFloat((packed >> 22u) & 0x3FFu, 5u), 1.0f };
					return true;
				}
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				{
					std::uint16_t channels[4]{};
					std::memcpy(channels, a_pixel, sizeof(channels));
					a_value = { DecodeHalf(channels[0]), DecodeHalf(channels[1]), DecodeHalf(channels[2]), DecodeHalf(channels[3]) };
					return true;
				}
			case DXGI_FORMAT_R16G16B16A16_UNORM:
				{
					std::uint16_t channels[4]{};
					std::memcpy(channels, a_pixel, sizeof(channels));
					a_value = { channels[0] / 65535.0f, channels[1] / 65535.0f, channels[2] / 65535.0f, channels[3] / 65535.0f };
					return true;
				}
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				std::memcpy(a_value.data(), a_pixel, sizeof(float) * a_value.size());
				return true;
			default:
				return false;
			}
		}

		std::string Hex(const std::uint8_t* a_bytes, std::size_t a_size)
		{
			static constexpr char digits[] = "0123456789abcdef";
			std::string result(a_size * 2, '0');
			for (std::size_t i = 0; i < a_size; ++i) {
				result[i * 2] = digits[a_bytes[i] >> 4u];
				result[i * 2 + 1] = digits[a_bytes[i] & 0x0Fu];
			}
			return result;
		}

		DXGI_FORMAT ViewFormat(ID3D11ShaderResourceView* a_view) noexcept
		{
			if (!a_view)
				return DXGI_FORMAT_UNKNOWN;
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			return desc.Format;
		}

		DXGI_FORMAT ViewFormat(ID3D11RenderTargetView* a_view) noexcept
		{
			if (!a_view)
				return DXGI_FORMAT_UNKNOWN;
			D3D11_RENDER_TARGET_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			return desc.Format;
		}

		DXGI_FORMAT ViewFormat(ID3D11UnorderedAccessView* a_view) noexcept
		{
			if (!a_view)
				return DXGI_FORMAT_UNKNOWN;
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
			a_view->GetDesc(&desc);
			return desc.Format;
		}

		void FailLocked(std::string_view a_error) noexcept
		{
			try {
				g_state.error.assign(a_error);
			} catch (...) {
				g_state.error.clear();
			}
			g_state.state = ProbeState::Failed;
			g_state.completedQpc = QueryQpc();
			g_state.completionQuery = nullptr;
			for (auto& slot : g_state.slots) {
				slot.staging = nullptr;
				slot.sourceOwner = nullptr;
			}
			g_captureActive.store(false, std::memory_order_release);
		}

		void ExpireLocked()
		{
			if ((g_state.state == ProbeState::Armed || g_state.state == ProbeState::Capturing ||
					g_state.state == ProbeState::ReadbackPending) &&
				std::chrono::steady_clock::now() >= g_state.deadline)
				FailLocked("the colour-pipeline capture exceeded its wall-clock deadline");
		}

		std::string ObserveResourceLocked(ID3D11Texture2D* a_texture)
		{
			const auto key = reinterpret_cast<std::uintptr_t>(a_texture);
			if (const auto found = g_state.resourceIds.find(key); found != g_state.resourceIds.end())
				return found->second;
			const auto id = std::format("obs-resource-{}-g1", g_state.resourceIds.size() + 1);
			g_state.resourceIds.emplace(key, id);
			return id;
		}

		bool ViewMatchesTexture(ID3D11View* a_view, ID3D11Texture2D* a_texture) noexcept
		{
			if (!a_view)
				return true;
			winrt::com_ptr<ID3D11Resource> resource;
			a_view->GetResource(resource.put());
			return resource.get() == static_cast<ID3D11Resource*>(a_texture);
		}

		ColourPipelineSceneObservation::Snapshot ObserveScene(std::uint32_t a_eye)
		{
			ColourPipelineSceneObservation::Snapshot result{};
			const auto* state = globals::state;
			if (!state || a_eye >= kEyeCount)
				return result;
			result.beginQpc = QueryQpc();
			LARGE_INTEGER frequency{};
			if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
				result.qpcFrequency = static_cast<std::uint64_t>(frequency.QuadPart);
			result.cpuFrame = state->frameCount;
			result.eye = a_eye;
			result.inWorld = state->inWorld;
			result.lastStartedWorldFrame = state->lastWorldRenderFrame;
			result.lastCompletedWorldFrame = state->lastCompletedWorldRenderFrame;
			const auto& cache = globals::game::frameBufferCached;
			const std::array<const Matrix*, 5> matrices{
				&cache.GetCameraView(a_eye), &cache.GetCameraProj(a_eye), &cache.GetCameraProjUnjittered(a_eye),
				&cache.GetCameraViewProjUnjittered(a_eye), &cache.GetCameraPreviousViewProjUnjittered(a_eye)
			};
			for (std::size_t index = 0; index < matrices.size(); ++index)
				for (std::size_t row = 0; row < 4; ++row)
					for (std::size_t column = 0; column < 4; ++column)
						result.matrices[index][row][column] = matrices[index]->m[row][column];
			const auto& position = cache.GetCameraPosAdjust(a_eye);
			const auto& previous = cache.GetCameraPreviousPosAdjust(a_eye);
			result.positionAdjust = { position.x, position.y, position.z };
			result.previousPositionAdjust = { previous.x, previous.y, previous.z };
			if (const auto* manager = RE::ImageSpaceManager::GetSingleton()) {
				const auto& data = manager->GetImageSpaceData().baseData;
				result.imageSpaceAvailable = true;
				result.hdr = { data.hdr.eyeAdaptSpeed, data.hdr.eyeAdaptStrength, data.hdr.bloomBlurRadius,
					data.hdr.bloomThreshold, data.hdr.bloomScale, data.hdr.receiveBloomThreshold,
					data.hdr.white, data.hdr.sunlightScale, data.hdr.skyScale };
				result.cinematic = { data.cinematic.saturation, data.cinematic.brightness, data.cinematic.contrast };
				result.tint = { data.tint.amount, data.tint.color.red, data.tint.color.green, data.tint.color.blue };
			}
			result.endQpc = QueryQpc();
			return result;
		}

		bool QueueSlotLocked(
			Stage a_stage,
			std::uint32_t a_eye,
			ID3D11DeviceContext* a_context,
			ID3D11Texture2D* a_texture,
			ID3D11ShaderResourceView* a_srv,
			ID3D11RenderTargetView* a_rtv,
			ID3D11UnorderedAccessView* a_uav,
			std::uint32_t a_subresource,
			std::uint32_t a_x,
			std::uint32_t a_y,
			std::uint32_t a_width,
			std::uint32_t a_height,
			const char* a_symbol,
			const char* a_callsite)
		{
			if (static_cast<std::uint32_t>(a_stage) >= kStageCount || a_eye >= kEyeCount ||
				!a_context || !a_texture || !a_width || !a_height)
				return false;
			auto& slot = g_state.slots[SlotIndex(a_stage, a_eye)];
			if (slot.queued)
				return true;
			if (g_state.context && g_state.context != a_context) {
				FailLocked("capture crossed D3D11 immediate contexts");
				return false;
			}
			g_state.context = a_context;
			if (!ViewMatchesTexture(a_srv, a_texture) || !ViewMatchesTexture(a_rtv, a_texture) ||
				!ViewMatchesTexture(a_uav, a_texture)) {
				FailLocked("capture view evidence did not reference its observed source texture");
				return false;
			}
			D3D11_TEXTURE2D_DESC sourceDesc{};
			a_texture->GetDesc(&sourceDesc);
			if (sourceDesc.SampleDesc.Count != 1 || !sourceDesc.MipLevels ||
				a_subresource % sourceDesc.MipLevels != 0 ||
				a_subresource / sourceDesc.MipLevels >= sourceDesc.ArraySize ||
				!Policy::ValidRectangle(sourceDesc.Width, sourceDesc.Height, a_x, a_y, a_width, a_height)) {
				FailLocked("capture source rectangle is incompatible with the observed texture");
				return false;
			}
			const auto bytesPerPixel = BytesPerPixel(sourceDesc.Format);
			if (!bytesPerPixel) {
				FailLocked(std::format("unsupported capture source format {}", static_cast<std::uint32_t>(sourceDesc.Format)));
				return false;
			}
			if (!Policy::CanAllocate(a_width, a_height, bytesPerPixel, g_state.stagingPayloadBytes)) {
				FailLocked("colour-pipeline staging payload exceeds the slot or capture budget");
				return false;
			}

			D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
			stagingDesc.Width = a_width;
			stagingDesc.Height = a_height;
			stagingDesc.MipLevels = 1;
			stagingDesc.ArraySize = 1;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			winrt::com_ptr<ID3D11Device> device;
			a_texture->GetDevice(device.put());
			if (!device || FAILED(device->CreateTexture2D(&stagingDesc, nullptr, slot.staging.put()))) {
				FailLocked("failed to create a colour-pipeline staging texture");
				return false;
			}
			Util::SetResourceName(slot.staging.get(), "ColourPipelineProbe::%sEye%u", StageName(a_stage), a_eye);
			slot.sourceOwner.copy_from(a_texture);
			g_state.stagingPayloadBytes += std::uint64_t{ a_width } * a_height * bytesPerPixel;
			const D3D11_BOX box{ a_x, a_y, 0, a_x + a_width, a_y + a_height, 1 };
			slot.sceneObservation = ObserveScene(a_eye);
			{
				CS_GPU_PASS("ColourPipelineProbe::Copy");
				a_context->CopySubresourceRegion(slot.staging.get(), 0, 0, 0, 0, a_texture, a_subresource, &box);
			}

			slot.queued = true;
			slot.stage = a_stage;
			slot.eye = a_eye;
			slot.sourceSubresource = a_subresource;
			slot.sourceX = a_x;
			slot.sourceY = a_y;
			slot.width = a_width;
			slot.height = a_height;
			slot.queuedQpc = QueryQpc();
			slot.writeEpoch = g_state.nextWriteEpoch++;
			slot.symbol = a_symbol ? a_symbol : "unknown";
			slot.callsite = a_callsite ? a_callsite : "unknown";
			slot.sourceDesc = sourceDesc;
			slot.srvFormat = ViewFormat(a_srv);
			slot.rtvFormat = ViewFormat(a_rtv);
			slot.uavFormat = ViewFormat(a_uav);
			slot.resourceObservationId = ObserveResourceLocked(a_texture);
			slot.resourceVersionObservationId = std::format("obs-resource-version-{}-g1", SlotIndex(a_stage, a_eye) + 1);
			slot.stagingResourceObservationId = std::format("obs-resource-{}-g1", kSlotCount + SlotIndex(a_stage, a_eye) + 1);
			slot.sourcePointer = Pointer(a_texture);
			return true;
		}

		void QueueCompletionIfReadyLocked(ID3D11DeviceContext* a_context)
		{
			if (g_state.state == ProbeState::Failed || g_state.state == ProbeState::ReadbackPending)
				return;
			if (!std::ranges::all_of(g_state.slots, [](const Slot& a_slot) { return a_slot.queued; }))
				return;
			if (g_state.flows.size() != kEyeCount) {
				FailLocked("the captured frame did not contain both observed FSR copy-back calls");
				return;
			}
			D3D11_QUERY_DESC desc{ D3D11_QUERY_EVENT, 0 };
			winrt::com_ptr<ID3D11Device> device;
			a_context->GetDevice(device.put());
			if (!device || FAILED(device->CreateQuery(&desc, g_state.completionQuery.put()))) {
				FailLocked("failed to create the colour-pipeline completion query");
				return;
			}
			Util::SetResourceName(g_state.completionQuery.get(), "ColourPipelineProbe::Completion");
			a_context->End(g_state.completionQuery.get());
			g_state.queryQueuedQpc = QueryQpc();
			g_state.state = ProbeState::ReadbackPending;
		}

		json Descriptor(const D3D11_TEXTURE2D_DESC& a_desc)
		{
			return {
				{ "dimension", "texture-2d" },
				{ "widthOrBytes", a_desc.Width },
				{ "height", a_desc.Height },
				{ "depthOrArraySize", a_desc.ArraySize },
				{ "mipLevels", a_desc.MipLevels },
				{ "format", Format(a_desc.Format) },
				{ "sampleCount", a_desc.SampleDesc.Count },
				{ "sampleQuality", a_desc.SampleDesc.Quality },
				{ "usage", static_cast<std::uint32_t>(a_desc.Usage) },
				{ "bindFlags", a_desc.BindFlags },
				{ "cpuAccessFlags", a_desc.CPUAccessFlags },
				{ "miscFlags", a_desc.MiscFlags },
				{ "structureByteStride", 0 },
			};
		}

		enum class MapResult
		{
			Ready,
			Pending,
			Failed
		};

		DXGI_FORMAT DecodeFormat(const Slot& a_slot) noexcept
		{
			switch (a_slot.sourceDesc.Format) {
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R32G32B32A32_TYPELESS:
				break;
			default:
				return a_slot.sourceDesc.Format;
			}
			DXGI_FORMAT resolved = DXGI_FORMAT_UNKNOWN;
			for (auto format : { a_slot.srvFormat, a_slot.rtvFormat, a_slot.uavFormat }) {
				if (format == DXGI_FORMAT_UNKNOWN)
					continue;
				if (format == a_slot.sourceDesc.Format)
					return DXGI_FORMAT_UNKNOWN;
				if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
					format = DXGI_FORMAT_R8G8B8A8_UNORM;
				else if (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
					format = DXGI_FORMAT_B8G8R8A8_UNORM;
				else if (format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
					format = DXGI_FORMAT_B8G8R8X8_UNORM;
				if (!BytesPerPixel(format) || (resolved != DXGI_FORMAT_UNKNOWN && resolved != format))
					return DXGI_FORMAT_UNKNOWN;
				resolved = format;
			}
			return resolved;
		}

		MapResult MapSlotLocked(ID3D11DeviceContext* a_context, Slot& a_slot)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto mapBegin = QueryQpc();
			const HRESULT result = a_context->Map(a_slot.staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			const auto mapEnd = QueryQpc();
			if (result == DXGI_ERROR_WAS_STILL_DRAWING)
				return MapResult::Pending;
			if (FAILED(result)) {
				a_slot.result = {
					{ "map", { { "resultHresult", std::format("0x{:08X}", static_cast<std::uint32_t>(result)) }, { "succeeded", false }, { "durationQpcTicks", mapEnd - mapBegin } } },
				};
				return MapResult::Failed;
			}
			struct ScopedUnmap
			{
				ID3D11DeviceContext* context;
				ID3D11Resource* resource;

				~ScopedUnmap()
				{
					context->Unmap(resource, 0);
				}
			};
			const auto stagingOwner = a_slot.staging;
			const ScopedUnmap unmap{ a_context, stagingOwner.get() };
			const std::uint32_t bytesPerPixel = BytesPerPixel(a_slot.sourceDesc.Format);
			const auto decodeFormat = DecodeFormat(a_slot);
			if (!mapped.pData || mapped.RowPitch < std::uint64_t{ a_slot.width } * bytesPerPixel)
				return MapResult::Failed;
			json samples = json::array();
			std::array<double, 4> sums{};
			double lumaSum = 0.0;
			double minimumLuma = std::numeric_limits<double>::infinity();
			double maximumLuma = -std::numeric_limits<double>::infinity();
			std::uint32_t finiteSamples = 0;
			for (std::uint32_t gridY = 0; gridY < kGridSize; ++gridY) {
				const std::uint32_t y = kGridSize == 1 ? 0 : static_cast<std::uint32_t>((static_cast<std::uint64_t>(a_slot.height - 1) * gridY) / (kGridSize - 1));
				for (std::uint32_t gridX = 0; gridX < kGridSize; ++gridX) {
					const std::uint32_t x = kGridSize == 1 ? 0 : static_cast<std::uint32_t>((static_cast<std::uint64_t>(a_slot.width - 1) * gridX) / (kGridSize - 1));
					const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch + static_cast<std::size_t>(x) * bytesPerPixel;
					std::array<float, 4> decoded{};
					const bool decodedOk = DecodePixel(decodeFormat, pixel, decoded);
					json value = nullptr;
					if (decodedOk && std::ranges::all_of(decoded, [](float a_channel) { return std::isfinite(a_channel); })) {
						value = decoded;
						for (std::size_t channel = 0; channel < decoded.size(); ++channel)
							sums[channel] += decoded[channel];
						const double luma = 0.2126 * decoded[0] + 0.7152 * decoded[1] + 0.0722 * decoded[2];
						lumaSum += luma;
						minimumLuma = std::min(minimumLuma, luma);
						maximumLuma = std::max(maximumLuma, luma);
						++finiteSamples;
					}
					samples.push_back({
						{ "grid", { gridX, gridY } },
						{ "sourcePixel", { x, y } },
						{ "rawLittleEndianHex", Hex(pixel, bytesPerPixel) },
						{ "decodedRgba", std::move(value) },
					});
				}
			}
			a_slot.mapped = true;
			a_slot.mappedQpc = mapEnd;
			json mean = nullptr;
			json luma = nullptr;
			if (finiteSamples) {
				mean = json::array();
				for (const double sum : sums)
					mean.push_back(sum / finiteSamples);
				luma = { { "mean", lumaSum / finiteSamples }, { "minimum", minimumLuma }, { "maximum", maximumLuma } };
			}
			a_slot.result = {
				{ "map", {
							 { "schema", "resource-cpu-access-v1" },
							 { "phase", "map" },
							 { "mapObservationId", std::format("obs-resource-cpu-access-{}-g1", SlotIndex(a_slot.stage, a_slot.eye) + 1) },
							 { "resourceObservationId", a_slot.stagingResourceObservationId },
							 { "subresource", 0 },
							 { "mapType", "read" },
							 { "mapTypeValue", static_cast<std::uint32_t>(D3D11_MAP_READ) },
							 { "mapFlags", static_cast<std::uint32_t>(D3D11_MAP_FLAG_DO_NOT_WAIT) },
							 { "doNotWait", true },
							 { "resultHresult", "0x00000000" },
							 { "succeeded", true },
							 { "matchedMap", true },
							 { "readable", true },
							 { "writable", false },
							 { "durationQpcTicks", mapEnd - mapBegin },
							 { "rowPitch", mapped.RowPitch },
							 { "depthPitch", mapped.DepthPitch },
							 { "visibilityBoundary", "cpu-readable-after-map-return" },
							 { "publicationBoundary", nullptr },
						 } },
				{ "sampling", {
								  { "decodeFormat", Format(decodeFormat) },
								  { "gridSize", kGridSize },
								  { "sampleCount", kGridSize * kGridSize },
								  { "finiteSampleCount", finiteSamples },
								  { "selection", "inclusive endpoints with integer uniform stratification" },
								  { "transferConversion", "none" },
								  { "channelDecode", "format-native numeric decode; sRGB formats remain encoded" },
								  { "luminanceConvention", "Rec.709 weights applied to decoded format-native channels without transfer conversion" },
								  { "meanRgba", std::move(mean) },
								  { "luminance", std::move(luma) },
								  { "samples", std::move(samples) },
							  } },
			};
			a_slot.staging = nullptr;
			return MapResult::Ready;
		}

		json Dispatch(const DispatchMetadata& a_dispatch)
		{
			return {
				{ "colourContractRevision", a_dispatch.colourContractRevision },
				{ "frame", a_dispatch.frame },
				{ "dispatchSerial", a_dispatch.dispatchSerial },
				{ "contextGeneration", a_dispatch.dispatchSerial ? json(a_dispatch.contextGeneration) : json(nullptr) },
				{ "contextIndex", a_dispatch.dispatchSerial ? json(a_dispatch.contextIndex) : json(nullptr) },
				{ "path", a_dispatch.dispatchSerial ? json(a_dispatch.path) : json(nullptr) },
				{ "renderWidth", a_dispatch.renderWidth },
				{ "renderHeight", a_dispatch.renderHeight },
				{ "displayWidth", a_dispatch.displayWidth },
				{ "displayHeight", a_dispatch.displayHeight },
				{ "requestedHighDynamicRangeInput", a_dispatch.requestedHighDynamicRangeInput },
				{ "requestedAutoExposure", a_dispatch.requestedAutoExposure },
				{ "effectiveHighDynamicRangeInput", a_dispatch.dispatchSerial ? json(a_dispatch.effectiveHighDynamicRangeInput) : json(nullptr) },
				{ "effectiveAutoExposure", a_dispatch.dispatchSerial ? json(a_dispatch.effectiveAutoExposure) : json(nullptr) },
				{ "exposureResourceBound", a_dispatch.dispatchSerial ? json(a_dispatch.exposureResourceBound) : json(nullptr) },
				{ "preExposure", a_dispatch.dispatchSerial ? json(a_dispatch.preExposure) : json(nullptr) },
				{ "configuredSharpnessAtDispatch", a_dispatch.dispatchSerial && std::isfinite(a_dispatch.configuredSharpness) ? json(a_dispatch.configuredSharpness) : json(nullptr) },
				{ "effectiveSharpness", a_dispatch.dispatchSerial ? json(a_dispatch.effectiveSharpness) : json(nullptr) },
				{ "sharpeningEnabled", a_dispatch.dispatchSerial ? json(a_dispatch.sharpeningEnabled) : json(nullptr) },
				{ "dispatchQpc", a_dispatch.dispatchQpc ? json(a_dispatch.dispatchQpc) : json(nullptr) },
				{ "submittedInputs", FSRDispatchInputTelemetry::ToJson(a_dispatch.submittedInputs, a_dispatch.dispatchSerial != 0) },
				{ "attribution", a_dispatch.dispatchSerial ? "observed-successful-dispatch" : "pending-successful-dispatch" },
			};
		}

		json SlotResult(const Slot& a_slot, Stage a_stage, std::uint32_t a_eye)
		{
			json result{
				{ "stage", StageName(a_stage) },
				{ "eye", a_eye == 0 ? "left" : "right" },
				{ "eyeMask", 1u << a_eye },
				{ "queued", a_slot.queued },
				{ "mapped", a_slot.mapped },
				{ "sceneObservation", ColourPipelineSceneObservation::ToJson(a_slot.sceneObservation) },
			};
			if (!a_slot.queued)
				return result;
			auto resource = Descriptor(a_slot.sourceDesc);
			resource.update({
				{ "schema", "resource-observation-v1" },
				{ "resourceObservationId", a_slot.resourceObservationId },
				{ "d3dObjectPointer", a_slot.sourcePointer },
				{ "pointerEvidence", a_slot.sourcePointer },
				{ "pointerGeneration", 1 },
				{ "viewFormats", { { "srv", Format(a_slot.srvFormat) }, { "rtv", Format(a_slot.rtvFormat) }, { "uav", Format(a_slot.uavFormat) } } },
			});
			result.update({
				{ "sampleRole", a_slot.stage == Stage::ImageSpaceInput ? "destination-before" : "observed-stage-resource" },
				{ "dispatch", Dispatch(a_slot.dispatch) },
				{ "producerConsumer", { { "symbol", a_slot.symbol }, { "callsite", a_slot.callsite } } },
				{ "engineTarget", a_slot.hasEngineTarget ? json({ { "value", a_slot.engineTarget }, { "matchesKMain", a_slot.matchesMainTarget } }) : json(nullptr) },
				{ "frame", { { "cpuFrame", g_state.cpuFrame }, { "sceneEpoch", nullptr }, { "submissionEpoch", nullptr }, { "eye", a_slot.eye == 0 ? "left" : "right" }, { "eyeMask", 1u << a_slot.eye } } },
				{ "resource", std::move(resource) },
				{ "resourceVersion", {
										 { "schema", "resource-version-observation-v1" },
										 { "resourceVersionObservationId", a_slot.resourceVersionObservationId },
										 { "resourceObservationId", a_slot.resourceObservationId },
										 { "subresources", { { "first", a_slot.sourceSubresource }, { "count", 1 } } },
										 { "writeEpoch", a_slot.writeEpoch },
										 { "producerFrame", g_state.cpuFrame },
										 { "readinessDomain", "same-immediate-context-order" },
										 { "eye", a_slot.eye == 0 ? "left" : "right" },
										 { "eyeMask", 1u << a_slot.eye },
									 } },
				{ "activeRectangle", { { "x", a_slot.sourceX }, { "y", a_slot.sourceY }, { "width", a_slot.width }, { "height", a_slot.height } } },
				{ "readbackFlow", {
									  { "schema", "resource-flow-v1" },
									  { "operation", "copy-subresource-region" },
									  { "sourceResourceObservationId", a_slot.resourceObservationId },
									  { "destinationResourceObservationId", a_slot.stagingResourceObservationId },
									  { "sourceSubresource", a_slot.sourceSubresource },
									  { "destinationSubresource", 0 },
								  } },
				{ "queuedQpc", a_slot.queuedQpc },
				{ "mappedQpc", a_slot.mappedQpc },
				{ "readback", a_slot.result },
			});
			return result;
		}
	}

	bool Arm(
		const std::string& a_captureId,
		std::uint64_t a_expectedColourContractRevision,
		const nlohmann::json& a_metadata,
		std::uint64_t& a_generation,
		std::string& a_error)
	{
		std::lock_guard lock(g_state.mutex);
		ExpireLocked();
		a_generation = g_state.generation;
		if (!globals::game::isVR) {
			a_error = "the stereo colour-pipeline probe requires Skyrim VR";
			return false;
		}
		if (a_captureId.empty() || a_captureId.size() > Policy::kMaximumCaptureIdBytes ||
			!a_expectedColourContractRevision || !a_metadata.is_object() ||
			a_metadata.dump().size() > Policy::kMaximumMetadataBytes) {
			a_error = "invalid capture identity, revision or metadata bound";
			return false;
		}
		if (g_state.state == ProbeState::Armed || g_state.state == ProbeState::Capturing || g_state.state == ProbeState::ReadbackPending) {
			a_error = "a colour-pipeline capture is already active";
			return false;
		}
		if (g_state.generation == std::numeric_limits<std::uint64_t>::max()) {
			a_error = "capture generation is exhausted";
			return false;
		}
		const auto nextGeneration = g_state.generation + 1;
		auto captureId = a_captureId;
		auto metadata = a_metadata;
		ClearStateLocked();
		g_state.generation = nextGeneration;
		g_state.state = ProbeState::Armed;
		g_state.captureId = std::move(captureId);
		g_state.expectedColourContractRevision = a_expectedColourContractRevision;
		g_state.metadata = std::move(metadata);
		g_state.armedQpc = QueryQpc();
		g_state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(Policy::kTimeoutSeconds);
		a_generation = g_state.generation;
		g_captureActive.store(true, std::memory_order_release);
		return true;
	}

	bool Reset(const std::string& a_captureId, std::uint64_t a_expectedGeneration,
		std::uint64_t& a_generation, std::string& a_error)
	{
		std::lock_guard lock(g_state.mutex);
		ExpireLocked();
		a_generation = g_state.generation;
		if (a_captureId != g_state.captureId || a_expectedGeneration != g_state.generation) {
			a_error = "reset capture identity or generation did not match";
			return false;
		}
		if (g_state.state == ProbeState::Armed || g_state.state == ProbeState::Capturing || g_state.state == ProbeState::ReadbackPending) {
			a_error = "an active colour-pipeline capture cannot be reset";
			return false;
		}
		if (g_state.generation == std::numeric_limits<std::uint64_t>::max()) {
			a_error = "capture generation is exhausted";
			return false;
		}
		const auto nextGeneration = g_state.generation + 1;
		ClearStateLocked();
		g_state.generation = nextGeneration;
		a_generation = g_state.generation;
		g_captureActive.store(false, std::memory_order_release);
		return true;
	}

	bool WantsVendorCapture() noexcept
	{
		return g_captureActive.load(std::memory_order_acquire);
	}

	void CaptureVendorStage(
		Stage a_stage,
		std::uint32_t a_eye,
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		ID3D11RenderTargetView* a_rtv,
		ID3D11UnorderedAccessView* a_uav,
		std::uint32_t a_activeWidth,
		std::uint32_t a_activeHeight,
		const DispatchMetadata& a_dispatch,
		const char* a_symbol,
		const char* a_callsite) noexcept
	{
		if (!WantsVendorCapture())
			return;
		try {
			std::lock_guard lock(g_state.mutex);
			ExpireLocked();
			if (g_state.state != ProbeState::Armed && g_state.state != ProbeState::Capturing)
				return;
			if ((a_stage != Stage::FsrInput && a_stage != Stage::FsrOutput) || a_eye >= kEyeCount) {
				FailLocked("invalid vendor capture stage or eye");
				return;
			}
			const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
			if (!frame) {
				FailLocked("the colour-pipeline capture observed no engine frame");
				return;
			}
			if (g_state.state == ProbeState::Armed) {
				if (a_stage != Stage::FsrInput)
					return;
				g_state.cpuFrame = frame;
				g_state.dispatch = a_dispatch;
				g_state.state = ProbeState::Capturing;
			}
			if (g_state.cpuFrame != frame) {
				FailLocked("capture stages crossed engine frames");
				return;
			}
			if (a_dispatch.colourContractRevision != g_state.expectedColourContractRevision) {
				FailLocked("the FSR colour-contract revision changed before capture");
				return;
			}
			if (a_stage == Stage::FsrOutput &&
				(!a_dispatch.dispatchSerial || a_dispatch.frame != frame)) {
				FailLocked("the successful FSR dispatch did not belong to the captured engine frame");
				return;
			}
			if (g_state.slots[SlotIndex(a_stage, a_eye)].queued)
				return;
			if (a_stage == Stage::FsrOutput) {
				auto& input = g_state.slots[SlotIndex(Stage::FsrInput, a_eye)];
				if (!input.queued || !Policy::BindInputDispatch(input.dispatch, a_dispatch,
										 frame, g_state.expectedColourContractRevision, a_eye)) {
					FailLocked("the retained input did not match the current eye dispatch");
					return;
				}
			}
			g_state.dispatch = a_dispatch;
			QueueSlotLocked(a_stage, a_eye, globals::d3d::context, a_texture, a_srv, a_rtv, a_uav, 0, 0, 0, a_activeWidth, a_activeHeight, a_symbol, a_callsite);
			g_state.slots[SlotIndex(a_stage, a_eye)].dispatch = a_dispatch;
		} catch (const std::exception& e) {
			std::lock_guard lock(g_state.mutex);
			FailLocked(e.what());
		} catch (...) {
			std::lock_guard lock(g_state.mutex);
			FailLocked("unknown vendor-stage capture failure");
		}
	}

	void CaptureImageSpaceStage(
		Stage a_stage,
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		ID3D11RenderTargetView* a_rtv,
		ID3D11UnorderedAccessView* a_uav,
		std::uint32_t a_engineTarget,
		bool a_matchesMainTarget,
		const char* a_symbol,
		const char* a_callsite) noexcept
	{
		if (!WantsVendorCapture())
			return;
		try {
			std::lock_guard lock(g_state.mutex);
			if (g_state.state != ProbeState::Capturing || !a_texture ||
				!Policy::ValidImageSpaceTarget(a_stage, a_engineTarget, a_matchesMainTarget,
					static_cast<std::uint32_t>(RE::RENDER_TARGETS::kMAIN),
					static_cast<std::uint32_t>(RE::RENDER_TARGETS::kVR_FRAMEBUFFER)))
				return;
			const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
			if (frame != g_state.cpuFrame)
				return;
			D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(&desc);
			const auto eyeWidth = g_state.dispatch.displayWidth;
			const auto eyeHeight = g_state.dispatch.displayHeight;
			if (!eyeWidth || !eyeHeight) {
				FailLocked("the successful FSR dispatch dimensions were unavailable");
				return;
			}
			for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
				std::uint32_t subresource = 0;
				std::uint32_t x = eye * eyeWidth;
				if (std::uint64_t{ desc.Width } != std::uint64_t{ eyeWidth } * kEyeCount) {
					if (desc.Width != eyeWidth || desc.ArraySize != kEyeCount) {
						FailLocked("the ImageSpace texture did not expose a supported stereo layout");
						return;
					}
					x = 0;
					subresource = D3D11CalcSubresource(0, eye, desc.MipLevels);
				}
				if (!QueueSlotLocked(a_stage, eye, globals::d3d::context, a_texture, a_srv, a_rtv, a_uav, subresource, x, 0, eyeWidth, eyeHeight, a_symbol, a_callsite))
					return;
				auto& slot = g_state.slots[SlotIndex(a_stage, eye)];
				slot.hasEngineTarget = true;
				slot.engineTarget = a_engineTarget;
				slot.matchesMainTarget = a_matchesMainTarget;
				slot.dispatch = g_state.slots[SlotIndex(Stage::FsrOutput, eye)].dispatch;
			}
			QueueCompletionIfReadyLocked(globals::d3d::context);
		} catch (const std::exception& e) {
			std::lock_guard lock(g_state.mutex);
			FailLocked(e.what());
		} catch (...) {
			std::lock_guard lock(g_state.mutex);
			FailLocked("unknown ImageSpace-stage capture failure");
		}
	}

	void RecordFsrOutputCopyBack(
		ID3D11Resource* a_combinedDestination,
		ID3D11Texture2D* a_source,
		std::uint32_t a_eye,
		std::uint32_t a_destinationX,
		std::uint32_t a_width,
		std::uint32_t a_height,
		const char* a_symbol,
		const char* a_callsite) noexcept
	{
		if (!WantsVendorCapture() || !a_combinedDestination || !a_source || a_eye >= kEyeCount)
			return;
		try {
			std::lock_guard lock(g_state.mutex);
			if (g_state.state != ProbeState::Capturing)
				return;
			if (g_state.flows.size() > a_eye)
				return;
			const std::uint32_t frame = globals::state ? globals::state->frameCount : 0;
			if (frame != g_state.cpuFrame)
				return;
			const auto& source = g_state.slots[SlotIndex(Stage::FsrOutput, a_eye)];
			winrt::com_ptr<ID3D11Texture2D> destination;
			D3D11_TEXTURE2D_DESC destinationDesc{};
			if (FAILED(a_combinedDestination->QueryInterface(IID_PPV_ARGS(destination.put())))) {
				FailLocked("FSR copy-back destination was not a texture-2d");
				return;
			}
			destination->GetDesc(&destinationDesc);
			if (g_state.flows.size() != a_eye || !source.queued || source.sourceOwner.get() != a_source ||
				source.width != a_width || source.height != a_height ||
				!Policy::ValidRectangle(destinationDesc.Width, destinationDesc.Height, a_destinationX, 0, a_width, a_height)) {
				FailLocked("FSR copy-back did not match the captured source and bounded destination region");
				return;
			}
			const auto destinationId = ObserveResourceLocked(destination.get());
			g_state.flows.push_back({
				{ "flow", {
							  { "schema", "resource-flow-v1" },
							  { "operation", "copy-subresource-region" },
							  { "sourceResourceObservationId", source.resourceObservationId },
							  { "destinationResourceObservationId", destinationId },
							  { "sourceSubresource", 0 },
							  { "destinationSubresource", 0 },
						  } },
				{ "eye", a_eye == 0 ? "left" : "right" },
				{ "sourceRectangle", { { "x", 0 }, { "y", 0 }, { "width", a_width }, { "height", a_height } } },
				{ "destinationRectangle", { { "x", a_destinationX }, { "y", 0 }, { "width", a_width }, { "height", a_height } } },
				{ "evidence", "observed-queued-copy-call" },
				{ "producerConsumer", { { "symbol", a_symbol ? a_symbol : "unknown" }, { "callsite", a_callsite ? a_callsite : "unknown" } } },
				{ "frame", g_state.cpuFrame },
				{ "readinessDomain", "same-immediate-context-order" },
			});
		} catch (const std::exception& e) {
			std::lock_guard lock(g_state.mutex);
			FailLocked(e.what());
		} catch (...) {
			std::lock_guard lock(g_state.mutex);
			FailLocked("unknown FSR output copy-back observation failure");
		}
	}

	void ServiceReadbacks(ID3D11DeviceContext* a_context, std::uint32_t a_cpuFrame) noexcept
	{
		if (!WantsVendorCapture() || !a_context)
			return;
		try {
			std::lock_guard lock(g_state.mutex);
			ExpireLocked();
			if (g_state.state == ProbeState::Capturing && g_state.cpuFrame != a_cpuFrame) {
				FailLocked("the captured frame did not contain every required stage and eye");
				return;
			}
			if ((g_state.state == ProbeState::Capturing || g_state.state == ProbeState::ReadbackPending) &&
				g_state.cpuFrame && std::uint64_t{ a_cpuFrame } > std::uint64_t{ g_state.cpuFrame } + kCaptureTimeoutFrames) {
				FailLocked("the colour-pipeline capture exceeded its frame deadline");
				return;
			}
			if (g_state.state != ProbeState::ReadbackPending || !g_state.completionQuery)
				return;
			if (a_context != g_state.context) {
				FailLocked("readback service observed a different D3D11 immediate context");
				return;
			}
			const HRESULT ready = a_context->GetData(g_state.completionQuery.get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (ready == S_FALSE)
				return;
			if (FAILED(ready)) {
				FailLocked(std::format("completion query failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(ready)));
				return;
			}
			for (auto& slot : g_state.slots) {
				if (slot.mapped)
					continue;
				const auto mapped = MapSlotLocked(a_context, slot);
				if (mapped == MapResult::Pending)
					return;
				if (mapped == MapResult::Failed) {
					FailLocked(std::format("readback map failed for {} eye {}", StageName(slot.stage), slot.eye));
					return;
				}
			}
			g_state.completionQuery = nullptr;
			for (auto& slot : g_state.slots)
				slot.sourceOwner = nullptr;
			g_state.completedQpc = QueryQpc();
			g_state.state = ProbeState::Complete;
			g_captureActive.store(false, std::memory_order_release);
		} catch (const std::exception& e) {
			std::lock_guard lock(g_state.mutex);
			FailLocked(e.what());
		} catch (...) {
			std::lock_guard lock(g_state.mutex);
			FailLocked("unknown colour-pipeline readback failure");
		}
	}

	nlohmann::json BuildStatus()
	{
		std::lock_guard lock(g_state.mutex);
		ExpireLocked();
		const auto queued = std::ranges::count_if(g_state.slots, [](const Slot& a_slot) { return a_slot.queued; });
		const auto mapped = std::ranges::count_if(g_state.slots, [](const Slot& a_slot) { return a_slot.mapped; });
		auto stageEyeSlots = json::array();
		auto missingStageEyeSlots = json::array();
		for (const auto& slot : Policy::DescribeStageEyeSlots(g_state.slots)) {
			json identity{ { "stage", StageName(slot.stage) }, { "eye", slot.eye },
				{ "eyeName", slot.eye == 0 ? "left" : "right" } };
			if (!slot.queued)
				missingStageEyeSlots.push_back(identity);
			identity.update({ { "queued", slot.queued }, { "mapped", slot.mapped } });
			stageEyeSlots.push_back(std::move(identity));
		}
		return {
			{ "schemaVersion", kSchemaVersion },
			{ "state", StateName(g_state.state) },
			{ "generation", g_state.generation },
			{ "captureId", g_state.captureId },
			{ "cpuFrame", g_state.cpuFrame ? json(g_state.cpuFrame) : json(nullptr) },
			{ "sceneEpoch", nullptr },
			{ "submissionEpoch", nullptr },
			{ "expectedStageEyeSlots", kSlotCount },
			{ "queuedStageEyeSlots", queued },
			{ "mappedStageEyeSlots", mapped },
			{ "stageEyeSlots", std::move(stageEyeSlots) },
			{ "missingStageEyeSlots", std::move(missingStageEyeSlots) },
			{ "expectedColourContractRevision", g_state.expectedColourContractRevision },
			{ "stagingPayloadBytes", g_state.stagingPayloadBytes },
			{ "maximumStagingPayloadBytes", Policy::kMaximumCaptureBytes },
			{ "timeoutSeconds", Policy::kTimeoutSeconds },
			{ "error", g_state.error.empty() ?
						   (g_state.state == ProbeState::Failed ? json("capture failed; error text unavailable") : json(nullptr)) :
						   json(g_state.error) },
			{ "armedQpc", g_state.armedQpc },
			{ "queryQueuedQpc", g_state.queryQueuedQpc },
			{ "completedQpc", g_state.completedQpc },
		};
	}

	nlohmann::json BuildCapture(const std::string& a_captureId,
		std::uint64_t a_generation, Stage a_stage, std::uint32_t a_eye)
	{
		std::lock_guard lock(g_state.mutex);
		ExpireLocked();
		if (g_state.captureId != a_captureId || g_state.generation != a_generation ||
			static_cast<std::uint32_t>(a_stage) >= kStageCount || a_eye >= kEyeCount)
			return { { "error", "capture identity, generation or page did not match" } };
		return {
			{ "schema", "csx-colour-pipeline-probe-v3" },
			{ "captureId", g_state.captureId },
			{ "generation", g_state.generation },
			{ "state", StateName(g_state.state) },
			{ "error", g_state.error.empty() ?
						   (g_state.state == ProbeState::Failed ? json("capture failed; error text unavailable") : json(nullptr)) :
						   json(g_state.error) },
			{ "frame", { { "cpuFrame", g_state.cpuFrame ? json(g_state.cpuFrame) : json(nullptr) }, { "sceneEpoch", nullptr }, { "submissionEpoch", nullptr }, { "eye", "both" }, { "eyeMask", 3 } } },
			{ "immediateContext", { { "observationId", "obs-device-context-1-g1" }, { "pointer", Pointer(g_state.context) }, { "kind", "immediate" } } },
			{ "dispatch", Dispatch(g_state.slots[SlotIndex(a_stage, a_eye)].dispatch) },
			{ "dispatchEvidence", "input attribution is finalized from the actual successful dispatch for its eye; queuedQpc remains the sample time" },
			{ "metadata", g_state.metadata },
			{ "samplingContract", { { "gridSize", kGridSize }, { "rawBytesRetainedPerSample", true }, { "fullResourceReadbackRetained", false }, { "implicitTransferConversion", false } } },
			{ "resourceFlows", g_state.flows },
			{ "stages", json::array({ SlotResult(g_state.slots[SlotIndex(a_stage, a_eye)], a_stage, a_eye) }) },
		};
	}
}

#endif
