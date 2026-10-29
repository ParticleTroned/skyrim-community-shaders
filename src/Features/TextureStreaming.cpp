#include "TextureStreaming.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu/SettingsPage.h"
#include "State.h"
#include "TextureStreaming/ConsumerInventory.h"
#include "TextureStreaming/DemandDiagnostics.h"
#include "TextureStreaming/Diagnostics.h"
#include "TextureStreaming/GeometryDemand.h"
#include "TextureStreaming/LoadHooks.h"
#include "TextureStreaming/Preview.h"
#include "TextureStreaming/RestorationCache.h"
#include "TextureStreaming/RestorationPriority.h"
#include "TextureStreaming/Settings.h"
#include "TextureStreaming/TextureData.h"
#include "TruePBR/TextureEnumeration.h"
#include "Utils/Game.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/ResourceName.h"
#include "Utils/UI.h"
#include "Utils/VirtualFunctionHook.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/EngineStutterMonitor.h"
#	include "Diagnostics/AssetLoadHooks.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace RE
{
	BSShaderProperty::ForEachVisitor::~ForEachVisitor() = default;
}

namespace
{
	using Microsoft::WRL::ComPtr;
	using Memory = Util::GpuMemoryBudget;
	namespace Policy = TextureStreamingPolicy;
	using Diagnostics = StreamingTextures::Diagnostics;
	using Block = Diagnostics::Block;
	using Exclusion = Diagnostics::Exclusion;
	constexpr auto streamingConflictWarning = "SPS detected. Disable the SPS mod and restart to use CSX texture streaming.";
	constexpr std::size_t maximumRecords = 16384, maximumConsumers = 256, maximumFrontier = 16384;
	constexpr std::size_t maximumReducedTextures = 512;
	constexpr std::uint64_t uploadBytesPerFrame = 4 * Policy::MiB;

	/** Visits used child slots, rejecting implausible addresses and inventory overflow before retention. */
	template <class Children, class Visitor>
	bool VisitInventoryChildren(const Children& children, std::size_t available, Visitor&& visit)
	{
		const auto capacity = children.capacity();
		const auto slots = children.free_idx();
		const auto count = children.size();
		const auto* data = children.begin();
		if (slots > capacity || count > slots || count > available ||
			(capacity && !Util::IsLikelyValidPointer(data)))
			return false;
		std::size_t observed = 0;
		// NiTArray::end() includes unused capacity; free_idx() includes active slots and holes.
		for (std::uint32_t index = 0; index < slots; ++index) {
			auto* child = data[index].get();
			if (!child)
				continue;
			// Address plausibility does not establish lifetime against concurrent native mutation.
			if (!Util::IsLikelyValidPointer(child) || observed >= count)
				return false;
			++observed;
			visit(child);
		}
		return observed == count;
	}

	std::atomic_bool originHookReady{ false };
	std::atomic_uint64_t originSerial{ 0 };
	// DDS-loader ABI and runtime addresses reference dnai92/SPS; see README.md#texture-streaming-reference.
	using DDSLoader = std::int32_t (*)(void*, void*, RE::BSGraphics::Texture**, void*, std::uint64_t, std::uint64_t);
	DDSLoader originalDDSLoader = nullptr;

