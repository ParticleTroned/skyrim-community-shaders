#include "Features/LightLimitFix/ClusterBoundsCache.h"
#include "Features/LightLimitFix/PreparedPointLightColours.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

using uint = unsigned;
using UINT = unsigned;
struct float2
{
	float x, y;
};
struct ID3D11Buffer
{};
struct ID3D11ShaderResourceView
{};
struct ID3D11UnorderedAccessView
{
	unsigned clears = 0;
};
struct ID3D11ComputeShader
{
	bool building = false;
};
namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* value = nullptr;
		T* get() const { return value; }
		T** put() { return &value; }
		void copy_from(T* source) { value = source; }
	};
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		~scope_exit() { function(); }
	};
}
#define CS_GPU_PASS(name) ((void)0)
#define CS_PROFILE_CPU_SCOPE(name) ((void)0)
#define ARRAYSIZE(value) std::size(value)
constexpr unsigned D3D11_MAP_WRITE_DISCARD = 4;
struct D3D11_MAPPED_SUBRESOURCE
{
	void* pData;
};
namespace DX
{
	void ThrowIfFailed(int result)
	{
		if (result)
			throw std::runtime_error("Map failed");
	}
}

template <class T>
struct View
{
	T value{};
	T* get() { return &value; }
	explicit operator bool() const { return true; }
};
struct Buffer
{
	View<ID3D11Buffer> resource;
	View<ID3D11ShaderResourceView> srv;
	View<ID3D11UnorderedAccessView> uav;
	unsigned updates = 0;
	bool failUpdate = false;
	template <class T>
	void Update(const T&)
	{
		if (failUpdate)
			throw std::runtime_error("Constant upload failed");
		++updates;
	}
	ID3D11Buffer* CB() { return resource.get(); }
};
struct Context
{
	ID3D11ComputeShader* shader = nullptr;
	unsigned builds = 0, culls = 0, maps = 0, unmaps = 0, cleanups = 0;
	std::array<std::byte, 1024 * 96> upload{};
	void CSSetShader(ID3D11ComputeShader* value, void*, unsigned)
	{
		shader = value;
		if (!value)
			++cleanups;
	}
	void CSSetConstantBuffers(unsigned, unsigned, ID3D11Buffer**) {}
	void CSGetConstantBuffers(unsigned, unsigned, ID3D11Buffer** value) { *value = nullptr; }
	void CSSetShaderResources(unsigned, unsigned, ID3D11ShaderResourceView**) {}
	void PSSetShaderResources(unsigned, unsigned, ID3D11ShaderResourceView**) {}
	void CSSetUnorderedAccessViews(unsigned, unsigned, ID3D11UnorderedAccessView**, const unsigned*) {}
	void ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* view, const unsigned*) { ++view->clears; }
	void Dispatch(unsigned x, unsigned y, unsigned z)
	{
		if (x && y && z)
			shader->building ? ++builds : ++culls;
	}
	int Map(ID3D11Buffer*, unsigned, unsigned, unsigned, D3D11_MAPPED_SUBRESOURCE* mapped)
	{
		++maps;
		mapped->pData = upload.data();
		return 0;
	}
	void Unmap(ID3D11Buffer*, unsigned) { ++unmaps; }
};
struct State
{
	bool inWorld = true;
	Buffer* featureDataCB = nullptr;
	float2 screenSize{ 2048, 1024 };
};
namespace globals
{
	Context testContext;
	State testState;
	State* state = &testState;
	bool projectionProven = true;
	bool IsFrameBufferCacheCurrentForCS(Context*) { return projectionProven; }
	namespace d3d
	{
		Context* context = &testContext;
	}
	namespace game
	{
		bool isVR = true;
		float nearValue = 1, farValue = 10000;
		float* cameraNear = &nearValue;
		float* cameraFar = &farValue;
		struct FrameBuffer
		{
			std::array<std::array<float, 16>, 2> projection{};
			const auto& GetCameraProjInverse(unsigned eye) { return projection[eye]; }
		} frameBufferCached;
	}
	namespace features
	{
		struct ReverseZ
		{
			float far = 1;
			float GetFarDepth() { return far; }
		} reverseZ;
	}
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return globals::game::isVR; }
	};
}
namespace Util
{
	float2 ConvertToDynamic(float2 value) { return value; }
	void BindSharedDataConstantBuffersForCS(Context*) {}
}
struct Shader
{
	ID3D11ComputeShader value;
	bool ready = true;
	ID3D11ComputeShader* replacement = nullptr;
	unsigned requests = 0;
	ID3D11ComputeShader* Get(const wchar_t*, std::initializer_list<std::pair<const char*, const char*>>, const char*, const char*, const char*)
	{
		++requests;
		return ready ? (replacement ? replacement : &value) : nullptr;
	}
};
struct LightLimitFix
{
	static constexpr unsigned MAX_LIGHTS = 1024;
	struct LightData
	{
		std::array<std::byte, 96> bytes{};
	};
	struct LightBuildingCB
	{
		float LightsNear, LightsFar;
		unsigned ClusterSize[4];
	};
	struct LightCullingCB
	{
		unsigned LightCount, PreparePointLightColours, ContactShadowFlags, ContactShadowParams, ClusterSize[4];
	};
	std::array<Buffer, 10> buffers;
	Buffer* lightBuildingCB = &buffers[0];
	Buffer* lightCullingCB = &buffers[1];
	Buffer* clusters = &buffers[2];
	Buffer* lights = &buffers[3];
	Buffer* lightIndexList = &buffers[4];
	Buffer* lightGrid = &buffers[5];
	Buffer* contactShadowIndexCounter = &buffers[6];
	Buffer* contactShadowIndexList = &buffers[7];
	Buffer* contactShadowGrid = &buffers[8];
	Buffer* preparedPointLightColours = &buffers[9];
	Shader clusterBuildingCS{ { true } }, clusterCullingCS{ { false } };
	LightLimitFixDetail::ClusterBoundsCache clusterBoundsCache;
	winrt::com_ptr<ID3D11ComputeShader> clusterBoundsShader;
	LightLimitFixDetail::PreparedPointLightColours preparedColourState;
	unsigned lightCount = 0, clusterSize[4]{}, settings = 0, effectUploads = 0;
	float lightsNear = 1, lightsFar = 10000;
	bool clusteredLightsAvailable = false;
	std::vector<LightData> lightsData;
	unsigned PackContactShadowFlags(unsigned) { return 0; }
	unsigned PackContactShadowParams(unsigned) { return 0; }
	void UploadEffectSpotlights() { ++effectUploads; }
	void UpdateStructure();
	void Upload()
	{
		auto* context = globals::d3d::context;
#include "cluster_upload_under_test.h"
	}
};
#include "cluster_structure_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}
int main()
{
	try {
		LightLimitFix lighting;
		auto& context = globals::testContext;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && !context.maps && !context.builds && !context.culls && lighting.effectUploads == 1, "Empty first frame performed clustered work");
		Require(lighting.lightGrid->uav.value.clears == 1 && lighting.contactShadowGrid->uav.value.clears == 1 && lighting.contactShadowIndexCounter->uav.value.clears == 1, "Empty frame did not clear control buffers");
		Require(!lighting.clusterBuildingCS.requests && !lighting.clusterCullingCS.requests, "Empty frame requested clustering shaders");
		Require(!lighting.lightIndexList->uav.value.clears && !lighting.contactShadowIndexList->uav.value.clears, "Empty frame cleared unreachable flat lists");
		lighting.lightsData.resize(2);
		lighting.Upload();
		Require(lighting.clusteredLightsAvailable && context.builds == 1 && context.culls == 1 && context.maps == 1, "First nonempty frame omitted work");
		lighting.lightsData[0].bytes[0] = std::byte{ 42 };
		lighting.Upload();
		Require(context.builds == 1 && context.culls == 2 && context.maps == 2 && context.upload[0] == std::byte{ 42 }, "AABB hit cached moving lights or culling");
		lighting.lightsData.clear();
		lighting.Upload();
		globals::testState.screenSize.x += 128;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && context.builds == 1 && context.culls == 2 && context.maps == 2 && lighting.effectUploads == 5, "Empty transition skipped other work or dispatched");
		Require(lighting.clusterSize[0] == 17, "Empty frame did not refresh rounded dimensions");
		lighting.lightsData.resize(1);
		lighting.Upload();
		Require(context.builds == 2 && context.culls == 3, "Changed empty-frame dimensions published an unbuilt key");
		globals::testState.screenSize.x += 2;
		lighting.Upload();
		Require(context.builds == 3, "Crossing a rounded cluster boundary did not rebuild");
		globals::testState.screenSize.x += 2;
		lighting.Upload();
		Require(context.builds == 3 && context.culls == 5, "Unchanged rounded dimensions did not reuse bounds");
		globals::projectionProven = false;
		lighting.Upload();
		globals::projectionProven = true;
		lighting.Upload();
		Require(context.builds == 5 && context.culls == 7, "Unproven upload was reused or published");
		lighting.clusterBoundsCache.Invalidate();
		lighting.Upload();
		Require(context.builds == 6, "Same-sized recreation retained an old key");
		lighting.clusterCullingCS.ready = false;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && context.culls == 8 && !context.shader, "Missing shader retained availability or bindings");
		lighting.clusterCullingCS.ready = true;
		lighting.Upload();
		Require(lighting.clusteredLightsAvailable && context.culls == 9 && !context.shader, "Shader recovery failed");
		auto expectRebuild = [&] {
			const auto builds = context.builds, culls = context.culls;
			lighting.Upload();
			Require(context.builds == builds + 1 && context.culls == culls + 1, "Changed production build input reused old bounds");
			lighting.Upload();
			Require(context.builds == builds + 1 && context.culls == culls + 2, "Exact repeat failed to reuse while culling current lights");
		};
		globals::game::frameBufferCached.projection[1][3] = std::bit_cast<float>(1u);
		expectRebuild();
		globals::game::frameBufferCached.projection[0][3] = std::bit_cast<float>(1u);
		expectRebuild();
		globals::game::nearValue = 2;
		expectRebuild();
		globals::game::farValue = 20000;
		expectRebuild();
		globals::features::reverseZ.far = 0;
		expectRebuild();
		ID3D11ComputeShader replacement{ true };
		lighting.clusterBuildingCS.replacement = &replacement;
		expectRebuild();
		globals::game::isVR = false;
		expectRebuild();
		const auto width = globals::testState.screenSize.x;
		const auto builds = context.builds, culls = context.culls;
		globals::testState.screenSize.x = 0;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && context.builds == builds && context.culls == culls, "Zero-sized dispatch published clustered availability");
		globals::testState.screenSize.x = width;
		lighting.Upload();
		Require(lighting.clusteredLightsAvailable && context.builds == builds && context.culls == culls + 1, "Valid-size recovery lost reusable bounds");
		lighting.clusterBoundsCache.Invalidate();
		lighting.lightBuildingCB->failUpdate = true;
		bool failed = false;
		try {
			lighting.Upload();
		} catch (const std::runtime_error&) {
			failed = true;
		}
		Require(failed && !lighting.clusteredLightsAvailable && !context.shader && context.builds == builds, "Failed upload published a build or retained bindings");
		lighting.lightBuildingCB->failUpdate = false;
		lighting.Upload();
		Require(context.builds == builds + 1 && lighting.clusteredLightsAvailable, "Failed upload incorrectly published an AABB key");
		Require(context.maps == context.unmaps, "Upload mapping was not balanced");
		std::cout << "Production empty/upload/build/cull scheduling checks passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
