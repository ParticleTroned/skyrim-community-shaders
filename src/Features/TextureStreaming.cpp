#include "TextureStreaming.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "GpuPass.h"
#include "State.h"
#include "TextureStreaming/ConsumerInventory.h"
#include "TextureStreaming/GeometryDemand.h"
#include "TextureStreaming/Settings.h"
#include "TextureStreaming/TextureData.h"
#include "Utils/Game.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/RendererContextAccess.h"
#include "Utils/ResourceName.h"
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

	std::optional<std::string> TexturePath(RE::NiSourceTexture* texture)
	{
		std::string path = texture->name.c_str();
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		if (!path.starts_with("textures\\"))
			path = "textures\\" + path;
		if (path.size() > 260 || !path.ends_with(".dds") || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
			return std::nullopt;
		for (const auto* excluded : { "actors\\", "landscape\\", "terrain\\", "effects\\", "interface\\", "cubemaps\\" })
			if (path.find(excluded) != std::string::npos)
				return std::nullopt;
		return path;
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
		bool published = false;
	};

	mutable std::mutex mutex;
	bool enabled = false;
	std::uint32_t maximumDrop = 2;
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
	StreamingTextures::DemandContext demand;
	std::uint64_t shrinks = 0, restores = 0, failures = 0, admissionDeferrals = 0, cancelled = 0;
	std::string detail = "Disabled";
	Memory::Snapshot memory;

	void ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, double metric, bool eligible);
	void Scan();
	void Prune();
	std::uint32_t Demand(Record& record);
	void Select();
	void ServiceTransaction();
	void CancelTransaction();
	void Publish(Record& record, Transaction& work);
	void Tick();
	void Fail(std::string reason)
	{
		++failures;
		detail = std::move(reason);
		logger::warn("[TextureStreaming] {}", detail);
	}
};