	std::int32_t LoadDDS(void* device, void* stream, RE::BSGraphics::Texture** output, void* header, std::uint64_t maximum, std::uint64_t flags)
	{
		auto remembered = StreamingTextures::LoadHooks::Prepare(static_cast<ID3D11Device*>(device), maximum);
		const auto limit = remembered ? (std::max(remembered->size.full.Width, remembered->size.full.Height) >> remembered->size.drop) : maximum;
		std::int32_t result;
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			CSX::Diagnostics::Stutters::AssetScope asset(CSX::Diagnostics::Stutters::AssetKind::Texture, {}, "Texture::NativeDDSLoad");
#endif
			result = originalDDSLoader(device, stream, output, header, limit, flags);
#ifdef DEVBENCH_BRIDGE_ENABLED
			asset.Result(result >= 0 && output && *output && (*output)->texture, result);
#endif
		}
		if (result >= 0 && output && *output && (*output)->texture) {
			const StreamingTextures::Origin origin{ ++originSerial, false };
			const auto status = StreamingTextures::WriteOrigin((*output)->texture, origin);
			if (FAILED(status))
				logger::warn("[TextureStreaming] DDS provenance unavailable: 0x{:08X}", static_cast<std::uint32_t>(status));
		}
		if (remembered)
			StreamingTextures::LoadHooks::Complete(std::move(*remembered), result >= 0 && output && *output ? (*output)->texture : nullptr, result >= 0);
		return result;
	}

	std::optional<std::string> TexturePath(RE::NiSourceTexture* texture, std::uint32_t& categories, std::uint32_t materialCategories)
	{
		if (!texture || !texture->name.c_str() || !*texture->name.c_str())
			return std::nullopt;
		std::string path = StreamingTextures::NormalizeTexturePath(texture->name.c_str());
		const auto required = StreamingTextures::PathCategories(path, materialCategories);
		if (!required)
			return std::nullopt;
		categories = *required;
		return path;
	}

	nlohmann::json DiagnosticStatus(const Diagnostics& value, const Diagnostics& initial = {})
	{
		nlohmann::json excluded = nlohmann::json::object(), blocked = nlohmann::json::object();
		bool hasBlockedChecks = false;
		for (std::size_t i = 0; i < value.excluded.size(); ++i)
			excluded[std::string(Diagnostics::exclusionNames[i])] = value.excluded[i] - initial.excluded[i];
		for (std::size_t i = 0; i < value.blocked.size(); ++i) {
			const auto count = value.blocked[i] - initial.blocked[i];
			blocked[std::string(Diagnostics::blockNames[i])] = count;
			hasBlockedChecks |= count != 0;
		}
		return { { "nodesVisited", value.nodesVisited - initial.nodesVisited },
			{ "geometryObservations", value.geometryObservations - initial.geometryObservations },
			{ "staticGeometryObservations", value.staticGeometryObservations - initial.staticGeometryObservations },
			{ "supportedMaterialObservations", value.supportedMaterialObservations - initial.supportedMaterialObservations },
			{ "measurableGeometryObservations", value.measurableGeometryObservations - initial.measurableGeometryObservations },
			{ "textureObservations", value.textureObservations - initial.textureObservations },
			{ "candidateRegistrations", value.candidateRegistrations - initial.candidateRegistrations },
			{ "scansCompleted", value.scansCompleted - initial.scansCompleted },
			{ "demandChecks", value.demandChecks - initial.demandChecks },
			{ "shrinkCandidates", value.shrinkCandidates - initial.shrinkCandidates },
			{ "requiredRestoreCandidates", value.requiredRestoreCandidates - initial.requiredRestoreCandidates },
			{ "retiredReplacements", value.retiredReplacements - initial.retiredReplacements },
			{ "retiredReductions", value.retiredReductions - initial.retiredReductions },
			{ "retiredLogicalReductionBytes", value.retiredLogicalReductionBytes - initial.retiredLogicalReductionBytes },
			{ "retiredCancelledUploads", value.retiredCancelledUploads - initial.retiredCancelledUploads },
			{ "excludedObservations", std::move(excluded) }, { "blockedChecks", std::move(blocked) }, { "lastBlock", hasBlockedChecks ? value.LastBlock() : "none" } };
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	template <std::size_t Count>
	nlohmann::json DiagnosticSampleStatus(const StreamingTextures::DiagnosticSamples<Count>& value, const std::array<std::string_view, Count>& names)
	{
		auto result = nlohmann::json::object();
		for (std::size_t i = 0; i < Count; ++i)
			if (value.observed[i])
				result[std::string(names[i])] = { { "texturePath", value.texturePaths[i] }, { "geometryName", value.geometryNames[i] } };
		return result;
	}

	nlohmann::json MaterialDiagnosticStatus(const StreamingTextures::MaterialDiagnostics& value)
	{
		nlohmann::json reasons = nlohmann::json::object(), types = nlohmann::json::object();
		nlohmann::json flags = nlohmann::json::object(), samples = nlohmann::json::object();
		for (std::size_t i = 0; i < value.reasons.size(); ++i) {
			const auto name = std::string(value.names[i]);
			reasons[name] = value.reasons[i];
			if (value.reasons[i]) {
				const auto& sample = value.samples[i];
				samples[name] = { { "geometryName", value.geometryNames[i] }, { "geometryType", sample.geometryType },
					{ "shaderFlags", sample.flags }, { "disallowedFlags", sample.disallowedFlags },
					{ "materialType", sample.materialType == UINT32_MAX ? nlohmann::json(nullptr) : nlohmann::json(sample.materialType) },
					{ "materialFeature", sample.materialFeature == UINT32_MAX ? nlohmann::json(nullptr) : nlohmann::json(sample.materialFeature) } };
			}
		}
		for (std::size_t i = 0; i < value.geometryTypes.size(); ++i)
			if (value.geometryTypes[i])
				types[std::to_string(i)] = value.geometryTypes[i];
		for (std::size_t i = 0; i < value.disallowedFlagBits.size(); ++i)
			if (value.disallowedFlagBits[i])
				flags[std::to_string(i)] = value.disallowedFlagBits[i];
		return { { "reasons", std::move(reasons) }, { "geometryTypes", std::move(types) },
			{ "disallowedFlagBits", std::move(flags) }, { "samples", std::move(samples) } };
	}

	nlohmann::json DemandDiagnosticStatus(const StreamingTextures::DemandDiagnostics& value)
	{
		nlohmann::json reasons = nlohmann::json::object(), samples = nlohmann::json::object();
		for (std::size_t i = 0; i < value.reasons.size(); ++i) {
			const auto name = std::string(value.names[i]);
			reasons[name] = value.reasons[i];
			if (!value.reasons[i])
				continue;
			const auto& s = value.samples[i];
			nlohmann::json eyes = nlohmann::json::array();
			for (unsigned eye = 0; eye < std::min<unsigned>(s.context.eyeCount, static_cast<unsigned>(s.context.eyes.size())); ++eye) {
				const auto& e = s.context.eyes[eye];
				eyes.push_back({ { "position", s.context.positions[eye] }, { "renderWidth", e.renderWidth }, { "renderHeight", e.renderHeight },
					{ "outputWidth", e.outputWidth }, { "outputHeight", e.outputHeight }, { "projectionX", e.projectionX }, { "projectionY", e.projectionY } });
			}
			samples[name] = { { "texturePath", value.texturePaths[i] }, { "geometryName", value.geometryNames[i] },
				{ "requiredEdge", std::isfinite(s.requiredEdge) ? nlohmann::json(s.requiredEdge) : nlohmann::json(nullptr) },
				{ "fullWidth", s.fullWidth }, { "fullHeight", s.fullHeight }, { "mipLevels", s.mipLevels }, { "desiredDrop", s.desiredDrop },
				{ "consumers", s.consumers }, { "boundCenter", s.boundCenter }, { "boundRadius", s.boundRadius },
				{ "currentFrame", s.currentFrame }, { "sourceFrame", s.context.frame }, { "sampledAtMs", s.context.sampledAtMs },
				{ "mipBias", s.context.mipBias }, { "eyeCount", s.context.eyeCount }, { "eyes", std::move(eyes) } };
		}
		return { { "reasons", std::move(reasons) }, { "samples", std::move(samples) } };
	}

	nlohmann::json TextureDiagnosticStatus(const StreamingTextures::TextureDiagnostics& value)
	{
		nlohmann::json reasons = nlohmann::json::object(), samples = nlohmann::json::object();
		for (std::size_t i = 0; i < value.reasons.size(); ++i) {
			const auto name = std::string(value.names[i]);
			reasons[name] = value.reasons[i];
			if (!value.reasons[i])
				continue;
			const auto& s = value.samples[i];
			const auto& d = s.texture;
			const auto& v = s.view;
			samples[name] = { { "texturePath", value.texturePaths[i] },
				{ "unorderedAccess", s.unorderedAccess }, { "viewAvailable", s.viewAvailable },
				{ "engineWidth", s.engineWidth }, { "engineHeight", s.engineHeight }, { "engineMips", s.engineMips },
				{ "texture", s.descriptorAvailable ? nlohmann::json{
														 { "width", d.Width }, { "height", d.Height }, { "mipLevels", d.MipLevels }, { "arraySize", d.ArraySize },
														 { "format", d.Format }, { "sampleCount", d.SampleDesc.Count }, { "sampleQuality", d.SampleDesc.Quality },
														 { "usage", d.Usage }, { "bindFlags", d.BindFlags }, { "cpuAccessFlags", d.CPUAccessFlags }, { "miscFlags", d.MiscFlags } } :
													 nlohmann::json(nullptr) },
				{ "view", s.descriptorAvailable ? nlohmann::json{ { "format", v.Format }, { "dimension", v.ViewDimension }, { "firstMip", v.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D ? nlohmann::json(v.Texture2D.MostDetailedMip) : nlohmann::json(nullptr) }, { "mipLevels", v.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D ? nlohmann::json(v.Texture2D.MipLevels) : nlohmann::json(nullptr) } } : nlohmann::json(nullptr) } };
		}
		return { { "reasons", std::move(reasons) }, { "samples", std::move(samples) } };
	}

#endif

	enum class ReadKind
	{
		ValidateSource,
		Restore
	};
	struct ReadRequest
	{
		std::uint64_t serial = 0, epoch = 0;
		std::string path;
		D3D11_TEXTURE2D_DESC full{};
		std::uint32_t drop = 0;
		ReadKind kind = ReadKind::Restore;
		std::uint64_t cacheGeneration = 0;
	};
	struct ReadResult
	{
		ReadRequest request;
		StreamingTextures::TexturePixels image;
		std::uint64_t headerHash = 0, fileBytes = 0;
		std::string error;

#ifdef DEVBENCH_BRIDGE_ENABLED
		bool cacheHit = false;
		std::uint64_t sourceBytesRead = 0;
#endif
	};

	struct ReaderMemory
	{
		std::uint64_t availableCommit = 0;
		bool cacheHealthy = false;
	};
	ReaderMemory CheckReaderMemory(StreamingTextures::RestorationCache& cache)
	{
		MEMORYSTATUSEX memory{ sizeof(memory) };
		if (!GlobalMemoryStatusEx(&memory))
			memory = { sizeof(memory) };
		const bool healthy = memory.ullAvailPhys >= Policy::SystemMemoryReserveBytes &&
		                     memory.ullAvailPageFile >= Policy::SystemMemoryReserveBytes + 2 * Policy::MaximumPayloadBytes;
		if (!healthy)
			cache.Clear(true);
		return { memory.ullAvailPageFile, healthy };
	}

	ReadResult ReadDDS(ReadRequest request, StreamingTextures::RestorationCache& cache)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		CSX::Diagnostics::Stutters::AssetScope asset(CSX::Diagnostics::Stutters::AssetKind::Texture, request.path, "TextureStreaming::ReadDDS");
#endif
		ReadResult result;
		result.request = std::move(request);
		try {
			const auto memory = CheckReaderMemory(cache);
			if (result.request.kind == ReadKind::Restore && memory.cacheHealthy) {
				result.image = cache.Find(result.request.serial, result.request.path, result.request.full, result.request.cacheGeneration);
				if (result.image) {
#ifdef DEVBENCH_BRIDGE_ENABLED
					result.cacheHit = true;
					asset.CacheHit(true);
					asset.Result(true);
					asset.Bytes(0);
#endif
					return result;
				}
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (result.request.kind == ReadKind::Restore)
				asset.CacheHit(false);
#endif
			RE::BSResourceNiBinaryStream stream(result.request.path);
			if (!stream.good() || !stream.stream)
				throw std::runtime_error("DDS source is unavailable in the game resource namespace");
			const auto size = stream.stream->totalSize;
			if (size < 128 || size > Policy::MaximumPayloadBytes)
				throw std::runtime_error("DDS payload exceeds the bounded reader contract");
			if (result.request.kind == ReadKind::ValidateSource) {
				std::array<std::uint8_t, 148> header{};
				const auto count = std::min<std::size_t>(size, header.size());
				if (!stream.read(header.data(), static_cast<std::uint32_t>(count)))
					throw std::runtime_error("DDS source header read was incomplete");
#ifdef DEVBENCH_BRIDGE_ENABLED
				result.sourceBytesRead = count;
				asset.Bytes(count);
				asset.Result(true);
#endif
				result.headerHash = StreamingTextures::HeaderHash(std::span(header.data(), count));
				result.fileBytes = size;
				if (!StreamingTextures::MatchesDDSHeader(std::span(header.data(), count), size, result.request.full))
					throw std::runtime_error("DDS source header no longer matches the original texture contract");
				return result;
			}
			if (memory.availableCommit < Policy::SystemMemoryReserveBytes + 2 * size)
				throw std::runtime_error("Insufficient system commit headroom for DDS restoration");
			std::vector<std::uint8_t> bytes(size);
			{
#ifdef DEVBENCH_BRIDGE_ENABLED
				CSX::Diagnostics::Stutters::AssetScope read(CSX::Diagnostics::Stutters::AssetKind::Texture, {}, "TextureStreaming::ReadPayload");
#endif
				const bool success = stream.read(bytes.data(), size);
#ifdef DEVBENCH_BRIDGE_ENABLED
				read.Result(success);
				if (success)
					read.Bytes(size);
#endif
				if (!success)
					throw std::runtime_error("DDS source read was incomplete");
			}
#ifdef DEVBENCH_BRIDGE_ENABLED
			result.sourceBytesRead = size;
			asset.Bytes(size);
#endif
			if (!StreamingTextures::MatchesDDSHeader(bytes, size, result.request.full))
				throw std::runtime_error("DDS source no longer matches the original texture contract");
			auto image = std::make_shared<DirectX::ScratchImage>();
			HRESULT hr;
			{
#ifdef DEVBENCH_BRIDGE_ENABLED
				CSX::Diagnostics::Stutters::AssetScope decode(CSX::Diagnostics::Stutters::AssetKind::Texture, {}, "TextureStreaming::DecodeDDS");
				decode.Bytes(bytes.size());
#endif
				hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, *image);
#ifdef DEVBENCH_BRIDGE_ENABLED
				decode.Result(SUCCEEDED(hr), hr);
#endif
			}
			if (FAILED(hr) || !StreamingTextures::MatchesDDS(*image, result.request.full))
				throw std::runtime_error("DDS mip chain is invalid or incomplete");
			result.image = std::move(image);
#ifdef DEVBENCH_BRIDGE_ENABLED
			asset.Result(true);
#endif
			if (memory.cacheHealthy)
				cache.Store(result.request.serial, result.request.path, result.request.full, result.image, result.request.cacheGeneration);
		} catch (const std::exception& error) {
			result.image.reset();
			result.error = error.what();
#ifdef DEVBENCH_BRIDGE_ENABLED
			asset.Result(false);
#endif
		}
		return result;
	}

	/** Only file names and immutable metadata cross the worker boundary. */
	class Reader
	{
	public:
		~Reader()
		{
			worker.request_stop();
			wake.notify_all();
		}
		void Start(ReadRequest request)
		{
			request.cacheGeneration = cache.GetStatus().generation;
			std::scoped_lock lock(mutex);
			input = std::move(request);
			if (!worker.joinable()) {
				worker = std::jthread([this](std::stop_token stop) {
					while (!stop.stop_requested()) {
						std::unique_lock lock(mutex);
						if (cache.GetStatus().entries)
							wake.wait_for(lock, stop, std::chrono::seconds(1), [&] { return input.has_value(); });
						else
							wake.wait(lock, stop, [&] { return input.has_value(); });
						if (stop.stop_requested())
							return;
						if (!input) {
							lock.unlock();
							CheckReaderMemory(cache);
							continue;
						}
						auto request = std::move(*input);
						input.reset();
						lock.unlock();
						auto value = ReadDDS(std::move(request), cache);
						lock.lock();
						output = std::move(value);
					}
				});
			}
			wake.notify_one();
		}
		std::optional<ReadResult> Poll()
		{
			std::scoped_lock lock(mutex);
			return std::exchange(output, std::nullopt);
		}
		StreamingTextures::RestorationCache::Snapshot CacheStatus() const { return cache.GetStatus(); }
		void ClearCache(bool onlyIfPopulated = false) { cache.Clear(onlyIfPopulated); }

	private:
		StreamingTextures::RestorationCache cache;
		std::mutex mutex;
		std::condition_variable_any wake;
		std::optional<ReadRequest> input;
		std::optional<ReadResult> output;
		std::jthread worker;
	};

	template <class Get, class Set>
	bool VisitBindings(ID3D11DeviceContext* context, Get get, Set set,
		ID3D11ShaderResourceView* oldView, ID3D11ShaderResourceView* replacement)
	{
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
		(context->*get)(0, static_cast<UINT>(views.size()), views.data());
		const SKSE::stl::scope_exit release([&] { for (auto* view : views) if (view) view->Release(); });
		bool found = false;
		for (UINT i = 0; i < views.size(); ++i) {
			if (views[i] == oldView) {
				found = true;
				if (replacement)
					(context->*set)(i, 1, &replacement);
			}
		}
		return found;
	}

	template <class Shadow>
	void ReplaceCachedView(Shadow& shadow, ID3D11ShaderResourceView* oldView, ID3D11ShaderResourceView* newView)
	{
		for (std::size_t i = 0; i < std::size(shadow.PSTexture); ++i) {
			if (shadow.PSTexture[i] == oldView) {
				shadow.PSTexture[i] = newView;
				shadow.PSResourceModifiedBits |= 1u << i;
			}
		}
		for (std::size_t i = 0; i < std::size(shadow.CSTexture); ++i) {
			if (shadow.CSTexture[i] == oldView) {
				shadow.CSTexture[i] = newView;
				shadow.CSResourceModifiedBits |= 1u << i;
			}
		}
	}
}

struct TextureStreaming::State
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
		std::uint64_t stableSince = 0, retryAfter = 0, restorationWaitingSince = 0;
		double restorationUrgency = 0;
		bool previewProtected = false;
		std::uint32_t drop = 0, desired = 0;
		bool protectedConsumer = false, sourceValidated = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool terrainConsumer = false;
#endif
		std::uint64_t headerHash = 0, fileBytes = 0;
		std::uint32_t rememberedDrop = UINT32_MAX, distanceDrop = 0;
		std::shared_ptr<void> loadLifetime;
	};
	struct Node
	{
		RE::NiPointer<RE::NiAVObject> object;
		bool staticOwner = false;
		bool protectedHierarchy = false;
	};
	struct Transaction
	{
		ReadResult payload;
		StreamingTextures::Upload upload;
		Memory::Reservation reservation;
		ComPtr<ID3D11Resource> oldTexture;
		ComPtr<ID3D11ShaderResourceView> oldView;
		ComPtr<ID3D11Query> retirement;
		bool published = false, replacementCommitted = false;
		std::uint64_t logicalReductionBytes = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool terrainConsumer = false;
#endif
	};

	mutable std::mutex mutex;
	bool requestedEnabled = true, enabled = true, externalStreamingConflict = false;
	StreamingTextures::Modes modes;
	StreamingTextures::Options options;
#ifdef DEVBENCH_BRIDGE_ENABLED
	StreamingTextures::MaterialDiagnostics materialDiagnostics, terrainMaterialDiagnostics;
	StreamingTextures::TextureDiagnostics textureDiagnostics, terrainTextureDiagnostics;
	StreamingTextures::DemandDiagnostics demandDiagnostics, terrainDemandDiagnostics;
	Diagnostics terrainInventoryDiagnostics;
	StreamingTextures::DiagnosticSamples<Diagnostics::exclusionNames.size()> terrainExclusions;
	StreamingTextures::DiagnosticSamples<Diagnostics::blockNames.size()> terrainBlocks;
