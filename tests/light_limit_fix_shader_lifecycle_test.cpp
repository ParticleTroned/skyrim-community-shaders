#include <spdlog/spdlog.h>
namespace logger = spdlog;
#include "Utils/AsyncShader.h"
#include "Utils/ComIdentity.h"

#include <array>
#include <d3dcompiler.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

using uint = unsigned;

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	struct Size
	{
		float x = 640, y = 480;
	};
	struct RecordingState
	{
		Size screenSize;
		bool toggleBlocked = false;
	};
	struct RecordingContext
	{
		std::vector<ID3D11UnorderedAccessView*> cleared;
		void ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* view, const UINT* values)
		{
			Require(values && values[0] == 0 && values[1] == 0 && values[2] == 0 && values[3] == 0,
				"Unavailable shaders must clear every light-grid channel");
			cleared.push_back(view);
		}
	};
	struct ConstantBuffer
	{
		static inline unsigned created = 0, destroyed = 0;
		explicit ConstantBuffer(int) { ++created; }
		~ConstantBuffer() { ++destroyed; }
	};
	template <class T>
	int ConstantBufferDesc()
	{
		return 0;
	}
	struct Buffer
	{
		struct View
		{
			ID3D11UnorderedAccessView* value;
			ID3D11UnorderedAccessView* get() const { return value; }
		} uav{ reinterpret_cast<ID3D11UnorderedAccessView*>(this) };
	};
}
namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
namespace globals
{
	RecordingState* state;
	namespace d3d
	{
		ID3D11Device* device;
		RecordingContext* context;
	}
}
namespace Util
{
	bool IsRuntimeToggleBlocked(const RecordingState* state) { return !state || state->toggleBlocked; }
}
struct Feature
{
	struct SettingsHeaderStatus
	{
		std::string_view label = "Ready";
		bool needsAttention = false;
	};
};
struct LightLimitFix : Feature
{
#include "llf_shader_paths_under_test.h"
	Util::AsyncShader<ID3D11ComputeShader> clusterBuildingCS, clusterCullingCS;
	Microsoft::WRL::ComPtr<IUnknown> shaderDeviceIdentity;
	struct LightBuildingCB
	{};
	struct LightCullingCB
	{};
	ConstantBuffer* lightBuildingCB = nullptr;
	ConstantBuffer* lightCullingCB = nullptr;
	ConstantBuffer* strictLightDataCB = nullptr;
	bool hasUploadedStrictLightData = false;
	Buffer* clusters = nullptr;
	Buffer* lights = nullptr;
	Buffer* lightIndexCounter = nullptr;
	Buffer* lightIndexList = nullptr;
	Buffer* lightGrid = nullptr;
	Buffer* contactShadowIndexCounter = nullptr;
	Buffer* contactShadowIndexList = nullptr;
	Buffer* contactShadowGrid = nullptr;
	uint clusterSize[3]{};
	unsigned readyDraws = 0;
	struct
	{
		bool EnableContactShadows = true;
	} settings;
	bool contactShadowsRequested = true;
	bool IsContactShadowsRequested() const { return contactShadowsRequested; }
	void ClearShaderCache();
	void PrepareShaderCache();
	void CompileComputeShaders();
	bool WaitShaderCacheReady();
	bool IsShaderCacheReady() const;
	bool IsPerformanceCostMeasurementReady() const;
	SettingsHeaderStatus GetSettingsHeaderStatus() const;
	void SetupResources();
	void SetupRenderTargetResources();
	void UpdateStructure();
	~LightLimitFix()
	{
		delete lightBuildingCB;
		delete lightCullingCB;
		delete strictLightDataCB;
	}
};
#include "llf_shader_device_under_test.h"
#include "llf_shader_draw_under_test.h"
#include "llf_shader_lifecycle_under_test.h"
#include "llf_shader_relatch_under_test.h"
#include "llf_shader_status_under_test.h"

