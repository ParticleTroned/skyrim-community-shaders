#include "ModelResolution.h"
#include "ComputeStateGuard.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "CharacterMaskReadback.h"
#	include <nlohmann/json.hpp>
#endif
#include "GpuPass.h"
#include "Utils/ComIdentity.h"
#include "Utils/ResourceName.h"
#include "Utils/ShaderCompiler.h"

#include <algorithm>
#include <array>
#include <format>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		struct Constants
		{
			std::array<std::uint32_t, 2> sourceSize, modelSize;
			std::array<std::uint32_t, 2> sourceOffset, sourceRegion, modelOffset, modelRegion;
			std::uint32_t hasMask = 0, outputFormat = 0;
			std::array<float, 2> motionScale{ 1.0f, 1.0f };
			std::array<float, 2> fullEyeSize{}, cropOrigin{};
			float centralScale = 1.0f, centralHorizontalScale = 1.0f, centralFeather = 0.0f;
			std::uint32_t centralActive = 0;
			std::array<float, 2> centralOffset{}, finalOutputSize{};
			std::uint32_t packedCount = 0, packedCoverageGuard = 0;
			std::array<std::uint32_t, 2> unpackedSize{};
			std::array<PackedCropLayout::Tile, kMaximumPackedCrops> packedTiles{};
			std::uint32_t selectionMode = 0, outputStorageFormat = 0, selectedOutput = 0, selectionPadding = 0;
			std::array<float, 4> selectionBounds{};
		};
		static_assert(sizeof(Constants) == 352);
		static_assert(sizeof(PackedCropLayout::Tile) == 48);

		bool ReadDescription(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc)
		{
			ComPtr<ID3D11Texture2D> texture;
			if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
				return false;
			texture->GetDesc(&desc);
			return desc.ArraySize == 1 && desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
		}

		bool ValidSelectionView(const RendererApplyArgs& input, UpscalingDLSS::Extent size, ID3D11ShaderResourceView* selection)
		{
			if (!selection)
				return false;
			ComPtr<ID3D11Resource> resource;
			selection->GetResource(&resource);
			if (!resource)
				return false;
			D3D11_TEXTURE2D_DESC desc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			selection->GetDesc(&view);
			ComPtr<ID3D11Device> owner;
			resource->GetDevice(&owner);
			return ReadDescription(resource.Get(), desc) && desc.Width == size.width && desc.Height == size.height &&
			       Util::SameIdentity(owner.Get(), input.device) && view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
			       view.Texture2D.MostDetailedMip == 0 && view.Texture2D.MipLevels == 1 &&
			       (view.Format == DXGI_FORMAT_R8_UNORM || view.Format == DXGI_FORMAT_R32_FLOAT);
		}

		bool CreateTexture(ID3D11Device* device, Color::Texture& texture, UpscalingDLSS::Extent extent,
			DXGI_FORMAT format, const std::string& name, HRESULT& result)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = extent.width;
			desc.Height = extent.height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = format;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			result = device->CreateTexture2D(&desc, nullptr, &texture.resource);
			if (texture.resource)
				Util::SetResourceName(texture.resource.Get(), "%s", name.c_str());
			if (SUCCEEDED(result))
				result = device->CreateShaderResourceView(texture.resource.Get(), nullptr, &texture.srv);
			if (texture.srv)
				Util::SetResourceName(texture.srv.Get(), "%s SRV", name.c_str());
			if (SUCCEEDED(result))
				result = device->CreateUnorderedAccessView(texture.resource.Get(), nullptr, &texture.uav);
			if (texture.uav)
				Util::SetResourceName(texture.uav.Get(), "%s UAV", name.c_str());
			return SUCCEEDED(result);
		}

		bool EnsureSourceView(ID3D11Device* device, ID3D11Resource* resource,
			ComPtr<ID3D11Resource>& retained, ComPtr<ID3D11ShaderResourceView>& view, const char* name, HRESULT& result)
		{
			if (resource == retained.Get() && view)
				return true;
			ComPtr<ID3D11ShaderResourceView> replacement;
			result = device->CreateShaderResourceView(resource, nullptr, &replacement);
			if (FAILED(result))
				return false;
			Util::SetResourceName(replacement.Get(), "%s SRV", name);
			retained = resource;
			view = std::move(replacement);
			return true;
		}

		std::uint32_t OutputFormat(DXGI_FORMAT format)
		{
			switch (format) {
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				return 1u;
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return 2u;
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
				return 3u;
			default:
				return 0u;
			}
		}

		bool ValidBatchLayout(std::span<const RendererApplyArgs> args) noexcept
		{
			if (args.empty() || args.size() > kMaximumRegionEvaluations || !args.front().device || !args.front().context)
				return false;
			for (std::size_t index = 0; index < args.size(); ++index) {
				const auto& input = args[index];
				if (input.featureSlot >= kLogicalFeatureSlotCount || !Util::SameIdentity(input.device, args.front().device) ||
					!Util::SameIdentity(input.context, args.front().context))
					return false;
				for (std::size_t previous = 0; previous < index; ++previous)
					if (args[previous].featureSlot == input.featureSlot)
						return false;
			}
			return true;
		}

		bool ValidateSelectedOutputs(std::span<const RendererApplyArgs> args)
		{
			for (std::size_t index = 0; index < args.size(); ++index) {
				const auto& input = args[index];
				if (!input.modelOutputSelection)
					continue;
				const auto& selection = *input.modelOutputSelection;
				ComPtr<ID3D11Resource> destination;
				if (!selection.destination || input.renderingMode != RenderingMode::ReducedResolution)
					return false;
				selection.destination->GetResource(&destination);
				D3D11_TEXTURE2D_DESC desc{};
				D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
				selection.destination->GetDesc(&view);
				ComPtr<ID3D11Device> owner;
				if (!destination || !Util::SameIdentity(destination.Get(), input.colorOutput) ||
					!ReadDescription(destination.Get(), desc) || desc.Width != input.outputWidth || desc.Height != input.outputHeight ||
					desc.Usage != D3D11_USAGE_DEFAULT || view.Format != desc.Format ||
					view.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice != 0)
					return false;
				destination->GetDevice(&owner);
				if (!Util::SameIdentity(owner.Get(), input.device))
					return false;
				switch (desc.Format) {
				case DXGI_FORMAT_R32G32B32A32_FLOAT:
				case DXGI_FORMAT_R16G16B16A16_FLOAT:
				case DXGI_FORMAT_R11G11B10_FLOAT:
				case DXGI_FORMAT_R8G8B8A8_UNORM:
				case DXGI_FORMAT_R10G10B10A2_UNORM:
				case DXGI_FORMAT_R16G16B16A16_UNORM:
					break;
				default:
					return false;
				}
				if (input.characterVisualIsolation && !selection.mask)
					return false;
				if (selection.providerBlending != input.providerBlending)
					return false;
				if (selection.mask && (!selection.maskSupport.Fits(input.outputWidth, input.outputHeight) ||
										  !ValidSelectionView(input, { input.outputWidth, input.outputHeight }, selection.mask.Get())))
					return false;
				if (selection.providerBlending && (!selection.mask || !input.providerBlending))
					return false;
				for (std::size_t other = 0; other < args.size(); ++other) {
					const auto& source = args[other];
					ComPtr<ID3D11Resource> actorMask, outputMask;
					if (source.actorSelection)
						source.actorSelection->GetResource(&actorMask);
					if (source.modelOutputSelection && source.modelOutputSelection->mask)
						source.modelOutputSelection->mask->GetResource(&outputMask);
					for (auto* resource : { source.colorInput, source.depthGuide, source.motionVectors,
							 source.controlMask.Get(), actorMask.Get(), outputMask.Get() })
						if (resource && Util::SameIdentity(destination.Get(), resource))
							return false;
					if (other != index && Util::SameIdentity(destination.Get(), source.colorOutput))
						return false;
				}
			}
			return true;
		}

		struct AllocationKey
		{
			UpscalingDLSS::Extent sourceSize{}, modelSize{}, modelGuideSize{};
			DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN, outputFormat = DXGI_FORMAT_UNKNOWN, motionFormat = DXGI_FORMAT_UNKNOWN;
			bool hasMask = false;
			ModelResolution::Sharing sharing = ModelResolution::Sharing::Private;
			bool selectedOutput = false;

			bool operator==(const AllocationKey&) const = default;

			static std::optional<AllocationKey> Read(const RendererApplyArgs& input, const RendererApplyArgs& projected, ModelResolution::Sharing sharing = ModelResolution::Sharing::Private)
			{
				D3D11_TEXTURE2D_DESC color{}, output{}, motion{};
				if (!ReadDescription(input.colorInput, color) || !ReadDescription(input.colorOutput, output) ||
					!ReadDescription(input.motionVectors, motion) ||
					color.Width != input.colorWidth || color.Height != input.colorHeight ||
					output.Width != input.outputWidth || output.Height != input.outputHeight ||
					motion.Width != input.guideWidth || motion.Height != input.guideHeight)
					return std::nullopt;
				return AllocationKey{ { input.outputWidth, input.outputHeight }, { projected.outputWidth, projected.outputHeight }, { projected.guideWidth, projected.guideHeight },
					color.Format, output.Format, motion.Format, input.controlMask != nullptr, sharing, input.modelOutputSelection.has_value() };
			}

			std::optional<std::uint64_t> Bytes() const noexcept
			{
				const auto fullOutput = selectedOutput ? std::optional<std::uint64_t>{ 0 } :
				                                         LogicalTextureBytes(outputFormat, sourceSize.width, sourceSize.height);
				std::uint64_t total = 0;
				const std::array formats{ colorFormat, DXGI_FORMAT_R32_FLOAT, motionFormat, outputFormat };
				const std::array sizes{ modelSize, modelGuideSize, modelGuideSize, modelSize };
				for (std::size_t index = 0; index < formats.size(); ++index) {
					if (sharing == ModelResolution::Sharing::All ||
						(sharing == ModelResolution::Sharing::Guides && (index == 1 || index == 2)))
						continue;
					const auto bytes = LogicalTextureBytes(formats[index], sizes[index].width, sizes[index].height);
					if (!bytes)
						return std::nullopt;
					total += *bytes;
				}
				if (hasMask)
					total += static_cast<std::uint64_t>(modelSize.width) * modelSize.height;
				return fullOutput ? std::optional{ total + *fullOutput } : std::nullopt;
			}
		};

		bool ValidateTarget(ID3D11Device* device, const Color::Texture& target,
			UpscalingDLSS::Extent size, DXGI_FORMAT format)
		{
			D3D11_TEXTURE2D_DESC desc{};
			if (!target.resource || !target.srv || !target.uav || !ReadDescription(target.resource.Get(), desc) ||
				desc.Width != size.width || desc.Height != size.height || desc.Format != format || desc.Usage != D3D11_USAGE_DEFAULT)
				return false;
			ComPtr<ID3D11Device> owner;
			target.resource->GetDevice(&owner);
			if (!Util::SameIdentity(owner.Get(), device))
				return false;
			ComPtr<ID3D11Resource> srvResource, uavResource;
			target.srv->GetResource(&srvResource);
			target.uav->GetResource(&uavResource);
			if (!Util::SameIdentity(srvResource.Get(), target.resource.Get()) || !Util::SameIdentity(uavResource.Get(), target.resource.Get()))
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			target.srv->GetDesc(&srv);
			target.uav->GetDesc(&uav);
			return srv.Format == format && srv.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
			       srv.Texture2D.MostDetailedMip == 0 && srv.Texture2D.MipLevels == 1 &&
			       uav.Format == format && uav.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D && uav.Texture2D.MipSlice == 0;
		}

		bool ValidateTargets(std::span<const RendererApplyArgs> args, std::span<const ModelResolution::Targets> targets, ModelResolution::Sharing sharing)
		{
			std::array<ComPtr<IUnknown>, kMaximumRegionEvaluations * 4> identities{};
			std::array<ComPtr<IUnknown>, kMaximumRegionEvaluations * 7> sources{};
			std::size_t count = 0, sourceCount = 0;
			for (const auto& input : args) {
				for (auto* source : { input.colorInput, input.depthGuide, input.motionVectors, input.colorOutput }) {
					if (!(sources[sourceCount] = Util::GetComIdentity(source)))
						return false;
					++sourceCount;
				}
				if (input.controlMask) {
					if (!(sources[sourceCount] = Util::GetComIdentity(input.controlMask.Get())))
						return false;
					++sourceCount;
				}
				for (auto* view : { input.actorSelection.Get(), input.modelOutputSelection ? input.modelOutputSelection->mask.Get() : nullptr }) {
					if (!view)
						continue;
					ComPtr<ID3D11Resource> selection;
					view->GetResource(&selection);
					if (!(sources[sourceCount] = Util::GetComIdentity(selection.Get())))
						return false;
					++sourceCount;
				}
			}
			for (std::size_t index = 0; index < args.size(); ++index) {
				RendererApplyArgs projected;
				if (!ModelResolution::Project(args[index], projected))
					return false;
				const auto allocation = AllocationKey::Read(args[index], projected, sharing);
				if (!allocation)
					return false;
				const auto& t = targets[index];
				const std::array formats{ allocation->colorFormat, DXGI_FORMAT_R32_FLOAT, allocation->motionFormat, allocation->outputFormat };
				const std::array textures{ &t.color, &t.depth, &t.motion, &t.output };
				const std::array sizes{ allocation->modelSize, allocation->modelGuideSize, allocation->modelGuideSize, allocation->modelSize };
				for (std::size_t role = 0; role < textures.size(); ++role) {
					if (sharing == ModelResolution::Sharing::Guides && (role == 0 || role == 3)) {
						if (textures[role]->resource || textures[role]->srv || textures[role]->uav)
							return false;
						continue;
					}
					if (!ValidateTarget(args[index].device, *textures[role], sizes[role], formats[role]))
						return false;
					const auto identity = Util::GetComIdentity(textures[role]->resource.Get());
					if (!identity || std::find(identities.begin(), identities.begin() + count, identity) != identities.begin() + count ||
						std::ranges::find(sources, identity) != sources.end())
						return false;
					identities[count++] = identity;
				}
			}
			return true;
		}

		ComputeSubrect SourceRegion(const RendererApplyArgs& args)
		{
			return args.roi                      ? args.roi->ownedOutput :
			       args.computeSubrect.IsValid() ? args.computeSubrect :
			                                       BuildCenteredComputeSubrect(args.outputWidth, args.outputHeight, args.tuning.singleSubrectScale);
		}
		std::optional<ModelResolutionGeometry> UnpackedGeometry(const RendererApplyArgs& args)
		{
			if (!args.centralArea.Valid())
				return std::nullopt;
			auto geometry = BuildModelResolutionGeometry(args.viewportCrop, { args.outputWidth, args.outputHeight }, SourceRegion(args),
				args.modelResolutionPercent, { args.guideWidth, args.guideHeight });
			if (geometry && args.centralArea.Active()) {
				const auto support = IntersectComputeSubrect(SourceRegion(args), BuildCentralAreaSupport(args.viewportCrop, args.centralArea));
				if (support.IsValid() && (!args.characterVisualIsolation || !args.roi || !args.roi->samplingSupport ||
											 IntersectComputeSubrect(support, *args.roi->samplingSupport).IsValid())) {
					const auto modelSupport = MapComputeSubrect(support, args.outputWidth, args.outputHeight, geometry->modelSize.width, geometry->modelSize.height);
					const auto context = BuildCharacterProviderComputeSubrect(modelSupport, geometry->modelSize.width, geometry->modelSize.height);
					geometry->modelRegion = IntersectComputeSubrect(geometry->modelRegion, context);
				}
			}
			return geometry;
		}

		std::optional<ModelResolutionGeometry> Geometry(const RendererApplyArgs& args
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			PackedCropAdmissionDiagnostics* diagnostics = nullptr
#endif
		)
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnostics && args.packedCrops.count >= 2) {
				diagnostics->stage = PackedCropStage::ModelProjection;
				diagnostics->reason = PackedCropReason::InvalidModelGeometry;
			}
