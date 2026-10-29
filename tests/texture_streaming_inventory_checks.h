#pragma once
#include "Features/TextureStreaming/ConsumerInventory.h"
#include "Features/TextureStreaming/Diagnostics.h"
#include "Features/TextureStreaming/RememberedSizes.h"
#include <cctype>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace StreamingTextures::LoadHooks
{
	inline std::string forgottenPreview;
	inline bool enabled = true;
	inline void Forget(const std::string& name) { forgottenPreview = name; }
	inline void Configure(bool active, const Modes&, const Options&, const Categories&, std::uint32_t) { enabled = active; }
}
namespace StreamingInventoryFixture
{
	using Microsoft::WRL::ComPtr;
	namespace Policy = TextureStreamingPolicy;
	using Diagnostics = StreamingTextures::Diagnostics;
	using Exclusion = Diagnostics::Exclusion;
	using Block = Diagnostics::Block;
	constexpr std::size_t maximumRecords = 16384, maximumConsumers = 256;
	namespace RE
	{
		template <class T>
		struct NiPointer
		{
			T* value = nullptr;
			NiPointer() = default;
			explicit NiPointer(T* p) : value(p) {}
			void reset(T* p) { value = p; }
			T* get() const { return value; }
		};
		namespace BSGraphics
		{
			struct Texture
			{
				ID3D11Resource* texture = nullptr;
				ID3D11ShaderResourceView* resourceView = nullptr;
				void* UAV = nullptr;
				std::uint32_t width = 0, height = 0, mips = 0;
			};
		}
		struct NiSourceTexture
		{
			struct Name
			{
				const char* text = nullptr;
				const char* c_str() const { return text; }
			} name;
			BSGraphics::Texture* rendererTexture = nullptr;
		};
		struct BSGeometry
		{
			NiSourceTexture::Name name;
		};
	}
#include "streaming_path_under_test.h"
	struct TextureStreaming
	{
		struct State
		{
			struct Consumer
			{
				RE::NiPointer<RE::BSGeometry> geometry;
			};
			struct Record
			{
				RE::NiPointer<RE::NiSourceTexture> source;
				RE::BSGraphics::Texture* engine = nullptr;
				ID3D11Resource* current = nullptr;
				ID3D11ShaderResourceView* currentView = nullptr;
				std::string path;
				D3D11_TEXTURE2D_DESC full{};
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				std::array<std::uint64_t, Policy::MaximumDrop + 1> bytes{};
				StreamingTextures::ConsumerInventory<Consumer> consumers;
				bool protectedConsumer = false, previewProtected = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
				bool terrainConsumer = false;
#endif
			};
			std::unordered_map<std::uint64_t, Record> records;
			std::unordered_set<std::uint64_t> reducedRecords;
			Diagnostics diagnostics;
#ifdef DEVBENCH_BRIDGE_ENABLED
			StreamingTextures::TextureDiagnostics textureDiagnostics, terrainTextureDiagnostics;
			Diagnostics terrainInventoryDiagnostics;
			StreamingTextures::DiagnosticSamples<Diagnostics::exclusionNames.size()> terrainExclusions;
			StreamingTextures::DiagnosticSamples<Diagnostics::blockNames.size()> terrainBlocks;
#endif
			StreamingTextures::Categories categories;
			StreamingTextures::Modes modes;
			StreamingTextures::Options options;
			std::uint32_t maximumDrop = 2;
			std::uint64_t epoch = 1, scan = 1, completeScan = 0;
			std::vector<int> frontier, previewFrontier, previewRoots;
			std::unordered_set<int> visited, previewVisited;
			std::string inventoryFailure;
			bool enabled = true;
			unsigned failures = 0;
			void Fail(const char*) { ++failures; }
			void ObserveTexture(RE::NiSourceTexture*, RE::BSGeometry*, bool, std::uint32_t);
			void ProtectPreviewTexture(RE::NiSourceTexture*);
			void SuspendInventory(const char*);
#include "streaming_defer_under_test.h"
		};
	};
#include "streaming_observation_under_test.h"
#include "streaming_preview_protection_under_test.h"
}

