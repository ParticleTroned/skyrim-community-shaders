#include "TerrainSeams.h"

#include "Globals.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>

namespace
{
	constexpr uint32_t FlagPbr = 1u << 0;
	constexpr uint32_t FlagGlint = 1u << 8;
	constexpr uint32_t FlagValid = 1u << 31;

	constexpr int GridMax = static_cast<int>(TerrainSeams::GridSize) - 1;
	constexpr float QuadSize = 2048.0f;
	constexpr float MinDelta = 6.0f / 255.0f;
	constexpr float MinLayerWeight = 4.0f / 255.0f;
	constexpr size_t MaxFieldTextures = 56;

	constexpr auto DiffuseTexture = static_cast<RE::BSTextureSet::Texture>(0);
	constexpr auto NormalTexture = static_cast<RE::BSTextureSet::Texture>(1);
	constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);

	constexpr int PerimeterIndex(int a_x, int a_y)
	{
		if (a_y == 0)
			return a_x;
		if (a_y == GridMax)
			return 17 + a_x;
		if (a_x == 0)
			return 33 + a_y;
		return 48 + a_y;
	}

	constexpr int VertexIndex(int a_x, int a_y)
	{
		return a_y * static_cast<int>(TerrainSeams::GridSize) + a_x;
	}

	RE::BSGeometry* GetQuadGeometry(RE::TESObjectLAND* a_land, uint32_t a_quad)
	{
		auto* node = a_land->loadedData->mesh[a_quad];
		if (node == nullptr)
			return nullptr;
		const auto& children = node->GetChildren();
		return children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
	}

	void ReadQuad(RE::TESObjectLAND* a_land, uint32_t a_quad, TerrainSeams::Quad& a_out)
	{
		const auto& data = *a_land->loadedData;

		auto* base = data.defQuadTextures[a_quad];
		a_out.slots[0] = base != nullptr ? base : Util::GetDefaultLandTexture();
		for (uint32_t layer = 0; layer + 1 < TerrainSeams::EngineLayers; ++layer) {
			a_out.slots[layer + 1] = data.quadTextures[a_quad][layer];
		}

		a_out.grid = std::make_unique<TerrainSeams::WeightGrid>();
		auto& grid = *a_out.grid;
		for (uint32_t vertex = 0; vertex < TerrainSeams::GridVertices; ++vertex) {
			int total = 0;
			for (uint32_t layer = 0; layer < TerrainSeams::EngineLayers; ++layer) {
				total += static_cast<uint8_t>(data.percents[a_quad][vertex][layer]);
			}
			grid[0][vertex] = static_cast<uint8_t>(std::clamp(255 - total, 0, 255));
			for (uint32_t layer = 0; layer + 1 < TerrainSeams::EngineLayers; ++layer) {
				grid[layer + 1][vertex] = a_out.slots[layer + 1] != nullptr ? static_cast<uint8_t>(data.percents[a_quad][vertex][layer]) : uint8_t{ 0 };
			}
		}

		for (int y = 0; y <= GridMax; ++y) {
			for (int x = 0; x <= GridMax; ++x) {
				if (x != 0 && x != GridMax && y != 0 && y != GridMax)
					continue;
				for (uint32_t layer = 0; layer < TerrainSeams::EngineLayers; ++layer) {
					a_out.border[layer][PerimeterIndex(x, y)] = grid[layer][VertexIndex(x, y)];
				}
			}
		}
	}

	struct FieldTexture
	{
		RE::TESLandTexture* texture = nullptr;
		int slot = -1;
		std::array<float, TerrainSeams::PerimeterVertices> delta{};
	};

	struct FieldTextures
	{
		std::array<FieldTexture, MaxFieldTextures> entries;
		size_t count = 0;

		int FindOrAdd(RE::TESLandTexture* a_texture)
		{
			for (size_t i = 0; i < count; ++i) {
				if (entries[i].texture == a_texture)
					return static_cast<int>(i);
			}
			if (count == MaxFieldTextures)
				return -1;
			entries[count].texture = a_texture;
			return static_cast<int>(count++);
		}
	};
}

bool TerrainSeams::IsBlended(RE::BSGeometry* a_geometry)
{
	if (!IsEnabled())
		return false;
	const std::shared_lock lock(mutex);
	const auto loadedIt = loadedQuads.find(a_geometry);
	if (loadedIt == loadedQuads.end())
		return false;
	const auto quadIt = quads.find(loadedIt->second);
	return quadIt != quads.end() && quadIt->second.resources;
}