#endif
	std::uint32_t maximumDrop = 2;
	StreamingTextures::Categories categories;
	std::uint64_t epoch = 1, scan = 0, completeScan = 0, nextScanMs = 0, deviceGeneration = 0;
	std::uint32_t worldStart = 0, lastFrame = UINT32_MAX;
	bool paused = false, reading = false;
	std::unordered_map<std::uint64_t, Record> records;
	std::unordered_set<std::uint64_t> reducedRecords;
	std::uint64_t selectionCursor = 0, reducedCursor = 0, pruneCursor = 0;
	std::vector<Node> frontier;
	std::unordered_set<RE::NiAVObject*> visited;
	std::optional<Transaction> transaction;
	Reader reader;
	Policy::Pressure pressure;
	bool firstPressureReductionPending = false;
	Diagnostics diagnostics;
	StreamingTextures::PressureEpisode pressureEpisode;
	StreamingTextures::DemandContext demand;
	std::uint64_t shrinks = 0, restores = 0, failures = 0, admissionDeferrals = 0, cancelled = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::uint64_t gpuCopyShrinks = 0, cachedRestores = 0, ddsRestores = 0, sourceHeaderReads = 0, sourcePayloadReads = 0, sourceBytesRead = 0;
	std::uint64_t terrainShrinks = 0, terrainRestores = 0, terrainRetiredReductions = 0, terrainRetiredLogicalBytes = 0;
	struct TerrainReduction
	{
		std::array<char, 257> path{};
		std::uint64_t serial = 0;
		std::uint32_t requiredCategories = 0, fullWidth = 0, fullHeight = 0, width = 0, height = 0, previousDrop = 0, drop = 0;
	} terrainReduction;
#endif
	std::string detail = "Disabled";
	std::string inventoryFailure;
	Memory::Snapshot memory;

	void SuspendInventory(const char* reason);
	void ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, bool eligible, std::uint32_t requiredCategories);
	void Defer(Block reason, const Record& record)
	{
		diagnostics.Defer(reason);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (record.terrainConsumer) {
			terrainInventoryDiagnostics.Defer(reason);
			const auto* geometry = record.consumers.committed.empty() ? nullptr : record.consumers.committed.front().geometry.get();
			terrainBlocks.Observe(static_cast<std::size_t>(reason), record.path, geometry && geometry->name.c_str() ? geometry->name.c_str() : "");
		}
#else
		(void)record;
#endif
	}
	std::vector<RE::NiPointer<RE::NiAVObject>> previewRoots, previewFrontier;
	std::unordered_set<RE::NiAVObject*> previewVisited;
	std::uint64_t nextPreviewScanMs = 0;
	bool previewActive = false;
	void ScanPreviews();
	void ProtectPreviewTexture(RE::NiSourceTexture* texture);
	void Scan();
	void Prune();
	void AdoptLoads();
	std::uint32_t Demand(Record& record);
	std::uint32_t Target(const Record& record, std::uint32_t demanded) const
	{
		return ReductionRequested() ? demanded : std::min(demanded, Policy::CappedDrop(record.full.Width, record.full.Height, record.full.MipLevels, options.maximumSize));
	}
	bool ReductionRequested() const noexcept;
	void UpdateConfigurationDetail();
	void Select(bool previewOnly = false);
	void ServiceTransaction();
	void CancelTransaction();
	void Publish(Record& record, Transaction& work);
	void Tick();
	void UpdatePressureDiagnostics(std::uint64_t now);
	void ReportPressureEpisode(std::string_view reason, std::uint64_t now);
	void Fail(std::string reason)
	{
		++failures;
		detail = std::move(reason);
		logger::warn("[TextureStreaming] {}", detail);
	}
};

void TextureStreaming::State::AdoptLoads()
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(400);
	for (unsigned count = 0; count < 64 && std::chrono::steady_clock::now() < deadline; ++count) {
		auto load = StreamingTextures::LoadHooks::Poll();
		if (!load)
			break;
		auto* source = load->source;
		auto* engine = source ? source->rendererTexture : nullptr;
		StreamingTextures::Origin origin;
		if (!engine || !engine->resourceView || !StreamingTextures::ReadOrigin(engine->texture, origin) || origin.serial != load->serial)
			continue;
		if (records.contains(origin.serial)) {
			Fail("Reduced-load source was observed before ownership transfer");
			continue;
		}
		Record record;
#ifdef DEVBENCH_BRIDGE_ENABLED
		record.terrainConsumer = ((origin.requiredCategories | load->size.categories) & StreamingTextures::Terrain) != 0;
#endif
		record.source.reset(source);
		record.engine = engine;
		record.current = engine->texture;
		record.currentView = engine->resourceView;
		record.path = std::move(load->path);
		record.full = load->size.full;
		engine->resourceView->GetDesc(&record.view);
		record.view.Texture2D.MipLevels = record.full.MipLevels;
		record.drop = record.desired = load->size.drop;
		auto resident = record.full;
		resident.Width >>= record.drop;
		resident.Height >>= record.drop;
		resident.MipLevels -= record.drop;
		StreamingTextures::UpdateEngineMetadata(*engine, resident);
		record.headerHash = load->size.headerHash;
		record.fileBytes = load->size.fileBytes;
		record.sourceValidated = true;
		record.protectedConsumer = origin.protectedConsumer;
		if (record.protectedConsumer)
			StreamingTextures::LoadHooks::Forget(record.path);
		record.loadLifetime = std::move(load->lifetime);
		origin.requiredCategories |= load->size.categories;
		if (FAILED(StreamingTextures::WriteOrigin(record.current, origin)))
			record.protectedConsumer = true;
		for (std::uint32_t drop = 0; drop < record.bytes.size(); ++drop)
			record.bytes[drop] = StreamingTextures::LogicalBytes(record.full, drop);
		records.emplace(origin.serial, std::move(record));
		reducedRecords.insert(origin.serial);
	}
}

void TextureStreaming::State::ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, bool eligible, std::uint32_t requiredCategories)
{
	++diagnostics.textureObservations;
#ifdef DEVBENCH_BRIDGE_ENABLED
	const bool terrain = (requiredCategories & StreamingTextures::Terrain) != 0;
	if (terrain)
		++terrainInventoryDiagnostics.textureObservations;
#endif
	const auto exclude = [&](Exclusion reason) {
		diagnostics.Exclude(reason);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (terrain) {
			terrainInventoryDiagnostics.Exclude(reason);
			terrainExclusions.Observe(static_cast<std::size_t>(reason), source && source->name.c_str() ? source->name.c_str() : "",
				geometry && geometry->name.c_str() ? geometry->name.c_str() : "");
		}
#endif
	};
	if (!source || !source->rendererTexture || !source->rendererTexture->texture) {
		exclude(Exclusion::MissingTexture);
		return;
	}
	auto* engine = source->rendererTexture;
	StreamingTextures::Origin origin;
	if (!StreamingTextures::ReadOrigin(engine->texture, origin)) {
		exclude(Exclusion::MissingProvenance);
		return;
	}
	std::uint32_t pathCategories = 0;
	const auto path = TexturePath(source, pathCategories, requiredCategories);
	eligible &= path.has_value();
	requiredCategories |= pathCategories;
	if (!eligible && path)
		StreamingTextures::LoadHooks::Forget(*path);
	if ((!eligible && !origin.protectedConsumer) || (requiredCategories & ~origin.requiredCategories)) {
		origin.protectedConsumer |= !eligible;
		origin.requiredCategories |= requiredCategories;
		if (FAILED(StreamingTextures::WriteOrigin(engine->texture, origin))) {
			Fail("Could not preserve texture consumer requirements");
			enabled = false;
			StreamingTextures::LoadHooks::Configure(false, modes, options, categories, maximumDrop);
			return;
		}
	}
	auto it = records.find(origin.serial);
	if (it == records.end()) {
		if (origin.preloaded)
			return;
		if (origin.protectedConsumer || !eligible) {
			exclude(path ? Exclusion::ProtectedConsumer : Exclusion::ProtectedPath);
			return;
		}
		if (records.size() >= maximumRecords) {
			exclude(Exclusion::InventoryLimit);
			return;
		}
		if (!categories.Allows(origin.requiredCategories)) {
			exclude(Exclusion::CategoryDisabled);
			return;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		StreamingTextures::TextureInspection inspection;
		inspection.unorderedAccess = engine->UAV != nullptr;
		inspection.viewAvailable = engine->resourceView != nullptr;
		inspection.engineWidth = engine->width;
		inspection.engineHeight = engine->height;
		inspection.engineMips = engine->mips;
#endif
		const auto classify = [&]([[maybe_unused]] StreamingTextures::TextureRejection reason) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			inspection.reason = reason;
			textureDiagnostics.Observe(inspection, *path);
			if (terrain)
				terrainTextureDiagnostics.Observe(inspection, *path);
#endif
			if (reason != StreamingTextures::TextureRejection::None)
				exclude(Exclusion::TextureContract);
		};
		if (engine->UAV) {
			classify(StreamingTextures::TextureRejection::UnorderedAccess);
			return;
		}
		if (!engine->resourceView) {
			classify(StreamingTextures::TextureRejection::MissingView);
			return;
		}
		ComPtr<ID3D11Texture2D> texture;
		if (FAILED(engine->texture->QueryInterface(IID_PPV_ARGS(&texture)))) {
			classify(StreamingTextures::TextureRejection::NotTexture2D);
			return;
		}
		ComPtr<ID3D11Resource> viewedResource;
		engine->resourceView->GetResource(&viewedResource);
		if (viewedResource.Get() != engine->texture) {
			classify(StreamingTextures::TextureRejection::ViewResource);
			return;
		}
		Record value;
		texture->GetDesc(&value.full);
		engine->resourceView->GetDesc(&value.view);
#ifdef DEVBENCH_BRIDGE_ENABLED
		inspection.descriptorAvailable = true;
		inspection.texture = value.full;
		inspection.view = value.view;
#endif
		const auto contract = StreamingTextures::ClassifyTexture(value.full, value.view, engine->width, engine->height, engine->mips);
		if (contract != StreamingTextures::TextureRejection::None) {
			classify(contract);
			return;
		}
		if (!StreamingTextures::LogicalBytes(value.full, 0)) {
			classify(StreamingTextures::TextureRejection::PayloadSize);
			return;
		}
		classify(StreamingTextures::TextureRejection::None);
		for (std::uint32_t drop = 0; drop < value.bytes.size(); ++drop)
			value.bytes[drop] = StreamingTextures::LogicalBytes(value.full, drop);
		value.source.reset(source);
		value.engine = engine;
		value.current = engine->texture;
		value.currentView = engine->resourceView;
		value.path = *path;
		it = records.emplace(origin.serial, std::move(value)).first;
		++diagnostics.candidateRegistrations;
	}
	auto& record = it->second;
#ifdef DEVBENCH_BRIDGE_ENABLED
	const bool terrainConsumer = (origin.requiredCategories & StreamingTextures::Terrain) != 0;
	if (terrainConsumer && !record.terrainConsumer)
		++terrainInventoryDiagnostics.candidateRegistrations;
	record.terrainConsumer |= terrainConsumer;
#endif
	record.consumers.Observe(scan, completeScan);
	record.protectedConsumer |= origin.protectedConsumer || !eligible || record.engine != engine;
	if (record.protectedConsumer)
		StreamingTextures::LoadHooks::Forget(record.path);
	if (!record.protectedConsumer && record.consumers.scanning.size() < maximumConsumers)
		record.consumers.scanning.push_back({ RE::NiPointer<RE::BSGeometry>(geometry) });
	else if (record.consumers.scanning.size() >= maximumConsumers)
		record.protectedConsumer = true;
}

