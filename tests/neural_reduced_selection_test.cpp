#include "Features/Upscaling/NeuralRendering/CharacterMultiRoi.h"

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
		CharacterComputeRegionPlan computeRegions{};
	};
	std::optional<uint64_t> LogicalTextureBytes(unsigned, unsigned w, unsigned h) { return uint64_t(w) * h * 4; }
	namespace Color
	{
		template <unsigned>
		struct ComputeStateGuard
		{
			explicit ComputeStateGuard(Context*) {}
		};
	}
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
auto GetPreparedCharacterMask(const Settings&, unsigned, unsigned, unsigned, uint64_t, unsigned, unsigned) { return currentMask; }
#define CS_GPU_PASS_CAPTURE(name, capture) (void)(capture)
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
	unsigned allocations = 0, dispatches = 0;
	bool blendSucceeds = true;
	uint64_t measuredPixels = 0;
	std::optional<uint64_t> measuredCopiedBytes;
	bool EnsureFoveatedTexture(std::unique_ptr<Texture>& t, Resource*, unsigned w, unsigned h, bool, bool, bool, bool, const char*)
	{
		if (!t || t->desc.Width != w || t->desc.Height != h) {
			t = std::make_unique<Texture>(w, h, -1000.0f);
			++allocations;
		}
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
		ID3D11ShaderResourceView* mask, unsigned colorMode, bool fullImage)
	{
		Require(fullImage && colorMode == 0 && (!baseline == !mask));
		++dispatches;
		if (!blendSucceeds)
			return false;
		for (unsigned y = visible.minY; y < visible.maxY; ++y)
			for (unsigned x = visible.minX; x < visible.maxX; ++x) {
				Require(x < width && y < height);
				const unsigned index = y * width + x;
				const float weight = mask ? mask->resource->pixels[index] : 1.0f;
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
	const std::vector<std::vector<ComputeSubrect>> plans{
		{}, { { 0, 0, 11, 7 } }, { { 0, 0, 5, 7 }, { 5, 0, 6, 7 } },
		{ { 1, 1, 8, 5 } }, { { 0, 0, 3, 7 }, { 7, 0, 4, 7 } }
	};
	for (const unsigned eye : { 0u, 1u })
		for (const bool masked : { false, true })
			for (const auto& plan : plans) {
				Upscaling u;
				u.foveatedCenterColorIn[eye] = std::make_unique<Texture>(11, 7, 4.0f);
				u.foveatedCenterNeuralOut[eye] = std::make_unique<Texture>(11, 7, 12.0f);
				Texture mask(11, 7, 0.25f);
				currentMask = masked ? mask.srv : Handle<ID3D11ShaderResourceView>{};
				NeuralRendering::RendererApplyArgs args;
				args.colorInput = u.foveatedCenterColorIn[eye]->resource.get();
				args.featureSlot = eye;
				args.characterVisualIsolation = masked;
				args.computeRegions.count = static_cast<unsigned>(plan.size());
				std::copy(plan.begin(), plan.end(), args.computeRegions.regions.begin());
				std::vector<float> expected(77, 4.0f);
				uint64_t area = plan.empty() ? 77u : 0u;
				for (const auto& r : plan.empty() ? std::vector<ComputeSubrect>{ args.computeSubrect } : plan) {
					if (!plan.empty())
						area += r.Area();
					for (unsigned y = r.baseY; y < r.baseY + r.height; ++y)
						for (unsigned x = r.baseX; x < r.baseX + r.width; ++x)
							expected[y * 11 + x] = masked ? 6.0f : 12.0f;
				}
				// Repeat with poisoned storage: skipping a copy must not expose prior-frame pixels.
				for (unsigned repeat = 0; repeat < 2; ++repeat) {
					globals::d3d::instance.copies = 0;
					if (u.foveatedCenterNeuralSelected[eye])
						std::fill(u.foveatedCenterNeuralSelected[eye]->resource->pixels.begin(), u.foveatedCenterNeuralSelected[eye]->resource->pixels.end(), -500.0f);
					Require(u.PrepareReducedResolutionNeuralOutput(eye, args));
					Require(u.foveatedCenterNeuralSelected[eye]->resource->pixels == expected);
					Require(globals::d3d::instance.copies == (area == 77 ? 0u : 1u));
					Require(u.measuredPixels == area && u.measuredCopiedBytes == (area == 77 ? 0u : 308u));
					Require(u.allocations == 1);
				}
				const auto originalAllocations = u.allocations;
				for (const auto& invalid : std::vector<std::vector<ComputeSubrect>>{
						 { { 0, 0, 8, 7 }, { 7, 0, 4, 7 } }, { { 10, 0, 2, 7 } }, { { 0, 0, 0, 7 } } }) {
					args.computeRegions.count = static_cast<unsigned>(invalid.size());
					std::copy(invalid.begin(), invalid.end(), args.computeRegions.regions.begin());
					globals::d3d::instance.copies = u.dispatches = 0;
					Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
					Require(!globals::d3d::instance.copies && !u.dispatches && u.allocations == originalAllocations);
				}
				args.computeRegions = {};
				args.characterVisualIsolation = true;
				currentMask = {};
				Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
				args.characterVisualIsolation = false;
				u.blendSucceeds = false;
				Require(!u.PrepareReducedResolutionNeuralOutput(eye, args));
			}
}
