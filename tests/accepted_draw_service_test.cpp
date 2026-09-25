#include "Api/AcceptedDrawGeometryScope.h"
#include "Api/AcceptedDrawRegistry.h"

#include <iostream>
#include <stdexcept>
#include <thread>

using DWORD = uint32_t;
struct D3D11_TEXTURE2D_DESC
{
	uint32_t ArraySize = 1;
	struct
	{
		uint32_t Count = 1;
	} SampleDesc;
};
struct ID3D11Resource
{};
struct ID3D11Texture2D : ID3D11Resource
{
	D3D11_TEXTURE2D_DESC description;
	void GetDesc(D3D11_TEXTURE2D_DESC* output) { *output = description; }
};
struct ID3D11DepthStencilView
{
	ID3D11Resource* resource = nullptr;
	void GetResource(ID3D11Resource** output) { *output = resource; }
};
struct ID3D11DeviceContext
{
	ID3D11DepthStencilView* depth = nullptr;
	uint32_t inspections = 0;
	void OMGetRenderTargets(uint32_t, void*, ID3D11DepthStencilView** output)
	{
		++inspections;
		*output = depth;
	}
};
namespace Microsoft::WRL
{
	// Recording views have fixture lifetime; COM ownership is outside this test.
	template <class T>
	struct ComPtr
	{
		T* value = nullptr;
		T** GetAddressOf() { return &value; }
		T* Get() const { return value; }
		T* operator->() const { return value; }
		explicit operator bool() const { return value != nullptr; }
	};
}
namespace RE::RENDER_TARGETS_DEPTHSTENCIL
{
	constexpr uint32_t kMAIN = 0;
}
struct Renderer
{
	struct
	{
		struct
		{
			ID3D11Texture2D* texture = nullptr;
		} depthStencils[1];
	} data;
	auto& GetDepthStencilData() { return data; }
};
namespace globals::game
{
	Renderer* renderer = nullptr;
}
namespace CSX::Api
{
	void AdvanceAcceptedDrawFrame(ID3D11DeviceContext*) noexcept;
}
namespace
{
	using namespace CSXAcceptedDrawAPI;
	CSX::Api::AcceptedDrawRegistry registry;
	std::atomic_bool ready{ false };
	std::atomic<ID3D11DeviceContext*> immediate{ nullptr };
	std::atomic<DWORD> renderThread{ 0 };
	std::atomic_uint64_t filteredDraws{ 0 }, wrongThreadDraws{ 0 };
	thread_local CSX::Api::AcceptedDrawGeometryScope geometryScope;
	thread_local uint32_t suppressionDepth = 0;
	thread_local DWORD currentThread = 1;
	DWORD MockGetCurrentThreadId() { return currentThread; }
	uint32_t callbacks = 0;
	bool resetDuringCallback = false;
	void __cdecl Observe(const Draw* draw, void*)
	{
		++callbacks;
		if (resetDuringCallback)
			CSX::Api::AdvanceAcceptedDrawFrame(draw->context);
	}
	void NativeReplay(ID3D11DeviceContext*, const Arguments&) {}
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

// Substitute graphics and engine boundaries, retaining production filtering.
#define GetCurrentThreadId MockGetCurrentThreadId
namespace CSX::Api
{
#include "accepted_draw_publish_under_test.h"
}
#undef GetCurrentThreadId

int main()
try {
	ID3D11Texture2D sceneDepth, shadowDepth;
	ID3D11DepthStencilView view{ &shadowDepth };
	ID3D11DeviceContext context{ &view }, otherContext;
	Renderer renderer;
	renderer.data.depthStencils[0].texture = &sceneDepth;
	globals::game::renderer = &renderer;
	immediate = &context;
	const Arguments arguments{ Indexed, 3, 1, 0, 0, 0 };
	auto publish = [&] { CSX::Api::PublishAcceptedDraw(&context, arguments, &NativeReplay); };
	int pass = 0, geometry = 0;
	geometryScope.Begin(&pass);
	geometryScope.Activate(&pass, &geometry);
	uint64_t subscription = 0;
	Require(registry.Register(&Observe, nullptr, &subscription) == Success, "registration failed");
	publish();
	Require(!renderThread && !context.inspections && !callbacks, "unready service inspected or bound a draw");
	ready = true;

	std::jthread shadowWorker([&] {
		currentThread = 2;
		geometryScope.Begin(&pass);
		geometryScope.Activate(&pass, &geometry);
		publish();
		geometryScope.End(&pass);
	});
	shadowWorker.join();
	Require(!renderThread && !callbacks && filteredDraws == 1, "filtered worker shadow draw claimed the scene thread");

	view.resource = &sceneDepth;
	for (unsigned missing = 0; missing < 5; ++missing) {
		switch (missing) {
		case 0:
			globals::game::renderer = nullptr;
			break;
		case 1:
			renderer.data.depthStencils[0].texture = nullptr;
			break;
		case 2:
			context.depth = nullptr;
			break;
		case 3:
			sceneDepth.description.ArraySize = 2;
			break;
		case 4:
			sceneDepth.description.SampleDesc.Count = 2;
			break;
		}
		publish();
		Require(!renderThread && !callbacks, "unavailable or incompatible depth claimed the scene thread");
		globals::game::renderer = &renderer;
		renderer.data.depthStencils[0].texture = &sceneDepth;
		context.depth = &view;
		sceneDepth.description = {};
	}
	const auto inspectionsBeforeEarlyFilters = context.inspections;
	suppressionDepth = 1;
	publish();
	suppressionDepth = 0;
	geometryScope.Suspend();
	publish();
	geometryScope.Activate(&pass, &geometry);
	CSX::Api::PublishAcceptedDraw(&otherContext, arguments, &NativeReplay);
	auto empty = arguments;
	empty.indexCount = 0;
	CSX::Api::PublishAcceptedDraw(&context, empty, &NativeReplay);
	empty = arguments;
	empty.instanceCount = 0;
	CSX::Api::PublishAcceptedDraw(&context, empty, &NativeReplay);
	Require(!renderThread && context.inspections == inspectionsBeforeEarlyFilters && !callbacks, "early filters inspected or claimed a draw");

	publish();
	Require(renderThread == currentThread && callbacks == 1, "first accepted scene draw did not bind and deliver");
	const auto inspectionsBeforeWrongThread = context.inspections;
	std::jthread wrongWorker([&] {
		currentThread = 2;
		geometryScope.Begin(&pass);
		geometryScope.Activate(&pass, &geometry);
		publish();
	});
	wrongWorker.join();
	Require(wrongThreadDraws == 1 && callbacks == 1 && context.inspections == inspectionsBeforeWrongThread,
		"wrong-thread draw reached resource inspection or delivery");
	publish();
	Require(callbacks == 2, "accepted scene thread did not retain ownership");
	CSX::Api::AdvanceAcceptedDrawFrame(&otherContext);
	Require(renderThread == 1, "another context retired the scene thread");
	ready = false;
	CSX::Api::AdvanceAcceptedDrawFrame(&context);
	Require(renderThread == 1, "unready frame transition retired the scene thread");
	ready = true;
	CSX::Api::AdvanceAcceptedDrawFrame(&context);
	Require(!renderThread, "completed frame retained the loading/menu thread");
	view.resource = &shadowDepth;
	publish();
	Require(!renderThread && callbacks == 2, "shadow draw claimed the new frame");
	view.resource = &sceneDepth;
	resetDuringCallback = true;
	std::jthread gameplayWorker([&] {
		currentThread = 2;
		geometryScope.Begin(&pass);
		geometryScope.Activate(&pass, &geometry);
		publish();
		geometryScope.End(&pass);
	});
	gameplayWorker.join();
	Require(renderThread == 2 && callbacks == 3, "gameplay thread did not recover after frame transition, or callback retired its owner");
	resetDuringCallback = false;
	const auto inspectionsBeforeOldThread = context.inspections;
	publish();
	Require(wrongThreadDraws == 2 && callbacks == 3 && context.inspections == inspectionsBeforeOldThread,
		"old loading/menu thread stole ownership during gameplay frame");
	CSX::Api::AdvanceAcceptedDrawFrame(&context);
	publish();
	Require(renderThread == 1 && callbacks == 4, "second thread transition failed to recover");
	Require(registry.Unregister(subscription) == Success, "unregister failed");
	const auto inspectionsBeforeEmptyRegistry = context.inspections;
	publish();
	Require(callbacks == 4 && context.inspections == inspectionsBeforeEmptyRegistry, "empty registry inspected or delivered a draw");
	geometryScope.End(&pass);
	std::cout << "Accepted draw service: filtering, frame-boundary thread recovery, callback isolation and empty-registry fast path passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
