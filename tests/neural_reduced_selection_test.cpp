#include "Features/Upscaling/NeuralRendering/ComputeSubrect.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

void Require(bool value)
{
	if (!value)
		throw std::runtime_error("Reduced selection copy/coverage invariant");
}
struct Resource
{
	std::vector<float> pixels;
};
struct ID3D11ShaderResourceView
{
	Resource* resource;
};
struct ID3D11SamplerState
{};
template <class T>
struct Handle
{
	T* value = nullptr;
	T* get() const { return value; }
	T* Get() const { return value; }
	T** put() { return &value; }
	explicit operator bool() const { return value != nullptr; }
};
namespace winrt
{
	template <class T>
	using com_ptr = Handle<T>;
}
namespace Microsoft::WRL
{
	template <class T>
	using ComPtr = Handle<T>;
}
struct Texture
{
	struct
	{
		unsigned Format = 1, Width, Height;
	} desc;
	std::unique_ptr<Resource> resource;
	ID3D11ShaderResourceView view;
	Handle<ID3D11ShaderResourceView> srv, uav;
	Texture(unsigned width, unsigned height, float value) : desc{ 1, width, height },
															resource(std::make_unique<Resource>(Resource{ std::vector<float>(width * height, value) })),
															view{ resource.get() }, srv{ &view }, uav{ &view } {}
};
struct Context
{
	unsigned copies = 0;
	void CopyResource(Resource* destination, Resource* source)
	{
		Require(destination && source && destination != source && destination->pixels.size() == source->pixels.size());
		destination->pixels = source->pixels;
		++copies;
	}
	void CSGetSamplers(unsigned, unsigned, ID3D11SamplerState**) {}
	void CSSetSamplers(unsigned, unsigned, ID3D11SamplerState**) {}
};
namespace globals::d3d
{
	Context instance;
	Context* context = &instance;
}
namespace NeuralRendering
{
	struct RendererApplyArgs
	{
		unsigned colorWidth = 11, colorHeight = 7, outputWidth = 11, outputHeight = 7;
		unsigned featureSlot = 0, frameId = 1, sourceWorldFrame = 1;
		uint64_t generation = 1;
		bool characterVisualIsolation = false;
		Resource* colorInput = nullptr;
		ComputeSubrect computeSubrect{ 0, 0, 11, 7 };
	};
	std::optional<uint64_t> LogicalTextureBytes(unsigned, unsigned w, unsigned h) { return uint64_t(w) * h * 4; }
	template <unsigned>
	struct ComputeStateGuard
	{
		explicit ComputeStateGuard(Context*) {}
	};
}
template <class F>
struct ScopeExit
{
	F f;
	~ScopeExit() { f(); }
};
struct float2
{
	float x, y;
};
struct Settings
{};
Handle<ID3D11ShaderResourceView> currentMask;
bool currentProviderBlending = false;
namespace NeuralRendering
{
	struct CharacterPreparedSelection
	{
		Handle<ID3D11ShaderResourceView> mask;
		ComputeSubrect maskSupport{};
		bool providerBlending = false;
	};
}
auto GetPreparedCharacterSelection(const Settings&, unsigned, unsigned, unsigned, uint64_t, unsigned width, unsigned height)
{
	return NeuralRendering::CharacterPreparedSelection{ currentMask, { 0, 0, width, height }, currentProviderBlending };
}
#define CS_GPU_PASS_CAPTURE(name, capture) (void)(capture)
namespace UpscalingTextures
{
	enum class CenterInput
	{
		Color
	};
}
struct Upscaling
{
	enum class NeuralStereoRouteRole
	{
		Main,
		Submit
	};
	struct FoveatedDispatchRect
	{
		unsigned inputWidth = 0, outputWidth = 0, inputHeight = 0, outputHeight = 0;
	};
	struct FoveatedRegionPlan
	{
		struct Rect
		{
			unsigned minX, minY, maxX, maxY;
		};
	};
	Settings settings;
	std::array<std::unique_ptr<Texture>, 2> foveatedCenterColorIn, foveatedCenterNeuralOut, foveatedCenterNeuralSelected;
	Texture* preparedInput = nullptr;
	Texture* GetFoveatedCenterInput(unsigned eye, UpscalingTextures::CenterInput) const
	{
		return preparedInput ? preparedInput : foveatedCenterColorIn[eye].get();
	}
	unsigned allocations = 0, allocationAttempts = 0, selectedPreparations = 0, dispatches = 0;
	bool selectedAllocationAllowed = true, blendSucceeds = true;
	uint64_t measuredPixels = 0;
	std::optional<uint64_t> measuredCopiedBytes;
	bool EnsureReducedResolutionNeuralSelected(unsigned eye, Resource*, unsigned w, unsigned h)
	{
		++selectedPreparations;
		auto& selected = foveatedCenterNeuralSelected[eye];
		if (selected && selected->resource && selected->srv && selected->uav &&
			selected->desc.Width == w && selected->desc.Height == h)
			return true;
		++allocationAttempts;
		if (!selectedAllocationAllowed)
			return false;
		selected = std::make_unique<Texture>(w, h, -1000.0f);
		++allocations;
		return true;
	}
	unsigned CaptureNeuralStage(NeuralStereoRouteRole, unsigned, unsigned, unsigned, uint64_t, const char*) { return 1; }
	void RecordNeuralStageWork(unsigned, uint64_t pixels, std::optional<uint64_t> copied)
	{
		measuredPixels = pixels;
		measuredCopiedBytes = copied;
	}
	bool DispatchFoveatedBlendPass(ID3D11ShaderResourceView* neural, ID3D11ShaderResourceView* output,
		unsigned width, unsigned height, FoveatedDispatchRect, FoveatedRegionPlan::Rect visible,
		float, float, float2, float, unsigned, ID3D11ShaderResourceView* baseline,
		ID3D11ShaderResourceView* mask, unsigned colorMode, bool fullImage,
		const NeuralRendering::ComputeSubrect* support, bool providerBlending)
	{
		Require(fullImage && colorMode == 0 && (!baseline == !mask) && (!mask == !support));
		Require(!support || *support == NeuralRendering::ComputeSubrect{ 0, 0, width, height });
		++dispatches;
		if (!blendSucceeds)
			return false;
		for (unsigned y = visible.minY; y < visible.maxY; ++y)
			for (unsigned x = visible.minX; x < visible.maxX; ++x) {
				Require(x < width && y < height);
				const unsigned index = y * width + x;
				const float strength = mask ? mask->resource->pixels[index] : 1.0f;
				const float weight = providerBlending ? (strength > 0 ? 1.0f : 0.0f) : strength;
				const float base = baseline ? baseline->resource->pixels[index] : 0.0f;
				output->resource->pixels[index] = weight * neural->resource->pixels[index] + (1.0f - weight) * base;
			}
		return true;
	}
	bool PrepareReducedResolutionNeuralOutput(uint32_t, const NeuralRendering::RendererApplyArgs&);
};
#include "neural_reduced_selection_under_test.h"