#endif
			auto geometry = UnpackedGeometry(args);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (geometry && diagnostics && args.packedCrops.count >= 2) {
				diagnostics->modelAreasAvailable = true;
				diagnostics->effectiveBaselineModelPixels = geometry->modelRegion.Area();
				diagnostics->reason = PackedCropReason::UnsupportedRoute;
			}
#endif
			if (geometry && args.packedCrops.count >= 2 && args.characterVisualIsolation && args.renderingMode &&
				(*args.renderingMode == RenderingMode::FullResolution || *args.renderingMode == RenderingMode::ReducedResolution)) {
				if (args.packedCropPlan && args.packedCropCapacity == args.packedCropPlan->layout.extent && args.packedCropPlan->Matches(args.packedCrops, geometry->sourceSize, geometry->modelSize, SourceRegion(args), geometry->modelRegion.Area())) {
					geometry->packedCrops = args.packedCropPlan->layout;
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (diagnostics && args.packedCropPlanDiagnostics)
						*diagnostics = *args.packedCropPlanDiagnostics;
#endif
				} else if (!args.packedCropCapacity || args.packedCropCapacity->IsValid()) {
					geometry->packedCrops = BuildPackedCropLayout(args.packedCrops, geometry->sourceSize, geometry->modelSize, SourceRegion(args)
#ifdef DEVBENCH_BRIDGE_ENABLED
																																   ,
						diagnostics
#endif
						,
						{ .capacity = args.packedCropCapacity.value_or(UpscalingDLSS::Extent{}), .baselinePixels = geometry->modelRegion.Area() });
				}
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (diagnostics && args.packedCropCapacity) {
					diagnostics->capacity = args.packedCropCapacityDiagnostics;
					if (!args.packedCropCapacity->IsValid()) {
						diagnostics->stage = PackedCropStage::AreaAdmission;
						diagnostics->reason = args.packedCropCapacityDiagnostics.reason;
					}
				}
#endif
				if (geometry->packedCrops.count) {
					// Layout changes alter evaluated work, never native creation/storage capacity.
					const auto size = geometry->packedCrops.extent;
					geometry->modelRegion = { 0, 0, size.width, size.height };
#ifdef DEVBENCH_BRIDGE_ENABLED
					if (diagnostics) {
						diagnostics->stage = PackedCropStage::Admitted;
						diagnostics->reason = PackedCropReason::Admitted;
					}
#endif
				}
			}

			return geometry;
		}
	}

	struct ModelResolution::Work
	{
		AllocationKey allocation{};
		Color::Texture color, depth, motion, mask, output, reconstructed;
		ComPtr<ID3D11Resource> sourceColor, sourceMotion, sourceMask;
		ComPtr<ID3D11ShaderResourceView> colorView, motionView, maskView;
		Constants constants{};
	};

	struct ModelResolution::CoverageWork
	{
		ComPtr<ID3D11Buffer> flags;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11UnorderedAccessView> uav;
#ifdef DEVBENCH_BRIDGE_ENABLED
		ComPtr<ID3D11Buffer> staging;
		ComPtr<ID3D11Query> query;
		std::uint32_t sampledFrame = 0, queuedFrame = 0, missing = 0;
		bool pending = false, sampled = false, inspectionRequested = false;
		HRESULT inspectionResult = S_OK;
#endif
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	void ModelResolution::ServiceCoverageInspection(ID3D11DeviceContext* context)
	{
		if (!coverage_ || !coverage_->inspectionRequested)
			return;
		auto& work = *coverage_;
		work.inspectionRequested = false;
		if (work.pending) {
			std::uint32_t missing = 0;
			const auto result = PollCharacterMaskBounds(context, work.query.Get(), work.staging.Get(),
				std::as_writable_bytes(std::span(&missing, 1)));
			work.inspectionResult = result.result;
			if (result.status == CharacterMaskReadbackStatus::Pending)
				return;
			work.pending = false;
			if (!result.Ready())
				return;
			work.missing = missing;
			work.sampledFrame = work.queuedFrame;
			work.sampled = true;
		}
		if (!work.staging) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(std::uint32_t);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			work.inspectionResult = device_->CreateBuffer(&desc, nullptr, &work.staging);
			if (FAILED(work.inspectionResult))
				return;
			Util::SetResourceName(work.staging.Get(), "NeuralRendering::PackedCropCoverageReadback");
		}
		if (!work.query) {
			const D3D11_QUERY_DESC desc{ D3D11_QUERY_EVENT, 0 };
			work.inspectionResult = device_->CreateQuery(&desc, &work.query);
			if (FAILED(work.inspectionResult))
				return;
			Util::SetResourceName(work.query.Get(), "NeuralRendering::PackedCropCoverageReady");
		}
		context->CopyResource(work.staging.Get(), work.flags.Get());
		context->End(work.query.Get());
		work.queuedFrame = lastCoverageFrame_;
		work.pending = true;
		work.inspectionResult = S_OK;
	}

	nlohmann::json ModelResolution::GetPackedCoverageDiagnostics()
	{
		using json = nlohmann::json;
		if (coverage_ && lastBatchGuarded_)
			coverage_->inspectionRequested = true;
		return { { "lastBatchGuarded", lastBatchGuarded_ },
			{ "lastBatchFrame", lastBatchGuarded_ ? json(lastCoverageFrame_) : json(nullptr) },
			{ "completedSampleFrame", coverage_ && coverage_->sampled ? json(coverage_->sampledFrame) : json(nullptr) },
			{ "wholeBatchFallback", coverage_ && coverage_->sampled ? json(coverage_->missing != 0) : json(nullptr) },
			{ "inspectionPending", coverage_ && coverage_->pending },
			{ "inspectionResult", coverage_ ? coverage_->inspectionResult : S_OK },
			{ "scope", "current_selection_and_required_context_for_complete_batch" },
			{ "sampling", "explicit_devbench_inspection_only_nonblocking_on_render_thread" } };
	}