namespace
{
	using Preparation = Util::ShaderPreparation;
	struct Request
	{
		std::wstring path;
		std::string resourceName;
		std::shared_ptr<Preparation> operation;
	};
	std::vector<Request> requests;
	std::function<void(const std::shared_ptr<Preparation>&, unsigned)> onRequest;
	unsigned currentChecks = 0;
	std::uint64_t compilationGeneration = 1, sourceGeneration = 1;
	Microsoft::WRL::ComPtr<ID3D11Device> devices[2];
	winrt::com_ptr<ID3DBlob> bytecode;
	void Complete(const std::shared_ptr<Preparation>& operation, Preparation::State state)
	{
		if (state == Preparation::State::Ready) {
			winrt::com_ptr<ID3D11ComputeShader> shader;
			Require(SUCCEEDED(operation->device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, shader.put())),
				"WARP fixture could not create a compute shader");
			operation->shader = shader.as<ID3D11DeviceChild>();
		}
		operation->Finish(state);
	}
	void FinishPair(std::size_t first, Preparation::State building, Preparation::State culling)
	{
		Complete(requests.at(first).operation, building);
		Complete(requests.at(first + 1).operation, culling);
	}
	struct Fixture
	{
		RecordingState state;
		RecordingContext context;
		std::array<Buffer, 8> buffers;
		LightLimitFix feature;
		Fixture()
		{
			requests.clear();
			onRequest = {};
			currentChecks = 0;
			++compilationGeneration;
			++sourceGeneration;
			globals::state = &state;
			globals::d3d::device = devices[0].Get();
			globals::d3d::context = &context;
			REL::Module::vr = false;
			feature.clusters = &buffers[0];
			feature.lights = &buffers[1];
			feature.lightIndexCounter = &buffers[2];
			feature.lightIndexList = &buffers[3];
			feature.lightGrid = &buffers[4];
			feature.contactShadowIndexCounter = &buffers[5];
			feature.contactShadowIndexList = &buffers[6];
			feature.contactShadowGrid = &buffers[7];
			feature.SetupResources();
			Require(requests.size() == 2 && !feature.IsShaderCacheReady(), "Setup must enqueue the exact shader pair without adopting it");
			Require(requests[0].path == L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl" &&
						requests[1].path == L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl" &&
						requests[0].resourceName == "LightLimitFix::ClusterBuildingCS" &&
						requests[1].resourceName == "LightLimitFix::ClusterCullingCS",
				"LLF requests must preserve their exact source and resource identity");
		}
	};
	void ReadinessAndDraws()
	{
		Fixture f;
		Require(f.feature.GetSettingsHeaderStatus().label == "Pending" && f.feature.GetSettingsHeaderStatus().needsAttention,
			"A queued pair must report pending readiness");
		Require(!f.feature.IsPerformanceCostMeasurementReady(), "Performance measurements must wait for the shader pair");
		FinishPair(0, Preparation::State::Ready, Preparation::State::Ready);
		const auto checksBeforeDraw = currentChecks;
		f.feature.UpdateStructure();
		Require(f.context.cleared == std::vector<ID3D11UnorderedAccessView*>({ f.buffers[4].uav.get(), f.buffers[7].uav.get() }) && f.feature.readyDraws == 0,
			"Draws must clear both grids until the explicit boundary adopts completed work");
		Require(currentChecks == checksBeforeDraw && requests.size() == 2, "Draws must not poll or request preparation");
		Require(f.feature.WaitShaderCacheReady() && f.feature.IsShaderCacheReady(), "The boundary must adopt both completed shaders");
		Require(f.feature.GetSettingsHeaderStatus().label == "Ready" && !f.feature.GetSettingsHeaderStatus().needsAttention,
			"An adopted pair must report ready");
		Require(f.feature.IsPerformanceCostMeasurementReady(), "An adopted pair must permit settled performance measurements");
		f.state.toggleBlocked = true;
		Require(!f.feature.IsPerformanceCostMeasurementReady(), "Existing runtime-toggle blocking must remain effective");
		f.state.toggleBlocked = false;
		f.feature.contactShadowsRequested = false;
		Require(!f.feature.IsPerformanceCostMeasurementReady(), "An unapplied runtime toggle must remain measurement-unready");
		const auto readyChecks = currentChecks;
		for (unsigned draw = 0; draw < 10000; ++draw)
			f.feature.UpdateStructure();
		Require(f.feature.readyDraws == 10000 && f.context.cleared.size() == 2 && currentChecks == readyChecks && requests.size() == 2,
			"Warmed draws must use resident shaders without polling, preparing, waiting or clearing grids");
		f.feature.clusters = nullptr;
		f.feature.UpdateStructure();
		Require(f.feature.readyDraws == 10000, "Incomplete resources must still reject draw dispatch");
	}
	void FailureStillWaitsForSibling()
	{
		Fixture f;
		Complete(requests[0].operation, Preparation::State::Failed);
		Complete(requests[1].operation, Preparation::State::Ready);
		Require(!f.feature.WaitShaderCacheReady() && f.feature.clusterBuildingCS.HasFailed() && f.feature.clusterCullingCS.IsReady(),
			"A building failure must not short-circuit culling adoption");
		Require(requests.size() == 2 && f.feature.GetSettingsHeaderStatus().label == "Failed", "A genuine failure must latch without retrying");
		f.feature.PrepareShaderCache();
		Require(requests.size() == 2 && !f.feature.WaitShaderCacheReady(), "Repeated preparation must retain the failure latch and sibling shader");
		f.feature.ClearShaderCache();
		Require(!f.feature.IsShaderCacheReady() && f.feature.GetSettingsHeaderStatus().label == "Pending", "Clear must reset both slots and readiness");
		f.feature.PrepareShaderCache();
		Require(requests.size() == 4, "Clear must permit a fresh pair after a real failure");
		FinishPair(2, Preparation::State::Ready, Preparation::State::Failed);
		Require(!f.feature.WaitShaderCacheReady() && f.feature.clusterBuildingCS.IsReady() && f.feature.clusterCullingCS.HasFailed(),
			"Culling failure must retain the independently adopted building shader");
	}
	void StaleRecoveryAndExhaustion()
	{
		{
			Fixture f;
			FinishPair(0, Preparation::State::Stale, Preparation::State::Ready);
			onRequest = [](const auto& operation, unsigned) { Complete(operation, Preparation::State::Ready); };
			Require(f.feature.WaitShaderCacheReady() && requests.size() == 4, "A stale member must reprepare both shaders at the explicit boundary");
			Require(f.feature.clusterBuildingCS.IsReady() && f.feature.clusterCullingCS.IsReady(), "Retry must publish one complete pair");
		}
		{
			Fixture f;
			FinishPair(0, Preparation::State::Stale, Preparation::State::Ready);
			onRequest = [](const auto& operation, unsigned index) { Complete(operation, index % 2 ? Preparation::State::Ready : Preparation::State::Stale); };
			Require(!f.feature.WaitShaderCacheReady() && requests.size() == 6, "Repeated invalidation must stop after exactly three pair attempts");
			Require(f.feature.clusterBuildingCS.HasFailed() && f.feature.clusterCullingCS.HasFailed() && f.feature.GetSettingsHeaderStatus().label == "Failed",
				"An exhausted boundary must failure-latch both slots rather than exposing empty readiness");
			f.feature.PrepareShaderCache();
			Require(requests.size() == 6, "Exhausted preparation must not retry on later requests");
		}
		{
			Fixture f;
			FinishPair(0, Preparation::State::Failed, Preparation::State::Ready);
			++compilationGeneration;
			onRequest = [](const auto& operation, unsigned) { Complete(operation, Preparation::State::Ready); };
			Require(f.feature.WaitShaderCacheReady() && requests.size() == 4, "A superseded failed result must recover as stale rather than latch failure");
		}
	}
	void RelatchesAndDevices()
	{
		Fixture f;
		const auto created = ConstantBuffer::created, destroyed = ConstantBuffer::destroyed;
		auto* buildingCB = f.feature.lightBuildingCB;
		f.feature.SetupRenderTargetResources();
		Require(requests.size() == 2 && f.feature.lightBuildingCB == buildingCB && ConstantBuffer::created == created && ConstantBuffer::destroyed == destroyed,
			"Same-device relatch must retain pending pair and constant buffers");
		FinishPair(0, Preparation::State::Ready, Preparation::State::Ready);
		Require(f.feature.WaitShaderCacheReady(), "Device test must first adopt its pair");
		auto* building = f.feature.clusterBuildingCS.get();
		auto* culling = f.feature.clusterCullingCS.get();
		++compilationGeneration;
		f.feature.SetupRenderTargetResources();
		Require(requests.size() == 2 && f.feature.clusterBuildingCS.get() == building && f.feature.clusterCullingCS.get() == culling,
			"Unrelated generation changes must not evict the completed same-device pair on relatch");
		Require(f.feature.WaitShaderCacheReady() && requests.size() == 2, "Resident readiness must ignore unrelated engine compilation epochs");
		++sourceGeneration;
		onRequest = [](const auto& operation, unsigned) { Complete(operation, Preparation::State::Ready); };
		f.feature.SetupRenderTargetResources();
		Require(f.feature.WaitShaderCacheReady() && requests.size() == 4, "Source invalidation must refresh both resident shaders at the relatch boundary");
		onRequest = {};
		globals::d3d::device = devices[1].Get();
		f.feature.SetupRenderTargetResources();
		Require(requests.size() == 6 && !f.feature.IsShaderCacheReady() && ConstantBuffer::created == created + 2 && ConstantBuffer::destroyed == destroyed + 2,
			"A changed canonical device must reset resources and enqueue a fresh pair");
		FinishPair(4, Preparation::State::Ready, Preparation::State::Ready);
		Require(f.feature.WaitShaderCacheReady(), "A replacement device must recover at the readiness boundary");
	}
}
std::shared_ptr<Util::ShaderPreparation> Util::RequestShaderPreparation(const wchar_t* path,
	const std::vector<std::pair<const char*, const char*>>& defines, const char* target, const char* entry, const char* name)
{
	Require(path && defines.empty() && std::string_view(target) == "cs_5_0" && std::string_view(entry) == "main" && name,
		"LLF must request the original fixed compute inputs");
	auto operation = std::make_shared<ShaderPreparation>();
	operation->device.copy_from(globals::d3d::device);
	operation->compilationGeneration = compilationGeneration;
	operation->sourceGeneration = sourceGeneration;
	const auto index = static_cast<unsigned>(requests.size());
	requests.push_back({ path, name, operation });
	if (onRequest)
		onRequest(operation, index);
	return operation;
}
bool Util::IsShaderPreparationCurrent(const ShaderPreparation& preparation, bool resident)
{
	++currentChecks;
	return (resident || preparation.compilationGeneration == compilationGeneration) && preparation.sourceGeneration == sourceGeneration &&
	       SameIdentity(preparation.device.get(), globals::d3d::device);
}
int main()
{
	try {
		for (auto& device : devices)
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
						device.GetAddressOf(), nullptr, nullptr)),
				"WARP fixture could not create a device");
		constexpr std::string_view source = "[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {}";
		Require(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, bytecode.put(), nullptr)),
			"WARP fixture could not compile its compute shader");
		ReadinessAndDraws();
		FailureStillWaitsForSibling();
		StaleRecoveryAndExhaustion();
		RelatchesAndDevices();
		requests.clear();
		Require(ConstantBuffer::created == ConstantBuffer::destroyed, "Every fixture constant buffer must release exactly once");
		std::cout << "LLF shader lifecycle: pair readiness, independent waits, bounded stale recovery, resident draws and device relatches passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
