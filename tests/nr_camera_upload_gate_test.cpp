#include <array>
#include <cstdint>
#include <stdexcept>

using UINT = unsigned;
struct ID3D11Resource
{};
struct ID3D11DeviceContext
{
	unsigned unmaps = 0;
	void Unmap(ID3D11Resource*, UINT) { ++unmaps; }
};
namespace globals
{
	namespace game
	{
		bool isVR = true;
		struct FrameBufferVR
		{
			std::array<std::uint8_t, 0x570> bytes{};
			bool operator==(const FrameBufferVR&) const = default;
		};
		struct FrameBuffer
		{
			std::array<std::uint8_t, 0x400> bytes{};
			bool operator==(const FrameBuffer&) const = default;
		};
		struct
		{
			FrameBufferVR vr;
			FrameBuffer nonVR;
		} frameBufferCached;
		void* mappedFrameBuffer = nullptr;
		ID3D11Resource* frameResource = nullptr;
		ID3D11Resource** perFrame = &frameResource;
	}
	namespace d3d
	{
		ID3D11DeviceContext* context = nullptr;
	}
	struct State
	{
		unsigned frameCount = 0;
	} stateStorage;
	State* state = &stateStorage;
	namespace features
	{
		struct Upscaling
		{
			bool enabled = false;
			unsigned captures = 0;
			bool IsNeuralRenderingEnabled() const { return enabled; }
#ifdef DEVBENCH_BRIDGE_ENABLED
			void RecordNeuralCaptureCamera(unsigned) { ++captures; }
#endif
		} upscaling;
	}
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return globals::game::isVR; }
	};
}
namespace globals
{
#include "nr_camera_upload_under_test.h"
}
int main()
{
	using namespace globals;
	const auto require = [](bool condition) { if (!condition) throw std::runtime_error("NR-off camera isolation failed"); };
	ID3D11DeviceContext context;
	ID3D11Resource resource, otherResource;
	d3d::context = &context;
	game::frameResource = &resource;
	game::FrameBufferVR upload;
	unsigned expectedCaptures = 0;
	unsigned updates = 0;
	for (const bool enabled : { false, true, false, true, false }) {
		features::upscaling.enabled = enabled;
		for (unsigned frame = 0; frame < 64; ++frame) {
			++updates;
			state->frameCount = updates;
			for (std::size_t i = 0; i < upload.bytes.size(); ++i)
				upload.bytes[i] = static_cast<std::uint8_t>((updates * 17) ^ i);
			game::mappedFrameBuffer = &upload;
			ObserveVRFrameBufferUpload(&context, &resource, 0, &upload);
			require(game::frameBufferCached.vr == upload);
			require(game::mappedFrameBuffer == nullptr);
			require(context.unmaps == updates);
#ifdef DEVBENCH_BRIDGE_ENABLED
			expectedCaptures += enabled;
#endif
			require(features::upscaling.captures == expectedCaptures);
		}
	}
	const auto last = game::frameBufferCached.vr;
	game::mappedFrameBuffer = &upload;
	ObserveVRFrameBufferUpload(&context, &resource, 0, nullptr);
	require(game::frameBufferCached.vr == last && game::mappedFrameBuffer == nullptr);
	upload.bytes.fill(0);
	ObserveVRFrameBufferUpload(&context, &otherResource, 0, &upload);
	ObserveVRFrameBufferUpload(&context, &resource, 1, &upload);
	ID3D11DeviceContext otherContext;
	ObserveVRFrameBufferUpload(&otherContext, &resource, 0, &upload);
	require(game::frameBufferCached.vr == last);
	require(context.unmaps == updates + 3 && otherContext.unmaps == 1);
	require(features::upscaling.captures == expectedCaptures);
	// The shared SE/AE cache remains independent of VR diagnostic admission.
	game::isVR = false;
	features::upscaling.enabled = true;
	game::FrameBuffer flat;
	flat.bytes.fill(42);
	CacheFramebuffer(&flat);
	require(game::frameBufferCached.nonVR == flat);
	require(features::upscaling.captures == expectedCaptures);
}
