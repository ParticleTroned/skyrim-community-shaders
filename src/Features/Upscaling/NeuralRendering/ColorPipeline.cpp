#include "ColorPipeline.h"

#include "Renderer.h"
#include "Utils/D3D.h"

#include <array>
#include <cstddef>
#include <utility>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace NeuralRendering
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		constexpr wchar_t kConfigPath[] = L"Data/SKSE/Plugins/CommunityShaders/NRColor.ini";
		constexpr wchar_t kShaderPath[] = L"Data/Shaders/Upscaling/NeuralRendering/ColorPipelineCS.hlsl";
		struct Constants
		{
			std::uint32_t baseX, baseY, width, height;
			std::uint32_t mode, sourceTransfer, modelCodec, radius;
			float whitePoint, detailStrength, appearanceMix, maxDetailStops;
			float maximumR, maximumG, maximumB, padding;
		};
		static_assert(sizeof(Constants) == 64);
		static_assert(offsetof(Constants, whitePoint) == 32);
		static_assert(offsetof(Constants, maximumR) == 48);

		// Preserve every compute slot used below, including predication: a leftover
		// occlusion predicate must not selectively suppress preparation or resolve.
		class ScopedComputeState
		{
		public:
			explicit ScopedComputeState(ID3D11DeviceContext* a_context) : context_(a_context)
			{
				context_->CSGetShader(&shader_, instances_.data(), &instanceCount_);
				context_->CSGetConstantBuffers(0, 1, &constant_);
				context_->CSGetShaderResources(0, 3, resources_.data());
				context_->CSGetUnorderedAccessViews(0, 1, &output_);
				context_->GetPredication(&predicate_, &predicateValue_);
				context_->SetPredication(nullptr, FALSE);
				Unbind();
			}
			~ScopedComputeState()
			{
				Unbind();
				context_->CSSetShader(shader_, instances_.data(), instanceCount_);
				context_->CSSetConstantBuffers(0, 1, &constant_);
				context_->CSSetShaderResources(0, 3, resources_.data());
				context_->CSSetUnorderedAccessViews(0, 1, &output_, nullptr);
				context_->SetPredication(predicate_, predicateValue_);
				if (shader_) shader_->Release();
				if (constant_) constant_->Release();
				if (output_) output_->Release();
				if (predicate_) predicate_->Release();
				for (auto* resource : resources_) if (resource) resource->Release();
				for (UINT i = 0; i < instanceCount_; ++i) if (instances_[i]) instances_[i]->Release();
			}
			ScopedComputeState(const ScopedComputeState&) = delete;
			ScopedComputeState& operator=(const ScopedComputeState&) = delete;
		private:
			void Unbind()
			{
				ID3D11ShaderResourceView* nullResources[3]{};
				ID3D11UnorderedAccessView* nullOutput = nullptr;
				context_->CSSetShaderResources(0, 3, nullResources);
				context_->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
			}
			ID3D11DeviceContext* context_;
			ID3D11ComputeShader* shader_ = nullptr;
			ID3D11Buffer* constant_ = nullptr;
			ID3D11UnorderedAccessView* output_ = nullptr;
			ID3D11Predicate* predicate_ = nullptr;
			BOOL predicateValue_ = FALSE;
			std::array<ID3D11ShaderResourceView*, 3> resources_{};
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> instances_{};
			UINT instanceCount_ = D3D11_SHADER_MAX_INTERFACES;
		};

		struct Texture
		{
			ComPtr<ID3D11Texture2D> resource;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
			void Abandon() noexcept { (void)resource.Detach(); (void)srv.Detach(); (void)uav.Detach(); }
		};
		bool CreateTexture(ID3D11Device* a_device, D3D11_TEXTURE2D_DESC a_desc, Texture& a_texture)
		{
			a_desc.MiscFlags = 0;
			a_desc.CPUAccessFlags = 0;
			a_desc.Usage = D3D11_USAGE_DEFAULT;
			a_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			return SUCCEEDED(a_device->CreateTexture2D(&a_desc, nullptr, &a_texture.resource)) &&
			       SUCCEEDED(a_device->CreateShaderResourceView(a_texture.resource.Get(), nullptr, &a_texture.srv)) &&
			       SUCCEEDED(a_device->CreateUnorderedAccessView(a_texture.resource.Get(), nullptr, &a_texture.uav));
		}
		Constants MakeConstants(const Color::Settings& a_settings, const ComputeSubrect& a_rect, DXGI_FORMAT a_format)
		{
			float maximum = std::numeric_limits<float>::max();
			if (a_format == DXGI_FORMAT_R16G16B16A16_FLOAT) maximum = 65504.0f;
			if (a_format == DXGI_FORMAT_R8G8B8A8_UNORM || a_format == DXGI_FORMAT_B8G8R8A8_UNORM ||
				a_format == DXGI_FORMAT_R10G10B10A2_UNORM) maximum = 1.0f;
			Constants constants{
				a_rect.baseX, a_rect.baseY, a_rect.width, a_rect.height,
				static_cast<std::uint32_t>(a_settings.mode), static_cast<std::uint32_t>(a_settings.sourceTransfer),
				static_cast<std::uint32_t>(a_settings.modelCodec), a_settings.radius,
				a_settings.whitePoint, a_settings.detailStrength, a_settings.appearanceMix, a_settings.maxDetailStops,
				maximum, maximum, maximum, 0.0f
			};
			if (a_format == DXGI_FORMAT_R11G11B10_FLOAT) {
				constants.maximumR = constants.maximumG = 65024.0f;
				constants.maximumB = 64512.0f;
			}
			return constants;
		}
	}

	struct ColorPipeline::State
	{
		struct Slot
		{
			Texture baseline, result;
			UINT width = 0, height = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		};
		std::array<Slot, Runtime::kFeatureSlotCount> slots;
		Color::Configuration configuration;
		bool loaded = false, valid = true;
		std::string description = "NR colour: configuration not loaded";
		ComPtr<ID3D11ComputeShader> prepare, resolve;
		ComPtr<ID3D11Buffer> constants;

		bool Shaders(ID3D11Device* a_device)
		{
			if (!prepare) prepare.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(kShaderPath, {}, "cs_5_0", "Prepare")));
			if (!resolve) resolve.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(kShaderPath, {}, "cs_5_0", "Resolve")));
			if (!prepare || !resolve) return false;
			if (!constants) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(Constants);
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				if (FAILED(a_device->CreateBuffer(&desc, nullptr, &constants))) return false;
			}
			return true;
		}
		void Dispatch(ID3D11DeviceContext* a_context, ID3D11ComputeShader* a_shader,
			const Constants& a_constants, ID3D11ShaderResourceView* a_baseline,
			ID3D11ShaderResourceView* a_prepared, ID3D11ShaderResourceView* a_model,
			ID3D11UnorderedAccessView* a_output)
		{
			a_context->UpdateSubresource(constants.Get(), 0, nullptr, &a_constants, 0, 0);
			ID3D11Buffer* buffer = constants.Get();
			ID3D11ShaderResourceView* sources[]{ a_baseline, a_prepared, a_model };
			a_context->CSSetConstantBuffers(0, 1, &buffer);
			a_context->CSSetShaderResources(0, 3, sources);
			a_context->CSSetUnorderedAccessViews(0, 1, &a_output, nullptr);
			a_context->CSSetShader(a_shader, nullptr, 0);
			a_context->Dispatch((a_constants.width + 7u) / 8u, (a_constants.height + 7u) / 8u, 1);
		}
	};

	ColorPipeline::ColorPipeline() : state_(std::make_unique<State>()) {}
	ColorPipeline::~ColorPipeline() = default;

	bool ColorPipeline::LoadSettings()
	{
		if (state_->loaded) return state_->valid;
		state_->loaded = true;
		std::error_code error;
		const bool exists = std::filesystem::exists(kConfigPath, error);
		if (!exists && !error) {
			state_->description = "NR colour: raw (NRColor.ini absent); exposure unknown; model contract unverified";
			return true;
		}
		std::ifstream input(std::filesystem::path(kConfigPath), std::ios::binary);
		std::array<char, Color::kMaximumConfigurationBytes + 1> bytes{};
		if (error || !input) {
			state_->valid = false;
			state_->description = "NR colour: cannot read NRColor.ini; NR rejected until reset";
			return false;
		}
		input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		std::string reason;
		state_->valid = !input.bad() && Color::Parse(
			std::string_view(bytes.data(), static_cast<std::size_t>(input.gcount())), state_->configuration, reason);
		if (!state_->valid) {
			state_->description = "NR colour: invalid NRColor.ini: " + reason;
			return false;
		}
		std::ostringstream summary;
		summary << "NR colour: explicit developer profiles; exposure unavailable (WhitePoint is manual, not game exposure)";
		for (std::size_t i = 0; i < state_->configuration.profiles.size(); ++i) {
			const auto& s = state_->configuration.profiles[i];
			summary << "; " << (i == 0 ? "upscaled_center=" : "final_ldr_pre_ui=") << Color::Name(s.mode)
				<< ",transfer=" << static_cast<std::uint32_t>(s.sourceTransfer)
				<< ",codec=" << static_cast<std::uint32_t>(s.modelCodec) << ",white=" << s.whitePoint
				<< ",detail=" << s.detailStrength << ",appearance=" << s.appearanceMix
				<< ",radius=" << s.radius << ",roundtrip=" << s.roundTrip;
		}
		state_->description = summary.str();
		return true;
	}

	const Color::Settings& ColorPipeline::Settings(InsertionPoint a_point) const noexcept
	{
		static const Color::Settings raw;
		const auto index = static_cast<std::size_t>(a_point);
		return index < state_->configuration.profiles.size() ? state_->configuration.profiles[index] : raw;
	}
	const std::string& ColorPipeline::Description() const noexcept { return state_->description; }

	bool ColorPipeline::Ensure(std::uint32_t a_slot, ID3D11Device* a_device,
		const SharedTexture& a_input, const SharedTexture& a_output)
	{
		if (a_slot >= state_->slots.size() || !a_device || !a_input.srv11 || !a_input.uav11 || !a_output.srv11 ||
			a_input.desc.Width != a_output.desc.Width || a_input.desc.Height != a_output.desc.Height ||
			a_input.desc.Format != a_output.desc.Format) return false;
		auto& slot = state_->slots[a_slot];
		if (!slot.baseline.resource || slot.width != a_input.desc.Width || slot.height != a_input.desc.Height || slot.format != a_input.desc.Format) {
			State::Slot replacement;
			if (!CreateTexture(a_device, a_input.desc, replacement.baseline) || !CreateTexture(a_device, a_output.desc, replacement.result)) return false;
			replacement.width = a_input.desc.Width;
			replacement.height = a_input.desc.Height;
			replacement.format = a_input.desc.Format;
			slot = std::move(replacement);
		}
		return state_->Shaders(a_device);
	}

	bool ColorPipeline::Prepare(const RendererApplyArgs& a_args, const ComputeSubrect& a_rect, const SharedTexture& a_input)
	{
		if (a_args.featureSlot >= state_->slots.size() || !state_->prepare || !a_args.context) return false;
		auto& slot = state_->slots[a_args.featureSlot];
		if (!slot.baseline.resource) return false;
		ScopedComputeState saved(a_args.context);
		CS_PROFILE_SCOPE("Upscaling::DLSSNRColorPrepare");
		const D3D11_BOX box{ a_rect.baseX, a_rect.baseY, 0, a_rect.baseX + a_rect.width, a_rect.baseY + a_rect.height, 1 };
		a_args.context->CopySubresourceRegion(slot.baseline.resource.Get(), 0, a_rect.baseX, a_rect.baseY, 0, a_input.resource11.Get(), 0, &box);
		const auto& settings = Settings(a_args.insertionPoint);
		if (settings.modelCodec == Color::Codec::Identity)
			return SUCCEEDED(a_args.device->GetDeviceRemovedReason());
		const auto constants = MakeConstants(settings, a_rect, slot.format);
		state_->Dispatch(a_args.context, state_->prepare.Get(), constants, slot.baseline.srv.Get(), nullptr, nullptr, a_input.uav11.Get());
		return SUCCEEDED(a_args.device->GetDeviceRemovedReason());
	}

	bool ColorPipeline::Resolve(const RendererApplyArgs& a_args, const ComputeSubrect& a_rect,
		const SharedTexture& a_input, const SharedTexture& a_output)
	{
		if (a_args.featureSlot >= state_->slots.size() || !state_->resolve || !a_args.context) return false;
		auto& slot = state_->slots[a_args.featureSlot];
		if (!slot.result.uav) return false;
		ScopedComputeState saved(a_args.context);
		CS_PROFILE_SCOPE("Upscaling::DLSSNRColorResolve");
		const auto constants = MakeConstants(Settings(a_args.insertionPoint), a_rect, slot.format);
		state_->Dispatch(a_args.context, state_->resolve.Get(), constants,
			slot.baseline.srv.Get(), a_input.srv11.Get(), a_output.srv11.Get(), slot.result.uav.Get());
		return SUCCEEDED(a_args.device->GetDeviceRemovedReason());
	}

	ID3D11Texture2D* ColorPipeline::Output(std::uint32_t a_slot) const noexcept
	{
		return a_slot < state_->slots.size() ? state_->slots[a_slot].result.resource.Get() : nullptr;
	}

	void ColorPipeline::RecordRoundTrip(ID3D12GraphicsCommandList* a_list,
		const SharedTexture& a_input, const SharedTexture& a_output, const ComputeSubrect& a_rect) const
	{
		D3D12_RESOURCE_BARRIER barriers[2]{};
		for (auto& barrier : barriers) {
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}
		barriers[0].Transition.pResource = a_input.resource12.Get();
		barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barriers[1].Transition.pResource = a_output.resource12.Get();
		barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		a_list->ResourceBarrier(2, barriers);
		D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
		source.pResource = a_input.resource12.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.pResource = a_output.resource12.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		const D3D12_BOX box{ a_rect.baseX, a_rect.baseY, 0, a_rect.baseX + a_rect.width, a_rect.baseY + a_rect.height, 1 };
		a_list->CopyTextureRegion(&destination, a_rect.baseX, a_rect.baseY, 0, &source, &box);
		for (auto& barrier : barriers) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		a_list->ResourceBarrier(2, barriers);
	}

	void ColorPipeline::ResetResources(bool a_resetShaders)
	{
		state_->slots = {};
		// Constant buffers and compiled shaders belong to the old D3D11 device.
		state_->constants.Reset();
		state_->prepare.Reset();
		state_->resolve.Reset();
		(void)a_resetShaders;
	}
	void ColorPipeline::ReloadSettings()
	{
		state_->configuration = {};
		state_->loaded = false;
		state_->valid = true;
		state_->description = "NR colour: reload pending";
	}
	void ColorPipeline::Abandon() noexcept
	{
		for (auto& slot : state_->slots) { slot.baseline.Abandon(); slot.result.Abandon(); }
		(void)state_->constants.Detach();
		(void)state_->prepare.Detach();
		(void)state_->resolve.Detach();
	}
}
