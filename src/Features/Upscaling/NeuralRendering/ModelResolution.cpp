#include "ModelResolution.h"
#include "ComputeStateGuard.h"
#include "GpuPass.h"
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
		};
		static_assert(sizeof(Constants) == 64);

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

		struct AllocationKey
		{
			UpscalingDLSS::Extent sourceSize{}, modelSize{};
			DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN, outputFormat = DXGI_FORMAT_UNKNOWN, motionFormat = DXGI_FORMAT_UNKNOWN;
			bool hasMask = false;

			bool operator==(const AllocationKey&) const = default;

			static std::optional<AllocationKey> Read(const RendererApplyArgs& input, const RendererApplyArgs& projected)
			{
				D3D11_TEXTURE2D_DESC color{}, output{}, motion{};
				if (!ReadDescription(input.colorInput, color) || !ReadDescription(input.colorOutput, output) ||
					!ReadDescription(input.motionVectors, motion) ||
					color.Width != input.colorWidth || color.Height != input.colorHeight ||
					output.Width != input.outputWidth || output.Height != input.outputHeight ||
					motion.Width != input.guideWidth || motion.Height != input.guideHeight)
					return std::nullopt;
				return AllocationKey{ { input.outputWidth, input.outputHeight }, { projected.outputWidth, projected.outputHeight },
					color.Format, output.Format, motion.Format, input.controlMask != nullptr };
			}

			std::optional<std::uint64_t> Bytes() const noexcept
			{
				std::uint64_t total = 0;
				for (auto format : { colorFormat, DXGI_FORMAT_R32_FLOAT, motionFormat, outputFormat }) {
					const auto bytes = LogicalTextureBytes(format, modelSize.width, modelSize.height);
					if (!bytes)
						return std::nullopt;
					total += *bytes;
				}
				if (hasMask)
					total += static_cast<std::uint64_t>(modelSize.width) * modelSize.height;
				const auto fullOutput = LogicalTextureBytes(outputFormat, sourceSize.width, sourceSize.height);
				return fullOutput ? std::optional{ total + *fullOutput } : std::nullopt;
			}
		};

		ComputeSubrect SourceRegion(const RendererApplyArgs& args)
		{
			return args.roi                      ? args.roi->ownedOutput :
			       args.computeSubrect.IsValid() ? args.computeSubrect :
			                                       BuildCenteredComputeSubrect(args.outputWidth, args.outputHeight, args.tuning.singleSubrectScale);
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
		const auto geometry = BuildModelResolutionGeometry(input.viewportCrop,
			{ input.outputWidth, input.outputHeight }, SourceRegion(input), input.modelResolutionPercent);
		if (!geometry || input.renderingMode != RenderingMode::ReducedResolution || !input.reset || input.featureUpscaling ||
			input.modelResolutionPercent == kMaximumModelResolutionPercent ||
			input.colorWidth != input.outputWidth || input.colorHeight != input.outputHeight ||
			input.guideWidth != input.outputWidth || input.guideHeight != input.outputHeight ||
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
		adapted.colorWidth = adapted.guideWidth = adapted.outputWidth = modelSize.width;
		adapted.colorHeight = adapted.guideHeight = adapted.outputHeight = modelSize.height;
		adapted.computeSubrect = geometry->modelRegion;
		adapted.viewportCrop = geometry->nativeCrop;
		if (input.roi) {
			auto& roi = *adapted.roi;
			roi.ownedOutput = map(input.roi->ownedOutput);
			roi.inferenceContext = map(input.roi->inferenceContext);
			roi.allocationCapacity = modelSize;
			if (input.roi->samplingSupport)
				roi.samplingSupport = map(*input.roi->samplingSupport);
			if (input.roi->temporalEnvelope)
				roi.temporalEnvelope = map(*input.roi->temporalEnvelope);
		}
		if (input.controlMask) {
			adapted.controlMaskWidth = modelSize.width;
			adapted.controlMaskHeight = modelSize.height;
		}
		if (input.actorSelection)
			adapted.actorSelectionSupport = map(input.actorSelectionSupport);
		return true;
	}

	std::optional<std::uint64_t> ModelResolution::AdditionalBytes(std::span<const RendererApplyArgs> args) const
	{
		std::uint64_t total = 0;
		for (const auto& input : args) {
			if (input.featureSlot >= slots_.size())
				return std::nullopt;
			RendererApplyArgs projected;
			if (!Project(input, projected))
				return std::nullopt;
			const auto allocation = AllocationKey::Read(input, projected);
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

	bool ModelResolution::Prepare(std::span<const RendererApplyArgs> args, Batch& batch, HRESULT& result)
	{
		result = E_INVALIDARG;
		batch = {};
		if (args.empty() || args.size() > batch.arguments.size())
			return false;
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& input = args[index];
			if (!input.device || !input.context || input.featureSlot >= slots_.size() || !Project(input, batch.arguments[index]))
				return false;
		}
		if (!EnsureShaders(args.front().device, result))
			return false;
		try {
			for (std::size_t index = 0; index < args.size(); ++index) {
				const auto& input = args[index];
				const auto allocation = AllocationKey::Read(input, batch.arguments[index]);
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
					if (!CreateTexture(input.device, work->color, modelSize, key.colorFormat, prefix + "Color", result) ||
						!CreateTexture(input.device, work->depth, modelSize, DXGI_FORMAT_R32_FLOAT, prefix + "Depth", result) ||
						!CreateTexture(input.device, work->motion, modelSize, key.motionFormat, prefix + "Motion", result) ||
						!CreateTexture(input.device, work->output, modelSize, key.outputFormat, prefix + "Output", result) ||
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

				const auto geometry = BuildModelResolutionGeometry(input.viewportCrop, sourceSize, SourceRegion(input), input.modelResolutionPercent);
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
					geometry->motionNormalization
				};
				auto& adapted = batch.arguments[index];
				adapted.colorInput = work->color.resource.Get();
				adapted.depthGuide = work->depth.resource.Get();
				adapted.depthGuideSRV = work->depth.srv.Get();
				adapted.motionVectors = work->motion.resource.Get();
				adapted.colorOutput = work->output.resource.Get();

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
		batch.count = args.size();
		batch.reconstructionShader = reconstruct_;
		batch.constants = constants_;
		auto* context = args.front().context;
		ComputeStateGuard<4, 4> guard(context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			context->UpdateSubresource(constants_.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = constants_.Get();
			const std::array srvs{ work.colorView.Get(), args[index].depthGuideSRV, work.motionView.Get(), work.allocation.hasMask ? work.maskView.Get() : nullptr };
			const std::array uavs{ work.color.uav.Get(), work.depth.uav.Get(), work.motion.uav.Get(), work.allocation.hasMask ? work.mask.uav.Get() : nullptr };
			context->CSSetShader(prepare_.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
			{
				CS_GPU_PASS("NeuralRendering::ModelResolutionPrepare");
				context->Dispatch((work.constants.modelRegion[0] + 7u) / 8u, (work.constants.modelRegion[1] + 7u) / 8u, 1);
			}
			guard.Unbind();
		}
		result = args.front().device->GetDeviceRemovedReason();
		return SUCCEEDED(result);
	}

	bool ModelResolution::Reconstruct(std::span<const RendererApplyArgs> args, const Batch& batch, HRESULT& result)
	{
		result = E_INVALIDARG;
		if (batch.count != args.size() || args.empty() || args.size() > batch.resources.size() ||
			!batch.reconstructionShader || !batch.constants ||
			std::ranges::any_of(std::span(batch.resources.data(), batch.count), [](const auto& work) { return !work; }))
			return false;
		auto* context = args.front().context;
		ComputeStateGuard<3> guard(context);
		for (std::size_t index = 0; index < args.size(); ++index) {
			const auto& work = *batch.resources[index];
			context->UpdateSubresource(batch.constants.Get(), 0, nullptr, &work.constants, 0, 0);
			auto* cb = batch.constants.Get();
			const std::array srvs{ work.colorView.Get(), work.color.srv.Get(), work.output.srv.Get() };
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
			if (input.modelResolutionPercent == kMaximumModelResolutionPercent)
				ReleaseSlot(input.featureSlot);
	}
	void ModelResolution::ReleaseSlot(std::uint32_t slot) noexcept
	{
		if (slot < slots_.size())
			slots_[slot].reset();
	}
}
