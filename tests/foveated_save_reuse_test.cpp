#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace eastl
{
	using std::make_unique;
	using std::unique_ptr;
}

using DXGI_FORMAT = uint32_t;
constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN = 0;
constexpr DXGI_FORMAT DXGI_FORMAT_R16G16_FLOAT = 1;
constexpr DXGI_FORMAT DXGI_FORMAT_R16_FLOAT = 2;
constexpr uint32_t D3D11_RTV_DIMENSION_TEXTURE2D = 1;
constexpr uint32_t D3D11_SRV_DIMENSION_TEXTURE2D = 1;
constexpr uint32_t D3D11_UAV_DIMENSION_TEXTURE2D = 1;
constexpr uint32_t D3D11_SRV_DIMENSION_BUFFER = 2;
constexpr uint32_t D3D11_USAGE_DYNAMIC = 1;
constexpr uint32_t D3D11_CPU_ACCESS_WRITE = 1;
constexpr uint32_t D3D11_BIND_SHADER_RESOURCE = 1;
constexpr uint32_t D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 1;

struct D3D11_TEXTURE2D_DESC
{
	uint32_t Width = 0;
	uint32_t Height = 0;
	DXGI_FORMAT Format = 0;
};

struct D3D11_RENDER_TARGET_VIEW_DESC
{
	DXGI_FORMAT Format = 0;
	uint32_t ViewDimension = 0;
	struct
	{
		uint32_t MipSlice = 0;
	} Texture2D;
};

using D3D11_UNORDERED_ACCESS_VIEW_DESC = D3D11_RENDER_TARGET_VIEW_DESC;

struct D3D11_SHADER_RESOURCE_VIEW_DESC
{
	DXGI_FORMAT Format = 0;
	uint32_t ViewDimension = 0;
	struct
	{
		uint32_t MostDetailedMip = 0;
		uint32_t MipLevels = 0;
	} Texture2D;
	struct
	{
		uint32_t FirstElement = 0;
		uint32_t NumElements = 0;
	} Buffer;
};

struct D3D11_BUFFER_DESC
{
	uint32_t Usage = 0;
	uint32_t CPUAccessFlags = 0;
	uint32_t BindFlags = 0;
	uint32_t MiscFlags = 0;
	uint32_t StructureByteStride = 0;
	uint32_t ByteWidth = 0;
};

struct ID3D11Resource
{
	D3D11_TEXTURE2D_DESC desc;
};

namespace
{
	uint32_t textureCreations = 0;
	uint32_t textureDestructions = 0;
	uint32_t viewCreations = 0;
	uint32_t bufferCreations = 0;
	uint32_t shaderRequests = 0;
	uint32_t packetInvalidations = 0;
	uint32_t historyDestructions = 0;

	void Require(bool condition)
	{
		if (!condition)
			std::abort();
	}

	auto Counters()
	{
		return std::array{ textureCreations, textureDestructions, viewCreations,
			bufferCreations, shaderRequests, packetInvalidations, historyDestructions };
	}
}

struct Texture2D
{
	D3D11_TEXTURE2D_DESC desc;
	std::unique_ptr<ID3D11Resource> resource;
	std::unique_ptr<int> srv;
	std::unique_ptr<int> uav;
	std::unique_ptr<int> rtv;

	~Texture2D() { ++textureDestructions; }
	void CreateSRV(const D3D11_SHADER_RESOURCE_VIEW_DESC&)
	{
		srv = std::make_unique<int>(1);
		++viewCreations;
	}
	void CreateUAV(const D3D11_UNORDERED_ACCESS_VIEW_DESC&)
	{
		uav = std::make_unique<int>(1);
		++viewCreations;
	}
	void CreateRTV(const D3D11_RENDER_TARGET_VIEW_DESC&)
	{
		rtv = std::make_unique<int>(1);
		++viewCreations;
	}
};

struct Buffer
{
	std::unique_ptr<int> srv;
	explicit Buffer(const D3D11_BUFFER_DESC&) { ++bufferCreations; }
	void CreateSRV(const D3D11_SHADER_RESOURCE_VIEW_DESC&)
	{
		srv = std::make_unique<int>(1);
		++viewCreations;
	}
};

struct Rect
{
	uint32_t minX = 0;
	uint32_t minY = 0;
	uint32_t maxX = 0;
	uint32_t maxY = 0;
	bool IsValid() const { return maxX > minX && maxY > minY; }
	uint32_t Width() const { return maxX - minX; }
	uint32_t Height() const { return maxY - minY; }
};

struct RegionPlan
{
	uint32_t outputWidthPerEye = 100;
	uint32_t outputHeight = 80;
	struct Eye
	{
		Rect peripheryTAAHistoryOutput;
	};
	std::array<Eye, 2> eyes{ Eye{ { 10, 10, 90, 70 } }, Eye{ { 12, 8, 88, 72 } } };
	bool IsValid() const { return outputWidthPerEye != 0 && outputHeight != 0; }
};