void TerrainSeams::Bind(RE::BSGeometry* a_geometry)
{
	Restore();
	if (!globals::d3d::context)
		return;
	std::shared_ptr<Resources> resources;
	if (IsEnabled()) {
		const std::shared_lock lock(mutex);
		if (const auto loadedIt = loadedQuads.find(a_geometry); loadedIt != loadedQuads.end()) {
			if (const auto quadIt = quads.find(loadedIt->second); quadIt != quads.end())
				resources = quadIt->second.resources;
		}
	}

	std::array<ID3D11ShaderResourceView*, NumPSTextures> views{};
	if (resources) {
		views[0] = resources->weights.get();
		views[1] = resources->data.get();
		for (uint32_t extra = 0; extra < MaxExtraLayers; ++extra) {
			for (uint32_t texture = 0; texture < TexturesPerExtra; ++texture) {
				const auto& source = resources->extras[extra][texture];
				if (source != nullptr && source->rendererTexture != nullptr)
					views[2 + extra * TexturesPerExtra + texture] = source->rendererTexture->resourceView;
			}
		}
	}

	binding.emplace(globals::d3d::context, views);
}

void TerrainSeams::Heal(const QuadKey& a_key, const Quad& a_quad, Healed& a_out) const
{
	a_out.active = false;
	a_out.extraCount = 0;

	const auto& grid = *a_quad.grid;

	FieldTextures textures;
	std::array<int, EngineLayers> slotTexture{};
	for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
		slotTexture[slot] = -1;
		if (a_quad.slots[slot] == nullptr)
			continue;
		const int index = textures.FindOrAdd(a_quad.slots[slot]);
		slotTexture[slot] = index;
		if (index >= 0 && textures.entries[index].slot < 0)
			textures.entries[index].slot = static_cast<int>(slot);
	}

	float maxDelta = 0.0f;
	for (int y = 0; y <= GridMax; ++y) {
		for (int x = 0; x <= GridMax; ++x) {
			if (x != 0 && x != GridMax && y != 0 && y != GridMax)
				continue;

			const int perimeter = PerimeterIndex(x, y);
			const int vertex = VertexIndex(x, y);

			std::array<float, MaxFieldTextures> own{};
			std::array<float, MaxFieldTextures> sum{};

			float ownTotal = 0.0f;
			for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
				if (slotTexture[slot] >= 0)
					ownTotal += grid[slot][vertex];
			}
			if (ownTotal <= 0.0f)
				continue;
			for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
				if (slotTexture[slot] >= 0)
					own[slotTexture[slot]] += grid[slot][vertex] / ownTotal;
			}
			sum = own;

			int sharing = 1;
			const int dxMin = x == 0 ? -1 : 0;
			const int dxMax = x == GridMax ? 1 : 0;
			const int dyMin = y == 0 ? -1 : 0;
			const int dyMax = y == GridMax ? 1 : 0;
			for (int dy = dyMin; dy <= dyMax; ++dy) {
				for (int dx = dxMin; dx <= dxMax; ++dx) {
					if (dx == 0 && dy == 0)
						continue;

					const auto neighbourIt = quads.find(QuadKey{ a_key.worldSpace, a_key.x + dx, a_key.y + dy });
					if (neighbourIt == quads.end())
						continue;
					const auto& neighbour = neighbourIt->second;

					const int neighbourX = dx < 0 ? GridMax : (dx > 0 ? 0 : x);
					const int neighbourY = dy < 0 ? GridMax : (dy > 0 ? 0 : y);
					const int neighbourPerimeter = PerimeterIndex(neighbourX, neighbourY);

					float neighbourTotal = 0.0f;
					for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
						if (neighbour.slots[slot] != nullptr)
							neighbourTotal += neighbour.border[slot][neighbourPerimeter];
					}
					if (neighbourTotal <= 0.0f)
						continue;

					bool complete = true;
					std::array<float, MaxFieldTextures> contribution{};
					for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
						const auto weight = neighbour.border[slot][neighbourPerimeter];
						if (neighbour.slots[slot] == nullptr || weight == 0)
							continue;
						const int index = textures.FindOrAdd(neighbour.slots[slot]);
						if (index < 0) {
							complete = false;
							break;
						}
						contribution[index] += weight / neighbourTotal;
					}
					if (!complete)
						continue;

					for (size_t i = 0; i < textures.count; ++i) {
						sum[i] += contribution[i];
					}
					++sharing;
				}
			}

			const float inverse = 1.0f / static_cast<float>(sharing);
			for (size_t i = 0; i < textures.count; ++i) {
				const float delta = sum[i] * inverse - own[i];
				textures.entries[i].delta[perimeter] = delta;
				maxDelta = std::max(maxDelta, std::abs(delta));
			}
		}
	}

	if (maxDelta < MinDelta)
		return;

	std::array<float, GridSize> falloff{};
	for (int distance = 0; distance <= GridMax; ++distance) {
		const float t = std::min(1.0f, static_cast<float>(distance) / static_cast<float>(BlendRadius));
		falloff[distance] = 1.0f - t * t * (3.0f - 2.0f * t);
	}

	std::vector<std::array<float, GridVertices>> fields(textures.count);
	std::array<float, MaxFieldTextures> fieldSum{};
	std::array<float, MaxFieldTextures> fieldMax{};

	for (size_t i = 0; i < textures.count; ++i) {
		const auto& delta = textures.entries[i].delta;
		const float corner00 = delta[PerimeterIndex(0, 0)];
		const float corner10 = delta[PerimeterIndex(GridMax, 0)];
		const float corner01 = delta[PerimeterIndex(0, GridMax)];
		const float corner11 = delta[PerimeterIndex(GridMax, GridMax)];

		auto& field = fields[i];
		for (int y = 0; y <= GridMax; ++y) {
			const float fy0 = falloff[y];
			const float fy1 = falloff[GridMax - y];
			const float west = delta[PerimeterIndex(0, y)];
			const float east = delta[PerimeterIndex(GridMax, y)];
			for (int x = 0; x <= GridMax; ++x) {
				const float fx0 = falloff[x];
				const float fx1 = falloff[GridMax - x];
				const int vertex = VertexIndex(x, y);

				float raw = 0.0f;
				float rawTotal = 0.0f;
				for (uint32_t slot = 0; slot < EngineLayers; ++slot) {
					if (slotTexture[slot] < 0)
						continue;
					rawTotal += grid[slot][vertex];
					if (slotTexture[slot] == static_cast<int>(i))
						raw += grid[slot][vertex];
				}
				raw = rawTotal > 0.0f ? raw / rawTotal : 0.0f;

				float value = fx0 * west + fx1 * east + fy0 * delta[PerimeterIndex(x, 0)] + fy1 * delta[PerimeterIndex(x, GridMax)];
				value -= fx0 * fy0 * corner00 + fx1 * fy0 * corner10 + fx0 * fy1 * corner01 + fx1 * fy1 * corner11;
				value = std::max(0.0f, raw + value);

				field[vertex] = value;
				fieldSum[i] += value;
				fieldMax[i] = std::max(fieldMax[i], value);
			}
		}
	}

	std::array<int, MaxFieldTextures> layerOf{};
	layerOf.fill(-1);

	std::array<int, MaxFieldTextures> candidates{};
	size_t candidateCount = 0;
	for (size_t i = 0; i < textures.count; ++i) {
		const auto& entry = textures.entries[i];
		if (entry.slot >= 0) {
			layerOf[i] = entry.slot;
		} else if (fieldMax[i] >= MinLayerWeight && entry.texture->textureSet != nullptr) {
			// The vanilla terrain shader would read a PBR base color as an sRGB diffuse.
			if (!a_out.pbr && globals::features::truePBR.IsPBRTextureSet(Util::GetSeasonalSwap(entry.texture->textureSet)))
				continue;
			candidates[candidateCount++] = static_cast<int>(i);
		}
	}
	std::sort(candidates.begin(), candidates.begin() + candidateCount, [&](int a_left, int a_right) { return fieldSum[a_left] > fieldSum[a_right]; });

	for (size_t i = 0; i < candidateCount && a_out.extraCount < MaxExtraLayers; ++i) {
		layerOf[candidates[i]] = static_cast<int>(EngineLayers + a_out.extraCount);
		a_out.extras[a_out.extraCount++] = textures.entries[candidates[i]].texture;
	}

	for (auto& layer : a_out.weights) {
		layer.fill(0);
	}

	for (uint32_t vertex = 0; vertex < GridVertices; ++vertex) {
		float total = 0.0f;
		for (size_t i = 0; i < textures.count; ++i) {
			if (layerOf[i] >= 0)
				total += fields[i][vertex];
		}
		if (total <= 0.0f) {
			a_out.weights[0][vertex] = 255;
			continue;
		}
		for (size_t i = 0; i < textures.count; ++i) {
			if (layerOf[i] >= 0)
				a_out.weights[layerOf[i]][vertex] = static_cast<uint8_t>(std::lround(fields[i][vertex] / total * 255.0f));
		}
	}

	a_out.active = true;
}