void TextureStreaming::State::SuspendInventory(const char* reason)
{
	if (!inventoryFailure.empty())
		return;
	inventoryFailure = reason;
	enabled = false;
	++epoch;
	completeScan = 0;
	frontier.clear();
	visited.clear();
	previewFrontier.clear();
	previewVisited.clear();
	previewRoots.clear();
	for (auto& [id, record] : records)
		record.consumers.Clear();
	StreamingTextures::LoadHooks::Configure(false, modes, options, categories, maximumDrop);
	for (const auto id : reducedRecords)
		if (auto found = records.find(id); found != records.end()) {
			found->second.previewProtected = found->second.protectedConsumer = true;
			StreamingTextures::LoadHooks::Forget(found->second.path);
		}
	Fail(reason);
}

void TextureStreaming::State::ProtectPreviewTexture(RE::NiSourceTexture* texture)
{
	if (!texture || !texture->rendererTexture)
		return;
	const auto* name = texture->name.c_str();
	if (name && *name)
		StreamingTextures::LoadHooks::Forget(StreamingTextures::NormalizeTexturePath(name));
	StreamingTextures::Origin origin;
	auto* resource = texture->rendererTexture->texture;
	if (!StreamingTextures::ReadOrigin(resource, origin))
		return;
	origin.protectedConsumer = true;
	if (FAILED(StreamingTextures::WriteOrigin(resource, origin))) {
		enabled = false;
		StreamingTextures::LoadHooks::Configure(false, modes, options, categories, maximumDrop);
		Fail("Could not protect a preview texture; distance streaming suspended");
	}
	if (const auto found = records.find(origin.serial); found != records.end()) {
		found->second.previewProtected = true;
		found->second.protectedConsumer = true;
	}
}