std::unique_ptr<Texture2D> MakeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
	++textureCreations;
	auto texture = std::make_unique<Texture2D>();
	texture->desc = { width, height, format };
	texture->resource = std::make_unique<ID3D11Resource>(ID3D11Resource{ texture->desc });
	texture->srv = std::make_unique<int>(1);
	texture->uav = std::make_unique<int>(1);
	texture->rtv = std::make_unique<int>(1);
	return texture;
}

bool TryGetTexture2DDesc(ID3D11Resource* source, D3D11_TEXTURE2D_DESC& desc)
{
	if (!source)
		return false;
	desc = source->desc;
	return true;
}

auto CreateNamedTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format, bool, bool, bool, const char*)
{
	return MakeTexture(width, height, format);
}

template <class... Args>
void LogWarnOnce(bool&, Args&&...)
{}
template <class... Args>
void LogWarnOnceFmt(bool&, Args&&...)
{}

class Upscaling
{
public:
	bool reuseOnly = true;
	bool ShouldReuseOrdinarySaveResources() const { return reuseOnly; }
	struct
	{
		RegionPlan plan;
	} foveatedRectCache;
	std::array<Rect, 2> peripheryTAAHistoryRects;
	using History = std::array<std::array<std::unique_ptr<Texture2D>, 2>, 2>;
	History peripheryTAAHistoryColor;
	History peripheryTAAVelocityHistory;
	History peripheryTAALockHistory;
	std::array<std::unique_ptr<Buffer>, 2> peripheryTAATileBuffer;
	std::array<uint32_t, 2> peripheryTAATileCapacity{};
	struct TileCache
	{
		bool uploaded = false;
	};
	std::array<TileCache, 2> peripheryTAATileCache;
	struct PeripheryTAATile
	{
		uint32_t x, y;
	};
	uint32_t peripheryTAAHistoryReadIndex = 1;
	bool peripheryTAAHistoryValid = true;
	uint32_t submitStageFoveatedPeripheryTAAFrame = 50;
	std::array<bool, 2> submitStageFoveatedPeripheryTAAEyeReady{ true, true };
	bool foveatedPeripheryCS = true;
	bool peripheryTAACS = true;
	bool foveatedCenterBlendCS = true;
	bool foveatedSpatialCompositeCS = true;
	bool foveatedPeripheryCB = true;
	bool peripheryTAACB = true;
	bool foveatedCenterBlendCB = true;
	bool foveatedSpatialCompositeCB = true;
	int shader = 1;

	int* GetFoveatedPeripheryCS()
	{
		++shaderRequests;
		return &shader;
	}
	int* GetPeripheryTAACS()
	{
		++shaderRequests;
		return &shader;
	}
	int* GetFoveatedCenterBlendCS()
	{
		++shaderRequests;
		return &shader;
	}
	int* GetFoveatedSpatialCompositeCS()
	{
		++shaderRequests;
		return &shader;
	}
	void InvalidateVRRenderScaleStereoPresentationPacket(bool) { ++packetInvalidations; }
	template <class... Args>
	void MarkSubmitStageDeviceLostIfNeeded(Args&&...)
	{}
	void MarkSubmitStageDeviceLostIfDeviceRemoved(const char*) {}
	void DestroyPeripheryTAAResources() { ++historyDestructions; }
	auto CreateTextureFromSource(ID3D11Resource* source, uint32_t width, uint32_t height, bool, bool, bool, const char*, bool)
	{
		return MakeTexture(width, height, source->desc.Format);
	}
	bool EnsureFoveatedTexture(eastl::unique_ptr<Texture2D>&, ID3D11Resource*, uint32_t, uint32_t, bool, bool, bool, bool, const char*);
	bool EnsureFoveatedDispatchShaders(bool, bool, const char*, const char*);
	bool EnsurePeripheryTAAResources(uint32_t, uint32_t, ID3D11Resource*);
	bool EnsurePeripheryTAATileBuffer(uint32_t, uint32_t);
};

#include "foveated_save_reuse_under_test.h"

namespace
{
	void TestTextures()
	{
		Upscaling upscaling;
		ID3D11Resource source{ { 100, 80, 3 } };
		for (uint32_t fault = 0; fault < 9; ++fault) {
			auto texture = MakeTexture(100, 80, 3);
			switch (fault) {
			case 1:
				texture->desc.Width = 99;
				break;
			case 2:
				texture->desc.Height = 79;
				break;
			case 3:
				texture->desc.Format = 4;
				break;
			case 4:
				texture->srv.reset();
				break;
			case 5:
				texture->uav.reset();
				break;
			case 6:
				texture->rtv.reset();
				break;
			case 7:
				texture->resource.reset();
				break;
			case 8:
				texture.reset();
				break;
			}
			const auto before = Counters();
			const auto* identity = texture.get();
			Require(upscaling.EnsureFoveatedTexture(texture, &source, 100, 80, false, true, true, true, "test") == (fault == 0));
			Require(Counters() == before && texture.get() == identity);
		}
		upscaling.reuseOnly = false;
		std::unique_ptr<Texture2D> texture;
		Require(upscaling.EnsureFoveatedTexture(texture, &source, 100, 80, false, true, true, true, "test"));
		Require(texture != nullptr);
	}

