#include "ModelResolution.h"
#include "ComputeStateGuard.h"
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
		};
		static_assert(sizeof(Constants) == 112);

		bool ReadDescription(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc)
		{
			ComPtr<ID3D11Texture2D> texture;
			if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
				return false;
			texture->GetDesc(&desc);
			return desc.ArraySize == 1 && desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
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

		bool HasActorInputs(const RendererApplyArgs& input) noexcept
		{
			return input.controlMask || input.actorSelection || input.providerBlending || input.characterVisualIsolation;
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

		struct AllocationKey
		{
			UpscalingDLSS::Extent sourceSize{}, modelSize{}, modelGuideSize{};
			DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN, outputFormat = DXGI_FORMAT_UNKNOWN, motionFormat = DXGI_FORMAT_UNKNOWN;
			bool hasMask = false, sharedTargets = false;

			bool operator==(const AllocationKey&) const = default;

			static std::optional<AllocationKey> Read(const RendererApplyArgs& input, const RendererApplyArgs& projected, bool sharedTargets = false)
			{
				if (sharedTargets && HasActorInputs(input))
					return std::nullopt;
				D3D11_TEXTURE2D_DESC color{}, output{}, motion{};
				if (!ReadDescription(input.colorInput, color) || !ReadDescription(input.colorOutput, output) ||
					!ReadDescription(input.motionVectors, motion) ||
					color.Width != input.colorWidth || color.Height != input.colorHeight ||
					output.Width != input.outputWidth || output.Height != input.outputHeight ||
					motion.Width != input.guideWidth || motion.Height != input.guideHeight)
					return std::nullopt;
				return AllocationKey{ { input.outputWidth, input.outputHeight }, { projected.outputWidth, projected.outputHeight }, { projected.guideWidth, projected.guideHeight },
					color.Format, output.Format, motion.Format, input.controlMask != nullptr, sharedTargets };
			}

			std::optional<std::uint64_t> Bytes() const noexcept
			{
				const auto fullOutput = LogicalTextureBytes(outputFormat, sourceSize.width, sourceSize.height);
				if (sharedTargets)
					return fullOutput;
				std::uint64_t total = 0;
				const std::array formats{ colorFormat, DXGI_FORMAT_R32_FLOAT, motionFormat, outputFormat };
				const std::array sizes{ modelSize, modelGuideSize, modelGuideSize, modelSize };
				for (std::size_t index = 0; index < formats.size(); ++index) {
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

		bool ValidateTargets(std::span<const RendererApplyArgs> args, std::span<const ModelResolution::Targets> targets)
		{
			std::array<ComPtr<IUnknown>, kMaximumRegionEvaluations * 4> identities{}, sources{};
			std::size_t count = 0, sourceCount = 0;
			for (const auto& input : args)
				for (auto* source : { input.colorInput, input.depthGuide, input.motionVectors, input.colorOutput }) {
					if (!(sources[sourceCount] = Util::GetComIdentity(source)))
						return false;
					++sourceCount;
				}
			for (std::size_t index = 0; index < args.size(); ++index) {
				RendererApplyArgs projected;
				if (!ModelResolution::Project(args[index], projected))
					return false;
				const auto allocation = AllocationKey::Read(args[index], projected, true);
				if (!allocation)
					return false;
				const auto& t = targets[index];
				const std::array formats{ allocation->colorFormat, DXGI_FORMAT_R32_FLOAT, allocation->motionFormat, allocation->outputFormat };
				const std::array textures{ &t.color, &t.depth, &t.motion, &t.output };
				const std::array sizes{ allocation->modelSize, allocation->modelGuideSize, allocation->modelGuideSize, allocation->modelSize };
				for (std::size_t role = 0; role < textures.size(); ++role) {
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
		std::optional<ModelResolutionGeometry> Geometry(const RendererApplyArgs& args)
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
	}

	struct ModelResolution::Work
	{
		AllocationKey allocation{};
		Color::Texture color, depth, motion, mask, output, reconstructed;
		ComPtr<ID3D11Resource> sourceColor, sourceMotion, sourceMask;
		ComPtr<ID3D11ShaderResourceView> colorView, motionView, maskView;
		Constants constants{};
	};

	bool ModelResolution::Project(const RendererApplyArgs& input, RendererApplyArgs& adapted) noexcept
	{
		const auto geometry = Geometry(input);
		const auto featureUpscaling = ResolveFeatureUpscaling(input.guideWidth, input.guideHeight, input.outputWidth, input.outputHeight);
		if (!geometry || !input.renderingMode ||
			EffectiveModelResolutionPercent(*input.renderingMode, input.modelResolutionPercent) != input.modelResolutionPercent ||
			(input.renderingMode == RenderingMode::ReducedResolution && (!input.reset || input.featureUpscaling)) ||
			!featureUpscaling || input.featureUpscaling != *featureUpscaling ||
			!Required(input) ||
			input.colorWidth != input.outputWidth || input.colorHeight != input.outputHeight ||
			!SourceRegion(input).Fits(input.outputWidth, input.outputHeight))
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (input.roi && input.roi->compactSource)
			return false;
#endif
		const auto sourceSize = geometry->sourceSize;
		const auto modelSize = geometry->modelSize;
		const auto map = [&](const ComputeSubrect& rect) {
			return MapComputeSubrect(rect, sourceSize.width, sourceSize.height, modelSize.width, modelSize.height);
		};
		adapted = input;
		adapted.modelResolutionPercent = kMaximumModelResolutionPercent;
		adapted.modelResolutionHistory = ModelResolutionHistory{ input.viewportCrop, input.modelResolutionPercent,
			SourceRegion(input), reinterpret_cast<std::uintptr_t>(Util::GetComIdentity(input.controlMask.Get()).Get()),
			input.centralArea.Active() ? input.centralArea : CentralArea{} };
		adapted.centralArea = {};
		adapted.guideWidth = geometry->modelGuideSize.width;
		adapted.guideHeight = geometry->modelGuideSize.height;
		adapted.featureUpscaling = *ResolveFeatureUpscaling(adapted.guideWidth, adapted.guideHeight, modelSize.width, modelSize.height);
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
				const auto support = IntersectComputeSubrect(map(*input.roi->samplingSupport), geometry->modelRegion);
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
			adapted.actorSelectionSupport = map(input.actorSelectionSupport);
		return true;
	}

	bool ModelResolution::CanUseSharedTargets(std::span<const RendererApplyArgs> args, bool colorProcessing, bool compactInputs) noexcept
	{
		return !colorProcessing && !compactInputs && ValidBatchLayout(args) &&
		       std::ranges::all_of(args, [](const auto& input) {
				   RendererApplyArgs projected;
				   return !HasActorInputs(input) && Project(input, projected);
			   });
	}

	std::optional<std::uint64_t> ModelResolution::AdditionalBytes(std::span<const RendererApplyArgs> args, bool sharedTargets) const
	{
		if (!ValidBatchLayout(args))
			return std::nullopt;
		std::uint64_t total = 0;
		for (const auto& input : args) {
			RendererApplyArgs projected;
			if (!Project(input, projected))
				return std::nullopt;
			const auto allocation = AllocationKey::Read(input, projected, sharedTargets);
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
			result = E_FAIL;
			return false;
		}
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

	bool ModelResolution::Prepare(std::span<const RendererApplyArgs> args, Batch& batch, HRESULT& result, std::span<const Targets> targets)
	{
		result = E_INVALIDARG;
		batch = {};
		if (!ValidBatchLayout(args) || (!targets.empty() && targets.size() != args.size()))
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
		const bool sharedTargets = !targets.empty();
		if (sharedTargets && !ValidateTargets(args, targets))
			return false;
		if (!EnsureShaders(args.front().device, result))
			return false;
		try {
			for (std::size_t index = 0; index < args.size(); ++index) {
				const auto& input = args[index];
				const auto allocation = AllocationKey::Read(input, batch.arguments[index], sharedTargets);
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
					if ((!sharedTargets &&
							(!CreateTexture(input.device, work->color, modelSize, key.colorFormat, prefix + "Color", result) ||
								!CreateTexture(input.device, work->depth, key.modelGuideSize, DXGI_FORMAT_R32_FLOAT, prefix + "Depth", result) ||
								!CreateTexture(input.device, work->motion, key.modelGuideSize, key.motionFormat, prefix + "Motion", result) ||
								!CreateTexture(input.device, work->output, modelSize, key.outputFormat, prefix + "Output", result))) ||
						!CreateTexture(input.device, work->reconstructed, sourceSize, key.outputFormat, prefix + "Reconstructed", result) ||
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
				auto& target = batch.targets[index];
				target = sharedTargets ? targets[index] : Targets{ work->color, work->depth, work->motion, work->output };
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
				if (input.actorSelection) {
					adapted.actorSelection = work->mask.srv;
				}
				batch.resources[index] = std::move(work);
			}
		} catch (const std::bad_alloc&) {
			result = E_OUTOFMEMORY;
			return false;
		}
		batch.reconstructionShader = reconstruct_;
		batch.constants = constants_;
		auto* context = args.front().context;
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
		result = args.front().device->GetDeviceRemovedReason();
		if (SUCCEEDED(result))
			batch.count = args.size();
		return SUCCEEDED(result);
	}

	bool ModelResolution::Reconstruct(std::span<const RendererApplyArgs> args, const Batch& batch, HRESULT& result)
	{
		result = E_INVALIDARG;
		if (batch.count != args.size() || args.empty() || args.size() > batch.resources.size() ||
			!batch.reconstructionShader || !batch.constants ||
			std::ranges::any_of(std::span(batch.resources.data(), batch.count), [](const auto& work) { return !work; }) ||
			std::ranges::any_of(std::span(batch.targets.data(), batch.count), [](const auto& target) { return !target.color.srv || !target.output.srv; }))
			return false;
		auto* context = args.front().context;
		ComputeStateGuard<3> guard(context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			context->UpdateSubresource(batch.constants.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = batch.constants.Get();
			const std::array srvs{ work.colorView.Get(), batch.targets[index].color.srv.Get(), batch.targets[index].output.srv.Get() };
			auto* uav = work.reconstructed.uav.Get();
			context->CSSetShader(batch.reconstructionShader.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			{
				CS_GPU_PASS("NeuralRendering::ModelResolutionReconstruct");
				context->Dispatch((work.constants.sourceRegion[0] + 7u) / 8u, (work.constants.sourceRegion[1] + 7u) / 8u, 1);
			}
			guard.Unbind();
		}
		result = args.front().device->GetDeviceRemovedReason();
		return SUCCEEDED(result);
	}

	void ModelResolution::Commit(std::span<const RendererApplyArgs> args, const Batch& batch) const
	{
		CS_GPU_PASS("NeuralRendering::ModelResolutionCommit");
		ComputeStateGuard<3> guard(args.front().context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			const auto& c = work.constants;
			const D3D11_BOX box{ c.sourceOffset[0], c.sourceOffset[1], 0,
				c.sourceOffset[0] + c.sourceRegion[0], c.sourceOffset[1] + c.sourceRegion[1], 1 };
			args[index].context->CopySubresourceRegion(args[index].colorOutput, 0, box.left, box.top, 0, work.reconstructed.resource.Get(), 0, &box);
		}
	}

	void ModelResolution::Reset() noexcept
	{
		slots_ = {};
		prepare_.Reset();
		reconstruct_.Reset();
		constants_.Reset();
		device_.Reset();
	}

	void ModelResolution::ReleaseUnscaledSlots(std::span<const RendererApplyArgs> args) noexcept
	{
		for (const auto& input : args)
			if (!Required(input))
				ReleaseSlot(input.featureSlot);
	}
	void ModelResolution::ReleaseSlot(std::uint32_t slot) noexcept
	{
		if (slot < slots_.size())
			slots_[slot].reset();
	}
}