void TextureStreaming::State::ObserveTexture(RE::NiSourceTexture* source, RE::BSGeometry* geometry, double metric, bool eligible)
{
	if (!source || !source->rendererTexture || !source->rendererTexture->texture)
		return;
	auto* engine = source->rendererTexture;
	StreamingTextures::Origin origin;
	if (!StreamingTextures::ReadOrigin(engine->texture, origin))
		return;
	if (!eligible) {
		origin.protectedConsumer = true;
		if (FAILED(StreamingTextures::WriteOrigin(engine->texture, origin))) {
			Fail("Could not protect a special texture consumer");
			enabled = false;
		}
	}
	auto it = records.find(origin.serial);
	if (it == records.end()) {
		if (origin.protectedConsumer || !eligible || records.size() >= maximumRecords)
			return;
		const auto path = TexturePath(source);
		ComPtr<ID3D11Texture2D> texture;
		if (!path || engine->UAV || !engine->resourceView || FAILED(engine->texture->QueryInterface(IID_PPV_ARGS(&texture))))
			return;
		Record value;
		texture->GetDesc(&value.full);
		engine->resourceView->GetDesc(&value.view);
		if (!StreamingTextures::Suitable(value.full, value.view) || !StreamingTextures::LogicalBytes(value.full, 0) ||
			engine->width != value.full.Width || engine->height != value.full.Height || engine->mips != value.full.MipLevels)
			return;
		for (std::uint32_t drop = 0; drop < value.bytes.size(); ++drop)
			value.bytes[drop] = StreamingTextures::LogicalBytes(value.full, drop);
		value.source.reset(source);
		value.engine = engine;
		value.current = engine->texture;
		value.currentView = engine->resourceView;
		value.path = *path;
		it = records.emplace(origin.serial, std::move(value)).first;
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
		if (auto* reference = node.object->GetUserData()) {
			auto* base = reference->GetBaseObject();
			node.staticOwner = base && base->GetFormType() == RE::FormType::Static;
			node.protectedHierarchy |= !node.staticOwner;
		}
		node.protectedHierarchy |= node.object->GetControllers() != nullptr;
		if (auto* geometry = node.object->AsGeometry()) {
			auto* material = node.staticOwner && !node.protectedHierarchy ? StreamingTextures::StaticMaterial(geometry) : nullptr;
			const double metric = material ? StreamingTextures::UnitsPerUV(geometry, material) : 0;
			const bool eligible = material && std::isfinite(metric) && metric > 0;
			if (auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get()) {
				struct Visitor final : RE::BSShaderProperty::ForEachVisitor
				{
					State& state;
					RE::BSGeometry* geometry;
					RE::BSLightingShaderMaterialBase* material;
					double metric;
					bool eligible;
					Visitor(State& s, RE::BSGeometry* g, RE::BSLightingShaderMaterialBase* m, double u, bool e) :
						state(s), geometry(g), material(m), metric(u), eligible(e) {}
					std::uint32_t Accept(RE::NiSourceTexture* texture) override
					{
						const bool ordinarySlot = material && (texture == material->diffuseTexture.get() || texture == material->normalTexture.get());
						state.ObserveTexture(texture, geometry, metric, eligible && ordinarySlot);
						return 1;
					}
				} visitor(*this, geometry, material, metric, eligible);
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
		nextScanMs = GetTickCount64() + 250;
	}
}

std::uint32_t TextureStreaming::State::Demand(Record& record)
{
	record.consumers.Promote(completeScan);
	if (!enabled || !completeScan || record.protectedConsumer || record.consumers.verified != completeScan || record.consumers.committed.empty())
		return 0;
	StreamingTextures::Origin origin;
	if (record.source->rendererTexture != record.engine || record.engine->texture != record.current ||
		!StreamingTextures::ReadOrigin(record.current, origin) || origin.protectedConsumer)
		return 0;
	double required = 0;
	for (auto& consumer : record.consumers.committed) {
		const auto* material = StreamingTextures::StaticMaterial(consumer.geometry.get());
		if (!material || material->texCoordScale[0] != consumer.uvScale ||
			(material->diffuseTexture.get() != record.source.get() && material->normalTexture.get() != record.source.get()))
			return 0;
		required = std::max(required, StreamingTextures::RequiredEdge(consumer.geometry.get(), consumer.unitsPerUV, this->demand));
	}
	return Policy::DesiredDrop(record.full.Width, record.full.MipLevels, required, maximumDrop);
}

void TextureStreaming::State::Select()
{
	if (reading || transaction || !memory.Fresh(GetTickCount64()))
		return;
	std::uint64_t selected = 0, largestSaving = 0;
	bool selectedRefill = false;
	const auto now = GetTickCount64();
	const auto consider = [&](std::uint64_t id, Record& record) {
		if (record.source->rendererTexture != record.engine || record.engine->texture != record.current)
			return;
		const auto demanded = Demand(record);
		const auto target = pressure.conserving && enabled ? demanded : 0u;
		if (record.desired != target) {
			record.desired = target;
			record.stableSince = now;
		}
		if (record.drop == target || now < record.retryAfter)
			return;
		const bool refill = target < record.drop;
		if (refill && memory.priorityWork)
			return;
		if (!refill && (!completeScan || !frontier.empty() || now - record.stableSince < Policy::StableDemandMs))
			return;
		if (!record.drop && reducedRecords.size() >= maximumReducedTextures)
			return;
		const auto saving = record.bytes[record.drop] - std::min(record.bytes[record.drop], record.bytes[target]);
		if (!selected || (refill && !selectedRefill) || (refill == selectedRefill && saving > largestSaving)) {
			selected = id;
			selectedRefill = refill;
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
	if (selectedRefill && memory.priorityWork) {
		++admissionDeferrals;
		detail = "Refill waits for NR or render-scale recovery";
		return;
	}
	if (!Memory::StreamingFits(memory.local.Budget, memory.local.CurrentUsage, memory.pendingBytes,
			StreamingTextures::LogicalBytes(record.full, record.desired), selectedRefill, Demand(record) < record.drop, memory.priorityWork)) {
		record.retryAfter = now + 1000;
		++admissionDeferrals;
		return;
	}
	reader.Start({ selected, epoch, record.path, record.full, record.desired });
	reading = true;
	detail = selectedRefill ? "Reading restoration mips" : "Reading reduced mip chain";
}

void TextureStreaming::State::Publish(Record& record, Transaction& work)
{
	auto* context = globals::d3d::context;
	auto* old = record.engine->resourceView;
	const bool fullDetail = work.payload.request.drop == 0;
	auto* replacement = fullDetail ? work.upload.view.Get() : nullptr;
	bool specialBinding = false;
	specialBinding |= VisitBindings(context, &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::VSSetShaderResources, old, replacement);
	specialBinding |= VisitBindings(context, &ID3D11DeviceContext::HSGetShaderResources, &ID3D11DeviceContext::HSSetShaderResources, old, replacement);
	specialBinding |= VisitBindings(context, &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::DSSetShaderResources, old, replacement);
	specialBinding |= VisitBindings(context, &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::GSSetShaderResources, old, replacement);
	specialBinding |= VisitBindings(context, &ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::CSSetShaderResources, old, replacement);
	if (specialBinding && !fullDetail) {
		record.protectedConsumer = true;
		CancelTransaction();
		return;
	}
	// Complete fallible bookkeeping before transferring the engine's resource references.
	if (work.payload.request.drop)
		reducedRecords.insert(work.payload.request.serial);
	else
		reducedRecords.erase(work.payload.request.serial);
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
	record.drop = work.payload.request.drop;
	record.stableSince = GetTickCount64();
	context->End(work.retirement.Get());
	work.published = true;
	work.payload.image.Release();
	detail = "Waiting for old texture GPU retirement";
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
			if (memory.Fresh(GetTickCount64()))
				transaction.reset();
		} else if (FAILED(hr)) {
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
		CancelTransaction();
		return;
	}
	CS_GPU_PASS("TextureStreaming::Upload");
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
	pressure.Update(GetTickCount64(), memory.sampledAtMs, memory.Fresh(GetTickCount64()), memory.local.Budget,
		Memory::Add(memory.local.CurrentUsage, memory.pendingBytes));
	if (blocked) {
		CancelTransaction();
		ServiceTransaction();
		Prune();
		detail = "Waiting for world or render-scale transition";
		return;
	}
	if (enabled && originHookReady)
		Scan();
	ServiceTransaction();
	Select();
	Prune();
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
		{ "Static opaque DDS materials only", "Stereo and mip-bias aware detail", "Bounded restoration with shared NR and render-scale headroom" } };
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
void TextureStreaming::Configure(bool enabled, std::uint32_t maximumDrop)
{
	if (maximumDrop < 1 || maximumDrop > Policy::MaximumDrop)
		throw std::invalid_argument("MaximumMipDrop must be between 1 and 3");
	std::scoped_lock lock(state->mutex);
	state->enabled = enabled;
	state->maximumDrop = maximumDrop;
	state->detail = enabled ? "Monitoring GPU memory pressure" : "Restoring texture detail as headroom permits";
}
void TextureStreaming::LoadSettings(nlohmann::json& object)
{
	try {
		const auto maximum = StreamingTextures::ParseMaximumMipDrop(object.value("MaximumMipDrop", nlohmann::json(2)));
		Configure(object.value("Enabled", false), maximum);
	} catch (const std::exception& error) {
		logger::warn("[TextureStreaming] Invalid saved settings retained previous configuration: {}", error.what());
	}
}
void TextureStreaming::SaveSettings(nlohmann::json& object)
{
	std::scoped_lock lock(state->mutex);
	object = { { "Enabled", state->enabled }, { "MaximumMipDrop", state->maximumDrop } };
}
void TextureStreaming::RestoreDefaultSettings() { Configure(false, 2); }
nlohmann::json TextureStreaming::GetStatus() const
{
	std::scoped_lock lock(state->mutex);
	std::uint64_t full = 0, resident = 0, protectedCount = 0, reduced = 0;
	for (const auto& [id, record] : state->records) {
		full += record.bytes[0];
		resident += record.bytes[record.drop];
		protectedCount += record.protectedConsumer;
		reduced += record.drop != 0;
	}
	const auto& memory = state->memory;
	return { { "enabled", state->enabled }, { "maximumMipDrop", state->maximumDrop }, { "originHookReady", originHookReady.load() },
		{ "conserving", state->pressure.conserving }, { "records", state->records.size() }, { "protectedRecords", protectedCount },
		{ "reducedTextures", reduced }, { "fullLogicalBytes", full }, { "residentLogicalBytes", resident },
		{ "logicalBytesRemoved", full - resident }, { "physicalBytesReclaimed", nullptr },
		{ "shrinks", state->shrinks }, { "restores", state->restores }, { "failures", state->failures },
		{ "cancelled", state->cancelled }, { "admissionDeferrals", state->admissionDeferrals },
		{ "reading", state->reading }, { "transactionActive", state->transaction.has_value() }, { "detail", state->detail },
		{ "memory", { { "valid", memory.Fresh(GetTickCount64()) }, { "budgetBytes", memory.local.Budget },
						{ "usageBytes", memory.local.CurrentUsage }, { "pendingBytes", memory.pendingBytes }, { "priorityWork", memory.priorityWork },
						{ "sampledAtMs", memory.sampledAtMs }, { "deviceGeneration", memory.generation }, { "sampleSequence", memory.sequence },
						{ "adapterLuidLow", memory.adapter.LowPart }, { "adapterLuidHigh", memory.adapter.HighPart } } } };
}
void TextureStreaming::DrawSettingsEnabledControl()
{
	auto status = GetStatus();
	bool enabled = status["enabled"];
	if (ImGui::Checkbox("Enable texture streaming", &enabled))
		Configure(enabled, status["maximumMipDrop"]);
}
void TextureStreaming::DrawSettings()
{
	const auto status = GetStatus();
	int maximum = status["maximumMipDrop"];
	if (ImGui::SliderInt("Maximum mip levels removed", &maximum, 1, Policy::MaximumDrop))
		Configure(status["enabled"], maximum);
	ImGui::TextWrapped("Only suitable static opaque DDS materials are streamed. Detail follows both eyes, output resolution and shader mip bias. Neural Rendering scale does not lower texture detail.");
	ImGui::TextWrapped("Disabling restores textures gradually as memory headroom permits.");
	if (!status["originHookReady"].get<bool>())
		ImGui::TextWrapped("DDS provenance is unavailable; streaming is inactive.");
	ImGui::TextWrapped("%s", status["detail"].get<std::string>().c_str());
	ImGui::Text("Reduced textures: %llu", status["reducedTextures"].get<std::uint64_t>());
	ImGui::Text("Logical texture capacity removed: %.1f MiB", status["logicalBytesRemoved"].get<double>() / Policy::MiB);
	ImGui::TextWrapped("Logical capacity is not a measurement of physical VRAM reclaimed.");
}