	void TestShaders()
	{
		for (bool taa : { false, true }) {
			for (bool visualize : { false, true }) {
				Upscaling upscaling;
				const auto before = Counters();
				Require(upscaling.EnsureFoveatedDispatchShaders(taa, visualize, "test", "fallback"));
				upscaling.foveatedPeripheryCS = false;
				upscaling.foveatedSpatialCompositeCS = false;
				Require(!upscaling.EnsureFoveatedDispatchShaders(taa, visualize, "test", "fallback"));
				Require(Counters() == before);
			}
		}
		Upscaling upscaling;
		upscaling.peripheryTAACB = false;
		Require(!upscaling.EnsureFoveatedDispatchShaders(true, false, "test", "fallback"));
		upscaling.reuseOnly = false;
		upscaling.peripheryTAACB = true;
		const auto before = shaderRequests;
		Require(upscaling.EnsureFoveatedDispatchShaders(true, false, "test", "fallback"));
		Require(shaderRequests > before);
	}

	void PopulateHistory(Upscaling& upscaling)
	{
		for (uint32_t eye = 0; eye < 2; ++eye) {
			const auto rect = upscaling.foveatedRectCache.plan.eyes[eye].peripheryTAAHistoryOutput;
			upscaling.peripheryTAAHistoryRects[eye] = rect;
			for (uint32_t slot = 0; slot < 2; ++slot) {
				upscaling.peripheryTAAHistoryColor[eye][slot] = MakeTexture(rect.Width(), rect.Height(), 3);
				upscaling.peripheryTAAVelocityHistory[eye][slot] = MakeTexture(rect.Width(), rect.Height(), DXGI_FORMAT_R16G16_FLOAT);
				upscaling.peripheryTAALockHistory[eye][slot] = MakeTexture(rect.Width(), rect.Height(), DXGI_FORMAT_R16_FLOAT);
			}
		}
	}

	void TestHistory()
	{
		ID3D11Resource source{ { 100, 80, 3 } };
		for (uint32_t fault = 0; fault < 10; ++fault) {
			Upscaling upscaling;
			PopulateHistory(upscaling);
			switch (fault) {
			case 1:
				upscaling.peripheryTAAHistoryColor[1][1]->desc.Format = 4;
				break;
			case 2:
				upscaling.peripheryTAAVelocityHistory[1][1]->desc.Width = 1;
				break;
			case 3:
				upscaling.peripheryTAALockHistory[1][1]->uav.reset();
				break;
			case 4:
				upscaling.peripheryTAALockHistory[1][1]->resource.reset();
				break;
			case 5:
				upscaling.peripheryTAAHistoryRects[1].minX += 1;
				break;
			case 6:
				upscaling.foveatedRectCache.plan.eyes[1].peripheryTAAHistoryOutput.maxX = 101;
				break;
			case 7:
				upscaling.foveatedRectCache.plan.eyes[1].peripheryTAAHistoryOutput = {};
				break;
			case 8:
				upscaling.peripheryTAALockHistory[1][1].reset();
				break;
			case 9:
				upscaling.peripheryTAAHistoryColor[0][0]->srv.reset();
				break;
			}
			const auto before = Counters();
			const auto* firstEye = upscaling.peripheryTAALockHistory[0][0].get();
			Require(upscaling.EnsurePeripheryTAAResources(100, 80, &source) == (fault == 0));
			Require(Counters() == before);
			Require(upscaling.peripheryTAALockHistory[0][0].get() == firstEye);
			Require(upscaling.peripheryTAAHistoryReadIndex == 1 && upscaling.peripheryTAAHistoryValid);
			Require(upscaling.submitStageFoveatedPeripheryTAAFrame == 50);
			Require(upscaling.submitStageFoveatedPeripheryTAAEyeReady[0] && upscaling.submitStageFoveatedPeripheryTAAEyeReady[1]);
		}
		Upscaling upscaling;
		upscaling.reuseOnly = false;
		Require(upscaling.EnsurePeripheryTAAResources(100, 80, &source));
		Require(upscaling.peripheryTAAHistoryReadIndex == 0 && !upscaling.peripheryTAAHistoryValid);
	}

	void TestTileBuffers()
	{
		Upscaling upscaling;
		auto before = Counters();
		Require(!upscaling.EnsurePeripheryTAATileBuffer(0, 16));
		Require(Counters() == before);
		upscaling.reuseOnly = false;
		Require(upscaling.EnsurePeripheryTAATileBuffer(0, 16));
		upscaling.reuseOnly = true;
		before = Counters();
		const auto* identity = upscaling.peripheryTAATileBuffer[0].get();
		Require(upscaling.EnsurePeripheryTAATileBuffer(0, 16));
		Require(upscaling.EnsurePeripheryTAATileBuffer(0, 8));
		Require(!upscaling.EnsurePeripheryTAATileBuffer(0, 17));
		Require(Counters() == before && upscaling.peripheryTAATileBuffer[0].get() == identity);
	}
}

int main()
{
	TestTextures();
	TestShaders();
	TestHistory();
	TestTileBuffers();
	std::cout << "Foveated save resource reuse passed\n";
}