std::shared_ptr<TerrainSeams::Resources> TerrainSeams::CreateResources(const Healed& a_healed) const
{
	auto resources = std::make_shared<Resources>();
	const auto& defaults = globals::game::graphicsState->GetRuntimeData();
	auto& truePBR = globals::features::truePBR;

	QuadData data{};
	data.Origin = float2{ static_cast<float>(a_healed.key.x) * QuadSize, static_cast<float>(a_healed.key.y) * QuadSize };
	data.Flags = FlagValid;
	data.ExtraCount = a_healed.extraCount;

	for (uint32_t extra = 0; extra < MaxExtraLayers; ++extra) {
		data.PBRParams[extra] = float4{ 1.0f, 1.0f, 0.04f, 0.0f };
	}

	for (uint32_t extra = 0; extra < a_healed.extraCount; ++extra) {
		auto* landTexture = a_healed.extras[extra];
		auto* textureSet = Util::GetSeasonalSwap(landTexture->textureSet);
		if (textureSet == nullptr) {
			logger::warn("[Terrain Seams] Borrowed texture set is unavailable; retaining native terrain");
			return nullptr;
		}

		auto& layer = resources->extras[extra];
		textureSet->SetTexture(DiffuseTexture, layer[0]);
		textureSet->SetTexture(NormalTexture, layer[1]);

		if (a_healed.pbr) {
			if (const auto* pbrData = truePBR.GetPBRTextureSetData(textureSet)) {
				textureSet->SetTexture(RmaosTexture, layer[2]);

				data.Flags |= FlagPbr << extra;
				if (pbrData->glintParameters.enabled)
					data.Flags |= FlagGlint << extra;
				data.PBRParams[extra] = float4{ pbrData->roughnessScale, pbrData->displacementScale, pbrData->specularLevel, 0.0f };
				data.GlintParams[extra] = float4{
					pbrData->glintParameters.screenSpaceScale,
					40.0f - pbrData->glintParameters.logMicrofacetDensity,
					pbrData->glintParameters.microfacetRoughness,
					pbrData->glintParameters.densityRandomization
				};
				if (layer[2] == nullptr)
					layer[2] = defaults.defaultTextureWhite;
			}
		} else {
			static_assert(MaxExtraLayers == 2, "IsSnow and SpecPower hold one component per borrowed layer");
			(extra == 0 ? data.IsSnow.x : data.IsSnow.y) = landTexture->shaderTextureIndex != 0 ? 1.0f : 0.0f;
			(extra == 0 ? data.SpecPower.x : data.SpecPower.y) = static_cast<float>(static_cast<uint8_t>(landTexture->specularExponent));
		}

		if (layer[0] == nullptr)
			layer[0] = defaults.defaultTextureBlack;
		if (layer[1] == nullptr)
			layer[1] = defaults.defaultTextureNormalMap;
		const uint32_t requiredTextures = (data.Flags & (FlagPbr << extra)) ? TexturesPerExtra : 2u;
		for (uint32_t texture = 0; texture < requiredTextures; ++texture) {
			if (!layer[texture] || !layer[texture]->rendererTexture || !layer[texture]->rendererTexture->resourceView) {
				logger::warn("[Terrain Seams] Borrowed material view is unavailable; retaining native terrain");
				return nullptr;
			}
		}
	}

	constexpr uint32_t sliceCount = (MaxLayers + 3) / 4;
	constexpr uint32_t sliceBytes = GridVertices * 4;
	std::array<std::array<uint8_t, sliceBytes>, sliceCount> pixels{};
	for (uint32_t layer = 0; layer < MaxLayers; ++layer) {
		auto& slice = pixels[layer / 4];
		const uint32_t channel = layer % 4;
		for (uint32_t vertex = 0; vertex < GridVertices; ++vertex) {
			slice[vertex * 4 + channel] = a_healed.weights[layer][vertex];
		}
	}

	auto* device = globals::d3d::device;
	if (!device)
		return nullptr;

	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = GridSize;
	textureDesc.Height = GridSize;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = sliceCount;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc = { 1, 0 };
	textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	std::array<D3D11_SUBRESOURCE_DATA, sliceCount> textureData{};
	for (uint32_t slice = 0; slice < sliceCount; ++slice) {
		textureData[slice].pSysMem = pixels[slice].data();
		textureData[slice].SysMemPitch = GridSize * 4;
	}

	winrt::com_ptr<ID3D11Texture2D> texture;
	if (FAILED(device->CreateTexture2D(&textureDesc, textureData.data(), texture.put()))) {
		logger::warn("[Terrain Seams] Weight texture creation failed; retaining native terrain");
		return nullptr;
	}
	Util::SetResourceName(texture.get(), "TerrainSeams::Weights");

	D3D11_SHADER_RESOURCE_VIEW_DESC textureViewDesc{};
	textureViewDesc.Format = textureDesc.Format;
	textureViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	textureViewDesc.Texture2DArray.MipLevels = 1;
	textureViewDesc.Texture2DArray.ArraySize = sliceCount;
	if (FAILED(device->CreateShaderResourceView(texture.get(), &textureViewDesc, resources->weights.put()))) {
		logger::warn("[Terrain Seams] Failed to create weight view; retaining native terrain");
		return nullptr;
	}
	Util::SetResourceName(resources->weights.get(), "TerrainSeams::Weights SRV");

	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = sizeof(QuadData);
	bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(QuadData);

	D3D11_SUBRESOURCE_DATA bufferData{};
	bufferData.pSysMem = &data;

	winrt::com_ptr<ID3D11Buffer> buffer;
	if (FAILED(device->CreateBuffer(&bufferDesc, &bufferData, buffer.put()))) {
		logger::warn("[Terrain Seams] Failed to create quad buffer; retaining native terrain");
		return nullptr;
	}
	Util::SetResourceName(buffer.get(), "TerrainSeams::QuadData");

	D3D11_SHADER_RESOURCE_VIEW_DESC bufferViewDesc{};
	bufferViewDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferViewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	bufferViewDesc.Buffer.NumElements = 1;
	if (FAILED(device->CreateShaderResourceView(buffer.get(), &bufferViewDesc, resources->data.put()))) {
		logger::warn("[Terrain Seams] Failed to create quad view; retaining native terrain");
		return nullptr;
	}
	Util::SetResourceName(resources->data.get(), "TerrainSeams::QuadData SRV");

	return resources;
}