void CheckStreamingInventory(ID3D11Device* device)
{
	using namespace StreamingInventoryFixture;
	using namespace StreamingTextures;
	const D3D11_TEXTURE2D_DESC full{ 1024, 1024, 11, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = full.Format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = full.MipLevels;
	for (bool terrainFirst : { false, true }) {
		Upload original;
		Check(original.Begin(device, full, view, 0));
		Check(WriteOrigin(original.texture.Get(), { 71, false, 0 }));
		RE::BSGraphics::Texture engine{ original.texture.Get(), original.view.Get() };
		RE::NiSourceTexture source{ { "textures/landscape/rock.dds" }, &engine };
		RE::BSGeometry geometry;
		StreamingInventoryFixture::TextureStreaming::State state;
		state.categories.terrain = true;
		state.categories.landscapeStatics = false;
		state.ObserveTexture(&source, &geometry, true, terrainFirst ? Terrain : 0u);
		state.ObserveTexture(&source, &geometry, true, terrainFirst ? 0u : Terrain);
		Origin observed;
		Require(ReadOrigin(original.texture.Get(), observed) && observed.requiredCategories == (Terrain | LandscapeStatics) && !state.categories.Allows(observed.requiredCategories),
			"Shared terrain/static coverage depended on scan order");
		state.categories.landscapeStatics = true;
		state.ObserveTexture(&source, &geometry, true, 0);
		Require(state.records.size() == 1, "Both category toggles failed to admit a shared texture");
#ifdef DEVBENCH_BRIDGE_ENABLED
		Require(state.records.at(71).terrainConsumer && state.terrainInventoryDiagnostics.candidateRegistrations == 1,
			"Shared terrain/static diagnostic attribution depended on scan order");
#endif
		state.ProtectPreviewTexture(&source);
		Require(state.records.at(71).previewProtected && state.records.at(71).protectedConsumer &&
					ReadOrigin(original.texture.Get(), observed) && observed.protectedConsumer &&
					StreamingTextures::LoadHooks::forgottenPreview == NormalizeTexturePath(source.name.c_str()),
			"A shared inventory preview did not revoke streaming and remembered loads");
		state.records.at(71).previewProtected = false;
		state.reducedRecords.insert(71);
		state.completeScan = state.scan;
		state.records.at(71).consumers.committed.push_back({ RE::NiPointer<RE::BSGeometry>(&geometry) });
		state.records.at(71).consumers.scanning.push_back({ RE::NiPointer<RE::BSGeometry>(&geometry) });
		state.records.at(71).consumers.verified = state.scan;
		state.frontier = state.previewFrontier = state.previewRoots = { 1, 2 };
		state.visited = state.previewVisited = { 1, 2 };
		state.SuspendInventory("test overflow");
		Require(state.failures == 1 && !state.enabled && !StreamingTextures::LoadHooks::enabled && state.records.at(71).previewProtected && state.records.at(71).protectedConsumer,
			"Incomplete traversal left reduced consumers eligible to remain downscaled");
		Require(state.epoch == 2 && !state.completeScan && state.inventoryFailure == "test overflow" &&
					state.frontier.empty() && state.previewFrontier.empty() && state.previewRoots.empty() && state.visited.empty() && state.previewVisited.empty() &&
					state.records.at(71).consumers.scanning.empty() && state.records.at(71).consumers.committed.empty() &&
					!state.records.at(71).consumers.seen && !state.records.at(71).consumers.verified,
			"Rejected inventory retained partial coverage, queued traversal or pending reduction eligibility");
		state.SuspendInventory("another failure");
		Require(state.failures == 1 && state.epoch == 2 && state.inventoryFailure == "test overflow",
			"Repeated suspension replaced the original failure or repeatedly invalidated restoration work");
		source.name.text = "textures/armor/shared.dds";
		state.ObserveTexture(&source, &geometry, true, 0);
		Require(state.records.at(71).protectedConsumer && ReadOrigin(original.texture.Get(), observed) && observed.protectedConsumer,
			"A protected path alias did not invalidate an existing candidate");
	}
	for (const char* name : { static_cast<const char*>(nullptr), "" }) {
		Upload original;
		Check(original.Begin(device, full, view, 0));
		Check(WriteOrigin(original.texture.Get(), { 72, false, 0 }));
		RE::BSGraphics::Texture engine{ original.texture.Get(), original.view.Get() };
		RE::NiSourceTexture source{ { name }, &engine };
		RE::BSGeometry geometry;
		StreamingInventoryFixture::TextureStreaming::State state;
		state.ObserveTexture(&source, &geometry, true, 0);
		Origin observed;
		Require(state.records.empty() && ReadOrigin(original.texture.Get(), observed) && observed.protectedConsumer, "An unnamed texture was admitted or lost protection");
	}
	Upload original, other;
	Check(original.Begin(device, full, view, 0));
	Check(other.Begin(device, full, view, 0));
	Check(WriteOrigin(original.texture.Get(), { 73, false, 0 }));
	RE::BSGraphics::Texture engine{ original.texture.Get(), other.view.Get() };
	RE::NiSourceTexture source{ { "textures/architecture/wall.dds" }, &engine };
	RE::BSGeometry geometry;
	StreamingInventoryFixture::TextureStreaming::State state;
	state.ObserveTexture(&source, &geometry, true, 0);
	Require(state.records.empty(), "A view of a different texture entered replacement publication");

#ifdef DEVBENCH_BRIDGE_ENABLED
	{
		Upload untracked;
		Check(untracked.Begin(device, full, view, 0));
		RE::BSGraphics::Texture terrainEngine{ untracked.texture.Get(), untracked.view.Get() };
		RE::NiSourceTexture terrainSource{ { "textures/landscape/terrain.dds" }, &terrainEngine };
		RE::BSGeometry terrainGeometry{ { "TerrainPatch" } };
		StreamingInventoryFixture::TextureStreaming::State terrainState;
		terrainState.categories.terrain = true;
		terrainState.ObserveTexture(&terrainSource, &terrainGeometry, true, Terrain);
		const auto missing = static_cast<std::size_t>(Exclusion::MissingProvenance);
		Require(terrainState.records.empty() && terrainState.terrainInventoryDiagnostics.textureObservations == 1 &&
					terrainState.terrainInventoryDiagnostics.excluded[missing] == 1 &&
					terrainState.terrainExclusions.texturePaths[missing] == terrainSource.name.text &&
					terrainState.terrainExclusions.geometryNames[missing] == "TerrainPatch",
			"Untracked terrain was not distinguished from material rejection");
		terrainState.ObserveTexture(&terrainSource, &terrainGeometry, true, 0);
		Require(terrainState.terrainInventoryDiagnostics.textureObservations == 1, "Static observations polluted terrain diagnostics");
		Check(WriteOrigin(untracked.texture.Get(), { 74, false, 0 }));
		terrainState.categories.terrain = false;
		terrainState.ObserveTexture(&terrainSource, &terrainGeometry, true, Terrain);
		Require(terrainState.terrainInventoryDiagnostics.excluded[static_cast<std::size_t>(Exclusion::CategoryDisabled)] == 1,
			"Terrain consent rejection lost its diagnostic attribution");
		terrainState.categories.terrain = true;
		terrainState.ObserveTexture(&terrainSource, &terrainGeometry, true, Terrain);
		Require(terrainState.records.size() == 1 && terrainState.records.at(74).terrainConsumer &&
					terrainState.terrainTextureDiagnostics.reasons[static_cast<std::size_t>(TextureRejection::None)] == 1,
			"Accepted terrain did not expose its resource contract");
		auto& terrainRecord = terrainState.records.at(74);
		terrainRecord.consumers.committed.push_back({ RE::NiPointer<RE::BSGeometry>(&terrainGeometry) });
		terrainState.Defer(Block::InventoryIncomplete, terrainRecord);
		terrainState.Defer(Block::FullDetailDemand, terrainRecord);
		const auto fullDetail = static_cast<std::size_t>(Block::FullDetailDemand);
		Require(terrainState.terrainInventoryDiagnostics.blocked[fullDetail] == 1 &&
					terrainState.terrainBlocks.texturePaths[fullDetail] == "textures\\landscape\\terrain.dds" &&
					terrainState.terrainBlocks.geometryNames[fullDetail] == "TerrainPatch",
			"Terrain admission blocker lost its texture or limiting geometry identity");
		terrainRecord.terrainConsumer = false;
		terrainState.Defer(Block::FullDetailDemand, terrainRecord);
		Require(terrainState.terrainInventoryDiagnostics.blocked[fullDetail] == 1 && terrainState.diagnostics.blocked[fullDetail] == 2,
			"Static admission polluted terrain diagnostics");
		Check(WriteOrigin(untracked.texture.Get(), { 74, true, 0 }));
		terrainState.ObserveTexture(&terrainSource, &terrainGeometry, true, Terrain);
		Require(terrainState.records.at(74).protectedConsumer, "Terrain diagnostics weakened shared-resource protection");
	}
	{
		RE::BSGeometry terrainGeometry{ { "TerrainPatch" } };
		StreamingInventoryFixture::TextureStreaming::State terrainState;
		terrainState.categories.terrain = true;
		Check(WriteOrigin(original.texture.Get(), { 73, false, 0 }));
		terrainState.ObserveTexture(&source, &terrainGeometry, true, Terrain);
		const auto failedView = static_cast<std::size_t>(TextureRejection::ViewResource);
		Require(terrainState.records.empty() && terrainState.terrainTextureDiagnostics.reasons[failedView] == 1 &&
					terrainState.terrainInventoryDiagnostics.excluded[static_cast<std::size_t>(Exclusion::TextureContract)] == 1,
			"Terrain DirectX rejection did not retain its exact failed check");
	}
	{
		DiagnosticSamples<2> samples;
		samples.Observe(0, std::string(300, 't'), std::string(200, 'g'));
		samples.Observe(0, "replacement", "replacement");
		samples.Observe(2, "out-of-range", "out-of-range");
		Require(samples.texturePaths[0].size() == 256 && samples.geometryNames[0].size() == 128 && !samples.observed[1],
			"Diagnostic samples exceeded their bounds or replaced the first observation");
	}
#endif
}
