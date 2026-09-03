#include "CharacterRendering.h"

#include "Buffer.h"
#include "Globals.h"
#include "Profiler.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <SimpleMath.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		constexpr std::uint32_t kReadbackLatency = 3;
		constexpr std::uint32_t kCoverageSampleInterval = 30;
		constexpr std::uint32_t kRegionQuantization = 4;
		constexpr std::uint32_t kNearbyRegionPixels = 8;
		constexpr float kVisibilityDepthThreshold = 0.001f;

		bool IsSupportedDepthViewFormat(DXGI_FORMAT a_format) noexcept
		{
			return a_format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
			       a_format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS ||
			       a_format == DXGI_FORMAT_R32_FLOAT ||
			       a_format == DXGI_FORMAT_R16_UNORM;
		}

		void Increment(std::uint64_t& a_value) noexcept
		{
			if (a_value != std::numeric_limits<std::uint64_t>::max())
				++a_value;
		}

		void Increment(std::uint32_t& a_value) noexcept
		{
			if (a_value != std::numeric_limits<std::uint32_t>::max())
				++a_value;
		}

		bool SameIdentity(IUnknown* a_left, IUnknown* a_right) noexcept
		{
			if (!a_left || !a_right)
				return false;
			ComPtr<IUnknown> left;
			ComPtr<IUnknown> right;
			return SUCCEEDED(a_left->QueryInterface(IID_PPV_ARGS(&left))) &&
			       SUCCEEDED(a_right->QueryInterface(IID_PPV_ARGS(&right))) &&
			       left.Get() == right.Get();
		}

		std::uintptr_t GetIdentityToken(IUnknown* a_object) noexcept
		{
			if (!a_object)
				return 0;
			ComPtr<IUnknown> identity;
			return SUCCEEDED(a_object->QueryInterface(IID_PPV_ARGS(&identity))) ?
			           reinterpret_cast<std::uintptr_t>(identity.Get()) :
			           0;
		}

		std::uint64_t HashCombine(std::uint64_t a_hash, std::uint64_t a_value) noexcept
		{
			return (a_hash ^ a_value) * 1099511628211ull;
		}

		std::uint64_t BuildSettingsKey(const CharacterSettings& a_settings) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			auto add = [&](std::uint64_t a_value) { hash = HashCombine(hash, a_value); };
			auto addFloat = [&](float a_value) {
				std::uint32_t bits = 0;
				std::memcpy(&bits, &a_value, sizeof(bits));
				add(bits);
			};
			add(a_settings.enabled);
			add(a_settings.faces);
			add(a_settings.skin);
			add(a_settings.hair);
			addFloat(a_settings.faceStrength);
			addFloat(a_settings.skinStrength);
			addFloat(a_settings.hairStrength);
			addFloat(a_settings.maximumDistanceMeters);
			add(a_settings.minimumFacePixelSize);
			addFloat(a_settings.roiMargin);
			add(a_settings.maximumRoiRegions);
			add(a_settings.roiHoldFrames);
			add(a_settings.depthAwareFeather);
			add(a_settings.featherRadius);
			addFloat(a_settings.featherDepthThreshold);
			add(static_cast<std::uint32_t>(a_settings.maskTestMode));
			return hash;
		}

		bool IsFiniteSettings(const CharacterSettings& a_settings) noexcept
		{
			const auto validStrength = [](float a_value) {
				return std::isfinite(a_value) &&
				       a_value >= CharacterPolicy::kMinimumStrength &&
				       a_value <= CharacterPolicy::kMaximumStrength;
			};
			return validStrength(a_settings.faceStrength) &&
			       validStrength(a_settings.skinStrength) &&
			       validStrength(a_settings.hairStrength) &&
			       std::isfinite(a_settings.maximumDistanceMeters) &&
			       a_settings.maximumDistanceMeters >=
			           CharacterPolicy::kMinimumDistanceMeters &&
			       a_settings.maximumDistanceMeters <=
			           CharacterPolicy::kMaximumDistanceMeters &&
			       a_settings.minimumFacePixelSize >=
			           CharacterPolicy::kMinimumFacePixelSize &&
			       a_settings.minimumFacePixelSize <=
			           CharacterPolicy::kMaximumFacePixelSize &&
			       std::isfinite(a_settings.roiMargin) &&
			       a_settings.roiMargin >= CharacterPolicy::kMinimumRoiMargin &&
			       a_settings.roiMargin <= CharacterPolicy::kMaximumRoiMargin &&
			       a_settings.maximumRoiRegions >= 1 &&
			       a_settings.maximumRoiRegions <=
			           CharacterPolicy::kMaximumRoiRegions &&
			       a_settings.roiHoldFrames <=
			           CharacterPolicy::kMaximumRoiHoldFrames &&
			       a_settings.featherRadius <=
			           CharacterPolicy::kMaximumFeatherRadius &&
			       std::isfinite(a_settings.featherDepthThreshold) &&
			       a_settings.featherDepthThreshold >= 0.0f &&
			       a_settings.featherDepthThreshold <=
			           CharacterPolicy::kMaximumFeatherDepthThreshold &&
			       a_settings.maskTestMode < CharacterMaskTestMode::Count;
		}

		bool IsCategoryEnabled(
			CharacterCategory a_category,
			const CharacterSettings& a_settings) noexcept
		{
			switch (a_category) {
			case CharacterCategory::Face:
				return a_settings.faces && a_settings.faceStrength > 0.0f;
			case CharacterCategory::Skin:
				return a_settings.skin && a_settings.skinStrength > 0.0f;
			case CharacterCategory::Hair:
				return a_settings.hair && a_settings.hairStrength > 0.0f;
			default:
				return false;
			}
		}

		CharacterRect Union(const CharacterRect& a_left, const CharacterRect& a_right) noexcept
		{
			if (!a_left.IsValid())
				return a_right;
			if (!a_right.IsValid())
				return a_left;
			return {
				.minX = std::min(a_left.minX, a_right.minX),
				.minY = std::min(a_left.minY, a_right.minY),
				.maxX = std::max(a_left.maxX, a_right.maxX),
				.maxY = std::max(a_left.maxY, a_right.maxY),
			};
		}

		bool OverlapsOrNear(
			const CharacterRect& a_left,
			const CharacterRect& a_right) noexcept
		{
			return a_left.minX <= a_right.maxX + kNearbyRegionPixels &&
			       a_right.minX <= a_left.maxX + kNearbyRegionPixels &&
			       a_left.minY <= a_right.maxY + kNearbyRegionPixels &&
			       a_right.minY <= a_left.maxY + kNearbyRegionPixels;
		}

		void MergeRegions(
			std::vector<CharacterRect>& a_regions,
			std::uint32_t a_maximumRegions)
		{
			const auto mergeOverlapsToFixedPoint = [&]() {
				for (;;) {
					bool merged = false;
					for (std::size_t left = 0;
						left < a_regions.size() && !merged; ++left) {
						for (std::size_t right = left + 1;
							right < a_regions.size(); ++right) {
							if (!OverlapsOrNear(a_regions[left], a_regions[right]))
								continue;
							a_regions[left] = Union(a_regions[left], a_regions[right]);
							a_regions.erase(a_regions.begin() + right);
							merged = true;
							break;
						}
					}
					if (!merged)
						return;
				}
			};
			mergeOverlapsToFixedPoint();

			while (a_regions.size() > a_maximumRegions) {
				std::size_t bestLeft = 0;
				std::size_t bestRight = 1;
				std::uint64_t bestInflation = std::numeric_limits<std::uint64_t>::max();
				for (std::size_t left = 0; left < a_regions.size(); ++left) {
					for (std::size_t right = left + 1; right < a_regions.size(); ++right) {
						const auto combined = Union(a_regions[left], a_regions[right]);
						const auto sourceArea = a_regions[left].Area() + a_regions[right].Area();
						const auto inflation = combined.Area() > sourceArea ?
						                           combined.Area() - sourceArea :
						                           0;
						if (inflation < bestInflation) {
							bestInflation = inflation;
							bestLeft = left;
							bestRight = right;
						}
					}
				}
				a_regions[bestLeft] = Union(a_regions[bestLeft], a_regions[bestRight]);
				a_regions.erase(a_regions.begin() + bestRight);
				// A forced least-inflation union can overlap a third region.
				// Canonicalize again so coverage is a true union and slots are not redundant.
				mergeOverlapsToFixedPoint();
			}

			std::ranges::sort(a_regions, [](const CharacterRect& a_left, const CharacterRect& a_right) {
				if (a_left.minY != a_right.minY)
					return a_left.minY < a_right.minY;
				if (a_left.minX != a_right.minX)
					return a_left.minX < a_right.minX;
				return a_left.Area() > a_right.Area();
			});
		}

		class ComputeStateGuard
		{
		public:
			explicit ComputeStateGuard(ID3D11DeviceContext* a_context) noexcept :
				context_(a_context)
			{
				if (!context_)
					return;
				classInstanceCount_ = static_cast<UINT>(classInstances_.size());
				context_->CSGetShader(&shader_, classInstances_.data(), &classInstanceCount_);
				context_->CSGetConstantBuffers(0, 1, &constantBuffer_);
				context_->CSGetShaderResources(0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data());
				context_->CSGetUnorderedAccessViews(0, static_cast<UINT>(unorderedAccess_.size()), unorderedAccess_.data());
				captured_ = true;
			}

			ComputeStateGuard(const ComputeStateGuard&) = delete;
			ComputeStateGuard& operator=(const ComputeStateGuard&) = delete;

			~ComputeStateGuard() noexcept
			{
				if (captured_) {
					std::array<ID3D11ShaderResourceView*, 3> nullSrvs{};
					std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
					context_->CSSetShaderResources(0, static_cast<UINT>(nullSrvs.size()), nullSrvs.data());
					context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(nullUavs.size()), nullUavs.data(), nullptr);
					context_->CSSetShader(shader_, classInstances_.data(), classInstanceCount_);
					context_->CSSetConstantBuffers(0, 1, &constantBuffer_);
					context_->CSSetShaderResources(0, static_cast<UINT>(shaderResources_.size()), shaderResources_.data());
					context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(unorderedAccess_.size()), unorderedAccess_.data(), nullptr);
				}
				if (shader_)
					shader_->Release();
				for (UINT index = 0; index < classInstanceCount_; ++index) {
					if (classInstances_[index])
						classInstances_[index]->Release();
				}
				if (constantBuffer_)
					constantBuffer_->Release();
				for (auto* resource : shaderResources_) {
					if (resource)
						resource->Release();
				}
				for (auto* resource : unorderedAccess_) {
					if (resource)
						resource->Release();
				}
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			ID3D11ComputeShader* shader_ = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classInstances_{};
			UINT classInstanceCount_ = 0;
			ID3D11Buffer* constantBuffer_ = nullptr;
			std::array<ID3D11ShaderResourceView*, 3> shaderResources_{};
			std::array<ID3D11UnorderedAccessView*, 2> unorderedAccess_{};
			bool captured_ = false;
		};

		class OutputMergerStateGuard
		{
		public:
			explicit OutputMergerStateGuard(ID3D11DeviceContext* a_context) noexcept :
				context_(a_context)
			{
				if (!context_)
					return;
				context_->OMGetRenderTargets(
					static_cast<UINT>(renderTargets_.size()),
					renderTargets_.data(), &depthStencil_);
				context_->OMSetRenderTargets(0, nullptr, nullptr);
				captured_ = true;
			}

			OutputMergerStateGuard(const OutputMergerStateGuard&) = delete;
			OutputMergerStateGuard& operator=(const OutputMergerStateGuard&) = delete;

			~OutputMergerStateGuard() noexcept
			{
				if (captured_) {
					context_->OMSetRenderTargets(
						static_cast<UINT>(renderTargets_.size()),
						renderTargets_.data(), depthStencil_);
				}
				for (auto* renderTarget : renderTargets_) {
					if (renderTarget)
						renderTarget->Release();
				}
				if (depthStencil_)
					depthStencil_->Release();
			}

			[[nodiscard]] bool Captured() const noexcept { return captured_; }

		private:
			ID3D11DeviceContext* context_ = nullptr;
			std::array<
				ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
				renderTargets_{};
			ID3D11DepthStencilView* depthStencil_ = nullptr;
			bool captured_ = false;
		};
	}

	class CharacterRendering::State
	{
	public:
		struct Observation
		{
			std::uint32_t actorFormId = 0;
			std::uintptr_t geometryIdentity = 0;
			CharacterCategory category = CharacterCategory::None;
			float3 center{};
			float radius = 0.0f;
		};

		struct HeldRegion
		{
			CharacterRect rect{};
			std::uint32_t lastSeenFrame = 0;
		};

		struct Readback
		{
			ComPtr<ID3D11Buffer> staging;
			ComPtr<ID3D11Query> ready;
			std::uint32_t frame = 0;
			std::uint32_t eyeIndex = 0;
			std::uint32_t featureSlot = 0;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t pixelCount = 0;
			bool pending = false;
		};

		struct PrepareKey
		{
			std::uint32_t frame = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint64_t settings = 0;
			UpscalingDLSS::ViewportCrop crop{};
			void* authoredMaskIdentity = nullptr;
			std::uintptr_t authoredDepthIdentity = 0;
			std::uintptr_t currentDepthIdentity = 0;
			float captureJitterX = 0.0f;
			float captureJitterY = 0.0f;

			bool operator==(const PrepareKey&) const = default;
		};

		struct Slot
		{
			ComPtr<ID3D11Texture2D> mask;
			ComPtr<ID3D11ShaderResourceView> maskSrv;
			ComPtr<ID3D11UnorderedAccessView> maskUav;
			ComPtr<ID3D11Buffer> coverageCounter;
			ComPtr<ID3D11UnorderedAccessView> coverageCounterUav;
			std::array<Readback, kReadbackLatency> readbacks{};
			std::uint32_t nextReadbackIndex = 0;
			PrepareKey prepareKey{};
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint32_t maskCoverageFrame =
				std::numeric_limits<std::uint32_t>::max();
			std::uint32_t maskCoverageFeatureSlot = 4;
			std::uint32_t maskCoverageWidth = 0;
			std::uint32_t maskCoverageHeight = 0;
			std::uint64_t maskPixels = 0;
			float maskCoveragePercent = 0.0f;
			bool maskCoverageReady = false;
			bool requiresEvaluation = true;
			bool prepared = false;
		};

		struct alignas(16) MaskConstants
		{
			std::uint32_t outputAndSourceSize[4]{};
			std::uint32_t sourceCrop[4]{};
			std::uint32_t options[4]{};
			float featherOptions[4]{};
			float depthLinearization[4]{};
			float jitter[4]{};
			float categoryStrengths[4]{};
			float roiRectangles[CharacterPolicy::kMaximumRoiRegions][4]{};
		};
		static_assert(sizeof(MaskConstants) % 16 == 0);

		struct ProjectedPlan
		{
			std::vector<CharacterRect> regions;
			std::uint32_t visibleFaces = 0;
			std::uint32_t visibleCharacters = 0;
			std::uint32_t droppedCharacters = 0;
		};

		State()
		{
			observationKeys_.reserve(
				CharacterPolicy::kMaximumObservationsPerFrame);
		}

		void BeginObservationFrame(std::uint32_t a_frame)
		{
			if (observationFrame_ == a_frame)
				return;
			observationFrame_ = a_frame;
			observations_.clear();
			observationKeys_.clear();
			snapshot_.observationFrame = a_frame;
			snapshot_.currentObservations = 0;
			snapshot_.currentCategoryObservations = {};
			snapshot_.currentClassificationRejections = {};
		}

		void InvalidatePreparedMasks() noexcept
		{
			lastSlotForEye_ = { 4, 4 };
			snapshot_.eyes = {};
			for (auto& slot : slots_) {
				slot.prepared = false;
				slot.prepareKey = {};
			}
		}

		void InvalidatePreparedSlot(
			std::uint32_t a_featureSlot,
			std::uint32_t a_eyeIndex) noexcept
		{
			if (a_featureSlot < slots_.size()) {
				auto& slot = slots_[a_featureSlot];
				slot.prepared = false;
				slot.requiresEvaluation = true;
				slot.prepareKey = {};
			}
			if (a_eyeIndex < lastSlotForEye_.size()) {
				if (lastSlotForEye_[a_eyeIndex] == a_featureSlot)
					lastSlotForEye_[a_eyeIndex] = 4;
				snapshot_.eyes[a_eyeIndex] = {};
				snapshot_.eyes[a_eyeIndex].featureSlot = a_featureSlot;
			}
		}

		void ClearMask(
			Slot& a_slot,
			ID3D11DeviceContext* a_context,
			std::uint32_t a_frame,
			std::uint32_t a_featureSlot,
			std::uint32_t a_width,
			std::uint32_t a_height,
			float a_value) noexcept
		{
			const std::array<float, 4> clear{ a_value, a_value, a_value, a_value };
			a_context->ClearUnorderedAccessViewFloat(a_slot.maskUav.Get(), clear.data());
			a_slot.maskCoverageFrame = a_frame;
			a_slot.maskCoverageFeatureSlot = a_featureSlot;
			a_slot.maskCoverageWidth = a_width;
			a_slot.maskCoverageHeight = a_height;
			const auto pixelCount = static_cast<std::uint64_t>(a_width) * a_height;
			a_slot.maskPixels = a_value > (0.5f / 255.0f) ? pixelCount : 0;
			a_slot.maskCoveragePercent = a_slot.maskPixels ? 100.0f : 0.0f;
			a_slot.maskCoverageReady = true;
		}

		void AdoptDevice(ID3D11Device* a_device)
		{
			if (device_ && !SameIdentity(device_.Get(), a_device)) {
				slots_ = {};
				shader_.Reset();
				constants_.Reset();
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
				capturedCategoriesEmpty_ = false;
				capturedJitterX_ = 0.0f;
				capturedJitterY_ = 0.0f;
				snapshot_.categoryCaptureFrame = capturedFrame_;
				snapshot_.categoryCaptureReady = false;
				snapshot_.categoryCaptureEmpty = false;
				shaderCompileFailed_ = false;
				heldRegions_ = {};
				heldCropValid_ = {};
				InvalidatePreparedMasks();
			}
			device_ = a_device;
		}

		bool EnsureCategoryCapture(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_sourceDesc)
		{
			if (capturedCategories_) {
				D3D11_TEXTURE2D_DESC current{};
				capturedCategories_->GetDesc(&current);
				if (current.Width == a_sourceDesc.Width &&
					current.Height == a_sourceDesc.Height &&
					current.Format == a_sourceDesc.Format) {
					return true;
				}
			}

			capturedCategories_.Reset();
			capturedCategoriesSrv_.Reset();
			capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			capturedCategoriesEmpty_ = false;
			snapshot_.categoryCaptureFrame = capturedFrame_;
			snapshot_.categoryCaptureReady = false;
			snapshot_.categoryCaptureEmpty = false;
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedCategories_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedCategories_.Get(), nullptr,
					&capturedCategoriesSrv_))) {
				capturedCategories_.Reset();
				capturedCategoriesSrv_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedCategories_.Get(),
				"DLSS5CharacterRendering::FrozenCategories");
			Util::SetResourceName(
				capturedCategoriesSrv_.Get(),
				"DLSS5CharacterRendering::FrozenCategories SRV");
			return true;
		}

		bool EnsureDepthCapture(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_sourceDesc,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& a_sourceViewDesc)
		{
			if (capturedDepth_) {
				D3D11_TEXTURE2D_DESC current{};
				capturedDepth_->GetDesc(&current);
				D3D11_SHADER_RESOURCE_VIEW_DESC currentView{};
				capturedDepthSrv_->GetDesc(&currentView);
				if (current.Width == a_sourceDesc.Width &&
					current.Height == a_sourceDesc.Height &&
					current.Format == a_sourceDesc.Format &&
					currentView.Format == a_sourceViewDesc.Format) {
					return true;
				}
			}

			capturedDepth_.Reset();
			capturedDepthSrv_.Reset();
			capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			capturedCategoriesEmpty_ = false;
			snapshot_.categoryCaptureFrame = capturedFrame_;
			snapshot_.categoryCaptureReady = false;
			snapshot_.categoryCaptureEmpty = false;
			InvalidatePreparedMasks();
			D3D11_TEXTURE2D_DESC captureDesc = a_sourceDesc;
			captureDesc.Usage = D3D11_USAGE_DEFAULT;
			captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			captureDesc.CPUAccessFlags = 0;
			captureDesc.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&captureDesc, nullptr, &capturedDepth_)) ||
				FAILED(a_device->CreateShaderResourceView(
					capturedDepth_.Get(), &a_sourceViewDesc,
					&capturedDepthSrv_))) {
				capturedDepth_.Reset();
				capturedDepthSrv_.Reset();
				return false;
			}
			Util::SetResourceName(
				capturedDepth_.Get(),
				"DLSS5CharacterRendering::FrozenDepth");
			Util::SetResourceName(
				capturedDepthSrv_.Get(),
				"DLSS5CharacterRendering::FrozenDepth SRV");
			return true;
		}

		bool ValidateTexture(
			ID3D11ShaderResourceView* a_view,
			ID3D11Device* a_device,
			DXGI_FORMAT a_format,
			ComPtr<ID3D11Texture2D>& a_texture,
			D3D11_TEXTURE2D_DESC& a_desc,
			void*& a_identity,
			std::string& a_error) const
		{
			if (!a_view) {
				a_error = "required character-mask source view is null";
				return false;
			}
			ComPtr<ID3D11Resource> resource;
			a_view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&a_texture)) || !a_texture) {
				a_error = "character-mask source is not a Texture2D";
				return false;
			}
			a_texture->GetDesc(&a_desc);
			if (a_desc.Format != a_format || a_desc.MipLevels != 1 ||
				a_desc.ArraySize != 1 || a_desc.SampleDesc.Count != 1) {
				a_error = std::format(
					"character-mask source has invalid format/layout (format={})",
					static_cast<std::uint32_t>(a_desc.Format));
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_view->GetDesc(&viewDesc);
			if (viewDesc.Format != a_format ||
				viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDesc.Texture2D.MostDetailedMip != 0 ||
				viewDesc.Texture2D.MipLevels != 1) {
				a_error = "character-mask source view has invalid format or mip layout";
				return false;
			}
			ComPtr<ID3D11Device> resourceDevice;
			a_texture->GetDevice(&resourceDevice);
			if (!SameIdentity(a_device, resourceDevice.Get())) {
				a_error = "character-mask source belongs to a different D3D11 device";
				return false;
			}
			ComPtr<IUnknown> identity;
			if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&identity)))) {
				a_error = "character-mask source identity query failed";
				return false;
			}
			a_identity = identity.Get();
			return true;
		}

		bool ValidateDepthTexture(
			ID3D11ShaderResourceView* a_view,
			ID3D11Device* a_device,
			std::uint32_t a_expectedWidth,
			std::uint32_t a_expectedHeight,
			ComPtr<ID3D11Texture2D>& a_texture,
			std::uintptr_t& a_identity,
			std::string& a_error) const
		{
			if (!a_view) {
				a_error = "character mask depth view is null";
				return false;
			}
			ComPtr<ID3D11Resource> resource;
			a_view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&a_texture)) || !a_texture) {
				a_error = "character mask depth view is not a Texture2D";
				return false;
			}
			D3D11_TEXTURE2D_DESC textureDesc{};
			a_texture->GetDesc(&textureDesc);
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_view->GetDesc(&viewDesc);
			if (textureDesc.Width != a_expectedWidth ||
				textureDesc.Height != a_expectedHeight ||
				textureDesc.MipLevels != 1 || textureDesc.ArraySize != 1 ||
				textureDesc.SampleDesc.Count != 1 ||
				viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				viewDesc.Texture2D.MostDetailedMip != 0 ||
				viewDesc.Texture2D.MipLevels != 1 ||
				!IsSupportedDepthViewFormat(viewDesc.Format)) {
				a_error = std::format(
					"character mask depth view has invalid layout (format={})",
					static_cast<std::uint32_t>(viewDesc.Format));
				return false;
			}
			ComPtr<ID3D11Device> resourceDevice;
			a_texture->GetDevice(&resourceDevice);
			if (!SameIdentity(a_device, resourceDevice.Get())) {
				a_error = "character mask depth view belongs to another D3D11 device";
				return false;
			}
			a_identity = GetIdentityToken(resource.Get());
			if (!a_identity) {
				a_error = "character mask depth-view identity could not be resolved";
				return false;
			}
			return true;
		}

		bool EnsureShader(ID3D11Device* a_device)
		{
			if (shader_)
				return true;
			if (shaderCompileFailed_)
				return false;

			auto* compiled = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\DLSS5CharacterMaskCS.hlsl", {}, "cs_5_0"));
			if (!compiled) {
				shaderCompileFailed_ = true;
				return false;
			}
			shader_.Attach(compiled);
			Util::SetResourceName(shader_.Get(), "DLSS5CharacterRendering::MaskResolveCS");

			D3D11_BUFFER_DESC constantsDesc{};
			constantsDesc.ByteWidth = static_cast<UINT>(sizeof(MaskConstants));
			constantsDesc.Usage = D3D11_USAGE_DEFAULT;
			constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(a_device->CreateBuffer(&constantsDesc, nullptr, &constants_))) {
				shader_.Reset();
				shaderCompileFailed_ = true;
				return false;
			}
			Util::SetResourceName(constants_.Get(), "DLSS5CharacterRendering::MaskConstants");
			return true;
		}

		bool EnsureSlot(
			Slot& a_slot,
			ID3D11Device* a_device,
			std::uint32_t a_slotIndex,
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			if (a_slot.mask && a_slot.width == a_width && a_slot.height == a_height)
				return true;

			a_slot = {};
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = DXGI_FORMAT_R8_UNORM;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (FAILED(a_device->CreateTexture2D(&textureDesc, nullptr, &a_slot.mask)) ||
				FAILED(a_device->CreateShaderResourceView(a_slot.mask.Get(), nullptr, &a_slot.maskSrv)) ||
				FAILED(a_device->CreateUnorderedAccessView(a_slot.mask.Get(), nullptr, &a_slot.maskUav))) {
				a_slot = {};
				return false;
			}
			const auto baseName = std::format(
				"DLSS5CharacterRendering::ControlMaskSlot{}", a_slotIndex);
			Util::SetResourceName(a_slot.mask.Get(), "%s", baseName.c_str());
			Util::SetResourceName(a_slot.maskSrv.Get(), "%s SRV", baseName.c_str());
			Util::SetResourceName(a_slot.maskUav.Get(), "%s UAV", baseName.c_str());

			D3D11_BUFFER_DESC counterDesc{};
			counterDesc.ByteWidth = sizeof(std::uint32_t);
			counterDesc.Usage = D3D11_USAGE_DEFAULT;
			counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			if (FAILED(a_device->CreateBuffer(&counterDesc, nullptr, &a_slot.coverageCounter))) {
				a_slot = {};
				return false;
			}
			D3D11_UNORDERED_ACCESS_VIEW_DESC counterUavDesc{};
			counterUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			counterUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			counterUavDesc.Buffer.NumElements = 1;
			counterUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			if (FAILED(a_device->CreateUnorderedAccessView(
					a_slot.coverageCounter.Get(), &counterUavDesc,
					&a_slot.coverageCounterUav))) {
				a_slot = {};
				return false;
			}
			Util::SetResourceName(a_slot.coverageCounter.Get(), "%s Coverage", baseName.c_str());
			Util::SetResourceName(a_slot.coverageCounterUav.Get(), "%s Coverage UAV", baseName.c_str());

			D3D11_BUFFER_DESC stagingDesc = counterDesc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			D3D11_QUERY_DESC queryDesc{ D3D11_QUERY_EVENT, 0 };
			for (std::uint32_t index = 0; index < kReadbackLatency; ++index) {
				if (FAILED(a_device->CreateBuffer(
						&stagingDesc, nullptr, &a_slot.readbacks[index].staging)) ||
					FAILED(a_device->CreateQuery(
						&queryDesc, &a_slot.readbacks[index].ready))) {
					a_slot = {};
					return false;
				}
				Util::SetResourceName(
					a_slot.readbacks[index].staging.Get(),
					"%s Coverage Readback %u", baseName.c_str(), index);
				Util::SetResourceName(
					a_slot.readbacks[index].ready.Get(),
					"%s Coverage Ready %u", baseName.c_str(), index);
			}
			a_slot.width = a_width;
			a_slot.height = a_height;
			return true;
		}

		bool ProjectSphere(
			const Observation& a_observation,
			std::uint32_t a_eyeIndex,
			std::uint32_t a_width,
			std::uint32_t a_height,
			CharacterRect& a_rect) const
		{
			const auto eye = Util::GetEyePosition(static_cast<int>(a_eyeIndex));
			const auto matrix = globals::game::frameBufferCached
			                        .GetCameraViewProjUnjittered(a_eyeIndex)
			                        .Transpose();
			const float3 relative{
				a_observation.center.x - eye.x,
				a_observation.center.y - eye.y,
				a_observation.center.z - eye.z,
			};
			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();
			bool projected = false;
			bool crossesNearPlane = false;
			for (int z = -1; z <= 1; z += 2) {
				for (int y = -1; y <= 1; y += 2) {
					for (int x = -1; x <= 1; x += 2) {
						const float4 point{
							relative.x + x * a_observation.radius,
							relative.y + y * a_observation.radius,
							relative.z + z * a_observation.radius,
							1.0f,
						};
						const auto clip = DirectX::SimpleMath::Vector4::Transform(point, matrix);
						if (!std::isfinite(clip.w))
							continue;
						if (clip.w <= 1.0e-4f) {
							crossesNearPlane = true;
							continue;
						}
						const float ndcX = clip.x / clip.w;
						const float ndcY = clip.y / clip.w;
						if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
							continue;
						const float pixelX = (ndcX * 0.5f + 0.5f) * a_width;
						const float pixelY = (0.5f - ndcY * 0.5f) * a_height;
						minX = std::min(minX, pixelX);
						minY = std::min(minY, pixelY);
						maxX = std::max(maxX, pixelX);
						maxY = std::max(maxY, pixelY);
						projected = true;
					}
				}
			}
			if (!projected)
				return false;
			// A clipped sphere cannot define a bounded eligibility region without
			// admitting unrelated actors; near-plane candidates fail closed.
			if (crossesNearPlane)
				return false;
			if (maxX <= 0.0f || maxY <= 0.0f ||
				minX >= a_width || minY >= a_height) {
				return false;
			}
			a_rect = {
				.minX = static_cast<std::uint32_t>(std::clamp(
					std::floor(minX), 0.0f, static_cast<float>(a_width))),
				.minY = static_cast<std::uint32_t>(std::clamp(
					std::floor(minY), 0.0f, static_cast<float>(a_height))),
				.maxX = static_cast<std::uint32_t>(std::clamp(
					std::ceil(maxX), 0.0f, static_cast<float>(a_width))),
				.maxY = static_cast<std::uint32_t>(std::clamp(
					std::ceil(maxY), 0.0f, static_cast<float>(a_height))),
			};
			return a_rect.IsValid();
		}

		ProjectedPlan BuildPlan(
			const CharacterMaskPrepareArgs& a_args,
			const std::vector<Observation>& a_observations)
		{
			CS_PROFILE_CPU_SCOPE("Upscaling::DLSS5CharacterRoiSetup");
			ProjectedPlan result;
			struct ActorBounds
			{
				std::vector<Observation> selected;
				std::vector<Observation> faces;
				float nearestFaceDistanceUnits =
					std::numeric_limits<float>::max();
			};
			std::map<std::uint32_t, ActorBounds> actors;
			const auto averageEye = Util::GetAverageEyePosition();
			for (const auto& observation : a_observations) {
				auto& actor = actors[observation.actorFormId];
				if (observation.category == CharacterCategory::Face) {
					actor.faces.push_back(observation);
					const float dx = observation.center.x - averageEye.x;
					const float dy = observation.center.y - averageEye.y;
					const float dz = observation.center.z - averageEye.z;
					const float surfaceDistance = std::max(
						0.0f,
						std::sqrt(dx * dx + dy * dy + dz * dz) - observation.radius);
					actor.nearestFaceDistanceUnits = std::min(
						actor.nearestFaceDistanceUnits, surfaceDistance);
				}
				if (IsCategoryEnabled(observation.category, a_args.settings))
					actor.selected.push_back(observation);
			}

			struct CurrentRegion
			{
				std::uint32_t actorFormId = 0;
				CharacterRect rect{};
				std::uint64_t faceArea = 0;
			};
			const auto& crop = a_args.viewportCrop.output;
			std::vector<CurrentRegion> currentRegions;
			currentRegions.reserve(std::min<std::size_t>(
				actors.size(), CharacterPolicy::kMaximumTrackedActorsPerEye));
			for (const auto& [actorFormId, actor] : actors) {
				if (actor.selected.empty() || actor.faces.empty() ||
					Util::Units::GameUnitsToMeters(actor.nearestFaceDistanceUnits) >
						a_args.settings.maximumDistanceMeters) {
					continue;
				}

				std::array<CharacterRect, 2> faceRects{};
				std::array<CharacterRect, 2> actorRects{};
				for (std::uint32_t eye = 0; eye < faceRects.size(); ++eye) {
					for (const auto& face : actor.faces) {
						CharacterRect projected{};
						if (ProjectSphere(
								face, eye,
								a_args.viewportCrop.fullOutput.width,
								a_args.viewportCrop.fullOutput.height,
								projected)) {
							faceRects[eye] = Union(faceRects[eye], projected);
						}
					}
					for (const auto& observation : actor.selected) {
						CharacterRect projected{};
						if (ProjectSphere(
								observation, eye,
								a_args.viewportCrop.fullOutput.width,
								a_args.viewportCrop.fullOutput.height,
								projected)) {
							actorRects[eye] = Union(actorRects[eye], projected);
						}
					}
				}

				const auto& faceRect = faceRects[a_args.eyeIndex];
				const auto& actorRect = actorRects[a_args.eyeIndex];
				if (!faceRect.IsValid() || !actorRect.IsValid())
					continue;
				++result.visibleFaces;

				std::uint32_t stereoMaximumSize = 0;
				for (const auto& sizeRect : faceRects) {
					if (!sizeRect.IsValid())
						continue;
					stereoMaximumSize = std::max(
						stereoMaximumSize,
						std::max(
							sizeRect.maxX - sizeRect.minX,
							sizeRect.maxY - sizeRect.minY));
				}
				if (stereoMaximumSize < a_args.settings.minimumFacePixelSize) {
					continue;
				}

				const float marginX =
					(actorRect.maxX - actorRect.minX) * a_args.settings.roiMargin;
				const float marginY =
					(actorRect.maxY - actorRect.minY) * a_args.settings.roiMargin;
				const std::uint32_t expandedMinX = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minX - marginX)));
				const std::uint32_t expandedMinY = static_cast<std::uint32_t>(
					std::max(0.0f, std::floor(actorRect.minY - marginY)));
				const std::uint32_t expandedMaxX = static_cast<std::uint32_t>(
					std::min(
						static_cast<float>(a_args.viewportCrop.fullOutput.width),
						std::ceil(actorRect.maxX + marginX)));
				const std::uint32_t expandedMaxY = static_cast<std::uint32_t>(
					std::min(
						static_cast<float>(a_args.viewportCrop.fullOutput.height),
						std::ceil(actorRect.maxY + marginY)));
				if (expandedMaxX <= crop.left || expandedMaxY <= crop.top ||
					expandedMinX >= crop.right || expandedMinY >= crop.bottom) {
					continue;
				}
				CharacterRect local{
					.minX = std::max(expandedMinX, crop.left) - crop.left,
					.minY = std::max(expandedMinY, crop.top) - crop.top,
					.maxX = std::min(expandedMaxX, crop.right) - crop.left,
					.maxY = std::min(expandedMaxY, crop.bottom) - crop.top,
				};
				if (!local.IsValid())
					continue;

				local.minX = (local.minX / kRegionQuantization) * kRegionQuantization;
				local.minY = (local.minY / kRegionQuantization) * kRegionQuantization;
				local.maxX = std::min(
					a_args.outputWidth,
					((local.maxX + kRegionQuantization - 1) / kRegionQuantization) *
						kRegionQuantization);
				local.maxY = std::min(
					a_args.outputHeight,
					((local.maxY + kRegionQuantization - 1) / kRegionQuantization) *
						kRegionQuantization);
				if (local.IsValid()) {
					currentRegions.push_back({
						.actorFormId = actorFormId,
						.rect = local,
						.faceArea = faceRect.Area(),
					});
				}
			}

			result.visibleCharacters = static_cast<std::uint32_t>(currentRegions.size());
			std::ranges::sort(
				currentRegions,
				[](const CurrentRegion& a_left, const CurrentRegion& a_right) {
					if (a_left.faceArea != a_right.faceArea)
						return a_left.faceArea > a_right.faceArea;
					return a_left.actorFormId < a_right.actorFormId;
				});
			if (currentRegions.size() >
				CharacterPolicy::kMaximumTrackedActorsPerEye) {
				result.droppedCharacters = static_cast<std::uint32_t>(
					currentRegions.size() -
					CharacterPolicy::kMaximumTrackedActorsPerEye);
				currentRegions.resize(
					CharacterPolicy::kMaximumTrackedActorsPerEye);
			}
			auto& held = heldRegions_[a_args.featureSlot];
			const auto cropKey = a_args.viewportCrop;
			if (!heldCropValid_[a_args.featureSlot] ||
				heldCrops_[a_args.featureSlot] != cropKey) {
				held.clear();
				heldCrops_[a_args.featureSlot] = cropKey;
				heldCropValid_[a_args.featureSlot] = true;
			}
			for (const auto& current : currentRegions)
				held[current.actorFormId] = { current.rect, a_args.frameId };
			for (auto it = held.begin(); it != held.end();) {
				const auto age = a_args.frameId - it->second.lastSeenFrame;
				if (age > a_args.settings.roiHoldFrames)
					it = held.erase(it);
				else
					++it;
			}
			if (held.size() > CharacterPolicy::kMaximumTrackedActorsPerEye) {
				std::vector<std::pair<std::uint32_t, std::uint64_t>> priorities;
				priorities.reserve(held.size());
				for (const auto& [actorFormId, region] : held)
					priorities.emplace_back(actorFormId, region.rect.Area());
				std::ranges::sort(priorities, [](const auto& a_left, const auto& a_right) {
					if (a_left.second != a_right.second)
						return a_left.second > a_right.second;
					return a_left.first < a_right.first;
				});
				for (std::size_t index =
						 CharacterPolicy::kMaximumTrackedActorsPerEye;
					index < priorities.size(); ++index) {
					held.erase(priorities[index].first);
				}
			}
			result.regions.reserve(held.size());
			for (const auto& [actorFormId, region] : held) {
				(void)actorFormId;
				result.regions.push_back(region.rect);
			}
			MergeRegions(result.regions, a_args.settings.maximumRoiRegions);
			return result;
		}

		void PollReadbacks(ID3D11DeviceContext* a_context)
		{
			for (std::uint32_t slotIndex = 0; slotIndex < slots_.size(); ++slotIndex) {
				auto& slot = slots_[slotIndex];
				for (auto& readback : slot.readbacks) {
					if (!readback.pending ||
						a_context->GetData(
							readback.ready.Get(), nullptr, 0,
							D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
						continue;
					}
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(a_context->Map(
							readback.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
						std::uint32_t coveredPixels = 0;
						std::memcpy(&coveredPixels, mapped.pData, sizeof(coveredPixels));
						a_context->Unmap(readback.staging.Get(), 0);
						const bool sampleIsCurrent =
							!slot.maskCoverageReady ||
							static_cast<std::int32_t>(
								readback.frame - slot.maskCoverageFrame) >= 0;
						if (readback.featureSlot == slotIndex && sampleIsCurrent) {
							slot.maskCoverageFrame = readback.frame;
							slot.maskCoverageFeatureSlot = readback.featureSlot;
							slot.maskCoverageWidth = readback.width;
							slot.maskCoverageHeight = readback.height;
							slot.maskPixels = coveredPixels;
							slot.maskCoveragePercent = readback.pixelCount ?
							                               100.0f * static_cast<float>(coveredPixels) /
							                                   static_cast<float>(readback.pixelCount) :
							                               0.0f;
							slot.maskCoverageReady = true;
						}
					}
					readback.pending = false;
				}
			}
		}

		bool Dispatch(
			const CharacterMaskPrepareArgs& a_args,
			ID3D11ShaderResourceView* a_authoredMaskSource,
			ID3D11ShaderResourceView* a_authoredDepthSource,
			Slot& a_slot,
			const ProjectedPlan& a_plan,
			std::uint32_t a_sourceEyeWidth,
			std::uint32_t a_sourceHeight)
		{
			MaskConstants constants{};
			constants.outputAndSourceSize[0] = a_args.outputWidth;
			constants.outputAndSourceSize[1] = a_args.outputHeight;
			constants.outputAndSourceSize[2] = a_sourceEyeWidth;
			constants.outputAndSourceSize[3] = a_sourceHeight;
			constants.sourceCrop[0] = a_args.eyeIndex * a_sourceEyeWidth;
			constants.sourceCrop[1] = a_args.viewportCrop.input.left;
			constants.sourceCrop[2] = a_args.viewportCrop.input.top;
			constants.sourceCrop[3] = a_args.viewportCrop.input.Width();
			constants.options[0] = a_args.viewportCrop.input.Height();
			constants.options[1] = static_cast<std::uint32_t>(a_plan.regions.size());
			constants.options[2] = a_args.settings.depthAwareFeather ?
			                           a_args.settings.featherRadius :
			                           0;
			constants.options[3] = a_args.settings.depthAwareFeather ? 1u : 0u;
			constants.featherOptions[0] = a_args.settings.featherDepthThreshold;
			constants.featherOptions[1] = static_cast<float>(
				a_args.settings.maskTestMode);
			const bool measureCoverage =
				(a_args.frameId + a_args.featureSlot) %
					kCoverageSampleInterval ==
				0;
			constants.featherOptions[2] = measureCoverage ? 1.0f : 0.0f;
			constants.featherOptions[3] = kVisibilityDepthThreshold;
			const auto cameraData = Util::GetCameraData();
			constants.depthLinearization[0] = cameraData.x;
			constants.depthLinearization[1] = cameraData.y;
			constants.depthLinearization[2] = cameraData.z;
			constants.depthLinearization[3] = cameraData.w;
			constants.jitter[0] = capturedJitterX_;
			constants.jitter[1] = capturedJitterY_;
			constants.categoryStrengths[0] =
				a_args.settings.faces ? a_args.settings.faceStrength : 0.0f;
			constants.categoryStrengths[1] =
				a_args.settings.skin ? a_args.settings.skinStrength : 0.0f;
			constants.categoryStrengths[2] =
				a_args.settings.hair ? a_args.settings.hairStrength : 0.0f;
			for (std::size_t index = 0; index < a_plan.regions.size(); ++index) {
				const auto& rect = a_plan.regions[index];
				constants.roiRectangles[index][0] = static_cast<float>(rect.minX);
				constants.roiRectangles[index][1] = static_cast<float>(rect.minY);
				constants.roiRectangles[index][2] = static_cast<float>(rect.maxX);
				constants.roiRectangles[index][3] = static_cast<float>(rect.maxY);
			}
			a_args.context->UpdateSubresource(
				constants_.Get(), 0, nullptr, &constants, 0, 0);

			ComputeStateGuard stateGuard(a_args.context);
			if (!stateGuard.Captured())
				return false;
			if (measureCoverage) {
				const std::array<UINT, 4> clear{};
				a_args.context->ClearUnorderedAccessViewUint(
					a_slot.coverageCounterUav.Get(), clear.data());
			}
			ID3D11Buffer* constantBuffer = constants_.Get();
			std::array<ID3D11ShaderResourceView*, 3> srvs{
				a_authoredMaskSource,
				a_authoredDepthSource,
				a_args.depthGuide,
			};
			std::array<ID3D11UnorderedAccessView*, 2> uavs{
				a_slot.maskUav.Get(),
				a_slot.coverageCounterUav.Get(),
			};
			a_args.context->CSSetShader(shader_.Get(), nullptr, 0);
			a_args.context->CSSetConstantBuffers(0, 1, &constantBuffer);
			a_args.context->CSSetShaderResources(
				0, static_cast<UINT>(srvs.size()), srvs.data());
			a_args.context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
			std::array<ID3D11ShaderResourceView*, 3> boundSrvs{};
			std::array<ID3D11UnorderedAccessView*, 2> boundUavs{};
			a_args.context->CSGetShaderResources(
				0, static_cast<UINT>(boundSrvs.size()), boundSrvs.data());
			a_args.context->CSGetUnorderedAccessViews(
				0, static_cast<UINT>(boundUavs.size()), boundUavs.data());
			const bool bindingsValid =
				boundSrvs[0] == srvs[0] && boundSrvs[1] == srvs[1] &&
				boundSrvs[2] == srvs[2] &&
				boundUavs[0] == uavs[0] && boundUavs[1] == uavs[1];
			for (auto* view : boundSrvs) {
				if (view)
					view->Release();
			}
			for (auto* view : boundUavs) {
				if (view)
					view->Release();
			}
			if (!bindingsValid)
				return false;
			{
				CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterMask");
				a_args.context->Dispatch(
					(a_args.outputWidth + 7u) / 8u,
					(a_args.outputHeight + 7u) / 8u,
					1);
			}

			std::array<ID3D11UnorderedAccessView*, 2> nullUavs{};
			a_args.context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(nullUavs.size()), nullUavs.data(), nullptr);
			if (measureCoverage) {
				Readback* readback = nullptr;
				for (std::uint32_t offset = 0;
					offset < a_slot.readbacks.size(); ++offset) {
					const auto index =
						(a_slot.nextReadbackIndex + offset) %
						static_cast<std::uint32_t>(a_slot.readbacks.size());
					if (!a_slot.readbacks[index].pending) {
						readback = &a_slot.readbacks[index];
						a_slot.nextReadbackIndex =
							(index + 1u) %
							static_cast<std::uint32_t>(a_slot.readbacks.size());
						break;
					}
				}
				if (readback) {
					a_args.context->CopyResource(
						readback->staging.Get(), a_slot.coverageCounter.Get());
					a_args.context->End(readback->ready.Get());
					readback->frame = a_args.frameId;
					readback->eyeIndex = a_args.eyeIndex;
					readback->featureSlot = a_args.featureSlot;
					readback->width = a_args.outputWidth;
					readback->height = a_args.outputHeight;
					readback->pixelCount =
						static_cast<std::uint64_t>(a_args.outputWidth) *
						a_args.outputHeight;
					readback->pending = true;
				} else {
					Increment(snapshot_.readbackDrops);
				}
			}
			return true;
		}

		mutable std::mutex mutex_;
		std::vector<Observation> observations_;
		std::unordered_set<std::uintptr_t> observationKeys_;
		std::uint32_t observationFrame_ = std::numeric_limits<std::uint32_t>::max();
		std::array<std::map<std::uint32_t, HeldRegion>, 4> heldRegions_{};
		std::array<UpscalingDLSS::ViewportCrop, 4> heldCrops_{};
		std::array<bool, 4> heldCropValid_{};
		std::array<Slot, 4> slots_{};
		std::array<std::uint32_t, 2> lastSlotForEye_{ 4, 4 };
		ComPtr<ID3D11ComputeShader> shader_;
		ComPtr<ID3D11Buffer> constants_;
		ComPtr<ID3D11Texture2D> capturedCategories_;
		ComPtr<ID3D11ShaderResourceView> capturedCategoriesSrv_;
		ComPtr<ID3D11Texture2D> capturedDepth_;
		ComPtr<ID3D11ShaderResourceView> capturedDepthSrv_;
		std::uint32_t capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
		bool capturedCategoriesEmpty_ = false;
		float capturedJitterX_ = 0.0f;
		float capturedJitterY_ = 0.0f;
		ComPtr<ID3D11Device> device_;
		bool shaderCompileFailed_ = false;
		CharacterSnapshot snapshot_{};
	};

	CharacterRendering& CharacterRendering::Instance()
	{
		static CharacterRendering instance;
		return instance;
	}

	CharacterRendering::CharacterRendering() : state_(std::make_unique<State>()) {}

	CharacterRendering::~CharacterRendering() = default;

	void CharacterRendering::ObserveGeometry(
		std::uint32_t a_frame,
		std::uint32_t a_actorFormId,
		std::uintptr_t a_geometryIdentity,
		CharacterCategory a_category,
		float a_centerX,
		float a_centerY,
		float a_centerZ,
		float a_radius) noexcept
	{
		if (!state_ || !a_actorFormId || !a_geometryIdentity ||
			a_category == CharacterCategory::None ||
			!std::isfinite(a_centerX) || !std::isfinite(a_centerY) ||
			!std::isfinite(a_centerZ) || !std::isfinite(a_radius) || a_radius <= 0.0f) {
			return;
		}
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->BeginObservationFrame(a_frame);
			if (state_->observationKeys_.contains(a_geometryIdentity))
				return;
			if (state_->observations_.size() >=
				CharacterPolicy::kMaximumObservationsPerFrame) {
				Increment(state_->snapshot_.observationCapacityDrops);
				return;
			}
			state_->observationKeys_.insert(a_geometryIdentity);
			state_->observations_.push_back({
				.actorFormId = a_actorFormId,
				.geometryIdentity = a_geometryIdentity,
				.category = a_category,
				.center = { a_centerX, a_centerY, a_centerZ },
				.radius = a_radius,
			});
			Increment(state_->snapshot_.observations);
			Increment(state_->snapshot_.currentObservations);
			const auto categoryIndex = static_cast<std::size_t>(a_category) - 1u;
			if (categoryIndex <
				state_->snapshot_.currentCategoryObservations.size()) {
				Increment(state_->snapshot_.currentCategoryObservations[categoryIndex]);
			}
		} catch (...) {
			// Render-hook observation must not affect the engine draw.
		}
	}

	void CharacterRendering::ObserveClassificationRejection(
		std::uint32_t a_frame,
		CharacterClassificationRejection a_reason) noexcept
	{
		if (!state_ || a_reason >= CharacterClassificationRejection::Count)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->BeginObservationFrame(a_frame);
			const auto index = static_cast<std::size_t>(a_reason);
			Increment(state_->snapshot_.currentClassificationRejections[index]);
			Increment(state_->snapshot_.classificationRejections[index]);
		} catch (...) {
			// Classification diagnostics must not affect the engine draw.
		}
	}

	bool CharacterRendering::CaptureAuthoredCategories(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_categorySource,
		ID3D11ShaderResourceView* a_depthSource,
		std::uint32_t a_frame,
		std::uint32_t a_enabledCategoryMask,
		float a_jitterX,
		float a_jitterY) noexcept
	{
		if (!state_)
			return false;
		try {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureAttempts);
			const auto fail = [&](std::string a_detail) {
				Increment(state_->snapshot_.categoryCaptureFailures);
				state_->capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
				state_->capturedCategoriesEmpty_ = false;
				state_->capturedJitterX_ = 0.0f;
				state_->capturedJitterY_ = 0.0f;
				state_->snapshot_.categoryCaptureFrame = state_->capturedFrame_;
				state_->snapshot_.categoryCaptureReady = false;
				state_->snapshot_.categoryCaptureEmpty = false;
				state_->InvalidatePreparedMasks();
				state_->snapshot_.status = "failed";
				state_->snapshot_.detail = std::move(a_detail);
				return false;
			};
			if (!a_device || !a_context || !a_categorySource || !a_depthSource ||
				a_frame == std::numeric_limits<std::uint32_t>::max() ||
				(a_enabledCategoryMask & ~0xEu) != 0 ||
				!std::isfinite(a_jitterX) || !std::isfinite(a_jitterY) ||
				a_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
				return fail("character category capture arguments are invalid");
			}
			ComPtr<ID3D11Device> contextDevice;
			a_context->GetDevice(&contextDevice);
			ComPtr<ID3D11Device> categoryDevice;
			a_categorySource->GetDevice(&categoryDevice);
			ComPtr<ID3D11Resource> depthResource;
			a_depthSource->GetResource(&depthResource);
			ComPtr<ID3D11Texture2D> depthTexture;
			ComPtr<ID3D11Device> depthDevice;
			if (!depthResource || FAILED(depthResource.As(&depthTexture)) ||
				!depthTexture) {
				return fail("character depth capture source is not a Texture2D");
			}
			depthTexture->GetDevice(&depthDevice);
			if (!SameIdentity(a_device, contextDevice.Get()) ||
				!SameIdentity(a_device, categoryDevice.Get()) ||
				!SameIdentity(a_device, depthDevice.Get())) {
				return fail("character category capture resources use different devices");
			}

			state_->AdoptDevice(a_device);
			state_->InvalidatePreparedMasks();
			const bool hasSelectedObservation =
				state_->observationFrame_ == a_frame &&
				std::ranges::any_of(
					state_->observations_,
					[&](const State::Observation& a_observation) {
						return (a_enabledCategoryMask &
								   CharacterPolicy::CategoryBit(a_observation.category)) != 0;
					});
			if (!hasSelectedObservation) {
				// A logical empty capture lets the stereo path bypass Feature 18
				// without copying the combined-resolution G-buffer every empty frame.
				state_->capturedFrame_ = a_frame;
				state_->capturedCategoriesEmpty_ = true;
				state_->capturedJitterX_ = a_jitterX;
				state_->capturedJitterY_ = a_jitterY;
				state_->snapshot_.categoryCaptureFrame = a_frame;
				state_->snapshot_.categoryCaptureReady = true;
				state_->snapshot_.categoryCaptureEmpty = true;
				state_->snapshot_.status = "ready_empty";
				state_->snapshot_.detail =
					"no enabled NPC character materials were observed; semantic snapshot bypassed";
				Increment(state_->snapshot_.categoryCaptureSuccesses);
				Increment(state_->snapshot_.categoryCaptureEmptyBypasses);
				return true;
			}

			D3D11_TEXTURE2D_DESC categoryDesc{};
			a_categorySource->GetDesc(&categoryDesc);
			D3D11_TEXTURE2D_DESC depthDesc{};
			depthTexture->GetDesc(&depthDesc);
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDesc{};
			a_depthSource->GetDesc(&depthViewDesc);
			if (categoryDesc.Format != DXGI_FORMAT_R16G16B16A16_UNORM ||
				categoryDesc.MipLevels != 1 || categoryDesc.ArraySize != 1 ||
				categoryDesc.SampleDesc.Count != 1 ||
				categoryDesc.Usage != D3D11_USAGE_DEFAULT ||
				(categoryDesc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
				return fail("character category source has an invalid layout");
			}
			if (depthDesc.Width != categoryDesc.Width ||
				depthDesc.Height != categoryDesc.Height ||
				depthDesc.MipLevels != 1 || depthDesc.ArraySize != 1 ||
				depthDesc.SampleDesc.Count != 1 ||
				depthDesc.Usage != D3D11_USAGE_DEFAULT ||
				depthViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				depthViewDesc.Texture2D.MostDetailedMip != 0 ||
				depthViewDesc.Texture2D.MipLevels != 1 ||
				!IsSupportedDepthViewFormat(depthViewDesc.Format)) {
				return fail("character depth capture source has an invalid layout");
			}
			if (!state_->EnsureCategoryCapture(a_device, categoryDesc)) {
				return fail("character category capture texture could not be created");
			}
			if (!state_->EnsureDepthCapture(
					a_device, depthDesc, depthViewDesc)) {
				return fail("character depth capture texture could not be created");
			}
			OutputMergerStateGuard outputMerger(a_context);
			if (!outputMerger.Captured())
				return fail("character category capture could not preserve output state");
			{
				CS_PROFILE_SCOPE("Upscaling::DLSS5CharacterCategoryCapture");
				a_context->CopyResource(
					state_->capturedCategories_.Get(), a_categorySource);
				a_context->CopyResource(
					state_->capturedDepth_.Get(), depthTexture.Get());
			}
			state_->capturedFrame_ = a_frame;
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedJitterX_ = a_jitterX;
			state_->capturedJitterY_ = a_jitterY;
			state_->snapshot_.categoryCaptureFrame = a_frame;
			state_->snapshot_.categoryCaptureReady = true;
			state_->snapshot_.categoryCaptureEmpty = false;
			Increment(state_->snapshot_.categoryCaptureSuccesses);
			state_->snapshot_.status = "captured";
			state_->snapshot_.detail =
				"same-frame character categories, frozen depth, and render jitter captured";
			return true;
		} catch (const std::exception& exception) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureFailures);
			state_->capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedJitterX_ = 0.0f;
			state_->capturedJitterY_ = 0.0f;
			state_->snapshot_.categoryCaptureFrame = state_->capturedFrame_;
			state_->snapshot_.categoryCaptureReady = false;
			state_->snapshot_.categoryCaptureEmpty = false;
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = exception.what();
			return false;
		} catch (...) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.categoryCaptureFailures);
			state_->capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedJitterX_ = 0.0f;
			state_->capturedJitterY_ = 0.0f;
			state_->snapshot_.categoryCaptureFrame = state_->capturedFrame_;
			state_->snapshot_.categoryCaptureReady = false;
			state_->snapshot_.categoryCaptureEmpty = false;
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = "unknown character category capture exception";
			return false;
		}
	}

	bool CharacterRendering::PrepareMask(
		const CharacterMaskPrepareArgs& a_args,
		CharacterMaskPrepareResult& a_result) noexcept
	{
		a_result = {};
		if (!state_)
			return false;
		try {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationAttempts);
			state_->snapshot_.enabled = a_args.settings.enabled;
			const auto fail = [&](std::string a_detail) {
				Increment(state_->snapshot_.preparationFailures);
				state_->InvalidatePreparedSlot(
					a_args.featureSlot, a_args.eyeIndex);
				state_->snapshot_.status = "failed";
				state_->snapshot_.detail = std::move(a_detail);
				return false;
			};

			if (!a_args.settings.enabled)
				return fail("character mask preparation was requested while disabled");
			if (!a_args.device || !a_args.context || a_args.eyeIndex >= 2 ||
				a_args.featureSlot >= state_->slots_.size() ||
				(a_args.featureSlot & 1u) != a_args.eyeIndex ||
				a_args.frameId == std::numeric_limits<std::uint32_t>::max() ||
				!a_args.outputWidth || !a_args.outputHeight ||
				!a_args.viewportCrop.MatchesEvaluationExtents(
					a_args.viewportCrop.input.Width(),
					a_args.viewportCrop.input.Height(),
					a_args.outputWidth,
					a_args.outputHeight) ||
				!IsFiniteSettings(a_args.settings)) {
				return fail("character mask preparation arguments are invalid");
			}
			if (a_args.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return fail("character masks require the immediate D3D11 context");
			ComPtr<ID3D11Device> contextDevice;
			a_args.context->GetDevice(&contextDevice);
			if (!SameIdentity(a_args.device, contextDevice.Get()))
				return fail("character mask context belongs to another D3D11 device");

			state_->AdoptDevice(a_args.device);
			state_->PollReadbacks(a_args.context);
			const bool logicalEmptyCapture = state_->capturedCategoriesEmpty_;
			if (state_->capturedFrame_ != a_args.frameId ||
				(!logicalEmptyCapture &&
					(!state_->capturedCategoriesSrv_ ||
						!state_->capturedDepthSrv_))) {
				return fail("no same-frame pre-decal character category capture is available");
			}

			ComPtr<ID3D11Texture2D> sourceTexture;
			D3D11_TEXTURE2D_DESC sourceDesc{};
			void* sourceIdentity = nullptr;
			std::uint32_t sourceEyeWidth = 0;
			std::uintptr_t currentDepthIdentity = 0;
			std::uintptr_t authoredDepthIdentity = 0;
			if (!logicalEmptyCapture) {
				std::string sourceError;
				if (!state_->ValidateTexture(
						state_->capturedCategoriesSrv_.Get(),
						a_args.device,
						DXGI_FORMAT_R16G16B16A16_UNORM,
						sourceTexture,
						sourceDesc,
						sourceIdentity,
						sourceError)) {
					return fail(std::move(sourceError));
				}
				if ((sourceDesc.Width & 1u) != 0)
					return fail("combined character-mask source width is not stereo-even");
				sourceEyeWidth = sourceDesc.Width / 2u;
				if (a_args.viewportCrop.fullInput.width > sourceEyeWidth ||
					a_args.viewportCrop.fullInput.height > sourceDesc.Height) {
					return fail(std::format(
						"character-mask source {}x{} cannot contain logical stereo input {}x{} per eye",
						sourceDesc.Width,
						sourceDesc.Height,
						a_args.viewportCrop.fullInput.width,
						a_args.viewportCrop.fullInput.height));
				}
				ComPtr<ID3D11Texture2D> authoredDepthTexture;
				std::string authoredDepthError;
				if (!state_->ValidateDepthTexture(
						state_->capturedDepthSrv_.Get(), a_args.device,
						sourceDesc.Width, sourceDesc.Height,
						authoredDepthTexture, authoredDepthIdentity,
						authoredDepthError)) {
					return fail(std::move(authoredDepthError));
				}
				ComPtr<ID3D11Texture2D> currentDepthTexture;
				std::string depthError;
				if (!state_->ValidateDepthTexture(
						a_args.depthGuide, a_args.device,
						a_args.viewportCrop.input.Width(),
						a_args.viewportCrop.input.Height(),
						currentDepthTexture, currentDepthIdentity,
						depthError)) {
					return fail(std::move(depthError));
				}
			}

			auto& slot = state_->slots_[a_args.featureSlot];
			if (!state_->EnsureSlot(
					slot, a_args.device, a_args.featureSlot,
					a_args.outputWidth, a_args.outputHeight)) {
				return fail("DLSS5 character-mask GPU resources could not be created");
			}

			const State::PrepareKey key{
				.frame = a_args.frameId,
				.width = a_args.outputWidth,
				.height = a_args.outputHeight,
				.settings = BuildSettingsKey(a_args.settings),
				.crop = a_args.viewportCrop,
				.authoredMaskIdentity = sourceIdentity,
				.authoredDepthIdentity = authoredDepthIdentity,
				.currentDepthIdentity = currentDepthIdentity,
				.captureJitterX = state_->capturedJitterX_,
				.captureJitterY = state_->capturedJitterY_,
			};
			if (!slot.prepared || slot.prepareKey != key) {
				const std::vector<State::Observation> observations =
					state_->observationFrame_ == a_args.frameId ?
						state_->observations_ :
						std::vector<State::Observation>{};
				const auto plan = state_->BuildPlan(a_args, observations);
				slot.requiresEvaluation =
					a_args.settings.maskTestMode != CharacterMaskTestMode::Authored ||
					(!logicalEmptyCapture && !plan.regions.empty());
				if (logicalEmptyCapture) {
					float clearValue = 0.0f;
					switch (a_args.settings.maskTestMode) {
					case CharacterMaskTestMode::ForceOne:
					case CharacterMaskTestMode::InvertAuthored:
						clearValue = 1.0f;
						break;
					case CharacterMaskTestMode::ForceHalf:
						clearValue = 0.5f;
						break;
					default:
						break;
					}
					state_->ClearMask(
						slot, a_args.context, a_args.frameId,
						a_args.featureSlot, a_args.outputWidth,
						a_args.outputHeight, clearValue);
				} else if (slot.requiresEvaluation) {
					if (!state_->EnsureShader(a_args.device)) {
						return fail(
							"DLSS5 character-mask compute shader could not be created");
					}
					if (!state_->Dispatch(
							a_args,
							state_->capturedCategoriesSrv_.Get(),
							state_->capturedDepthSrv_.Get(),
							slot, plan,
							sourceEyeWidth, sourceDesc.Height)) {
						return fail("DLSS5 character-mask dispatch failed");
					}
				} else {
					state_->ClearMask(
						slot, a_args.context, a_args.frameId,
						a_args.featureSlot, a_args.outputWidth,
						a_args.outputHeight, 0.0f);
				}
				slot.prepareKey = key;
				slot.prepared = true;

				auto& eye = state_->snapshot_.eyes[a_args.eyeIndex];
				eye.frame = a_args.frameId;
				eye.featureSlot = a_args.featureSlot;
				eye.evaluationWidth = a_args.outputWidth;
				eye.evaluationHeight = a_args.outputHeight;
				eye.visibleFaces = plan.visibleFaces;
				eye.visibleCharacterRegions = plan.visibleCharacters;
				eye.droppedCharacterRegions = plan.droppedCharacters;
				eye.mergedRegions = static_cast<std::uint32_t>(plan.regions.size());
				eye.regions = plan.regions;
				eye.roiPixels = 0;
				for (const auto& rect : plan.regions)
					eye.roiPixels += rect.Area();
				const auto evaluationPixels =
					static_cast<std::uint64_t>(a_args.outputWidth) * a_args.outputHeight;
				eye.roiCoveragePercent = evaluationPixels ?
				                             100.0f * static_cast<float>(eye.roiPixels) /
				                                 static_cast<float>(evaluationPixels) :
				                             0.0f;
				eye.maskPixels = slot.maskPixels;
				eye.maskCoveragePercent = slot.maskCoveragePercent;
				eye.maskCoverageFrame = slot.maskCoverageFrame;
				eye.maskCoverageFeatureSlot = slot.maskCoverageFeatureSlot;
				eye.maskCoverageWidth = slot.maskCoverageWidth;
				eye.maskCoverageHeight = slot.maskCoverageHeight;
				eye.maskCoverageReady = slot.maskCoverageReady;
				eye.maskPrepared = true;
				eye.evaluationRequired = slot.requiresEvaluation;
				state_->lastSlotForEye_[a_args.eyeIndex] = a_args.featureSlot;
			}

			a_result.controlMask = slot.mask;
			a_result.prepared = true;
			a_result.requiresEvaluation = slot.requiresEvaluation;
			Increment(state_->snapshot_.preparationSuccesses);
			state_->snapshot_.status = "ready";
			state_->snapshot_.detail = std::format(
				"Feature 18 ControlMask prepared for eye {} slot {} at {}x{}; evaluation={}; private single-subrect ROI remains disabled",
				a_args.eyeIndex,
				a_args.featureSlot,
				a_args.outputWidth,
				a_args.outputHeight,
				slot.requiresEvaluation ? "required" : "bypassed-empty");
			return true;
		} catch (const std::exception& exception) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationFailures);
			state_->InvalidatePreparedSlot(
				a_args.featureSlot, a_args.eyeIndex);
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = exception.what();
			return false;
		} catch (...) {
			std::scoped_lock lock(state_->mutex_);
			Increment(state_->snapshot_.preparationFailures);
			state_->InvalidatePreparedSlot(
				a_args.featureSlot, a_args.eyeIndex);
			state_->snapshot_.status = "failed";
			state_->snapshot_.detail = "unknown character-mask preparation exception";
			return false;
		}
	}

	void CharacterRendering::Reset() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->observations_.clear();
			state_->observationKeys_.clear();
			state_->observationFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->heldRegions_ = {};
			state_->heldCropValid_ = {};
			state_->slots_ = {};
			state_->shader_.Reset();
			state_->constants_.Reset();
			state_->shaderCompileFailed_ = false;
			state_->capturedCategories_.Reset();
			state_->capturedCategoriesSrv_.Reset();
			state_->capturedDepth_.Reset();
			state_->capturedDepthSrv_.Reset();
			state_->capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedJitterX_ = 0.0f;
			state_->capturedJitterY_ = 0.0f;
			state_->device_.Reset();
			state_->lastSlotForEye_ = { 4, 4 };
			state_->snapshot_ = {};
		} catch (...) {
		}
	}

	void CharacterRendering::Invalidate() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->heldRegions_ = {};
			state_->heldCropValid_ = {};
			state_->capturedFrame_ = std::numeric_limits<std::uint32_t>::max();
			state_->capturedCategoriesEmpty_ = false;
			state_->capturedJitterX_ = 0.0f;
			state_->capturedJitterY_ = 0.0f;
			state_->snapshot_.categoryCaptureFrame = state_->capturedFrame_;
			state_->snapshot_.categoryCaptureReady = false;
			state_->snapshot_.categoryCaptureEmpty = false;
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "invalidated";
			state_->snapshot_.detail.clear();
		} catch (...) {
		}
	}

	void CharacterRendering::ResetShaderCache() noexcept
	{
		if (!state_)
			return;
		try {
			std::scoped_lock lock(state_->mutex_);
			state_->shader_.Reset();
			state_->constants_.Reset();
			state_->shaderCompileFailed_ = false;
			state_->InvalidatePreparedMasks();
			state_->snapshot_.status = "shader_cache_reset";
			state_->snapshot_.detail.clear();
		} catch (...) {
		}
	}

	CharacterSnapshot CharacterRendering::GetSnapshot() const
	{
		if (!state_)
			return {};
		std::scoped_lock lock(state_->mutex_);
		return state_->snapshot_;
	}

	ComPtr<ID3D11ShaderResourceView> CharacterRendering::GetDebugMaskSrv(
		std::uint32_t a_eyeIndex) const noexcept
	{
		if (!state_ || a_eyeIndex >= 2)
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto slot = state_->lastSlotForEye_[a_eyeIndex];
			return slot < state_->slots_.size() && state_->slots_[slot].prepared ?
			           state_->slots_[slot].maskSrv :
			           ComPtr<ID3D11ShaderResourceView>{};
		} catch (...) {
			return {};
		}
	}

	ComPtr<ID3D11ShaderResourceView> CharacterRendering::GetPreparedMaskSrv(
		std::uint32_t a_featureSlot,
		std::uint32_t a_frameId,
		std::uint32_t a_width,
		std::uint32_t a_height) const noexcept
	{
		if (!state_ || a_featureSlot >= state_->slots_.size())
			return {};
		try {
			std::scoped_lock lock(state_->mutex_);
			const auto& slot = state_->slots_[a_featureSlot];
			return slot.prepared &&
			               slot.prepareKey.frame == a_frameId &&
			               slot.width == a_width &&
			               slot.height == a_height ?
			           slot.maskSrv :
			           ComPtr<ID3D11ShaderResourceView>{};
		} catch (...) {
			return {};
		}
	}
}