void TerrainSeams::Rebuild(const std::vector<QuadKey>& a_keys, uint64_t expectedEpoch)
{
	std::vector<Healed> pending;
	pending.reserve(a_keys.size());
	{
		const std::unique_lock lock(mutex);
		if (!IsEnabled() || epoch.load(std::memory_order_acquire) != expectedEpoch)
			return;
		for (const auto& key : a_keys) {
			const auto it = quads.find(key);
			if (it == quads.end() || !it->second.grid || !it->second.geometry)
				continue;

			auto& quad = it->second;
			auto& healed = pending.emplace_back();
			healed.key = key;
			healed.generation = quad.generation = ++nextGeneration;
			healed.pbr = quad.pbr;
		}
	}

	{
		const std::shared_lock lock(mutex);
		for (auto& healed : pending) {
			const auto it = quads.find(healed.key);
			if (it != quads.end() && it->second.grid && it->second.generation == healed.generation)
				Heal(healed.key, it->second, healed);
		}
	}

	std::vector<std::shared_ptr<Resources>> built(pending.size());
	for (size_t i = 0; i < pending.size(); ++i) {
		if (pending[i].active && IsEnabled() && epoch.load(std::memory_order_acquire) == expectedEpoch)
			built[i] = CreateResources(pending[i]);
	}

	{
		const std::unique_lock lock(mutex);
		for (size_t i = 0; i < pending.size(); ++i) {
			const auto it = quads.find(pending[i].key);
			if (IsEnabled() && epoch.load(std::memory_order_acquire) == expectedEpoch && it != quads.end() && it->second.generation == pending[i].generation)
				std::swap(it->second.resources, built[i]);
		}
	}
}

