#include "Features/LightLimitFix/PreparedPointLightColours.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>
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
	std::array<ID3D11Buffer*, 7> constants{};
	unsigned colourUnbinds = 0;
	std::array<std::byte, 1024 * 96> upload{};
	void CSSetShader(ID3D11ComputeShader* value, void*, unsigned)
	{
		shader = value;
		if (!value)
			++cleanups;
	}
	void CSSetConstantBuffers(unsigned start, unsigned count, ID3D11Buffer** buffers)
	{
		std::copy_n(buffers, count, constants.begin() + start);
	}
	void CSGetConstantBuffers(unsigned start, unsigned count, ID3D11Buffer** buffers)
	{
		std::copy_n(constants.begin() + start, count, buffers);
	}
	void PSSetShaderResources(unsigned start, unsigned count, ID3D11ShaderResourceView** views)
	{
		if (start == 41 && count == 1 && !*views)
			++colourUnbinds;
	}
	void CSSetShaderResources(unsigned, unsigned, ID3D11ShaderResourceView**) {}
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
	float2 screenSize{ 2048, 1024 };
	bool inWorld = true;
	Buffer feature;
	Buffer* featureDataCB = &feature;
};
namespace globals
{
	Context testContext;
	State testState;
	State* state = &testState;
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
	void BindSharedDataConstantBuffersForCS(Context* context)
	{
		ID3D11Buffer* buffers[]{ nullptr, globals::state->featureDataCB->CB() };
		context->CSSetConstantBuffers(5, 2, buffers);
	}
}
struct Shader
{
	ID3D11ComputeShader value;
	bool ready = true;
	unsigned requests = 0;
	ID3D11ComputeShader* Get(const wchar_t*, std::initializer_list<std::pair<const char*, const char*>>, const char*, const char*, const char*)
	{
		++requests;
		return ready ? &value : nullptr;
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
		unsigned LightCount, ContactShadowFlags, ContactShadowParams, PreparePointLightColours, ClusterSize[4];
	};
	std::array<Buffer, 9> buffers;
	Buffer* lightBuildingCB = &buffers[0];
	Buffer* lightCullingCB = &buffers[1];
	Buffer* clusters = &buffers[2];
	Buffer* lights = &buffers[3];
	Buffer* lightIndexList = &buffers[4];
	Buffer* lightGrid = &buffers[5];
	Buffer* contactShadowIndexCounter = &buffers[6];
	Buffer* contactShadowIndexList = &buffers[7];
	Buffer* contactShadowGrid = &buffers[8];
	Buffer colours;
	Buffer* preparedPointLightColours = &colours;
	LightLimitFixDetail::PreparedPointLightColours preparedColourState;
	Shader clusterBuildingCS{ { true } }, clusterCullingCS{ { false } };
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
		Require(context.builds == 2 && context.culls == 2 && context.maps == 2 && context.upload[0] == std::byte{ 42 }, "Nonempty frame omitted fresh light or bounds work");
		lighting.lightsData.clear();
		lighting.Upload();
		globals::testState.screenSize.x += 128;
		globals::game::nearValue = 2;
		globals::game::farValue = 20000;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && context.builds == 2 && context.culls == 2 && context.maps == 2 && lighting.effectUploads == 5, "Empty transition skipped spotlight work or dispatched");
		Require(lighting.clusterSize[0] == 17 && lighting.lightsNear == 2 && lighting.lightsFar == 20000, "Empty frame did not refresh camera and rounded dimensions");
		lighting.lightsData.resize(1);
		lighting.Upload();
		Require(context.builds == 3 && context.culls == 3, "Nonempty recovery omitted a fresh build or cull");
		lighting.clusterCullingCS.ready = false;
		lighting.Upload();
		Require(!lighting.clusteredLightsAvailable && context.builds == 3 && context.culls == 3 && !context.shader, "Missing shader retained availability or bindings");
		lighting.clusterCullingCS.ready = true;
		lighting.Upload();
		Require(lighting.clusteredLightsAvailable && context.builds == 4 && context.culls == 4 && !context.shader, "Shader recovery failed");
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			const auto width = globals::testState.screenSize.x;
			const auto builds = context.builds, culls = context.culls;
			globals::testState.screenSize.x = 0;
			lighting.Upload();
			Require(!lighting.clusteredLightsAvailable && context.builds == builds && context.culls == culls, "Zero-sized dispatch published clustered availability");
			globals::testState.screenSize.x = width;
			lighting.Upload();
			Require(lighting.clusteredLightsAvailable && context.builds == builds + 1 && context.culls == culls + 1, "Valid-size recovery omitted a fresh build");
			Require(lighting.clusterSize[0] == (vr ? 17u : 34u), "Runtime eye width was not respected");
		}
		const auto builds = context.builds;
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
		Require(context.builds == builds + 1 && lighting.clusteredLightsAvailable, "Upload recovery failed");
		LightLimitFixDetail::PointLightColourInputs inputs;
		inputs.transformEnabled = true;
		lighting.preparedColourState.PublishInputs(inputs);
		ID3D11Buffer previousShared, previousFeature;
		context.constants[5] = &previousShared;
		context.constants[6] = &previousFeature;
		lighting.Upload();
		Require(lighting.preparedColourState.IsReady() && context.colourUnbinds != 0, "Nonempty frame did not prepare colours");
		Require(context.constants[5] == &previousShared && context.constants[6] == &previousFeature, "Colour preparation did not restore shared constants");
		lighting.lightsData.clear();
		lighting.Upload();
		Require(!lighting.preparedColourState.IsReady(), "Empty frame retained stale prepared colours");
		lighting.lightsData.resize(1);
		lighting.Upload();
		Require(lighting.preparedColourState.IsReady(), "Lit recovery omitted colour preparation");
		lighting.lightCullingCB->failUpdate = true;
		failed = false;
		try {
			lighting.Upload();
		} catch (const std::runtime_error&) {
			failed = true;
		}
		Require(failed && !lighting.preparedColourState.IsReady() && !lighting.clusteredLightsAvailable && !context.shader, "Failed cull upload retained readiness or bindings");
		Require(context.constants[5] == &previousShared && context.constants[6] == &previousFeature, "Failed cull upload lost shared constants");
		lighting.lightCullingCB->failUpdate = false;
		lighting.Upload();
		Require(lighting.preparedColourState.IsReady(), "Cull upload recovery omitted colours");
		lighting.clusterCullingCS.ready = false;
		lighting.Upload();
		Require(!lighting.preparedColourState.IsReady(), "Missing cull shader retained prepared colours");
		Require(context.maps == context.unmaps, "Upload mapping was not balanced");
		std::cout << "Production standalone empty/upload/build/cull scheduling checks passed.\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
