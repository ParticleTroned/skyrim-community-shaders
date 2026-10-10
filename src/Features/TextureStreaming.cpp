#include "TextureStreaming.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "Menu/SettingsPage.h"
#include "State.h"
#include "TextureStreaming/ConsumerInventory.h"
#include "TextureStreaming/Diagnostics.h"
#include "TextureStreaming/GeometryDemand.h"
#include "TextureStreaming/Settings.h"
#include "TextureStreaming/TextureData.h"
#include "Utils/Game.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/ResourceName.h"
#include "Utils/UI.h"
#include "Utils/VirtualFunctionHook.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
	constexpr std::size_t maximumRecords = 16384, maximumConsumers = 256, maximumFrontier = 16384;
	constexpr std::size_t maximumReducedTextures = 512;
	constexpr std::uint64_t uploadBytesPerFrame = 4 * Policy::MiB;
	std::atomic_bool originHookReady{ false };
	std::atomic_uint64_t originSerial{ 0 };
	using DDSLoader = std::int32_t (*)(void*, void*, RE::BSGraphics::Texture**, void*, std::uint64_t, std::uint64_t);
	DDSLoader originalDDSLoader = nullptr;

	std::int32_t LoadDDS(void* device, void* stream, RE::BSGraphics::Texture** output, void* header, std::uint64_t maximum, std::uint64_t flags)
	{
		const auto result = originalDDSLoader(device, stream, output, header, maximum, flags);
		if (result >= 0 && output && *output && (*output)->texture) {
			const StreamingTextures::Origin origin{ ++originSerial, false };
			const auto status = StreamingTextures::WriteOrigin((*output)->texture, origin);
			if (FAILED(status))
				logger::warn("[TextureStreaming] DDS provenance unavailable: 0x{:08X}", static_cast<std::uint32_t>(status));
		}
		return result;
	}

	std::optional<std::string> TexturePath(RE::NiSourceTexture* texture, std::uint32_t& categories)
	{
		std::string path = texture->name.c_str();
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		if (!path.starts_with("textures\\"))
			path = "textures\\" + path;
		const auto required = StreamingTextures::PathCategories(path);
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

	struct ReadRequest
	{
		std::uint64_t serial = 0, epoch = 0;
		std::string path;
		D3D11_TEXTURE2D_DESC full{};
		std::uint32_t drop = 0;
	};
	struct ReadResult
	{
		ReadRequest request;
		DirectX::ScratchImage image;
		std::string error;
	};

	ReadResult ReadDDS(ReadRequest request)
	{
		ReadResult result;
		result.request = std::move(request);
		try {
			RE::BSResourceNiBinaryStream stream(result.request.path);
			if (!stream.good() || !stream.stream)
				throw std::runtime_error("DDS source is unavailable in the game resource namespace");
			const auto size = stream.stream->totalSize;
			if (size < 128 || size > Policy::MaximumPayloadBytes)
				throw std::runtime_error("DDS payload exceeds the bounded reader contract");
			MEMORYSTATUSEX memory{ sizeof(memory) };
			if (!GlobalMemoryStatusEx(&memory) || memory.ullAvailPageFile < 512 * Policy::MiB + 2 * size)
				throw std::runtime_error("Insufficient system commit headroom for DDS restoration");
			std::vector<std::uint8_t> bytes(size);
			if (!stream.read(bytes.data(), size))
				throw std::runtime_error("DDS source read was incomplete");
			DirectX::TexMetadata metadata{};
			auto hr = DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, metadata);
			const auto& full = result.request.full;
			if (FAILED(hr) || metadata.width != full.Width || metadata.height != full.Height || metadata.depth != 1 ||
				metadata.arraySize != 1 || metadata.mipLevels != full.MipLevels || metadata.format != full.Format ||
				metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.IsCubemap())
				throw std::runtime_error("DDS source no longer matches the original texture contract");
			hr = DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, result.image);
			if (FAILED(hr) || !StreamingTextures::MatchesDDS(result.image, full))
				throw std::runtime_error("DDS mip chain is invalid or incomplete");
		} catch (const std::exception& error) {
			result.image.Release();
			result.error = error.what();
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
			std::scoped_lock lock(mutex);
			input = std::move(request);
			if (!worker.joinable()) {
				worker = std::jthread([this](std::stop_token stop) {
					while (!stop.stop_requested()) {
						std::unique_lock lock(mutex);
						wake.wait(lock, stop, [&] { return input.has_value(); });
						if (stop.stop_requested())
							return;
						auto request = std::move(*input);
						input.reset();
						lock.unlock();
						auto value = ReadDDS(std::move(request));
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

	private:
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
		double unitsPerUV = 0;
		RE::NiPoint2 uvScale{};
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
		std::uint64_t stableSince = 0, retryAfter = 0;
		std::uint32_t drop = 0, desired = 0;
		bool protectedConsumer = false;
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
	};

	mutable std::mutex mutex;
	bool enabled = false;
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
	std::string detail = "Disabled";
	Memory::Snapshot memory;

	void ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, double metric, bool eligible, std::uint32_t requiredCategories);
	void Scan();
	void Prune();
	std::uint32_t Demand(Record& record);
	void Select();
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

void TextureStreaming::State::ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, double metric, bool eligible, std::uint32_t requiredCategories)
{
	++diagnostics.textureObservations;
	if (!source || !source->rendererTexture || !source->rendererTexture->texture) {
		diagnostics.Exclude(Exclusion::MissingTexture);
		return;
	}
	auto* engine = source->rendererTexture;
	StreamingTextures::Origin origin;
	if (!StreamingTextures::ReadOrigin(engine->texture, origin)) {
		diagnostics.Exclude(Exclusion::MissingProvenance);
		return;
	}
	if ((!eligible && !origin.protectedConsumer) || (requiredCategories & ~origin.requiredCategories)) {
		origin.protectedConsumer |= !eligible;
		origin.requiredCategories |= requiredCategories;
		if (FAILED(StreamingTextures::WriteOrigin(engine->texture, origin))) {
			Fail("Could not preserve texture consumer requirements");
			enabled = false;
			return;
		}
	}
	auto it = records.find(origin.serial);
	if (it == records.end()) {
		if (origin.protectedConsumer || !eligible) {
			diagnostics.Exclude(Exclusion::ProtectedConsumer);
			return;
		}
		if (records.size() >= maximumRecords) {
			diagnostics.Exclude(Exclusion::InventoryLimit);
			return;
		}
		std::uint32_t pathCategories = 0;
		const auto path = TexturePath(source, pathCategories);
		if (!path) {
			diagnostics.Exclude(Exclusion::ProtectedPath);
			return;
		}
		if (pathCategories & ~origin.requiredCategories) {
			origin.requiredCategories |= pathCategories;
			if (FAILED(StreamingTextures::WriteOrigin(engine->texture, origin))) {
				Fail("Could not preserve texture path requirements");
				enabled = false;
				return;
			}
		}
		if (!categories.Allows(origin.requiredCategories)) {
			diagnostics.Exclude(Exclusion::CategoryDisabled);
			return;
		}
		ComPtr<ID3D11Texture2D> texture;
		if (engine->UAV || !engine->resourceView || FAILED(engine->texture->QueryInterface(IID_PPV_ARGS(&texture)))) {
			diagnostics.Exclude(Exclusion::TextureContract);
			return;
		}
		Record value;
		texture->GetDesc(&value.full);
		engine->resourceView->GetDesc(&value.view);
		if (!StreamingTextures::Suitable(value.full, value.view) || !StreamingTextures::LogicalBytes(value.full, 0) ||
			engine->width != value.full.Width || engine->height != value.full.Height || engine->mips != value.full.MipLevels) {
			diagnostics.Exclude(Exclusion::TextureContract);
			return;
		}
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
	record.consumers.Observe(scan, completeScan);
	record.protectedConsumer |= origin.protectedConsumer || !eligible || record.engine != engine;
	if (!record.protectedConsumer && record.consumers.scanning.size() < maximumConsumers)
		record.consumers.scanning.push_back({ RE::NiPointer<RE::BSGeometry>(geometry), metric, geometry->GetGeometryRuntimeData().shaderProperty->material->texCoordScale[0] });
	else if (record.consumers.scanning.size() >= maximumConsumers)
		record.protectedConsumer = true;
}

void TextureStreaming::State::Scan()
{
	if (frontier.empty()) {
		if (GetTickCount64() < nextScanMs)
			return;
		auto* root = RE::Main::WorldRootNode();
		if (!root)
			return;
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
		if (auto* reference = node.object->GetUserData()) {
			auto* base = reference->GetBaseObject();
			node.staticOwner = base && base->GetFormType() == RE::FormType::Static;
			node.protectedHierarchy |= !node.staticOwner;
		}
		node.protectedHierarchy |= node.object->GetControllers() != nullptr;
		if (auto* geometry = node.object->AsGeometry()) {
			++diagnostics.geometryObservations;
			std::uint32_t requiredCategories = 0;
			auto* material = node.staticOwner && !node.protectedHierarchy ? StreamingTextures::StaticMaterial(geometry, &requiredCategories) : nullptr;
			const bool categoryEnabled = categories.Allows(requiredCategories);
			const double metric = material && categoryEnabled ? StreamingTextures::UnitsPerUV(geometry, material) : 0;
			const bool measurable = material && std::isfinite(metric) && metric > 0;
			const bool eligible = material && (!categoryEnabled || measurable);
			diagnostics.staticGeometryObservations += node.staticOwner && !node.protectedHierarchy;
			diagnostics.supportedMaterialObservations += material != nullptr;
			diagnostics.measurableGeometryObservations += measurable;
			if (auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get()) {
				struct Visitor final : RE::BSShaderProperty::ForEachVisitor
				{
					State& state;
					RE::BSGeometry* geometry;
					RE::BSLightingShaderMaterialBase* material;
					double metric;
					bool eligible;
					std::uint32_t requiredCategories;
					Visitor(State& s, RE::BSGeometry* g, RE::BSLightingShaderMaterialBase* m, double u, bool e, std::uint32_t c) :
						state(s), geometry(g), material(m), metric(u), eligible(e), requiredCategories(c) {}
					std::uint32_t Accept(RE::NiSourceTexture* texture) override
					{
						const bool ordinarySlot = material && (texture == material->diffuseTexture.get() || texture == material->normalTexture.get());
						state.ObserveTexture(texture, geometry, metric, eligible && ordinarySlot, requiredCategories);
						return 1;
					}
				} visitor(*this, geometry, material, metric, eligible, requiredCategories);
				property->ForEachTexture(visitor);
			}
		}
		if (auto* branch = node.object->AsNode())
			for (auto& child : branch->GetChildren())
				if (child)
					frontier.push_back({ child, node.staticOwner, node.protectedHierarchy });
		if (frontier.size() > maximumFrontier || visited.size() > 131072) {
			frontier.clear();
			completeScan = 0;
			nextScanMs = GetTickCount64() + 10000;
			Fail("Scene exceeds the bounded inventory; shrinking is suspended");
			return;
		}
	}
	if (frontier.empty()) {
		completeScan = scan;
		++diagnostics.scansCompleted;
		nextScanMs = GetTickCount64() + 250;
	}
}

std::uint32_t TextureStreaming::State::Demand(Record& record)
{
	++diagnostics.demandChecks;
	record.consumers.Promote(completeScan);
	if (!enabled)
		return 0;
	if (!completeScan || record.consumers.verified != completeScan || record.consumers.committed.empty()) {
		diagnostics.Defer(Block::InventoryIncomplete);
		return 0;
	}
	if (record.protectedConsumer) {
		diagnostics.Defer(Block::ProtectedConsumer);
		return 0;
	}
	StreamingTextures::Origin origin;
	if (record.source->rendererTexture != record.engine || record.engine->texture != record.current ||
		!StreamingTextures::ReadOrigin(record.current, origin) || origin.protectedConsumer) {
		diagnostics.Defer(Block::SourceChanged);
		return 0;
	}
	if (!categories.Allows(origin.requiredCategories)) {
		diagnostics.Defer(Block::CategoryDisabled);
		return 0;
	}
	double required = 0;
	for (auto& consumer : record.consumers.committed) {
		std::uint32_t requiredCategories = 0;
		const auto* material = StreamingTextures::StaticMaterial(consumer.geometry.get(), &requiredCategories);
		if (!material || !categories.Allows(requiredCategories) || material->texCoordScale[0] != consumer.uvScale ||
			(material->diffuseTexture.get() != record.source.get() && material->normalTexture.get() != record.source.get())) {
			diagnostics.Defer(Block::ConsumerContract);
			return 0;
		}
		required = std::max(required, StreamingTextures::RequiredEdge(consumer.geometry.get(), consumer.unitsPerUV, this->demand));
	}
	const auto drop = Policy::DesiredDrop(record.full.Width, record.full.MipLevels, required, maximumDrop);
	if (!drop)
		diagnostics.Defer(Block::FullDetailDemand);
	return drop;
}

void TextureStreaming::State::Select()
{
	if (reading || transaction || !memory.Fresh(GetTickCount64())) {
		diagnostics.Defer(reading ? Block::ReaderPending : transaction ? (transaction->published ? Block::RetirementPending : Block::UploadPending) :
																		 Block::StaleBudget);
		return;
	}
	std::uint64_t selected = 0, largestSaving = 0;
	bool selectedRefill = false, selectedRequired = false;
	const auto now = GetTickCount64();
	const auto consider = [&](std::uint64_t id, Record& record) {
		if (record.source->rendererTexture != record.engine || record.engine->texture != record.current) {
			diagnostics.Defer(Block::SourceChanged);
			return;
		}
		const auto demanded = Demand(record);
		const auto target = pressure.conserving && enabled ? demanded : 0u;
		if (record.desired != target) {
			record.desired = target;
			record.stableSince = now;
		}
		if (record.drop == target)
			return;
		if (now < record.retryAfter) {
			diagnostics.Defer(Block::RetryBackoff);
			return;
		}
		const bool refill = target < record.drop;
		const bool required = demanded < record.drop;
		if (!refill)
			++diagnostics.shrinkCandidates;
		else if (required)
			++diagnostics.requiredRestoreCandidates;
		if (refill && memory.priorityWork) {
			diagnostics.Defer(Block::PriorityWork);
			return;
		}
		if (!refill && (!completeScan || !frontier.empty() || now - record.stableSince < Policy::StableDemandMs)) {
			diagnostics.Defer(!completeScan || !frontier.empty() ? Block::InventoryIncomplete : Block::DemandSettling);
			return;
		}
		if (!record.drop && reducedRecords.size() >= maximumReducedTextures) {
			diagnostics.Defer(Block::ReducedLimit);
			return;
		}
		if (!Memory::StreamingFits(memory.local.Budget, memory.local.CurrentUsage, memory.pendingBytes,
				record.bytes[target], refill, required, memory.priorityWork, memory.recoveryDemandBytes)) {
			const bool softDemandBlocked = memory.recoveryDemandBytes && Memory::StreamingFits(memory.local.Budget,
																			 memory.local.CurrentUsage, memory.pendingBytes, record.bytes[target], refill, required, memory.priorityWork);
			diagnostics.Defer(softDemandBlocked ? Block::RecoveryHeadroom : Block::ReplacementHeadroom);
			record.retryAfter = now + 1000;
			++admissionDeferrals;
			return;
		}
		const auto saving = record.bytes[record.drop] - std::min(record.bytes[record.drop], record.bytes[target]);
		const auto rank = refill ? (required ? 2 : 1) : 0;
		const auto selectedRank = selectedRefill ? (selectedRequired ? 2 : 1) : 0;
		if (!selected || rank > selectedRank || (rank == selectedRank && saving > largestSaving)) {
			selected = id;
			selectedRefill = refill;
			selectedRequired = required;
			largestSaving = saving;
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
	reader.Start({ selected, epoch, record.path, record.full, record.desired });
	reading = true;
	detail = selectedRefill ? "Reading restoration mips" : "Reading reduced mip chain";
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
	record.engine->width = static_cast<std::uint16_t>(work.upload.desc.Width);
	record.engine->height = static_cast<std::uint16_t>(work.upload.desc.Height);
	record.engine->mips = static_cast<std::uint8_t>(work.upload.desc.MipLevels);
	record.current = record.engine->texture;
	record.currentView = record.engine->resourceView;
	if (work.payload.request.drop < record.drop)
		++restores;
	else
		++shrinks;
	const auto previousDrop = record.drop;
	record.drop = work.payload.request.drop;
	record.stableSince = GetTickCount64();
	context->End(work.retirement.Get());
	work.published = true;
	work.replacementCommitted = true;
	work.logicalReductionBytes = record.bytes[previousDrop] - std::min(record.bytes[previousDrop], record.bytes[record.drop]);
	work.payload.image.Release();
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
		work.payload.image.Release();
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
			if (result->request.epoch != epoch || !records.contains(result->request.serial)) {
				++cancelled;
			} else if (!result->error.empty()) {
				records.at(result->request.serial).retryAfter = GetTickCount64() + 10000;
				Fail(result->error);
			} else {
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
					}
				} else {
					++diagnostics.retiredCancelledUploads;
				}
				transaction.reset();
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
	if (record.source->rendererTexture != record.engine || record.engine->texture != record.current || record.engine->resourceView != record.currentView || record.engine->UAV || target > Demand(record) ||
		(target > record.drop && !pressure.conserving)) {
		CancelTransaction();
		return;
	}
	if (!work.reservation) {
		const bool refill = target < record.drop;
		work.reservation = Memory::Get().TryReserveStreaming(globals::d3d::device, StreamingTextures::LogicalBytes(record.full, target), refill, Demand(record) < record.drop);
		if (!work.reservation) {
			++admissionDeferrals;
			diagnostics.Defer(Block::ReplacementHeadroom);
			detail = "Replacement overlap waits for shared memory headroom";
			record.retryAfter = GetTickCount64() + 1000;
			transaction.reset();
			return;
		}
		auto hr = work.upload.Begin(globals::d3d::device, record.full, record.view, target);
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
	const auto hr = work.upload.Advance(context, work.payload.image, uploadBytesPerFrame);
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
	if (!enabled && records.empty() && !reading && !transaction) {
		detail = "Disabled";
		return;
	}
	Util::RendererOwnership owner(Util::GetRendererContextLock(globals::game::renderer, globals::d3d::context));
	if (!owner)
		return;
	memory = Memory::Get().Sample(globals::d3d::device);
	const bool transition = globals::features::upscaling.IsTextureStreamingTransitionActive();
	memory.priorityWork |= transition;
	const auto start = globals::state->worldLoadTransitionStartFrame.load(std::memory_order_acquire);
	const bool loading = globals::state->IsSaveLoadSafeModeActive();
	const bool blocked = loading || transition;
	if (start != worldStart || blocked != paused || deviceGeneration != memory.generation) {
		++epoch;
		worldStart = start;
		paused = blocked;
		if (deviceGeneration && deviceGeneration != memory.generation) {
			transaction.reset();
			records.clear();
			reducedRecords.clear();
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
		Memory::Add(memory.local.CurrentUsage, memory.pendingBytes));
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
		CancelTransaction();
		ServiceTransaction();
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
	return { "Reduces resident texture mips when GPU memory is under pressure.",
		{ "Conservative static DDS coverage with optional categories", "Stereo and mip-bias aware detail", "Bounded restoration with shared NR and render-scale headroom" } };
}
void TextureStreaming::PostPostLoad()
{
	if (originHookReady.load())
		return;
	try {
		originalDDSLoader = reinterpret_cast<DDSLoader>(REL::Relocation<std::uintptr_t>{ REL::VariantID(75721, 77533, 0xDD2B60) }.address());
		const auto result = Util::AttachDetour(reinterpret_cast<void**>(&originalDDSLoader), reinterpret_cast<void*>(&LoadDDS));
		originHookReady.store(result.error == NO_ERROR, std::memory_order_release);
		if (result.error != NO_ERROR)
			logger::error("[TextureStreaming] DDS provenance hook failed ({}); streaming unavailable", result.error);
	} catch (const std::exception& error) {
		logger::error("[TextureStreaming] DDS provenance unavailable: {}", error.what());
	}
}
void TextureStreaming::Reset()
{
	std::scoped_lock lock(state->mutex);
	try {
		state->Tick();
	} catch (const std::exception& error) {
		state->enabled = false;
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
void TextureStreaming::Configure(bool enabled, std::uint32_t maximumDrop, std::optional<StreamingTextures::Categories> categories)
{
	if (maximumDrop < 1 || maximumDrop > Policy::MaximumDrop)
		throw std::invalid_argument("MaximumMipDrop must be between 1 and 3");
	std::scoped_lock lock(state->mutex);
	if (categories && state->categories != *categories) {
		state->categories = *categories;
		++state->epoch;
		state->frontier.clear();
		state->visited.clear();
		state->completeScan = 0;
		state->nextScanMs = 0;
		for (auto& [id, record] : state->records)
			record.consumers.Clear();
	}
	state->enabled = enabled;
	if (!enabled)
		state->UpdatePressureDiagnostics(GetTickCount64());
	state->maximumDrop = maximumDrop;
	state->detail = enabled ? "Monitoring GPU memory pressure" : "Restoring texture detail as headroom permits";
}
void TextureStreaming::LoadSettings(nlohmann::json& object)
{
	try {
		const auto maximum = StreamingTextures::ParseMaximumMipDrop(object.value("MaximumMipDrop", nlohmann::json(2)));
		Configure(object.value("Enabled", false), maximum, StreamingTextures::ParseCategories(object.value("Categories", nlohmann::json::object())));
	} catch (const std::exception& error) {
		logger::warn("[TextureStreaming] Invalid saved settings retained previous configuration: {}", error.what());
	}
}
void TextureStreaming::SaveSettings(nlohmann::json& object)
{
	std::scoped_lock lock(state->mutex);
	object = { { "Enabled", state->enabled }, { "MaximumMipDrop", state->maximumDrop },
		{ "Categories", StreamingTextures::SerializeCategories(state->categories) } };
}
void TextureStreaming::RestoreDefaultSettings() { Configure(false, 2, StreamingTextures::Categories{}); }
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
	return { { "enabled", state->enabled }, { "maximumMipDrop", state->maximumDrop },
		{ "categories", StreamingTextures::SerializeCategories(state->categories) }, { "originHookReady", originHookReady.load() },
		{ "conserving", state->pressure.conserving }, { "records", state->records.size() }, { "protectedRecords", protectedCount },
		{ "reducedTextures", reduced }, { "fullLogicalBytes", full }, { "residentLogicalBytes", resident },
		{ "logicalBytesRemoved", full - resident }, { "physicalBytesReclaimed", nullptr },
		{ "shrinks", state->shrinks }, { "restores", state->restores }, { "failures", state->failures },
		{ "cancelled", state->cancelled }, { "admissionDeferrals", state->admissionDeferrals },
		{ "diagnostics", DiagnosticStatus(state->diagnostics) },
		{ "demand", { { "eyeCount", state->demand.eyeCount }, { "sourceFrame", state->demand.frame },
						{ "sampledAtMs", state->demand.sampledAtMs }, { "mipBias", state->demand.mipBias } } },
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
}
void TextureStreaming::DrawSettingsEnabledControl()
{
	bool enabled;
	std::uint32_t maximum;
	{
		std::scoped_lock lock(state->mutex);
		enabled = state->enabled;
		maximum = state->maximumDrop;
	}
	if (Util::Widgets::Checkbox("Enabled", &enabled))
		Configure(enabled, maximum);
}
void TextureStreaming::DrawSettings()
{
	bool enabled;
	int maximum;
	StreamingTextures::Categories categories;
	std::string detail;
	std::uint64_t reduced, logicalBytesRemoved = 0;
	{
		std::scoped_lock lock(state->mutex);
		enabled = state->enabled;
		maximum = state->maximumDrop;
		categories = state->categories;
		detail = state->detail;
		reduced = state->reducedRecords.size();
		for (const auto id : state->reducedRecords)
			if (auto entry = state->records.find(id); entry != state->records.end()) {
				const auto& record = entry->second;
				logicalBytesRemoved += record.bytes[0] - record.bytes[record.drop];
			}
	}
	const bool available = originHookReady.load();
	if (!available)
		Util::Text::WrappedWarning("Texture streaming is unavailable because texture source tracking could not start. Restart the game to try again.");
	MenuUI::SettingsPage page("TextureStreaming", {
													  { "quality", "Texture detail", "Choose how much texture detail can be reduced under memory pressure.", "Detail limit and restoration", true, true, "Balance detail and memory", nullptr, nullptr, available },
													  { "categories", "Optional categories", "Enable additional static texture categories individually.", "Controlled coverage tests", true, false, nullptr, nullptr, nullptr, available },
													  { "status", "Status", "Inspect streaming activity and logical texture capacity.", "Activity and memory accounting", true, true, nullptr },
												  });
	if (page.Is("quality")) {
		{
			const auto disabled = Util::DisableGuard(!available);
			if (Util::Widgets::SliderInt("Maximum mip levels removed", &maximum, 1, Policy::MaximumDrop))
				Configure(enabled, maximum);
		}
		MenuUI::DetailNote("Detail follows both eyes, output resolution and shader mip bias. Neural Rendering scale does not lower texture detail.");
		MenuUI::DetailText("Streams suitable static DDS materials within the enabled categories. Disabling restores textures gradually as memory permits.");
	}
	if (page.Is("categories")) {
		const auto disabled = Util::DisableGuard(!available);
		bool changed = Util::Widgets::Checkbox("Landscape textures on static meshes", &categories.landscapeStatics);
		MenuUI::DetailText("Includes ordinary static meshes using landscape-folder textures. Terrain and distant LOD remain protected.");
		changed |= Util::Widgets::Checkbox("Alpha-tested static surfaces", &categories.alphaTestedStatics);
		MenuUI::DetailText("Includes non-animated cutout surfaces. Reduced mips can change fine edges; blended transparency remains protected.");
		changed |= Util::Widgets::Checkbox("Emissive static surfaces", &categories.emissiveStatics);
		MenuUI::DetailText("Includes diffuse and normal maps on static emissive materials. Glow maps remain protected.");
		if (changed)
			Configure(enabled, maximum, categories);
		MenuUI::DetailNote("All optional categories default off. Turning one off queues restoration as memory permits.");
		MenuUI::DetailText("Characters, clothing, skin, terrain, LOD, water, parallax and runtime-generated textures remain protected.");
	}
	if (page.Is("status")) {
		MenuUI::DetailNote(detail.c_str());
		{
			MenuUI::DetailGrid grid("StreamingStatus");
			grid.Next();
			MenuUI::SectionHeading("Texture activity");
			MenuUI::DetailText(std::format("Reduced textures: {}", reduced).c_str());
			grid.Next();
			MenuUI::SectionHeading("Logical capacity");
			MenuUI::DetailText(std::format("Capacity removed: {:.1f} MiB", static_cast<double>(logicalBytesRemoved) / Policy::MiB).c_str());
		}
		MenuUI::DetailText("Logical capacity is not a measurement of physical VRAM reclaimed.");
	}
}