void TerrainSeams::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* a_land)
{
	const auto captureEpoch = epoch.load(std::memory_order_acquire);
	if (!IsEnabled())
		return;
	try {
		CaptureLand(a_land, captureEpoch);
	} catch (const std::exception& error) {
		const std::unique_lock lock(mutex);
		if (epoch.load(std::memory_order_acquire) == captureEpoch && enabled.load(std::memory_order_acquire) &&
			!failed.exchange(true, std::memory_order_acq_rel))
			logger::error("[Terrain Seams] Disabled after terrain capture failed: {}", error.what());
	}
}

void TerrainSeams::CaptureLand(RE::TESObjectLAND* a_land, uint64_t captureEpoch)
{
	if (!IsEnabled() || epoch.load(std::memory_order_acquire) != captureEpoch)
		return;
	if (a_land == nullptr || a_land->loadedData == nullptr || a_land->parentCell == nullptr)
		return;

	auto* cell = a_land->parentCell;
	if (!cell->IsExteriorCell())
		return;

	const auto* coordinates = cell->GetCoordinates();
	const auto* worldSpace = cell->GetRuntimeData().worldSpace;
	if (coordinates == nullptr || worldSpace == nullptr)
		return;

	struct LoadedQuad
	{
		QuadKey key;
		Quad quad;
	};
	std::vector<LoadedQuad> loadedNow;
	loadedNow.reserve(4);

	for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
		auto* geometry = GetQuadGeometry(a_land, quadIndex);
		if (geometry == nullptr)
			continue;

		auto& entry = loadedNow.emplace_back();
		entry.key = {
			worldSpace->GetFormID(),
			coordinates->cellX * 2 + static_cast<int32_t>(quadIndex & 1),
			coordinates->cellY * 2 + static_cast<int32_t>(quadIndex >> 1)
		};
		ReadQuad(a_land, quadIndex, entry.quad);
		entry.quad.geometry.reset(geometry);
		entry.quad.pbr = globals::features::truePBR.IsPBRLandscapeGeometry(geometry);
	}

	if (loadedNow.empty())
		return;

	std::vector<QuadKey> keys;
	keys.reserve(16);
	std::vector<std::shared_ptr<Resources>> staleResources;
	{
		const std::unique_lock lock(mutex);

		if (!IsEnabled() || epoch.load(std::memory_order_acquire) != captureEpoch)
			return;

		// Unloaded quads stay cached so new neighbours can still blend against their borders.
		// Keep border-only records only for the current worldspace.
		const uint32_t worldSpaceID = worldSpace->GetFormID();
		if (worldSpaceID != currentWorldSpace) {
			currentWorldSpace = worldSpaceID;
			std::erase_if(quads, [&](const auto& a_entry) { return a_entry.first.worldSpace != worldSpaceID && a_entry.second.geometry == nullptr; });
		}

		for (auto& entry : loadedNow) {
			auto& quad = quads[entry.key];
			if (quad.geometry != nullptr) {
				loadedQuads.erase(quad.geometry.get());
				retiredGeometry.push_back(std::move(quad.geometry));
			}
			loadedQuads[entry.quad.geometry.get()] = entry.key;

			// Invalidate in-flight rebuilds before publishing replacement source data.
			quad.generation = ++nextGeneration;
			quad.slots = entry.quad.slots;
			quad.border = entry.quad.border;
			quad.grid = std::move(entry.quad.grid);
			quad.geometry = std::move(entry.quad.geometry);
			quad.pbr = entry.quad.pbr;
			// Healed from the previous data, so they must not be bound until Rebuild replaces them.
			staleResources.push_back(std::move(quad.resources));

			keys.push_back(entry.key);
		}

		for (const auto& entry : loadedNow) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0)
						continue;
					const QuadKey neighbourKey{ entry.key.worldSpace, entry.key.x + dx, entry.key.y + dy };
					if (std::ranges::any_of(keys, [&](const QuadKey& key) { return key == neighbourKey; }))
						continue;
					const auto it = quads.find(neighbourKey);
					if (it != quads.end() && it->second.grid && it->second.geometry)
						keys.push_back(neighbourKey);
				}
			}
		}
	}

	Rebuild(keys, captureEpoch);
}