void TextureStreaming::State::ScanPreviews()
{
	if (!inventoryFailure.empty())
		return;
	auto preview = StreamingTextures::Preview::Roots();
	if (!preview.complete) {
		SuspendInventory("Invalid or oversized preview root inventory; texture streaming suspended until restart");
		return;
	}
	auto roots = std::move(preview.roots);
	previewActive = !roots.empty() || StreamingTextures::Preview::Active();
	if (roots != previewRoots) {
		previewRoots = std::move(roots);
		previewFrontier.clear();
		previewVisited.clear();
		nextPreviewScanMs = 0;
	}
	if (previewRoots.empty())
		return;
	const auto now = GetTickCount64();
	if (previewFrontier.empty()) {
		if (now < nextPreviewScanMs)
			return;
		previewFrontier = previewRoots;
		previewVisited.clear();
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(400);
	for (unsigned count = 0; !previewFrontier.empty() && count < 512 && std::chrono::steady_clock::now() < deadline; ++count) {
		auto object = std::move(previewFrontier.back());
		previewFrontier.pop_back();
		if (!object || !previewVisited.insert(object.get()).second)
			continue;
		if (auto* geometry = object->AsGeometry())
			if (auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get()) {
				struct Visitor final : RE::BSShaderProperty::ForEachVisitor
				{
					State& state;
					explicit Visitor(State& value) : state(value) {}
					std::uint32_t Accept(RE::NiSourceTexture* texture) override
					{
						state.ProtectPreviewTexture(texture);
						return 1;
					}
				} visitor(*this);
				PBRTextures::ForEach(*property, visitor);
			}
		if (auto* branch = object->AsNode()) {
			if (!VisitInventoryChildren(branch->GetChildren(), maximumFrontier - previewFrontier.size(), [&](auto* child) {
					previewFrontier.emplace_back(child);
				})) {
				SuspendInventory("Invalid or oversized preview child inventory; texture streaming suspended until restart");
				return;
			}
		}
		if (previewVisited.size() >= 20000) {
			SuspendInventory("Preview exceeds the bounded scan; texture streaming suspended until restart");
			return;
		}
	}
	if (previewFrontier.empty())
		nextPreviewScanMs = now + 250;
}

void TextureStreaming::State::Scan()
{
	if (!inventoryFailure.empty())
		return;
	if (frontier.empty()) {
		if (GetTickCount64() < nextScanMs)
			return;
		auto* root = RE::Main::WorldRootNode();
		if (!root)
			return;
		if (!Util::IsLikelyValidPointer(root)) {
			SuspendInventory("Invalid world inventory root; texture streaming suspended until restart");
			return;
		}
		++scan;
		visited.clear();
		frontier.push_back({ RE::NiPointer<RE::NiAVObject>(root), false });
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(400);
	unsigned count = 0;
	while (!frontier.empty() && ++count <= 512 && std::chrono::steady_clock::now() < deadline) {
		auto node = std::move(frontier.back());
		frontier.pop_back();
		if (!node.object || !visited.insert(node.object.get()).second)
			continue;
		++diagnostics.nodesVisited;
		if (auto* reference = Util::GetOwnUserData(node.object.get())) {
			auto* base = reference->GetBaseObject();
			node.staticOwner = base && (base->GetFormType() == RE::FormType::Static || base->GetFormType() == RE::FormType::Land);
			node.protectedHierarchy |= !node.staticOwner;
		}
		node.protectedHierarchy |= node.object->GetControllers() != nullptr;
		if (auto* geometry = node.object->AsGeometry()) {
			++diagnostics.geometryObservations;
			std::uint32_t requiredCategories = 0;
			bool staticGeometry = !node.protectedHierarchy;
#ifdef DEVBENCH_BRIDGE_ENABLED
			StreamingTextures::MaterialInspection inspection;
			auto* material = staticGeometry ? StreamingTextures::StaticMaterial(geometry, &requiredCategories, &inspection) : nullptr;
			if (staticGeometry) {
				const auto* name = geometry->name.c_str() ? geometry->name.c_str() : "";
				materialDiagnostics.Observe(inspection, name);
				if (inspection.materialFeature == static_cast<std::uint32_t>(RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend))
					terrainMaterialDiagnostics.Observe(inspection, name);
			}
#else
			auto* material = staticGeometry ? StreamingTextures::StaticMaterial(geometry, &requiredCategories) : nullptr;
#endif
			staticGeometry &= node.staticOwner || (requiredCategories & StreamingTextures::Terrain);
			if (!staticGeometry)
				material = nullptr;
			const bool categoryEnabled = categories.Allows(requiredCategories);
			const auto& bound = geometry->worldBound;
			const bool measurable = material && std::isfinite(bound.radius) && bound.radius > 0 &&
			                        std::isfinite(bound.center.x) && std::isfinite(bound.center.y) && std::isfinite(bound.center.z);
			const bool eligible = material && (!categoryEnabled || measurable);
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (inspection.materialFeature == static_cast<std::uint32_t>(RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)) {
				++terrainInventoryDiagnostics.geometryObservations;
				terrainInventoryDiagnostics.staticGeometryObservations += staticGeometry;
				terrainInventoryDiagnostics.supportedMaterialObservations += material != nullptr;
				terrainInventoryDiagnostics.measurableGeometryObservations += measurable;
			}
#endif
			diagnostics.staticGeometryObservations += staticGeometry;
			diagnostics.supportedMaterialObservations += material != nullptr;
			diagnostics.measurableGeometryObservations += measurable;
			if (auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get()) {
				struct Visitor final : RE::BSShaderProperty::ForEachVisitor
				{
					State& state;
					RE::BSGeometry* geometry;
					RE::BSLightingShaderMaterialBase* material;
					bool eligible;
					std::uint32_t requiredCategories;
					Visitor(State& s, RE::BSGeometry* g, RE::BSLightingShaderMaterialBase* m, bool e, std::uint32_t c) :
						state(s), geometry(g), material(m), eligible(e), requiredCategories(c) {}
					std::uint32_t Accept(RE::NiSourceTexture* texture) override
					{
						const bool ordinarySlot = StreamingTextures::IsStreamingSlot(material, texture);
						state.ObserveTexture(texture, geometry, eligible && ordinarySlot, requiredCategories);
						return 1;
					}
				} visitor(*this, geometry, material, eligible, requiredCategories);
				PBRTextures::ForEach(*property, visitor);
			}
		}
		if (auto* branch = node.object->AsNode()) {
			if (!VisitInventoryChildren(branch->GetChildren(), maximumFrontier - frontier.size(), [&](auto* child) {
					frontier.push_back({ RE::NiPointer<RE::NiAVObject>(child), node.staticOwner, node.protectedHierarchy });
				})) {
				SuspendInventory("Invalid or oversized world child inventory; texture streaming suspended until restart");
				return;
			}
		}
		if (visited.size() > 131072) {
			SuspendInventory("Scene exceeds the bounded inventory; texture streaming suspended until restart");
			return;
		}
	}
	if (frontier.empty()) {
		completeScan = scan;
		++diagnostics.scansCompleted;
#ifdef DEVBENCH_BRIDGE_ENABLED
		++terrainInventoryDiagnostics.scansCompleted;
#endif
		nextScanMs = GetTickCount64() + 250;
	}
}

std::uint32_t TextureStreaming::State::Demand(Record& record)
{
	++diagnostics.demandChecks;
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (record.terrainConsumer)
		++terrainInventoryDiagnostics.demandChecks;
#endif
	record.distanceDrop = 0;
	record.restorationUrgency = 1;
	if (record.previewProtected) {
		record.restorationUrgency = 64;
		return 0;
	}
	record.consumers.Promote(completeScan);
	if (!enabled)
		return 0;
	if (!completeScan || record.consumers.verified != completeScan || record.consumers.committed.empty()) {
		Defer(Block::InventoryIncomplete, record);
		return 0;
	}
	if (record.protectedConsumer) {
		Defer(Block::ProtectedConsumer, record);
		return 0;
	}
	StreamingTextures::Origin origin;
	if (record.source->rendererTexture != record.engine || record.engine->texture != record.current ||
		!StreamingTextures::ReadOrigin(record.current, origin) || origin.protectedConsumer) {
		Defer(Block::SourceChanged, record);
		return 0;
	}
	if (!categories.Allows(origin.requiredCategories)) {
		Defer(Block::CategoryDisabled, record);
		return 0;
	}
	double required = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
	RE::BSGeometry* limitingGeometry = nullptr;
#endif
	for (auto& consumer : record.consumers.committed) {
		std::uint32_t requiredCategories = 0;
		const auto* material = StreamingTextures::StaticMaterial(consumer.geometry.get(), &requiredCategories);
		if (!material || !categories.Allows(requiredCategories) ||
			!StreamingTextures::IsStreamingSlot(material, record.source.get())) {
			Defer(Block::ConsumerContract, record);
			return 0;
		}
		const auto edge = StreamingTextures::RequiredEdge(consumer.geometry.get(), this->demand);
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!limitingGeometry || edge > required)
			limitingGeometry = consumer.geometry.get();
#endif
		required = std::max(required, edge);
	}
	record.restorationUrgency = required / std::max(1u, std::max(record.full.Width, record.full.Height) >> record.drop);
	const auto drop = Policy::DesiredDrop(std::max(record.full.Width, record.full.Height), record.full.MipLevels, required, maximumDrop,
		std::min(record.full.Width, record.full.Height));
	const auto desired = std::max(drop, Policy::CappedDrop(record.full.Width, record.full.Height, record.full.MipLevels, options.maximumSize));
#ifdef DEVBENCH_BRIDGE_ENABLED
	StreamingTextures::DemandDiagnostics::Sample sample;
	sample.requiredEdge = required;
	sample.fullWidth = record.full.Width;
	sample.fullHeight = record.full.Height;
	sample.mipLevels = record.full.MipLevels;
	sample.desiredDrop = desired;
	sample.consumers = record.consumers.committed.size();
	sample.currentFrame = globals::state->frameCount;
	sample.context = demand;
	if (limitingGeometry) {
		const auto& bound = limitingGeometry->worldBound;
		sample.boundCenter = { bound.center.x, bound.center.y, bound.center.z };
		sample.boundRadius = bound.radius;
	}
	const auto* geometryName = limitingGeometry && limitingGeometry->name.c_str() ? limitingGeometry->name.c_str() : "";
	demandDiagnostics.Observe(sample, record.path, geometryName);
	if (record.terrainConsumer)
		terrainDemandDiagnostics.Observe(sample, record.path, geometryName);
#endif
	if (!desired)
		Defer(Block::FullDetailDemand, record);
	record.distanceDrop = drop;
	return desired;
}

bool TextureStreaming::State::ReductionRequested() const noexcept
{
	return enabled && (modes.continuous || (modes.onPressure && pressure.conserving));
}

void TextureStreaming::State::Select(bool previewOnly)
{
	if (reading || transaction || !memory.Fresh(GetTickCount64())) {
		diagnostics.Defer(reading ? Block::ReaderPending : transaction ? (transaction->published ? Block::RetirementPending : Block::UploadPending) :
																		 Block::StaleBudget);
		return;
	}
	std::uint64_t selected = 0;
	bool selectedRefill = false;
	StreamingTextures::RestorationPriority selectedPriority;
	const auto now = GetTickCount64();
	const auto consider = [&](std::uint64_t id, Record& record) {
		if (previewOnly && !record.previewProtected)
			return;
		if (record.source->rendererTexture != record.engine || record.engine->texture != record.current) {
			Defer(Block::SourceChanged, record);
			return;
		}
		if (record.loadLifetime && !completeScan && enabled && !record.previewProtected)
			return;
		const auto demanded = Demand(record);
		const auto target = Target(record, demanded);
		if (record.sourceValidated && record.headerHash && record.rememberedDrop != record.distanceDrop && completeScan && record.consumers.verified == completeScan &&
			!record.consumers.committed.empty() && !record.protectedConsumer) {
			StreamingTextures::Origin origin;
			if (StreamingTextures::ReadOrigin(record.current, origin) && !origin.protectedConsumer) {
				StreamingTextures::LoadHooks::Remember(record.path, { record.full, record.distanceDrop, origin.requiredCategories, record.headerHash, record.fileBytes });
				record.rememberedDrop = record.distanceDrop;
			}
		}
		if (record.desired != target) {
			record.desired = target;
			record.stableSince = now;
		}
		if (record.drop == target) {
			record.restorationWaitingSince = 0;
			return;
		}
		if (target < record.drop) {
			if (!record.restorationWaitingSince)
				record.restorationWaitingSince = now;
		} else {
			record.restorationWaitingSince = 0;
		}
		if (now < record.retryAfter) {
			Defer(Block::RetryBackoff, record);
			return;
		}
		const bool refill = target < record.drop;
		const bool required = demanded < record.drop;
		if (!refill)
			++diagnostics.shrinkCandidates;
		else if (required)
			++diagnostics.requiredRestoreCandidates;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (record.terrainConsumer) {
			terrainInventoryDiagnostics.shrinkCandidates += !refill;
			terrainInventoryDiagnostics.requiredRestoreCandidates += refill && required;
		}
#endif
		if (refill && memory.priorityWork) {
			Defer(Block::PriorityWork, record);
			return;
		}
		if (!refill && (!completeScan || !frontier.empty() || now - record.stableSince < Policy::StableDemandMs)) {
			Defer(!completeScan || !frontier.empty() ? Block::InventoryIncomplete : Block::DemandSettling, record);
			return;
		}
		if (!record.drop && reducedRecords.size() >= maximumReducedTextures) {
			Defer(Block::ReducedLimit, record);
			return;
		}
		if (!Memory::StreamingFits(memory.local.Budget, memory.local.CurrentUsage, memory.pendingBytes,
				record.bytes[target], refill, required, memory.priorityWork, memory.recoveryDemandBytes, options.reserveMiB * Policy::MiB)) {
			const bool softDemandBlocked = memory.recoveryDemandBytes && Memory::StreamingFits(memory.local.Budget,
																			 memory.local.CurrentUsage, memory.pendingBytes, record.bytes[target], refill, required, memory.priorityWork, 0, options.reserveMiB * Policy::MiB);
			Defer(softDemandBlocked ? Block::RecoveryHeadroom : Block::ReplacementHeadroom, record);
			record.retryAfter = now + 1000;
			++admissionDeferrals;
			return;
		}
		const auto saving = record.bytes[record.drop] - std::min(record.bytes[record.drop], record.bytes[target]);
		const StreamingTextures::RestorationPriority priority{ refill ? (record.previewProtected ? 3u : required ? 2u :
																												   1u) :
																		0u,
			record.restorationUrgency, record.restorationWaitingSince, saving, id };
		if (!selected || priority.BetterThan(selectedPriority, now)) {
			selected = id;
			selectedRefill = refill;
			selectedPriority = priority;
		}
	};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(400);
	// Service the bounded reduced set first so refill demand cannot queue behind the full inventory.
	auto reduced = reducedRecords.find(reducedCursor);
	if (reduced == reducedRecords.end())
		reduced = reducedRecords.begin();
	for (unsigned count = 0; reduced != reducedRecords.end() && count < 64 && std::chrono::steady_clock::now() < deadline; ++count) {
		const auto id = *reduced++;
		reducedCursor = reduced == reducedRecords.end() ? 0 : *reduced;
		if (auto entry = records.find(id); entry != records.end())
			consider(id, entry->second);
	}
	auto entry = records.find(selectionCursor);
	if (entry == records.end())
		entry = records.begin();
	for (unsigned count = 0; entry != records.end() && count < 64 && std::chrono::steady_clock::now() < deadline; ++count) {
		auto current = entry++;
		selectionCursor = entry == records.end() ? 0 : entry->first;
		if (!current->second.drop)
			consider(current->first, current->second);
	}

	if (!selected)
		return;
	auto& record = records.at(selected);
	ReadRequest request{ selected, epoch, record.path, record.full, record.desired, selectedRefill ? ReadKind::Restore : ReadKind::ValidateSource };
	if (!selectedRefill && record.sourceValidated) {
		transaction.emplace();
		transaction->payload.request = std::move(request);
		detail = "Copying resident lower mips on the GPU";
	} else {
		reader.Start(std::move(request));
		reading = true;
		detail = selectedRefill ? "Reading restoration mips or using RAM cache" : "Validating DDS source before GPU reduction";
	}
}

void TextureStreaming::State::Publish(Record& record, Transaction& work)
{
	auto* context = globals::d3d::context;
	auto* old = record.engine->resourceView;
	StreamingTextures::Origin origin;
	if (!StreamingTextures::ReadOrigin(record.current, origin) || origin.serial != work.payload.request.serial) {
		record.retryAfter = GetTickCount64() + 10000;
		Fail("Texture provenance changed before replacement publication");
		CancelTransaction();
		return;
	}
	origin.protectedConsumer |= record.protectedConsumer;
	origin.preloaded = false;
	if (FAILED(StreamingTextures::WriteOrigin(work.upload.texture.Get(), origin))) {
		record.retryAfter = GetTickCount64() + 10000;
		Fail("Could not preserve replacement consumer requirements");
		CancelTransaction();
		return;
	}
	const auto visitSpecialBindings = [&](ID3D11ShaderResourceView* replacement) {
		bool found = false;
		found |= VisitBindings(context, &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::VSSetShaderResources, old, replacement);
		found |= VisitBindings(context, &ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::HSSetShaderResources, old, replacement);
		found |= VisitBindings(context, &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::DSSetShaderResources, old, replacement);
		found |= VisitBindings(context, &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::GSSetShaderResources, old, replacement);
		found |= VisitBindings(context, &ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::CSSetShaderResources, old, replacement);
		return found;
	};
	const auto hasCachedComputeView = [&](const auto& shadow) {
		return std::ranges::find(shadow.CSTexture, old) != std::end(shadow.CSTexture);
	};
	const bool fullDetail = work.payload.request.drop == 0;
	if (!fullDetail) {
		const bool cachedCompute = globals::game::isVR ? hasCachedComputeView(globals::game::shadowState->GetVRRuntimeData()) :
		                                                 hasCachedComputeView(globals::game::shadowState->GetRuntimeData());
		if (visitSpecialBindings(nullptr) || cachedCompute) {
			record.protectedConsumer = origin.protectedConsumer = true;
			if (FAILED(StreamingTextures::WriteOrigin(record.current, origin))) {
				enabled = false;
				Fail("Could not protect a non-pixel texture consumer");
			}
			CancelTransaction();
			return;
		}
	}
	// Finish fallible preparation before changing live bindings or engine ownership.
	if (work.payload.request.drop)
		reducedRecords.insert(work.payload.request.serial);
	else
		reducedRecords.erase(work.payload.request.serial);
	if (fullDetail)
		visitSpecialBindings(work.upload.view.Get());
	VisitBindings(context, &ID3D11DeviceContext::PSGetShaderResources, &ID3D11DeviceContext::PSSetShaderResources, old, work.upload.view.Get());
	if (globals::game::isVR)
		ReplaceCachedView(globals::game::shadowState->GetVRRuntimeData(), old, work.upload.view.Get());
	else
		ReplaceCachedView(globals::game::shadowState->GetRuntimeData(), old, work.upload.view.Get());
	work.oldTexture.Attach(record.engine->texture);
	work.oldView.Attach(record.engine->resourceView);
	record.engine->texture = work.upload.texture.Detach();
	record.engine->resourceView = work.upload.view.Detach();
	// Some runtimes leave optional wrapper metadata unset; preserve that representation.
	StreamingTextures::UpdateEngineMetadata(*record.engine, work.upload.desc);
	record.current = record.engine->texture;
	record.currentView = record.engine->resourceView;
#ifdef DEVBENCH_BRIDGE_ENABLED
	work.terrainConsumer = (origin.requiredCategories & StreamingTextures::Terrain) != 0;
#endif
	if (work.payload.request.drop < record.drop) {
		++restores;
#ifdef DEVBENCH_BRIDGE_ENABLED
		work.payload.cacheHit ? ++cachedRestores : ++ddsRestores;
		terrainRestores += work.terrainConsumer;
#endif
	} else {
		++shrinks;
#ifdef DEVBENCH_BRIDGE_ENABLED
		++gpuCopyShrinks;
		if (work.terrainConsumer && work.payload.request.drop > record.drop) {
			++terrainShrinks;
			const auto length = std::min(record.path.size(), terrainReduction.path.size() - 1);
			std::copy_n(record.path.data(), length, terrainReduction.path.data());
			terrainReduction.path[length] = '\0';
			terrainReduction.serial = origin.serial;
			terrainReduction.requiredCategories = origin.requiredCategories;
			terrainReduction.fullWidth = record.full.Width;
			terrainReduction.fullHeight = record.full.Height;
			terrainReduction.width = work.upload.desc.Width;
			terrainReduction.height = work.upload.desc.Height;
			terrainReduction.previousDrop = record.drop;
			terrainReduction.drop = work.payload.request.drop;
		}
#endif
	}
	const auto previousDrop = record.drop;
	record.drop = work.payload.request.drop;
	if (!record.drop)
		record.loadLifetime.reset();
	record.stableSince = GetTickCount64();
	context->End(work.retirement.Get());
	work.published = true;
	work.replacementCommitted = true;
	work.logicalReductionBytes = record.bytes[previousDrop] - std::min(record.bytes[previousDrop], record.bytes[record.drop]);
	work.payload.image.reset();
	detail = "Waiting for old texture GPU retirement";
	if (firstPressureReductionPending && record.drop > previousDrop) {
		firstPressureReductionPending = false;
		logger::info("[TextureStreaming] First reduction under memory pressure committed: mip drop {} -> {}, logical capacity removed {:.2f} MiB; old resource awaiting GPU retirement",
			previousDrop, record.drop, static_cast<double>(record.bytes[previousDrop] - record.bytes[record.drop]) / Policy::MiB);
	}
}

void TextureStreaming::State::CancelTransaction()
{
	if (!transaction || transaction->published)
		return;
	++cancelled;
	auto& work = *transaction;
	if (work.upload.texture && work.retirement) {
		// Cancellation must retire submitted uploads before releasing their peak reservation.
		work.oldTexture = work.upload.texture;
		work.oldView = work.upload.view;
		work.upload.texture.Reset();
		work.upload.view.Reset();
		work.payload.image.reset();
		globals::d3d::context->End(work.retirement.Get());
		work.published = true;
	} else {
		transaction.reset();
	}
}

void TextureStreaming::State::ServiceTransaction()
{
	if (reading) {
		if (auto result = reader.Poll()) {
			reading = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
			sourceBytesRead += result->sourceBytesRead;
			if (result->sourceBytesRead)
				result->request.kind == ReadKind::ValidateSource ? ++sourceHeaderReads : ++sourcePayloadReads;
#endif
			if (result->request.epoch != epoch || !records.contains(result->request.serial)) {
				++cancelled;
			} else if (!result->error.empty()) {
				records.at(result->request.serial).retryAfter = GetTickCount64() + 10000;
				Fail(std::format("{}: {}", result->error, result->request.path));
			} else {
				auto& record = records.at(result->request.serial);
				record.sourceValidated = true;
				if (result->headerHash) {
					record.headerHash = result->headerHash;
					record.fileBytes = result->fileBytes;
				}
				transaction.emplace();
				transaction->payload = std::move(*result);
			}
		}
	}
	if (!transaction)
		return;
	auto& work = *transaction;
	auto* context = globals::d3d::context;
	if (work.published) {
		const auto hr = context->GetData(work.retirement.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr == S_OK) {
			work.oldView.Reset();
			work.oldTexture.Reset();
			work.upload.copySource.Reset();
			memory = Memory::Get().Sample(globals::d3d::device, 0, true);
			if (memory.Fresh(GetTickCount64())) {
				const auto logicalReductionBytes = work.logicalReductionBytes;
				const bool reportReduction = work.replacementCommitted && logicalReductionBytes &&
				                             pressureEpisode.active && !pressureEpisode.retirementReported;
				if (work.replacementCommitted) {
					++diagnostics.retiredReplacements;
					if (work.logicalReductionBytes) {
						++diagnostics.retiredReductions;
						diagnostics.retiredLogicalReductionBytes += work.logicalReductionBytes;
#ifdef DEVBENCH_BRIDGE_ENABLED
						if (work.terrainConsumer) {
							++terrainRetiredReductions;
							terrainRetiredLogicalBytes += work.logicalReductionBytes;
						}
#endif
					}
				} else {
					++diagnostics.retiredCancelledUploads;
				}
				transaction.reset();
				detail = enabled ? "Monitoring texture demand and GPU memory" : "Disabled";
				if (reportReduction) {
					pressureEpisode.retirementReported = true;
					logger::info("[TextureStreaming] First reduced replacement retired during pressure episode: logical capacity removed {:.2f} MiB; physical VRAM relief is not measured",
						static_cast<double>(logicalReductionBytes) / Policy::MiB);
				}
			}
		} else if (FAILED(hr)) {
			diagnostics.Defer(Block::RetirementFailed);
			detail = "Retirement fence failed; retaining the old texture and reservation";
		}
		return;
	}
	const auto found = records.find(work.payload.request.serial);
	if (work.payload.request.epoch != epoch || found == records.end()) {
		CancelTransaction();
		return;
	}
	auto& record = found->second;
	const auto target = work.payload.request.drop;
	if (record.source->rendererTexture != record.engine || record.engine->texture != record.current || record.engine->resourceView != record.currentView || record.engine->UAV || target > Target(record, Demand(record))) {
		CancelTransaction();
		return;
	}
	if (!work.reservation) {
		const bool refill = target < record.drop;
		work.reservation = Memory::Get().TryReserveStreaming(globals::d3d::device, StreamingTextures::LogicalBytes(record.full, target), refill, Demand(record) < record.drop, options.reserveMiB * Policy::MiB);
		if (!work.reservation) {
			++admissionDeferrals;
			diagnostics.Defer(Block::ReplacementHeadroom);
			detail = "Replacement overlap waits for shared memory headroom";
			record.retryAfter = GetTickCount64() + 1000;
			transaction.reset();
			return;
		}
		auto hr = work.payload.request.kind == ReadKind::ValidateSource ?
		              work.upload.BeginCopy(globals::d3d::device, record.full, record.view, target, record.current, record.drop) :
		              work.upload.Begin(globals::d3d::device, record.full, record.view, target);
		const D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
		if (SUCCEEDED(hr))
			hr = globals::d3d::device->CreateQuery(&query, &work.retirement);
		if (SUCCEEDED(hr)) {
			Util::SetResourceName(work.retirement.Get(), "TextureStreaming::RetirementFence");
			hr = StreamingTextures::WriteOrigin(work.upload.texture.Get(), { work.payload.request.serial, record.protectedConsumer });
		}
		if (FAILED(hr)) {
			record.retryAfter = GetTickCount64() + 10000;
			Fail(std::format("Replacement creation failed: 0x{:08X}", static_cast<std::uint32_t>(hr)));
			transaction.reset();
			return;
		}
	}
	if (target < record.drop && Memory::Get().Sample(globals::d3d::device).priorityWork) {
		diagnostics.Defer(Block::PriorityWork);
		CancelTransaction();
		return;
	}
	const auto hr = [&] {
		if (work.payload.request.kind == ReadKind::ValidateSource) {
			CS_GPU_PASS("TextureStreaming::Shrink");
			return work.upload.AdvanceCopy(context, uploadBytesPerFrame);
		}
		CS_GPU_PASS("TextureStreaming::Restore");
		return work.payload.image ? work.upload.Advance(context, *work.payload.image, uploadBytesPerFrame) : E_UNEXPECTED;
	}();
	if (FAILED(hr)) {
		record.retryAfter = GetTickCount64() + 10000;
		Fail(std::format("Replacement upload failed: 0x{:08X}", static_cast<std::uint32_t>(hr)));
		CancelTransaction();
	} else if (hr == S_OK && (target < record.drop || frontier.empty())) {
		Publish(record, work);
	}
}

void TextureStreaming::State::Tick()
{
	if (!globals::d3d::device || !globals::d3d::context || !globals::game::renderer || !globals::state || !globals::game::shadowState)
		return;
	const auto frame = globals::state->frameCount;
	if (lastFrame == frame)
		return;
	lastFrame = frame;
	if (!enabled && records.empty() && !reading && !transaction && !StreamingTextures::LoadHooks::HasPending()) {
		detail = "Disabled";
		return;
	}
	Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
	if (!owner)
		return;
	AdoptLoads();
	ScanPreviews();
	memory = Memory::Get().SampleStreaming(globals::d3d::device);
	const bool transition = globals::features::upscaling.IsTextureStreamingTransitionActive();
	memory.priorityWork |= transition;
	const auto start = globals::state->worldLoadTransitionStartFrame.load(std::memory_order_acquire);
	const bool loading = globals::state->IsSaveLoadSafeModeActive();
	const bool blocked = loading || transition;
	if (paused && !blocked)
		detail = ReductionRequested() ? "Scanning textures for safe detail reductions" : "Monitoring GPU memory pressure";
	if (start != worldStart || blocked != paused || deviceGeneration != memory.generation) {
		++epoch;
		worldStart = start;
		paused = blocked;
		if (deviceGeneration && deviceGeneration != memory.generation) {
			transaction.reset();
			records.clear();
			reducedRecords.clear();
			reader.ClearCache();
		}
		deviceGeneration = memory.generation;
		frontier.clear();
		visited.clear();
		completeScan = 0;
		for (auto& [id, record] : records) {
			record.consumers.Clear();
		}
	}
	const auto now = GetTickCount64();
	const auto pressureChange = pressure.Update(now, memory.sampledAtMs, memory.Fresh(now), memory.local.Budget,
		Memory::Add(memory.local.CurrentUsage, memory.pendingBytes), options.reserveMiB * Policy::MiB);
	if (pressureChange != Policy::PressureChange::None) {
		firstPressureReductionPending = pressure.conserving;
		logger::info("[TextureStreaming] Memory pressure {}: usage {:.1f} MiB, budget {:.1f} MiB, pending {:.1f} MiB; enabled={}, paused={}",
			pressure.conserving ? "entered" : "recovered; restoration remains subject to headroom and priority work",
			static_cast<double>(memory.local.CurrentUsage) / Policy::MiB, static_cast<double>(memory.local.Budget) / Policy::MiB,
			static_cast<double>(memory.pendingBytes) / Policy::MiB, enabled, blocked);
	}
	UpdatePressureDiagnostics(now);
	if (blocked) {
		diagnostics.Defer(loading ? Block::WorldLoading : Block::Transition);
		const bool previewWork = transaction && records.contains(transaction->payload.request.serial) && records.at(transaction->payload.request.serial).previewProtected;
		if (transition || !previewWork)
			CancelTransaction();
		ServiceTransaction();
		if (loading && !transition && previewActive)
			Select(true);
		Prune();
		detail = loading ? "Waiting for world loading" : "Waiting for render-scale or NR transition";
		return;
	}
	if (enabled && originHookReady)
		Scan();
	else if (enabled)
		diagnostics.Defer(Block::OriginUnavailable);
	ServiceTransaction();
	Select();
	Prune();
}

void TextureStreaming::State::UpdatePressureDiagnostics(std::uint64_t now)
{
	if (pressureEpisode.active && (!pressure.conserving || !enabled)) {
		ReportPressureEpisode(enabled ? "recovered" : "disabled", now);
		pressureEpisode.active = false;
	} else if (!pressureEpisode.active && pressure.conserving && enabled) {
		pressureEpisode = { true, false, false, now, diagnostics, shrinks, restores, failures, cancelled };
	}
	if (pressureEpisode.SummaryDue(now))
		ReportPressureEpisode("ongoing", now);
}

void TextureStreaming::State::ReportPressureEpisode(std::string_view reason, std::uint64_t now)
{
	if (!spdlog::should_log(spdlog::level::info))
		return;
	logger::info("[TextureStreaming] Pressure episode {}: elapsed {} ms, shrinks {}, restores {}, failures {}, cancelled {}, records {}, reduced {}, scan {}/{}, frontier {}, enabled {}, hook {}, paused {}, pending {:.1f} MiB, future recovery {:.1f} MiB, demand eyes {}, demand age {} ms; observations {}",
		reason, now >= pressureEpisode.startedAtMs ? now - pressureEpisode.startedAtMs : 0,
		shrinks - pressureEpisode.initialShrinks, restores - pressureEpisode.initialRestores,
		failures - pressureEpisode.initialFailures, cancelled - pressureEpisode.initialCancelled,
		records.size(), reducedRecords.size(), completeScan, scan, frontier.size(), enabled, originHookReady.load(), paused,
		static_cast<double>(memory.pendingBytes) / Policy::MiB, static_cast<double>(memory.recoveryDemandBytes) / Policy::MiB,
		demand.eyeCount, now >= demand.sampledAtMs ? now - demand.sampledAtMs : 0,
		DiagnosticStatus(diagnostics, pressureEpisode.initial).dump());
}

void TextureStreaming::State::Prune()
{
	if (!enabled && reducedRecords.empty() && !reading && !transaction)
		reader.ClearCache(true);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(200);
	auto entry = records.find(pruneCursor);
	if (entry == records.end())
		entry = records.begin();
	for (unsigned count = 0; entry != records.end() && count < 128 && std::chrono::steady_clock::now() < deadline; ++count) {
		auto current = entry++;
		pruneCursor = entry == records.end() ? 0 : entry->first;
		const auto& [id, record] = *current;
		const bool replaced = record.source->rendererTexture != record.engine || record.engine->texture != record.current;
		// A partially scanned texture belongs to the next inventory, not an expired one.
		if ((!record.drop && (!enabled || record.consumers.Expired(completeScan))) || replaced) {
			reducedRecords.erase(id);
			records.erase(current);
		}
	}
}

TextureStreaming::TextureStreaming() : state(std::make_unique<State>()) {}
TextureStreaming::~TextureStreaming() = default;
TextureStreaming& TextureStreaming::Instance()
{
	static TextureStreaming instance;
	return instance;
}
std::pair<std::string, std::vector<std::string>> TextureStreaming::GetFeatureSummary()
{
	return { "Streams distant textures on memory pressure or continuously.",
		{ "Conservative static DDS coverage with optional categories", "Stereo and mip-bias aware detail", "Bounded restoration with shared NR and render-scale headroom" } };
}
void TextureStreaming::PostPostLoad()
{
	// All SKSE plugins are loaded before this phase, independent of their DLL load order.
	if (GetModuleHandleW(L"SPS.dll")) {
		std::scoped_lock lock(state->mutex);
		state->externalStreamingConflict = true;
		state->enabled = false;
		state->detail = streamingConflictWarning;
		StreamingTextures::LoadHooks::Configure(false, state->modes, state->options, state->categories, state->maximumDrop);
		logger::warn("[TextureStreaming] {}", streamingConflictWarning);
		return;
	}
	if (originHookReady.load())
		return;
	try {
		originalDDSLoader = reinterpret_cast<DDSLoader>(REL::Relocation<std::uintptr_t>{ REL::VariantID(75721, 77533, 0xDD2B60) }.address());
		const auto result = Util::AttachDetour(reinterpret_cast<void**>(&originalDDSLoader), reinterpret_cast<void*>(&LoadDDS));
		originHookReady.store(result.error == NO_ERROR, std::memory_order_release);
#ifdef DEVBENCH_BRIDGE_ENABLED
		CSX::Diagnostics::Stutters::AssetLoadHooks::SetDDSAvailable(result.error == NO_ERROR);
#endif
		if (result.error == NO_ERROR)
			StreamingTextures::LoadHooks::Install();
		StreamingTextures::Preview::Install();
		if (result.error != NO_ERROR)
			logger::error("[TextureStreaming] DDS provenance hook failed ({}); streaming unavailable", result.error);
	} catch (const std::exception& error) {
		logger::error("[TextureStreaming] DDS provenance unavailable: {}", error.what());
	}
}
void TextureStreaming::Reset()
{
	std::scoped_lock lock(state->mutex);
	if (state->externalStreamingConflict)
		return;
	try {
		state->Tick();
	} catch (const std::exception& error) {
		state->enabled = false;
		StreamingTextures::LoadHooks::Configure(false, state->modes, state->options, state->categories, state->maximumDrop);
		state->Fail(std::format("Streaming stopped after an exception: {}", error.what()));
	}
}
void TextureStreaming::CaptureWorldDemand(float mipBias)
{
	if (!loaded)
		return;
	std::scoped_lock lock(state->mutex);
	if (!state->enabled || !globals::game::shadowState || !globals::state)
		return;
	const auto& plan = globals::features::upscaling.GetRuntimeResolutionPlan();
	auto& demand = state->demand;
	demand = {};
	if (plan.menuContextActive || plan.loadingMenuActive || globals::state->IsSaveLoadSafeModeActive())
		return;
	demand.eyeCount = globals::game::isVR ? 2 : 1;
	demand.mipBias = mipBias;
	demand.frame = globals::state->frameCount;
	demand.sampledAtMs = GetTickCount64();
	for (unsigned i = 0; i < demand.eyeCount; ++i) {
		const auto camera = Util::GetCameraData(i);
		const auto position = Util::GetEyePosition(i);
		demand.positions[i] = { position.x, position.y, position.z };
		demand.forward[i] = { camera.viewForward.x, camera.viewForward.y, camera.viewForward.z };
		demand.eyes[i] = { plan.engineRenderSize.x / demand.eyeCount, plan.engineRenderSize.y,
			plan.finalOutputSize.x / demand.eyeCount, plan.finalOutputSize.y,
			std::abs(camera.projMatrixUnjittered._11), std::abs(camera.projMatrixUnjittered._22), 0 };
	}
}
void TextureStreaming::State::UpdateConfigurationDetail()
{
	detail = externalStreamingConflict ? streamingConflictWarning :
	         !inventoryFailure.empty() ? inventoryFailure :
	         !enabled                  ? "Restoring texture detail as headroom permits" :
	         modes.continuous          ? "Continuous texture streaming" :
	         modes.onPressure          ? "Monitoring GPU memory pressure" :
	                                     "Distance streaming off; texture size limit retained";
}
void TextureStreaming::Configure(bool enabled, std::uint32_t maximumDrop, std::optional<StreamingTextures::Categories> categories,
	std::optional<StreamingTextures::Modes> modes, std::optional<StreamingTextures::Options> options)
{
	if (options)
		StreamingTextures::ValidateOptions(*options);
	if (maximumDrop < 1 || maximumDrop > Policy::MaximumDrop)
		throw std::invalid_argument("MaximumMipDrop must be between 1 and 3");
	std::scoped_lock lock(state->mutex);
	const auto nextModes = modes.value_or(state->modes);
	const bool coverageChanged = categories && state->categories != *categories;
	const bool active = enabled && !state->externalStreamingConflict && state->inventoryFailure.empty();
	if (coverageChanged || state->modes != nextModes || state->enabled != active || state->maximumDrop != maximumDrop || (options && state->options != *options))
		++state->epoch;
	if (options && state->options.reserveMiB != options->reserveMiB)
		state->pressure = {};
	if (options)
		state->options = *options;
	if (coverageChanged) {
		state->categories = *categories;
		state->frontier.clear();
		state->visited.clear();
		state->completeScan = 0;
		state->nextScanMs = 0;
		for (auto& [id, record] : state->records)
			record.consumers.Clear();
	}
	state->modes = nextModes;
	state->requestedEnabled = enabled;
	state->enabled = active;
	if (!active)
		state->UpdatePressureDiagnostics(GetTickCount64());
	state->maximumDrop = maximumDrop;
	StreamingTextures::LoadHooks::Configure(active, state->modes, state->options, state->categories, state->maximumDrop);
	state->UpdateConfigurationDetail();
}
#ifdef DEVBENCH_BRIDGE_ENABLED
void TextureStreaming::SetDebugForceStreaming(bool enabled)
{
	std::scoped_lock lock(state->mutex);
	if (enabled && !state->enabled)
		throw std::invalid_argument("Enable Texture Streaming before forcing reductions");
	if (state->modes.continuous == enabled)
		return;
	state->modes.continuous = enabled;
	state->enabled = state->requestedEnabled && !state->externalStreamingConflict && state->inventoryFailure.empty();
	StreamingTextures::LoadHooks::Configure(state->enabled, state->modes, state->options, state->categories, state->maximumDrop);
	++state->epoch;
	state->UpdateConfigurationDetail();
}
#endif
void TextureStreaming::LoadSettings(nlohmann::json& object)
{
	try {
		const auto maximum = StreamingTextures::ParseMaximumMipDrop(object.value("MaximumMipDrop", nlohmann::json(2)));
		const auto modes = object.contains("Modes") ? StreamingTextures::ParseModes(object.at("Modes")) :
		                                              StreamingTextures::Modes{};
		Configure(object.value("Enabled", modes.Enabled()), maximum, object.contains("Categories") ? StreamingTextures::ParseCategories(object.at("Categories")) : StreamingTextures::Categories{}, modes,
			object.contains("Options") ? StreamingTextures::ParseOptions(object.at("Options")) : StreamingTextures::Options{});
	} catch (const std::exception& error) {
		logger::warn("[TextureStreaming] Invalid saved settings retained previous configuration: {}", error.what());
	}
}
void TextureStreaming::SaveSettings(nlohmann::json& object)
{
	std::scoped_lock lock(state->mutex);
	object = { { "Enabled", state->requestedEnabled }, { "Modes", StreamingTextures::SerializeModes(state->modes) },
		{ "MaximumMipDrop", state->maximumDrop }, { "Categories", StreamingTextures::SerializeCategories(state->categories) },
		{ "Options", StreamingTextures::SerializeOptions(state->options) } };
}
void TextureStreaming::RestoreDefaultSettings() { Configure(true, 2, StreamingTextures::Categories{}, StreamingTextures::Modes{}, StreamingTextures::Options{}); }
nlohmann::json TextureStreaming::GetStatus() const
{
	std::scoped_lock lock(state->mutex);
	std::uint64_t full = 0, resident = 0, protectedCount = 0, reduced = 0, desiredShrinks = 0, desiredRestores = 0;
	for (const auto& [id, record] : state->records) {
		full += record.bytes[0];
		resident += record.bytes[record.drop];
		protectedCount += record.protectedConsumer;
		reduced += record.drop != 0;
		desiredShrinks += record.desired > record.drop;
		desiredRestores += record.desired < record.drop;
	}
	const auto& memory = state->memory;
	const auto unavailableReason = state->externalStreamingConflict ? streamingConflictWarning :
	                               !state->inventoryFailure.empty() ? state->inventoryFailure :
	                                                                  failedLoadedMessage;
	nlohmann::json result = { { "enabled", state->enabled }, { "requestedEnabled", state->requestedEnabled }, { "maximumMipDrop", state->maximumDrop },
		{ "externalStreamingConflict", state->externalStreamingConflict }, { "unavailableReason", unavailableReason },
		{ "modes", StreamingTextures::SerializeModes(state->modes) },
		{ "options", StreamingTextures::SerializeOptions(state->options) }, { "rememberedSizes", StreamingTextures::LoadHooks::Status() },
		{ "categories", StreamingTextures::SerializeCategories(state->categories) }, { "originHookReady", originHookReady.load() },
		{ "conserving", state->pressure.conserving }, { "records", state->records.size() }, { "protectedRecords", protectedCount },
		{ "reducedTextures", reduced }, { "fullLogicalBytes", full }, { "residentLogicalBytes", resident },
		{ "logicalBytesRemoved", full - resident }, { "physicalBytesReclaimed", nullptr },
		{ "shrinks", state->shrinks }, { "restores", state->restores }, { "failures", state->failures },
		{ "cancelled", state->cancelled }, { "admissionDeferrals", state->admissionDeferrals },
		{ "diagnostics", DiagnosticStatus(state->diagnostics) },
		{ "demand", { { "eyeCount", state->demand.eyeCount }, { "sourceFrame", state->demand.frame },
						{ "sampledAtMs", state->demand.sampledAtMs }, { "mipBias", state->demand.mipBias } } },
		{ "previewActive", state->previewActive }, { "maximumPayloadMiB", Policy::MaximumPayloadBytes / Policy::MiB },
		{ "inventory", { { "scan", state->scan }, { "completeScan", state->completeScan }, { "frontierNodes", state->frontier.size() },
						   { "desiredShrinks", desiredShrinks }, { "desiredRestores", desiredRestores } } },
		{ "pressureEpisode", { { "active", state->pressureEpisode.active }, { "startedAtMs", state->pressureEpisode.startedAtMs },
								 { "summaryReported", state->pressureEpisode.summaryReported },
								 { "committedShrinks", state->pressureEpisode.active ? nlohmann::json(state->shrinks - state->pressureEpisode.initialShrinks) : nlohmann::json(nullptr) },
								 { "committedRestores", state->pressureEpisode.active ? nlohmann::json(state->restores - state->pressureEpisode.initialRestores) : nlohmann::json(nullptr) },
								 { "observations", state->pressureEpisode.active ? DiagnosticStatus(state->diagnostics, state->pressureEpisode.initial) : nlohmann::json(nullptr) } } },
		{ "reading", state->reading }, { "transactionActive", state->transaction.has_value() }, { "paused", state->paused && (state->enabled || !state->records.empty() || state->reading || state->transaction.has_value()) }, { "detail", state->detail },
		{ "memory", { { "valid", memory.Fresh(GetTickCount64()) }, { "budgetBytes", memory.local.Budget },
						{ "usageBytes", memory.local.CurrentUsage }, { "pendingBytes", memory.pendingBytes }, { "recoveryDemandBytes", memory.recoveryDemandBytes }, { "priorityWork", memory.priorityWork },
						{ "sampledAtMs", memory.sampledAtMs }, { "deviceGeneration", memory.generation }, { "sampleSequence", memory.sequence },
						{ "adapterLuidLow", memory.adapter.LowPart }, { "adapterLuidHigh", memory.adapter.HighPart } } } };
#ifdef DEVBENCH_BRIDGE_ENABLED
	const auto simulation = Memory::Get().GetStreamingPressure();
	const auto now = GetTickCount64();
	const bool simulationActive = simulation.Active(now, simulation.generation);
	result["pressureSimulation"] = { { "active", simulationActive }, { "headroomMiB", simulation.headroomMiB },
		{ "budgetBytes", simulation.budgetBytes }, { "startedAtMs", simulation.startedAtMs }, { "expiresAtMs", simulation.expiresAtMs },
		{ "remainingMilliseconds", simulationActive ? simulation.expiresAtMs - now : 0 }, { "deviceGeneration", simulation.generation } };
	result["memory"]["physicalBudgetBytes"] = memory.physicalBudgetBytes;
	result["memory"]["pressureSimulated"] = memory.pressureSimulated;
	const auto cache = state->reader.CacheStatus();
	result["transfers"] = { { "gpuCopyShrinks", state->gpuCopyShrinks }, { "cachedRestores", state->cachedRestores }, { "ddsRestores", state->ddsRestores },
		{ "sourceHeaderReads", state->sourceHeaderReads }, { "sourcePayloadReads", state->sourcePayloadReads }, { "sourceBytesRead", state->sourceBytesRead } };
	result["restorationCache"] = { { "limitBytes", cache.limitBytes }, { "bytes", cache.bytes }, { "entries", cache.entries }, { "generation", cache.generation },
		{ "hits", cache.hits }, { "misses", cache.misses }, { "evictions", cache.evictions }, { "storeFailures", cache.storeFailures } };
	result["debugForceStreaming"] = state->modes.continuous;
	result["materialDiagnostics"] = MaterialDiagnosticStatus(state->materialDiagnostics);
	const auto& terrain = state->terrainReduction;
	std::uint64_t terrainRecords = 0, terrainReduced = 0, terrainDesiredShrinks = 0;
	for (const auto& [id, record] : state->records)
		if (record.terrainConsumer) {
			++terrainRecords;
			terrainReduced += record.drop != 0;
			terrainDesiredShrinks += record.desired > record.drop;
		}
	result["terrainDiagnostics"] = {
		{ "records", terrainRecords }, { "reducedTextures", terrainReduced }, { "desiredShrinks", terrainDesiredShrinks },
		{ "inventory", DiagnosticStatus(state->terrainInventoryDiagnostics) },
		{ "exclusionSamples", DiagnosticSampleStatus(state->terrainExclusions, Diagnostics::exclusionNames) },
		{ "blockedSamples", DiagnosticSampleStatus(state->terrainBlocks, Diagnostics::blockNames) },
		{ "textures", TextureDiagnosticStatus(state->terrainTextureDiagnostics) },
		{ "demand", DemandDiagnosticStatus(state->terrainDemandDiagnostics) },
		{ "committedShrinks", state->terrainShrinks }, { "committedRestores", state->terrainRestores },
		{ "retiredReductions", state->terrainRetiredReductions }, { "retiredLogicalReductionBytes", state->terrainRetiredLogicalBytes },
		{ "materials", MaterialDiagnosticStatus(state->terrainMaterialDiagnostics) },
		{ "lastReduction", !state->terrainShrinks ? nlohmann::json(nullptr) : nlohmann::json{ { "texturePath", terrain.path.data() }, { "resourceSerial", terrain.serial }, { "requiredCategories", terrain.requiredCategories }, { "fullWidth", terrain.fullWidth }, { "fullHeight", terrain.fullHeight }, { "width", terrain.width }, { "height", terrain.height }, { "previousDrop", terrain.previousDrop }, { "drop", terrain.drop } } }
	};
	result["textureDiagnostics"] = TextureDiagnosticStatus(state->textureDiagnostics);
	result["demandDiagnostics"] = DemandDiagnosticStatus(state->demandDiagnostics);
#endif
	result["distanceReductionRequested"] = state->ReductionRequested();
	result["reductionRequested"] = state->enabled && (state->ReductionRequested() || state->options.maximumSize != 0);
	return result;
}
void TextureStreaming::DrawSettingsEnabledControl()
{
	bool enabled;
	std::uint32_t maximum;
	bool conflict;
	bool inventorySuspended;
	{
		std::scoped_lock lock(state->mutex);
		enabled = state->requestedEnabled;
		maximum = state->maximumDrop;
		conflict = state->externalStreamingConflict;
		inventorySuspended = !state->inventoryFailure.empty();
	}
	const auto disabled = Util::DisableGuard(conflict || inventorySuspended || !originHookReady.load());
	if (Util::Widgets::Checkbox("Enabled", &enabled))
		Configure(enabled, maximum);
	Util::AddTooltip("Turns texture streaming on or off while keeping your current tuning.", ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
}
void TextureStreaming::DrawSettings()
{
	bool enabled, conflict;
	StreamingTextures::Modes modes;
	int maximum;
	StreamingTextures::Options options;
	StreamingTextures::Categories categories;
	std::string detail;
	std::string inventoryFailure;
	std::uint64_t reduced, logicalBytesRemoved = 0;
	{
		std::scoped_lock lock(state->mutex);
		enabled = state->requestedEnabled;
		conflict = state->externalStreamingConflict;
		modes = state->modes;
		maximum = state->maximumDrop;
		options = state->options;
		categories = state->categories;
		detail = state->detail;
		inventoryFailure = state->inventoryFailure;
		reduced = state->reducedRecords.size();
		for (const auto id : state->reducedRecords)
			if (auto entry = state->records.find(id); entry != state->records.end()) {
				const auto& record = entry->second;
				logicalBytesRemoved += record.bytes[0] - record.bytes[record.drop];
			}
	}
	const bool available = originHookReady.load() && !conflict && inventoryFailure.empty();
	if (conflict)
		Util::Text::WrappedWarning(streamingConflictWarning);
	else if (!inventoryFailure.empty())
		Util::Text::WrappedWarning(inventoryFailure.c_str());
	else if (!available)
		Util::Text::WrappedWarning("Texture streaming is unavailable because texture source tracking could not start. Restart the game to try again.");
	const auto settingsAvailabilityGuard = Util::DisableGuard(!available || !enabled);
	MenuUI::SettingsPage page("TextureStreaming", {
													  { "quality", "Texture detail", "Set VRAM reserve and texture detail.", "Streaming controls", true, true, nullptr, nullptr, nullptr, available && enabled },
													  { "categories", "Coverage", "Optional texture types.", "Additional coverage", true, false, nullptr, nullptr, nullptr, available && enabled },
													  { "status", "Status", "Current streaming activity.", "Activity and capacity", true, true, nullptr },
												  });
	if (page.Is("quality")) {
		const auto previousModes = modes;
		const auto previousOptions = options;
		const auto previousMaximum = maximum;
		Util::Widgets::Checkbox("On pressure", &modes.onPressure);
		Util::AddTooltip("Stream distant textures when GPU memory is tight. Restores detail when pressure clears. On by default.");
		ImGui::Spacing();
		Util::Widgets::Checkbox("Always stream", &modes.continuous);
		Util::AddTooltip("Stream distant textures continuously, even with enough GPU memory. Nearby textures restore detail within the selected size cap. Saved between sessions.");
		ImGui::Spacing();
		int reserveSteps = options.reserveMiB / Util::TextureMemoryPolicy::ReserveStepMiB;
		const auto reserveLabel = std::format("{} MiB", options.reserveMiB);
		if (Util::Widgets::SliderInt("Keep free", &reserveSteps, 1, Util::TextureMemoryPolicy::MaximumReserveMiB / Util::TextureMemoryPolicy::ReserveStepMiB,
				reserveLabel.c_str(), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
			options.reserveMiB = std::clamp(reserveSteps, 1, static_cast<int>(Util::TextureMemoryPolicy::MaximumReserveMiB / Util::TextureMemoryPolicy::ReserveStepMiB)) * Util::TextureMemoryPolicy::ReserveStepMiB;
		Util::AddTooltip("Start pressure streaming below this free VRAM budget. Refills preserve the reserve; optional refills wait for another 1 GiB of headroom.");
		Util::Widgets::Checkbox("Remember sizes", &options.rememberedSizes);
		Util::AddTooltip("Reuse validated texture sizes across loads and sessions when streaming is active. New or changed sources load normally first.");
		constexpr std::array sizes{ 0u, 1024u, 2048u, 4096u, 8192u };
		int selectedSize = static_cast<int>(std::find(sizes.begin(), sizes.end(), options.maximumSize) - sizes.begin());
		if (Util::Widgets::Combo("Maximum size", &selectedSize,
				"Unlimited\0"
				"1K\0"
				"2K\0"
				"4K\0"
				"8K\0"))
			options.maximumSize = sizes[selectedSize];
		Util::AddTooltip("Caps eligible textures even nearby and with enough VRAM. Reduces fine detail. Protected textures stay full size; Unlimited preserves source detail.");
		Util::Widgets::SliderInt("Maximum mip levels removed", &maximum, 1, Policy::MaximumDrop, "%d", ImGuiSliderFlags_AlwaysClamp);
		maximum = std::clamp(maximum, 1, static_cast<int>(Policy::MaximumDrop));
		Util::AddTooltip("Limits distance-based reductions. The optional size cap can remove more mips, including nearby. Texture protections still apply.");
		if (modes != previousModes || options != previousOptions || maximum != previousMaximum)
			Configure(enabled, maximum, std::nullopt, modes, options);
	}
	if (page.Is("categories")) {
		const auto previous = categories;
		const std::array choices{
			MenuUI::ToggleChoice{ "Landscape", &categories.landscapeStatics, "Landscape-folder textures used by ordinary static meshes." },
			MenuUI::ToggleChoice{ "Cutouts", &categories.alphaTestedStatics, "Non-animated alpha-tested static surfaces. Lower detail can soften fine edges." },
			MenuUI::ToggleChoice{ "Emissive", &categories.emissiveStatics, "Diffuse and normal maps on static emissive surfaces. Glow maps remain protected." },
			MenuUI::ToggleChoice{ "Terrain", &categories.terrain, "Near-terrain diffuse and normal layers. Shared layers keep nearby detail. Parallax terrain also requires Parallax. LOD, overlays and noise remain excluded." },
			MenuUI::ToggleChoice{ "Parallax", &categories.parallax, "Experimental diffuse and normal streaming on parallax meshes and terrain. For terrain also enable Terrain. Height maps stay full size; appearance may change." },
		};
		for (const auto& choice : choices) {
			Util::Widgets::Checkbox(choice.label, choice.value);
			Util::AddTooltip(choice.tooltip, ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::Spacing();
		}
		if (categories != previous)
			Configure(enabled, maximum, categories);
	}
	if (page.Is("status")) {
		MenuUI::DetailNote(detail.c_str());
		MenuUI::DetailText(std::format("Reduced textures: {}", reduced).c_str());
		MenuUI::DetailText(std::format("Capacity removed: {:.1f} MiB", static_cast<double>(logicalBytesRemoved) / Policy::MiB).c_str());
		Util::AddTooltip("Logical texture capacity; physical VRAM relief may differ.");
	}
}