int main()
{
	using NeuralRendering::ComputeSubrect;
	const std::vector<ComputeSubrect> subrects{
		{ 0, 0, 11, 7 }, { 1, 1, 8, 5 }, { 0, 0, 3, 7 }, { 7, 0, 4, 7 }
	};
	for (const unsigned eye : { 0u, 1u })
		for (const bool provider : { false, true })
			for (const bool masked : { false, true })
				for (const auto& subrect : subrects) {
					Upscaling u;
					u.foveatedCenterColorIn[eye] = std::make_unique<Texture>(11, 7, 4.0f);
					u.foveatedCenterNeuralOut[eye] = std::make_unique<Texture>(11, 7, 12.0f);
					Texture mask(11, 7, 0.25f);
					mask.resource->pixels[0] = 0;
					currentProviderBlending = provider;
					currentMask = masked ? mask.srv : Handle<ID3D11ShaderResourceView>{};
					NeuralRendering::RendererApplyArgs args;
					args.colorInput = u.foveatedCenterColorIn[eye]->resource.get();
					args.featureSlot = eye;
					args.characterVisualIsolation = masked;
					args.computeSubrect = subrect;
					std::vector<float> expected(77, 4.0f);
					const uint64_t area = subrect.Area();
					for (unsigned y = subrect.baseY; y < subrect.baseY + subrect.height; ++y)
						for (unsigned x = subrect.baseX; x < subrect.baseX + subrect.width; ++x)
							expected[y * 11 + x] = !masked ? 12.0f : y * 11 + x == 0 ? 4.0f :
							                                     provider            ? 12.0f :
							                                                           6.0f;
					// Repeat with poisoned storage: skipping a copy must not expose prior-frame pixels.
					std::unique_ptr<Texture> borrowed;
					for (unsigned repeat = 0; repeat < 2; ++repeat) {
						if (repeat) {
							borrowed = std::move(u.foveatedCenterColorIn[eye]);
							u.preparedInput = borrowed.get();
							u.foveatedCenterColorIn[eye] = std::make_unique<Texture>(11, 7, -999.0f);
						}
						globals::d3d::instance.copies = 0;
						u.dispatches = 0;
						if (u.foveatedCenterNeuralSelected[eye])
							std::fill(u.foveatedCenterNeuralSelected[eye]->resource->pixels.begin(), u.foveatedCenterNeuralSelected[eye]->resource->pixels.end(), -500.0f);
						u.selectedAllocationAllowed = repeat == 0;
						Require(u.PrepareReducedResolutionNeuralOutput(eye, args));
						Require(u.foveatedCenterNeuralSelected[eye]->resource->pixels == expected);
						Require(globals::d3d::instance.copies == (area == 77 ? 0u : 1u));
						Require(u.measuredPixels == area && u.measuredCopiedBytes == (area == 77 ? 0u : 308u));
						Require(u.allocations == 1 && u.allocationAttempts == 1);
						Require(u.selectedPreparations == repeat + 1 && u.dispatches == 1);
					}
					const auto originalAllocations = u.allocations;
					for (const auto& invalid : std::vector<ComputeSubrect>{
							 { 10, 0, 2, 7 }, { 0, 6, 11, 2 }, { 0, 0, 0, 7 }, { UINT32_MAX, 0, 2, 7 } }) {
						args.computeSubrect = invalid;
						globals::d3d::instance.copies = u.dispatches = 0;
						Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
						Require(!globals::d3d::instance.copies && !u.dispatches && u.allocations == originalAllocations);
					}
					args.computeSubrect = subrect;
					args.characterVisualIsolation = true;
					currentMask = {};
					Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
					args.characterVisualIsolation = false;
					u.blendSucceeds = false;
					Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
				}
	for (const unsigned eye : { 0u, 1u })
		for (const bool partial : { false, true })
			for (const unsigned cachedState : { 0u, 1u, 2u }) {
				Upscaling u;
				u.foveatedCenterColorIn[eye] = std::make_unique<Texture>(11, 7, 4.0f);
				u.foveatedCenterNeuralOut[eye] = std::make_unique<Texture>(11, 7, 12.0f);
				if (cachedState)
					u.foveatedCenterNeuralSelected[eye] = std::make_unique<Texture>(cachedState == 1 ? 3u : 11u, 7, -500.0f);
				if (cachedState == 2)
					u.foveatedCenterNeuralSelected[eye]->uav = {};
				auto* previous = u.foveatedCenterNeuralSelected[eye].get();
				const auto previousPixels = previous ? previous->resource->pixels : std::vector<float>{};
				u.selectedAllocationAllowed = false;
				currentMask = {};
				NeuralRendering::RendererApplyArgs args;
				args.colorInput = u.foveatedCenterColorIn[eye]->resource.get();
				args.featureSlot = eye;
				args.computeSubrect = partial ? ComputeSubrect{ 1, 1, 8, 5 } : ComputeSubrect{ 0, 0, 11, 7 };
				globals::d3d::instance.copies = 0;
				Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
				Require(u.selectedPreparations == 1 && u.allocationAttempts == 1 && !u.allocations);
				Require(!globals::d3d::instance.copies && !u.dispatches && !u.measuredPixels && !u.measuredCopiedBytes);
				Require(u.foveatedCenterNeuralSelected[eye].get() == previous);
				Require(!previous || previous->resource->pixels == previousPixels);
				Require(u.foveatedCenterColorIn[eye]->resource->pixels == std::vector<float>(77, 4.0f));
				Require(u.foveatedCenterNeuralOut[eye]->resource->pixels == std::vector<float>(77, 12.0f));
			}
}