void TerrainSeams::Reset()
{
	std::vector<RE::NiPointer<RE::BSGeometry>> released;
	std::vector<std::shared_ptr<Resources>> releasedResources;
	{
		const std::unique_lock lock(mutex, std::try_to_lock);
		if (!lock.owns_lock())
			return;
		released.swap(retiredGeometry);

		for (auto it = loadedQuads.begin(); it != loadedQuads.end();) {
			const auto quadIt = quads.find(it->second);
			if (quadIt == quads.end()) {
				it = loadedQuads.erase(it);
				continue;
			}

			auto& quad = quadIt->second;
			if (quad.geometry == nullptr || quad.geometry.get() != it->first) {
				it = loadedQuads.erase(it);
				continue;
			}

			if (quad.geometry->GetRefCount() <= 1) {
				released.push_back(std::move(quad.geometry));
				releasedResources.push_back(std::move(quad.resources));
				quad.geometry = nullptr;
				quad.grid.reset();
				quad.generation = ++nextGeneration;
				it = loadedQuads.erase(it);
				continue;
			}

			++it;
		}
	}
}

TerrainSeams& TerrainSeams::GetInstance()
{
	static TerrainSeams instance;
	return instance;
}

TerrainSeams::Binding::Binding(ID3D11DeviceContext* a_context, const std::array<ID3D11ShaderResourceView*, NumPSTextures>& views)
{
	context.copy_from(a_context);
	std::array<ID3D11ShaderResourceView*, NumPSTextures> old{};
	context->PSGetShaderResources(FirstPSTexture, NumPSTextures, old.data());
	for (size_t i = 0; i < old.size(); ++i)
		previous[i].attach(old[i]);
	context->PSSetShaderResources(FirstPSTexture, NumPSTextures, views.data());
}

