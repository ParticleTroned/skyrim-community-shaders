#pragma once

#include <atomic>
#include <optional>

#include <array>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

/** @brief Blends landscape textures across quad borders and borrows up to two neighbour layers. */
class TerrainSeams
{
public:
	/** Apply the requested state at a render-frame boundary; off releases all retained terrain data. */
	void Update(bool enabled);
	/** Whether this frame admits seam processing; loader threads read the same gate. */
	bool IsEnabled() const { return enabled.load(std::memory_order_acquire) && !failed.load(std::memory_order_acquire); }
	/** Restore the previous shader-resource slots after terrain geometry. Render thread only. */
	void Restore();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Snapshot retained quad/resource counts without GPU work. */
	json GetStatus();
#endif

	static TerrainSeams& GetInstance();

	static constexpr uint32_t GridSize = 17;
	static constexpr uint32_t GridVertices = GridSize * GridSize;
	static constexpr uint32_t PerimeterVertices = 64;
	static constexpr uint32_t EngineLayers = 6;
	static constexpr uint32_t MaxExtraLayers = 2;
	static constexpr uint32_t MaxLayers = EngineLayers + MaxExtraLayers;
	static constexpr uint32_t TexturesPerExtra = 3;
	static constexpr uint32_t FirstPSTexture = 104;
	static constexpr uint32_t NumPSTextures = 2 + MaxExtraLayers * TexturesPerExtra;

	static constexpr uint32_t BlendRadius = 3;

	struct QuadKey
	{
		uint32_t worldSpace = 0;
		int32_t x = 0;
		int32_t y = 0;

		bool operator==(const QuadKey&) const = default;
	};

	struct QuadKeyHash
	{
		size_t operator()(const QuadKey& a_key) const noexcept
		{
			uint64_t hash = a_key.worldSpace;
			hash = hash * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(a_key.x);
			hash = hash * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(a_key.y);
			return static_cast<size_t>(hash ^ (hash >> 32));
		}
	};

	using WeightGrid = std::array<std::array<uint8_t, GridVertices>, EngineLayers>;
	using Border = std::array<std::array<uint8_t, PerimeterVertices>, EngineLayers>;

	struct alignas(16) QuadData
	{
		float2 Origin;
		uint32_t Flags;
		uint32_t ExtraCount;
		float2 IsSnow;
		float2 SpecPower;
		float4 PBRParams[MaxExtraLayers];
		float4 GlintParams[MaxExtraLayers];
	};
	static_assert(sizeof(QuadData) == 96);

	struct Resources
	{
		winrt::com_ptr<ID3D11ShaderResourceView> weights;
		winrt::com_ptr<ID3D11ShaderResourceView> data;
		std::array<std::array<RE::NiSourceTexturePtr, TexturesPerExtra>, MaxExtraLayers> extras;
	};

	struct Quad
	{
		std::array<RE::TESLandTexture*, EngineLayers> slots{};
		Border border{};
		std::unique_ptr<WeightGrid> grid;
		RE::NiPointer<RE::BSGeometry> geometry;
		std::shared_ptr<Resources> resources;
		uint64_t generation = 0;
		bool pbr = false;
	};

	struct Healed
	{
		QuadKey key;
		uint64_t generation = 0;
		bool active = false;
		bool pbr = false;
		uint32_t extraCount = 0;
		std::array<RE::TESLandTexture*, MaxExtraLayers> extras{};
		std::array<std::array<uint8_t, GridVertices>, MaxLayers> weights{};
	};

	/** @brief True when this land quad geometry has blend resources and should draw with the seams permutation. */
	bool IsBlended(RE::BSGeometry* a_geometry);
	/** @brief Binds the quad's weights, quad data and borrowed textures to t104-t111. Render thread only, before the draw. */
	void Bind(RE::BSGeometry* a_geometry);
	/** @brief Reads a newly set up land record's quads and rebuilds them and their neighbours. */
	void TESObjectLAND_SetupMaterial(RE::TESObjectLAND* a_land);

private:
	struct Binding
	{
		Binding(ID3D11DeviceContext* context, const std::array<ID3D11ShaderResourceView*, NumPSTextures>& views);
		~Binding();
		Binding(const Binding&) = delete;
		Binding& operator=(const Binding&) = delete;
		winrt::com_ptr<ID3D11DeviceContext> context;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, NumPSTextures> previous;
	};
	void Clear();
	void Reset();
	void SeedLoadedTerrain();
	void CaptureLand(RE::TESObjectLAND* land, uint64_t expectedEpoch);
	std::atomic_bool enabled{ false };
	std::atomic_bool failed{ false };
	std::atomic_uint64_t epoch{ 0 };
	uint64_t nextGeneration = 0;
	bool seedPending = false;
	std::optional<Binding> binding;

	void Heal(const QuadKey& a_key, const Quad& a_quad, Healed& a_out) const;
	std::shared_ptr<Resources> CreateResources(const Healed& a_healed) const;
	void Rebuild(const std::vector<QuadKey>& a_keys, uint64_t expectedEpoch);

	std::shared_mutex mutex;
	std::unordered_map<QuadKey, Quad, QuadKeyHash> quads;
	std::unordered_map<RE::BSGeometry*, QuadKey> loadedQuads;
	std::vector<RE::NiPointer<RE::BSGeometry>> retiredGeometry;
	uint32_t currentWorldSpace = 0;
};