#endif

	bool ModelResolution::EnsureCoverage(ID3D11Device* device, HRESULT& result)
	{
		if (!coverageShader_) {
			const auto supportPixels = std::to_string(kPackedCropSupportPixels);
			coverageShader_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ModelResolutionCoverageCS.hlsl",
				{ { "PACKED_CROP_SUPPORT_PIXELS", supportPixels.c_str() } }, "cs_5_0", "main")));
			if (!coverageShader_) {
				result = E_FAIL;
				return false;
			}
			Util::SetResourceName(coverageShader_.Get(), "NeuralRendering::PackedCropCoverage");
		}
		if (!guardedReconstruct_) {
			guardedReconstruct_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl",
				{ { "PACKED_CROP_COVERAGE_GUARD", "1" } }, "cs_5_0", "main")));
			if (!guardedReconstruct_) {
				result = E_FAIL;
				return false;
			}
			Util::SetResourceName(guardedReconstruct_.Get(), "NeuralRendering::GuardedModelReconstruction");
		}
		if (coverage_)
			return true;
		auto work = std::make_shared<CoverageWork>();
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = desc.StructureByteStride = sizeof(std::uint32_t);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		result = device->CreateBuffer(&desc, nullptr, &work->flags);
		if (FAILED(result))
			return false;
		Util::SetResourceName(work->flags.Get(), "NeuralRendering::PackedCropCoverage");
		result = device->CreateShaderResourceView(work->flags.Get(), nullptr, &work->srv);
		if (FAILED(result))
			return false;
		Util::SetResourceName(work->srv.Get(), "NeuralRendering::PackedCropCoverage SRV");
		result = device->CreateUnorderedAccessView(work->flags.Get(), nullptr, &work->uav);
		if (FAILED(result))
			return false;
		Util::SetResourceName(work->uav.Get(), "NeuralRendering::PackedCropCoverage UAV");
		coverage_ = std::move(work);
		return true;
	}

	void ModelResolution::PlanPackedCrops(std::span<RendererApplyArgs> args, bool speculative) noexcept
	{
		for (auto& input : args) {
			input.packedCropCapacity.reset();
			input.packedCropPlan.reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
			input.packedCropPlanDiagnostics.reset();
#endif
			if (input.featureSlot >= cropPlanning_.size() || input.modelResolutionHistory)
				continue;
			std::optional<CropPlanning> tentative;
			if (speculative)
				tentative = cropPlanning_[input.featureSlot];
			auto& planning = tentative ? *tentative : cropPlanning_[input.featureSlot];
			if (!input.packedCrops.count
#ifdef DEVBENCH_BRIDGE_ENABLED
				&& !cropDiagnostics_.Enabled()
#endif
			) {
				planning.state = { .requiredFrames = planning.state.active ? kPackedCropRecoveryFrames : planning.state.requiredFrames };
				if (!PackingAvailable())
					input.packedCropsEnabled = false;
				continue;
			}
			const auto geometry = UnpackedGeometry(input);
#ifdef DEVBENCH_BRIDGE_ENABLED
			std::optional<PackedCropObservation> observation;
			std::optional<PackedCropAdmissionDiagnostics> retainedRejection;
			if (!speculative && cropDiagnostics_.Enabled()) {
				observation.emplace();
				observation->frame = input.frameId;
				observation->sourceFrame = input.sourceWorldFrame;
				observation->slot = input.featureSlot;
				observation->generation = input.generation;
				observation->sourceTransaction = input.executionContext.sourceTransactionId;
				observation->requested = input.packedCropsEnabled;
				observation->sources = input.packedCrops;
				observation->planning = input.packedCropPlanning;
				observation->reason = input.packedCropPlanning.reason;
				observation->available = SourceRegion(input);
				if (geometry) {
					observation->source = geometry->sourceSize;
					observation->model = geometry->modelSize;
					observation->guide = geometry->modelGuideSize;
					observation->baselinePixels = geometry->modelRegion.Area();
					observation->geometryChanged = planning.source != geometry->sourceSize || planning.model != geometry->modelSize ||
					                               planning.guide != geometry->modelGuideSize || planning.mode != input.renderingMode;
				}
			}
			const PackedCropObservationScope captureTransition{ cropDiagnostics_, observation, planning.state, retainedRejection };
#endif
			if (!geometry) {
				planning = {};
				continue;
			}
			if (planning.source != geometry->sourceSize || planning.model != geometry->modelSize ||
				planning.guide != geometry->modelGuideSize || planning.mode != input.renderingMode)
				planning = { {}, geometry->sourceSize, geometry->modelSize, geometry->modelGuideSize, input.renderingMode };
			if (!PackingAvailable()) {
				input.packedCrops = {};
				input.packedCropsEnabled = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
				input.packedCropPlanning.reason = PackedCropReason::ShadersUnavailable;
				if (observation)
					observation->reason = PackedCropReason::ShadersUnavailable;
#endif
			}
			if (!input.packedCrops.count || !input.characterVisualIsolation ||
				(input.renderingMode != RenderingMode::FullResolution && input.renderingMode != RenderingMode::ReducedResolution)) {
				planning.state = { .requiredFrames = planning.state.active ? kPackedCropRecoveryFrames : planning.state.requiredFrames };
				continue;
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			PackedCropAdmissionDiagnostics diagnostics;
#endif
			const auto layout = ResolvePackedCropLayout(input.packedCrops, geometry->sourceSize, geometry->modelSize,
				SourceRegion(input), geometry->modelRegion.Area(), input.frameId, planning.state
#ifdef DEVBENCH_BRIDGE_ENABLED
				,
				&diagnostics, observation ? &retainedRejection : nullptr
#endif
			);
			input.packedCropCapacity = layout.extent;
			input.packedCropPlan = PackedCropPlan{ input.packedCrops, geometry->sourceSize, geometry->modelSize,
				SourceRegion(input), geometry->modelRegion.Area(), layout };
#ifdef DEVBENCH_BRIDGE_ENABLED
			input.packedCropCapacityDiagnostics = { diagnostics.reason, planning.state.capacity, planning.state.stableFrames, planning.state.requiredFrames };
			const auto attribute = [&](auto& value) {
				value.frame = input.frameId;
				value.sourceFrame = input.sourceWorldFrame;
				value.generation = input.generation;
				value.modelResolutionPercent = input.modelResolutionPercent;
				value.baselineSourcePixels = input.computeSubrect.Area();
				value.planning = input.packedCropPlanning;
			};
			attribute(diagnostics);
			diagnostics.capacity = input.packedCropCapacityDiagnostics;
			diagnostics.modelAreasAvailable = true;
			diagnostics.effectiveBaselineModelPixels = geometry->modelRegion.Area();
			input.packedCropPlanDiagnostics = diagnostics;
			if (observation)
				observation->reason = retainedRejection ? retainedRejection->reason : diagnostics.reason;
			if (retainedRejection)
				attribute(*retainedRejection);
#endif
		}
	}

	bool ModelResolution::PreparePacking(std::span<const RendererApplyArgs> args, HRESULT& result)
	{
		result = E_INVALIDARG;
		if (args.empty())
			return false;
		const bool requested = std::ranges::any_of(args, [](const auto& input) { return input.packedCropsEnabled; });
		const bool selected = std::ranges::any_of(args, [](const auto& input) { return input.modelOutputSelection.has_value(); });
		// Warm transactions need no extra texture inspection or device-health sampling.
		if (device_.Get() == args.front().device && prepare_ && reconstruct_ && constants_ &&
			(!requested || !PackingAvailable() || (coverage_ && coverageShader_ && guardedReconstruct_ && (!selected || (selection_ && guardedSelection_))))) {
			result = S_OK;
			return true;
		}
		if (!ValidBatchLayout(args) || !ValidateSelectedOutputs(args) || !EnsureShaders(args.front().device, result))
			return false;
		if (requested && PackingAvailable()) {
			if (selected && !EnsureSelectionShader(false, result))
				return false;
			try {
				if (!EnsureCoverage(args.front().device, result) || (selected && !EnsureSelectionShader(true, result)))
					packingFailure_ = FAILED(result) ? result : E_FAIL;
			} catch (const std::bad_alloc&) {
				packingFailure_ = E_OUTOFMEMORY;
			} catch (const std::exception&) {
				packingFailure_ = E_FAIL;
			}
			if (!PackingAvailable()) {
				result = args.front().device->GetDeviceRemovedReason();
				if (FAILED(result))
					return false;
			}
		}
		result = S_OK;
		return true;
	}

	bool ModelResolution::Required(const RendererApplyArgs& args
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropAdmissionDiagnostics* diagnostics
#endif
		) noexcept
	{
		if (args.modelOutputSelection || args.modelResolutionPercent != kMaximumModelResolutionPercent || args.centralArea.Active() || !args.centralArea.Valid())
			return true;
		if (args.packedCrops.count < 2)
			return false;
		const auto geometry = Geometry(args
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			diagnostics
#endif
		);
		return geometry && geometry->packedCrops.count != 0;
	}

	bool ModelResolution::Project(const RendererApplyArgs& input, RendererApplyArgs& adapted
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		PackedCropAdmissionDiagnostics* diagnostics
#endif
		) noexcept
	{
		const auto geometry = Geometry(input
#ifdef DEVBENCH_BRIDGE_ENABLED
			,
			diagnostics
#endif
		);
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto reject = [&] {
			if (diagnostics) {
				diagnostics->stage = PackedCropStage::ModelProjection;
				diagnostics->reason = geometry ? PackedCropReason::InvalidProjection : PackedCropReason::InvalidModelGeometry;
			}
			return false;
		};
#endif
		const auto featureUpscaling = ResolveFeatureUpscaling(input.guideWidth, input.guideHeight, input.outputWidth, input.outputHeight);
		if (!geometry || !input.renderingMode ||
			EffectiveModelResolutionPercent(*input.renderingMode, input.modelResolutionPercent) != input.modelResolutionPercent ||
			(input.renderingMode == RenderingMode::ReducedResolution && (!input.reset || input.featureUpscaling)) ||
			!featureUpscaling || input.featureUpscaling != *featureUpscaling ||
			input.colorWidth != input.outputWidth || input.colorHeight != input.outputHeight ||
			!SourceRegion(input).Fits(input.outputWidth, input.outputHeight)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			return reject();
#else
			return false;
#endif
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (input.roi && input.roi->compactSource)
			return reject();
#endif
		const auto projectedUpscaling = ResolveFeatureUpscaling(geometry->modelGuideSize.width, geometry->modelGuideSize.height,
			geometry->modelSize.width, geometry->modelSize.height);
		if (!projectedUpscaling) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			return reject();
#else
			return false;
#endif
		}
		const auto sourceSize = geometry->sourceSize;
		const auto modelSize = geometry->modelSize;
		const auto map = [&](const ComputeSubrect& rect) {
			return MapComputeSubrect(rect, sourceSize.width, sourceSize.height, modelSize.width, modelSize.height);
		};
		adapted = input;
		adapted.modelOutputSelection.reset();
		adapted.modelResolutionPercent = kMaximumModelResolutionPercent;
		adapted.modelResolutionHistory = ModelResolutionHistory{ input.viewportCrop, input.modelResolutionPercent,
			SourceRegion(input), reinterpret_cast<std::uintptr_t>(Util::GetComIdentity(input.controlMask.Get()).Get()),
			input.centralArea.Active() ? input.centralArea : CentralArea{}, geometry->packedCrops };
		adapted.centralArea = {};
		adapted.packedCrops = {};
		adapted.packedCropsEnabled = false;
		adapted.packedCropCapacity.reset();
		adapted.packedCropPlan.reset();
#ifdef DEVBENCH_BRIDGE_ENABLED
		adapted.packedCropPlanDiagnostics.reset();
#endif
		adapted.guideWidth = geometry->modelGuideSize.width;
		adapted.guideHeight = geometry->modelGuideSize.height;
		adapted.featureUpscaling = *projectedUpscaling;
		adapted.colorWidth = adapted.outputWidth = modelSize.width;
		adapted.colorHeight = adapted.outputHeight = modelSize.height;
		adapted.computeSubrect = geometry->modelRegion;
		adapted.viewportCrop = geometry->nativeCrop;
		if (input.roi) {
			auto& roi = *adapted.roi;
			roi.ownedOutput = geometry->modelRegion;
			roi.inferenceContext = geometry->modelRegion;
			roi.allocationCapacity = modelSize;
			if (input.roi->samplingSupport) {
				const auto support = geometry->packedCrops.count ? geometry->modelRegion :
				                                                   IntersectComputeSubrect(map(*input.roi->samplingSupport), geometry->modelRegion);
				roi.samplingSupport = support.IsValid() ? std::optional{ support } : std::nullopt;
			}
			if (input.roi->temporalEnvelope)
				roi.temporalEnvelope = geometry->modelRegion;
		}
		if (input.controlMask) {
			adapted.controlMaskWidth = modelSize.width;
			adapted.controlMaskHeight = modelSize.height;
		}
		if (input.actorSelection)
			adapted.actorSelectionSupport = geometry->packedCrops.count ? geometry->modelRegion : map(input.actorSelectionSupport);
		return true;
	}

	bool ModelResolution::CanUseSharedTargets(std::span<const RendererApplyArgs> args, bool compactInputs) noexcept
	{
		return !compactInputs && ValidBatchLayout(args) &&
		       std::ranges::all_of(args, [](const auto& input) {
				   RendererApplyArgs projected;
				   return Project(input, projected);
			   });
	}

	std::optional<std::uint64_t> ModelResolution::AdditionalBytes(std::span<const RendererApplyArgs> args, Sharing sharing) const
	{
		if (!ValidBatchLayout(args) || !ValidateSelectedOutputs(args))
			return std::nullopt;
		std::uint64_t total = 0;
		for (const auto& input : args) {
			RendererApplyArgs projected;
			if (!Project(input, projected))
				return std::nullopt;
			const auto allocation = AllocationKey::Read(input, projected, sharing);
			if (!allocation)
				return std::nullopt;
			const auto& cached = slots_[input.featureSlot];
			if (device_.Get() == input.device && cached && cached->allocation == *allocation)
				continue;
			const auto bytes = allocation->Bytes();
			if (!bytes)
				return std::nullopt;
			total += *bytes;
		}
		return total;
	}

	std::optional<std::uint64_t> ModelResolution::RetainedBytes(std::uint32_t slot) const noexcept
	{
		return slot < slots_.size() && slots_[slot] ? slots_[slot]->allocation.Bytes() : std::optional<std::uint64_t>{ 0 };
	}

	bool ModelResolution::EnsureShaders(ID3D11Device* device, HRESULT& result)
	{
		if (device_.Get() != device) {
			Reset();
			device_ = device;
		}
		if (compileFailed_) {
			result = E_FAIL;
			return false;
		}
		compileFailed_ = true;
		if (!prepare_) {
			prepare_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ModelResolutionPrepareCS.hlsl", {}, "cs_5_0", "main")));
			if (prepare_)
				Util::SetResourceName(prepare_.Get(), "NeuralRendering::ModelResolutionPrepare");
		}
		if (!reconstruct_) {
			reconstruct_.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl", {}, "cs_5_0", "main")));
			if (reconstruct_)
				Util::SetResourceName(reconstruct_.Get(), "NeuralRendering::ModelResolutionReconstruct");
		}
		if (!prepare_ || !reconstruct_) {
			compileFailed_ = true;
			result = E_FAIL;
			return false;
		}
		compileFailed_ = false;
		if (!constants_) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(Constants);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			result = device->CreateBuffer(&desc, nullptr, &constants_);
			if (FAILED(result))
				return false;
			Util::SetResourceName(constants_.Get(), "NeuralRendering::ModelResolutionConstants");
		}
		return true;
	}

	bool ModelResolution::EnsureSelectionShader(bool guarded, HRESULT& result)
	{
		auto& shader = guarded ? guardedSelection_ : selection_;
		if (shader)
			return true;
		std::vector<std::pair<const char*, const char*>> defines{ { "MODEL_OUTPUT_SELECTION", "1" } };
		if (guarded)
			defines.emplace_back("PACKED_CROP_COVERAGE_GUARD", "1");
		shader.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data/Shaders/Upscaling/NeuralRendering/ModelResolutionReconstructCS.hlsl", defines, "cs_5_0", "main")));
		if (!shader) {
			result = E_FAIL;
			return false;
		}
		Util::SetResourceName(shader.Get(), guarded ? "NeuralRendering::GuardedSelectedReconstruction" : "NeuralRendering::SelectedReconstruction");
		return true;
	}

	void ModelResolution::DispatchCoverage(std::span<const RendererApplyArgs> args, Batch& batch)
	{
		auto* context = args.front().context;
		ComputeStateGuard<1> guard(context);
		const std::array<UINT, 4> clear{};
		context->ClearUnorderedAccessViewUint(coverage_->uav.Get(), clear.data());
		for (std::size_t index = 0; index < args.size(); ++index) {
			auto& work = *batch.resources[index];
			work.constants.packedCoverageGuard = 1u;
			if (!work.constants.packedCount)
				continue;
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = constants_.Get();
			auto* selection = args[index].actorSelection.Get();
			auto* destination = coverage_->uav.Get();
			context->CSSetShader(coverageShader_.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, 1, &selection);
			context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
			{
				CS_GPU_PASS("NeuralRendering::PackedCropCoverage");
				context->Dispatch((work.constants.sourceRegion[0] + 7u) / 8u, (work.constants.sourceRegion[1] + 7u) / 8u, 1);
			}
			guard.Unbind();
		}
		lastBatchGuarded_ = true;
		lastCoverageFrame_ = args.front().frameId;
	}

	bool ModelResolution::Prepare(std::span<const RendererApplyArgs> args, Batch& batch, HRESULT& result, std::span<const Targets> targets)
	{
		result = E_INVALIDARG;
		batch = {};
		lastBatchGuarded_ = false;
		if (!ValidBatchLayout(args) || !ValidateSelectedOutputs(args) || (!targets.empty() && targets.size() != args.size()))
			return false;
		ComPtr<ID3D11Device> contextDevice;
		args.front().context->GetDevice(&contextDevice);
		if (!Util::SameIdentity(contextDevice.Get(), args.front().device) || args.front().context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return false;
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& input = args[index];
			if (!input.depthGuideSRV || !Project(input, batch.arguments[index]))
				return false;
			ComPtr<ID3D11Resource> depth;
			input.depthGuideSRV->GetResource(&depth);
			D3D11_SHADER_RESOURCE_VIEW_DESC depthView{};
			input.depthGuideSRV->GetDesc(&depthView);
			D3D11_TEXTURE2D_DESC depthDesc{};
			if (!ReadDescription(depth.Get(), depthDesc) || depthDesc.Width != input.guideWidth || depthDesc.Height != input.guideHeight ||
				!Util::SameIdentity(depth.Get(), input.depthGuide) || depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				depthView.Texture2D.MostDetailedMip != 0 || depthView.Texture2D.MipLevels != 1)
				return false;
		}
		const auto sharing = targets.empty() ? Sharing::Private : targets.front().color.resource ? Sharing::All :
		                                                                                           Sharing::Guides;
		const bool sharedTargets = sharing != Sharing::Private;
		if (sharedTargets && !ValidateTargets(args, targets, sharing))
			return false;
		if (!PreparePacking(args, result))
			return false;
		if (!PackingAvailable() && std::ranges::any_of(args, [](const auto& input) { return input.packedCrops.count != 0; })) {
			std::array<RendererApplyArgs, kMaximumRegionEvaluations> fallback{};
			std::ranges::copy(args, fallback.begin());
			auto working = std::span(fallback.data(), args.size());
			PlanPackedCrops(working);
			return Prepare(working, batch, result, targets);
		}
		const bool selected = std::ranges::any_of(args, [](const auto& input) { return input.modelOutputSelection.has_value(); });
		batch.device = args.front().device;
		batch.context = args.front().context;
		try {
			for (std::size_t index = 0; index < args.size(); ++index) {
				const auto& input = args[index];
				const auto allocation = AllocationKey::Read(input, batch.arguments[index], sharing);
				if (!allocation) {
					result = E_INVALIDARG;
					return false;
				}
				const auto& key = *allocation;
				const auto sourceSize = key.sourceSize, modelSize = key.modelSize;
				const bool hasMask = key.hasMask;
				auto work = slots_[input.featureSlot];
				if (!work || work->allocation != key) {
					work = std::make_shared<Work>();
					work->allocation = key;
					const auto prefix = std::format("NeuralRendering::ModelSlot{}::", input.featureSlot);
					if ((sharing != Sharing::All &&
							(!CreateTexture(input.device, work->color, modelSize, key.colorFormat, prefix + "Color", result) ||
								!CreateTexture(input.device, work->output, modelSize, key.outputFormat, prefix + "Output", result))) ||
						(sharing == Sharing::Private &&
							(!CreateTexture(input.device, work->depth, key.modelGuideSize, DXGI_FORMAT_R32_FLOAT, prefix + "Depth", result) ||
								!CreateTexture(input.device, work->motion, key.modelGuideSize, key.motionFormat, prefix + "Motion", result))) ||
						(!key.selectedOutput && !CreateTexture(input.device, work->reconstructed, sourceSize, key.outputFormat, prefix + "Reconstructed", result)) ||
						(hasMask && !CreateTexture(input.device, work->mask, modelSize, DXGI_FORMAT_R8_UNORM, prefix + "Mask", result)))
						return false;
					slots_[input.featureSlot] = work;
				}
				if (!EnsureSourceView(input.device, input.colorInput, work->sourceColor, work->colorView,
						"NeuralRendering::ModelSourceColor", result) ||
					!EnsureSourceView(input.device, input.motionVectors, work->sourceMotion, work->motionView,
						"NeuralRendering::ModelSourceMotion", result) ||
					(hasMask && !EnsureSourceView(input.device, input.controlMask.Get(), work->sourceMask, work->maskView,
									"NeuralRendering::ModelSourceMask", result)))
					return false;

				const auto geometry = Geometry(input);
				if (!geometry) {
					result = E_INVALIDARG;
					return false;
				}
				const auto source = geometry->sourceRegion;
				const auto model = geometry->modelRegion;
				work->constants = {
					{ sourceSize.width, sourceSize.height }, { modelSize.width, modelSize.height },
					{ source.baseX, source.baseY }, { source.width, source.height },
					{ model.baseX, model.baseY }, { model.width, model.height },
					hasMask ? 1u : 0u, OutputFormat(key.outputFormat),
					geometry->motionNormalization,
					{ static_cast<float>(input.viewportCrop.fullOutput.width), static_cast<float>(input.viewportCrop.fullOutput.height) },
					{ static_cast<float>(input.viewportCrop.output.left), static_cast<float>(input.viewportCrop.output.top) },
					input.centralArea.Scale(), input.centralArea.horizontalScale, static_cast<float>(input.centralArea.featherPixels), input.centralArea.Active() ? 1u : 0u,
					input.centralArea.offset, { static_cast<float>(input.centralArea.finalOutput.width), static_cast<float>(input.centralArea.finalOutput.height) }
				};
				work->constants.packedCount = geometry->packedCrops.count;
				work->constants.unpackedSize = { geometry->packedCrops.unpackedExtent.width, geometry->packedCrops.unpackedExtent.height };
				work->constants.packedTiles = geometry->packedCrops.tiles;
				if (input.modelOutputSelection) {
					const auto& selection = *input.modelOutputSelection;
					const auto& support = selection.maskSupport;
					work->constants.selectedOutput = 1u;
					work->constants.selectionMode = !selection.mask ? 0u : selection.providerBlending ? 2u :
					                                                                                    1u;
					work->constants.outputStorageFormat = key.outputFormat;
					work->constants.selectionBounds = { static_cast<float>(support.baseX) / sourceSize.width, static_cast<float>(support.baseY) / sourceSize.height,
						static_cast<float>(support.baseX + support.width) / sourceSize.width, static_cast<float>(support.baseY + support.height) / sourceSize.height };
					batch.selectedOutputs[index] = selection.destination;
					batch.selectionMasks[index] = selection.mask;
					batch.selectionSupports[index] = selection.maskSupport;
					batch.providerSelections[index] = selection.providerBlending;
					batch.selectionSources[index] = input.colorInput;
					batch.selectionDestinations[index] = input.colorOutput;
				}
				auto& target = batch.targets[index];
				target = { work->color, work->depth, work->motion, work->output };
				if (sharedTargets) {
					target.depth = targets[index].depth;
					target.motion = targets[index].motion;
					if (sharing == Sharing::All) {
						target.color = targets[index].color;
						target.output = targets[index].output;
					}
				}
				auto& adapted = batch.arguments[index];
				adapted.colorInput = target.color.resource.Get();
				adapted.depthGuide = target.depth.resource.Get();
				adapted.depthGuideSRV = target.depth.srv.Get();
				adapted.motionVectors = target.motion.resource.Get();
				adapted.colorOutput = target.output.resource.Get();

				if (hasMask) {
					adapted.controlMask = work->mask.resource;
					adapted.controlMaskWidth = modelSize.width;
					adapted.controlMaskHeight = modelSize.height;
				}
				adapted.actorSelection = hasMask ? work->mask.srv : nullptr;
				if (work->constants.packedCount) {
					if (!ValidSelectionView(input, sourceSize, input.actorSelection.Get())) {
						result = E_INVALIDARG;
						return false;
					}
					if (!EnsureCoverage(input.device, result))
						return false;
					batch.packedCoverage = coverage_->srv;
				}
				batch.resources[index] = std::move(work);
			}
		} catch (const std::bad_alloc&) {
			result = E_OUTOFMEMORY;
			return false;
		}
		if (selected && !EnsureSelectionShader(batch.packedCoverage != nullptr, result))
			return false;
		batch.reconstructionShader = selected ? (batch.packedCoverage ? guardedSelection_ : selection_) :
		                                        (batch.packedCoverage ? guardedReconstruct_ : reconstruct_);
		batch.constants = constants_;
		auto* context = args.front().context;
		if (batch.packedCoverage)
			DispatchCoverage(args, batch);
		ComputeStateGuard<4, 4> guard(context);

		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = constants_.Get();
			const std::array srvs{ work.colorView.Get(), args[index].depthGuideSRV, work.motionView.Get(), work.allocation.hasMask ? work.maskView.Get() : nullptr };
			const auto& target = batch.targets[index];
			const std::array uavs{ target.color.uav.Get(), target.depth.uav.Get(), target.motion.uav.Get(), work.allocation.hasMask ? work.mask.uav.Get() : nullptr };
			context->CSSetShader(prepare_.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
			{
				CS_GPU_PASS("NeuralRendering::ModelResolutionPrepare");
				const auto guideRegion = MapComputeSubrect(batch.arguments[index].computeSubrect,
					work.allocation.modelSize.width, work.allocation.modelSize.height,
					work.allocation.modelGuideSize.width, work.allocation.modelGuideSize.height);
				context->Dispatch((std::max(work.constants.modelRegion[0], guideRegion.width) + 7u) / 8u,
					(std::max(work.constants.modelRegion[1], guideRegion.height) + 7u) / 8u, 1);
			}
			guard.Unbind();
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (batch.packedCoverage)
			ServiceCoverageInspection(context);
#endif
		result = args.front().device->GetDeviceRemovedReason();
		if (SUCCEEDED(result))
			batch.count = args.size();
		return SUCCEEDED(result);
	}

	template <std::size_t Bindings>
	void ModelResolution::DispatchReconstruction(std::span<const RendererApplyArgs> args, const Batch& batch)
	{
		auto* context = args.front().context;
		ComputeStateGuard<Bindings> guard(context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			context->UpdateSubresource(batch.constants.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = batch.constants.Get();
			std::array<ID3D11ShaderResourceView*, Bindings> srvs{};
			srvs[0] = work.colorView.Get();
			srvs[1] = batch.targets[index].color.srv.Get();
			srvs[2] = batch.targets[index].output.srv.Get();
			if constexpr (Bindings >= 4)
				srvs[3] = batch.packedCoverage.Get();
			if constexpr (Bindings == 5)
				srvs[4] = batch.selectionMasks[index].Get();
			auto* uav = batch.selectedOutputs[index] ? batch.selectedOutputs[index].Get() : work.reconstructed.uav.Get();
			context->CSSetShader(batch.reconstructionShader.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			{
				CS_GPU_PASS("NeuralRendering::ModelResolutionReconstruct");
				const auto extent = work.allocation.selectedOutput ? work.constants.sourceSize : work.constants.sourceRegion;
				context->Dispatch((extent[0] + 7u) / 8u, (extent[1] + 7u) / 8u, 1);
			}
			guard.Unbind();
		}
	}

	bool ModelResolution::Reconstruct(std::span<const RendererApplyArgs> args, const Batch& batch, HRESULT& result)
	{
		result = E_INVALIDARG;
		if (batch.count != args.size() || args.empty() || args.size() > batch.resources.size() ||
			!batch.reconstructionShader || !batch.constants ||
			std::ranges::any_of(std::span(batch.resources.data(), batch.count), [](const auto& work) { return !work; }) ||
			std::ranges::any_of(std::span(batch.targets.data(), batch.count), [](const auto& target) { return !target.color.srv || !target.output.srv; }))
			return false;
		if (!ValidBatchLayout(args) || args.front().device != batch.device.Get() || args.front().context != batch.context.Get())
			return false;
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& selection = args[index].modelOutputSelection;
			if (static_cast<bool>(selection) != static_cast<bool>(batch.selectedOutputs[index]) ||
				(selection && (args[index].colorInput != batch.selectionSources[index].Get() ||
								  args[index].colorOutput != batch.selectionDestinations[index].Get() ||
								  selection->maskSupport != batch.selectionSupports[index] ||
								  selection->providerBlending != batch.providerSelections[index] ||
								  selection->destination.Get() != batch.selectedOutputs[index].Get() ||
								  selection->mask.Get() != batch.selectionMasks[index].Get())))
				return false;
		}
		if (std::ranges::any_of(std::span(batch.selectedOutputs.data(), batch.count), [](const auto& output) { return output != nullptr; }))
			DispatchReconstruction<5>(args, batch);
		else if (batch.packedCoverage)
			DispatchReconstruction<4>(args, batch);
		else
			DispatchReconstruction<3>(args, batch);
		result = args.front().device->GetDeviceRemovedReason();
		return SUCCEEDED(result);
	}

	void ModelResolution::Commit(std::span<const RendererApplyArgs> args, const Batch& batch) const
	{
		if (std::ranges::all_of(std::span(batch.selectedOutputs.data(), batch.count), [](const auto& output) { return output != nullptr; }))
			return;
		CS_GPU_PASS("NeuralRendering::ModelResolutionCommit");
		ComputeStateGuard<3> guard(args.front().context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			if (batch.selectedOutputs[index])
				continue;
			const auto& c = work.constants;
			const D3D11_BOX box{ c.sourceOffset[0], c.sourceOffset[1], 0,
				c.sourceOffset[0] + c.sourceRegion[0], c.sourceOffset[1] + c.sourceRegion[1], 1 };
			args[index].context->CopySubresourceRegion(args[index].colorOutput, 0, box.left, box.top, 0, work.reconstructed.resource.Get(), 0, &box);
		}
	}

	void ModelResolution::InvalidateShaders() noexcept
	{
		prepare_.Reset();
		reconstruct_.Reset();
		coverageShader_.Reset();
		guardedReconstruct_.Reset();
		selection_.Reset();
		guardedSelection_.Reset();
		compileFailed_ = false;
		packingFailure_ = S_OK;
	}
	void ModelResolution::Reset() noexcept
	{
		slots_ = {};
		coverage_.reset();
		lastBatchGuarded_ = false;
		lastCoverageFrame_ = std::numeric_limits<std::uint32_t>::max();
		InvalidateShaders();
		constants_.Reset();
		device_.Reset();
	}

	void ModelResolution::ReleaseUnscaledSlots(std::span<const RendererApplyArgs> args) noexcept
	{
		// A mixed packed/unpacked pair still needs both private reconstruction targets.
		if (std::ranges::any_of(args, [](const auto& value) { return value.packedCropsEnabled || Required(value); }))
			return;
		for (const auto& input : args)
			ReleaseSlot(input.featureSlot);
	}
	void ModelResolution::ReleaseSlot(std::uint32_t slot) noexcept
	{
		if (slot < slots_.size())
			slots_[slot].reset();
	}
}