TerrainSeams::Binding::~Binding()
{
	std::array<ID3D11ShaderResourceView*, NumPSTextures> views{};
	for (size_t i = 0; i < views.size(); ++i)
		views[i] = previous[i].get();
	context->PSSetShaderResources(FirstPSTexture, NumPSTextures, views.data());
}

void TerrainSeams::Restore()
{
	binding.reset();
}

void TerrainSeams::Clear()
{
	decltype(quads) released;
	decltype(loadedQuads) loaded;
	decltype(retiredGeometry) retired;
	{
		const std::unique_lock lock(mutex);
		epoch.fetch_add(1, std::memory_order_acq_rel);
		released.swap(quads);
		retired.swap(retiredGeometry);
		loaded.swap(loadedQuads);
		currentWorldSpace = 0;
	}
}

void TerrainSeams::SeedLoadedTerrain()
{
	auto* tes = globals::game::tes;
	if (!tes || !tes->gridCells || !tes->gridCells->cells || !tes->gridCells->land3DAttached)
		return;
	auto* grid = tes->gridCells;
	for (uint32_t x = 0; x < grid->length; ++x) {
		for (uint32_t y = 0; y < grid->length; ++y) {
			if (auto* cell = grid->GetCell(x, y); cell && cell->IsAttached())
				TESObjectLAND_SetupMaterial(cell->GetRuntimeData().cellLand);
		}
	}
	seedPending = false;
}

void TerrainSeams::Update(bool requested)
{
	if (!requested && !enabled.load(std::memory_order_acquire) && !failed.load(std::memory_order_acquire))
		return;
	Restore();
	if (!requested)
		failed.store(false, std::memory_order_release);
	const bool target = requested && !failed.load(std::memory_order_acquire);
	const bool previous = enabled.exchange(target, std::memory_order_acq_rel);
	if (!target) {
		if (previous)
			Clear();
		seedPending = false;
		return;
	}
	if (!previous)
		seedPending = true;
	try {
		if (seedPending)
			SeedLoadedTerrain();
		Reset();
	} catch (const std::exception& error) {
		failed.store(true, std::memory_order_release);
		logger::error("[Terrain Seams] Disabled after frame maintenance failed: {}", error.what());
	}
	if (failed.load(std::memory_order_acquire)) {
		enabled.store(false, std::memory_order_release);
		Clear();
		seedPending = false;
	}
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json TerrainSeams::GetStatus()
{
	const std::shared_lock lock(mutex);
	size_t blended = 0;
	for (const auto& [key, quad] : quads)
		blended += quad.resources ? 1u : 0u;
	return { { "active", IsEnabled() }, { "failed", failed.load(std::memory_order_acquire) }, { "cachedQuads", quads.size() },
		{ "loadedQuads", loadedQuads.size() }, { "blendedQuads", blended },
		{ "maximumBorrowedLayers", MaxExtraLayers }, { "blendRadius", BlendRadius } };
}
#endif
