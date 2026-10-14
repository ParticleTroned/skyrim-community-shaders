#include "LoadHooks.h"
#include "Utils/FileSystem.h"
#include "Utils/GpuMemoryBudget.h"
#include "Utils/NativeTextureLoading.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace StreamingTextures::LoadHooks
{
	namespace
	{
		constexpr std::size_t maximumEntries = 4096, maximumLoads = 512;
		constexpr std::uint64_t maximumFileBytes = 8 * TextureStreamingPolicy::MiB;
		struct Catalog
		{
			std::mutex mutex;
			std::condition_variable_any wake;
			std::unordered_map<std::string, RememberedSize> entries;
			std::unordered_set<std::string> protectedPaths;
			std::array<std::optional<ReducedLoad>, maximumLoads> pending;
			std::size_t read = 0, write = 0;
			std::shared_ptr<std::atomic_size_t> activeLoads = std::make_shared<std::atomic_size_t>(0);
			std::uint64_t revision = 0, savedRevision = 0, hits = 0, rejected = 0, failures = 0;
			bool enabled = true, ready = false, hookReady = false, protectionLimit = false;
			Modes modes;
			Options options;
			Categories categories;
			std::uint32_t maximumDrop = 2;
			std::filesystem::path path;
			std::string error;
			std::jthread worker;

			~Catalog()
			{
				worker.request_stop();
				wake.notify_all();
				if (worker.joinable())
					worker.join();
			}
			void Save()
			{
				std::vector<std::pair<std::string, RememberedSize>> snapshot;
				std::filesystem::path destination;
				std::uint64_t version;
				{
					std::scoped_lock lock(mutex);
					if (revision == savedRevision || !ready)
						return;
					version = revision;
					destination = path;
					snapshot.reserve(entries.size());
					for (const auto& entry : entries)
						snapshot.push_back(entry);
				}
				nlohmann::json document = { { "version", 1 }, { "entries", nlohmann::json::object() } };
				for (const auto& [name, value] : snapshot)
					document["entries"][name] = SerializeRememberedSize(value);
				std::string message;
				if (!Util::FileHelpers::WriteTextFileAtomic(destination, document.dump(), message, false)) {
					std::scoped_lock lock(mutex);
					error = message;
					++failures;
					logger::warn("[TextureStreaming] Remembered sizes could not be saved: {}", message);
					return;
				}
				std::scoped_lock lock(mutex);
				savedRevision = version;
				error.clear();
			}
			void Run(std::stop_token stop)
			{
				try {
					std::error_code ec;
					const auto size = std::filesystem::file_size(path, ec);
					if (!ec) {
						if (size > maximumFileBytes)
							throw std::runtime_error("Remembered-size file exceeds its bounded size");
						std::ifstream input(path, std::ios::binary);
						if (!input)
							throw std::runtime_error("Remembered-size file is unreadable");
						std::string bytes(static_cast<std::size_t>(size), '\0');
						if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
							throw std::runtime_error("Remembered-size file read was incomplete");
						const auto document = nlohmann::json::parse(bytes);
						if (document.value("version", 0) != 1 || !document.contains("entries") ||
							!document["entries"].is_object() || document["entries"].size() > maximumEntries)
							throw std::runtime_error("Remembered-size file has an unsupported schema");
						std::scoped_lock lock(mutex);
						for (const auto& [name, value] : document["entries"].items())
							if (const auto entry = ParseRememberedSize(name, value); entry && entries.size() < maximumEntries && !protectedPaths.contains(name))
								entries.try_emplace(name, *entry);
					} else if (ec != std::errc::no_such_file_or_directory) {
						throw std::runtime_error(ec.message());
					}
				} catch (const std::exception& exception) {
					std::scoped_lock lock(mutex);
					error = exception.what();
					++failures;
					logger::warn("[TextureStreaming] Remembered sizes ignored: {}", error);
				}
				{
					std::scoped_lock lock(mutex);
					ready = true;
				}
				while (true) {
					std::unique_lock lock(mutex);
					wake.wait_for(lock, stop, std::chrono::seconds(10), [] { return false; });
					lock.unlock();
					try {
						Save();
					} catch (const std::exception& exception) {
						std::scoped_lock guard(mutex);
						error = exception.what();
						++failures;
						logger::warn("[TextureStreaming] Remembered-size persistence failed: {}", error);
					}
					if (stop.stop_requested())
						return;
				}
			}
		};
		Catalog catalog;
		std::atomic_uint32_t previewSources{ 0 };
		struct LoadLifetime
		{
			RE::NiPointer<RE::NiSourceTexture> source;
			std::shared_ptr<std::atomic_size_t> count = catalog.activeLoads;
			explicit LoadLifetime(RE::NiSourceTexture* value) : source(value) { ++*count; }
			~LoadLifetime() { --*count; }
		};
		struct Scope
		{
			RE::NiSourceTexture* source = nullptr;
			std::optional<ReducedLoad> load;
		};
		thread_local Scope* loading = nullptr;
		using CreateTexture = Util::NativeTextureLoading::Create;
		CreateTexture originalCreate = nullptr;

		void Create(void* manager, RE::NiSourceTexture* source)
		{
			Scope scope{ source };
			const auto previous = std::exchange(loading, &scope);
			const SKSE::stl::scope_exit restore([&] { loading = previous; });
			originalCreate(manager, source);
			if (scope.load) {
				std::scoped_lock lock(catalog.mutex);
				catalog.pending[catalog.write] = std::move(scope.load);
				catalog.write = (catalog.write + 1) % maximumLoads;
			}
		}
	}
	void Install()
	{
		std::scoped_lock lock(catalog.mutex);
		if (catalog.hookReady)
			return;
		catalog.path = Util::PathHelpers::GetCommunityShaderPath() / "TextureStreamingSizes.json";
		catalog.worker = std::jthread([](std::stop_token stop) { catalog.Run(stop); });
		// Texture-creation slot reference: dnai92/SPS; see README.md#texture-streaming-reference.
		// The resource manager's texture-creation slot has the same layout in SE, AE and VR.
		REL::Relocation<std::uintptr_t> table{ RE::VTABLE_BSShaderResourceManager[0] };
		originalCreate = reinterpret_cast<CreateTexture*>(table.address())[Util::NativeTextureLoading::kCreateSlot];
		table.write_vfunc(Util::NativeTextureLoading::kCreateSlot, &Create);
		catalog.hookReady = true;
	}
	void Configure(bool enabled, const Modes& modes, const Options& options, const Categories& categories, std::uint32_t maximumDrop)
	{
		std::scoped_lock lock(catalog.mutex);
		catalog.enabled = enabled;
		catalog.modes = modes;
		catalog.options = options;
		catalog.categories = categories;
		catalog.maximumDrop = maximumDrop;
	}
	void SetPreviewActive(bool active, std::uint32_t sourceMask)
	{
		if (active)
			previewSources.fetch_or(sourceMask);
		else
			previewSources.fetch_and(~sourceMask);
	}
	bool IsPreviewActive() { return previewSources.load() != 0; }
	std::optional<ReducedLoad> Prepare(ID3D11Device* device, std::uint64_t originalMaximum)
	{
		// An upstream size limit changes the restoration contract; leave that loader's request intact.
		if (IsPreviewActive() || !loading || !loading->source || loading->load || originalMaximum || !device)
			return std::nullopt;
		try {
			const auto* name = loading->source->name.c_str();
			if (!name || !*name)
				return std::nullopt;
			const auto path = NormalizeTexturePath(name);
			RememberedSize entry;
			Options options;
			Modes modes;
			Categories categories;
			std::uint32_t maximumDrop;
			{
				std::scoped_lock lock(catalog.mutex);
				const auto found = catalog.entries.find(path);
				if (!catalog.enabled || catalog.protectionLimit || !catalog.ready || found == catalog.entries.end() || catalog.protectedPaths.contains(path))
					return std::nullopt;
				entry = found->second;
				options = catalog.options;
				modes = catalog.modes;
				categories = catalog.categories;
				maximumDrop = catalog.maximumDrop;
			}
			const auto memory = Util::GpuMemoryBudget::Get().SampleStreaming(device);
			if (!memory.Fresh(GetTickCount64()))
				return std::nullopt;
			const auto usage = Util::GpuMemoryBudget::Add(memory.local.CurrentUsage, memory.pendingBytes);
			const auto free = memory.local.Budget > usage ? memory.local.Budget - usage : 0;
			entry.drop = entry.LoadDrop(options, categories, maximumDrop,
				modes.continuous || (modes.onPressure && free <= options.reserveMiB * TextureStreamingPolicy::MiB));
			if (!entry.drop)
				return std::nullopt;
			RE::BSResourceNiBinaryStream source(path);
			std::array<std::uint8_t, 148> header{};
			const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(entry.fileBytes, header.size()));
			if (!source.good() || !source.stream || source.stream->totalSize != entry.fileBytes ||
				!source.read(header.data(), count) || HeaderHash(std::span(header.data(), count)) != entry.headerHash ||
				!MatchesDDSHeader(std::span(header.data(), count), entry.fileBytes, entry.full)) {
				Forget(path);
				std::scoped_lock lock(catalog.mutex);
				++catalog.rejected;
				return std::nullopt;
			}
			std::scoped_lock lock(catalog.mutex);
			if (IsPreviewActive() || !catalog.enabled || catalog.options != options || catalog.modes != modes || catalog.categories != categories ||
				catalog.maximumDrop != maximumDrop || catalog.protectedPaths.contains(path) || catalog.activeLoads->load() >= maximumLoads)
				return std::nullopt;
			auto lifetime = std::make_shared<LoadLifetime>(loading->source);
			return ReducedLoad{ loading->source, path, entry, std::move(lifetime) };
		} catch (const std::exception& exception) {
			std::scoped_lock lock(catalog.mutex);
			++catalog.failures;
			catalog.error = exception.what();
			logger::warn("[TextureStreaming] Remembered load skipped: {}", catalog.error);
			return std::nullopt;
		}
	}
	void Complete(ReducedLoad load, ID3D11Resource* resource, bool success)
	{
		if (!success || !resource) {
			Forget(load.path);
			std::scoped_lock lock(catalog.mutex);
			++catalog.failures;
			logger::warn("[TextureStreaming] Reduced DDS load failed: {}", load.path);
			return;
		}
		Origin origin;
		if (!ReadOrigin(resource, origin))
			return;
		load.serial = origin.serial;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
			return;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		if (desc.Width == load.size.full.Width && desc.Height == load.size.full.Height && desc.MipLevels == load.size.full.MipLevels)
			return;
		for (unsigned drop = 1; drop <= TextureStreamingPolicy::MaximumDrop; ++drop)
			if (desc.Width == (load.size.full.Width >> drop) && desc.Height == (load.size.full.Height >> drop) &&
				desc.MipLevels == load.size.full.MipLevels - drop && desc.Format == load.size.full.Format) {
				load.size.drop = drop;
				origin.preloaded = true;
				if (FAILED(WriteOrigin(resource, origin))) {
					logger::error("[TextureStreaming] Could not track reduced DDS source {}", load.path);
					return;
				}
				loading->load = std::move(load);
				std::scoped_lock lock(catalog.mutex);
				++catalog.hits;
				return;
			}
		Forget(load.path);
		std::scoped_lock lock(catalog.mutex);
		++catalog.failures;
		logger::warn("[TextureStreaming] DDS loader returned an unexpected size for {}", load.path);
	}
	bool HasPending()
	{
		std::scoped_lock lock(catalog.mutex);
		return catalog.pending[catalog.read].has_value();
	}
	std::optional<ReducedLoad> Poll()
	{
		std::scoped_lock lock(catalog.mutex);
		auto result = std::move(catalog.pending[catalog.read]);
		if (result) {
			catalog.pending[catalog.read].reset();
			catalog.read = (catalog.read + 1) % maximumLoads;
		}
		return result;
	}
	void Remember(const std::string& path, const RememberedSize& size)
	{
		if (!size.Valid(path))
			return;
		std::scoped_lock lock(catalog.mutex);
		if (catalog.protectionLimit || catalog.protectedPaths.contains(path))
			return;
		if (const auto found = catalog.entries.find(path); found != catalog.entries.end() &&
														   found->second == size)
			return;
		if (catalog.entries.size() >= maximumEntries && !catalog.entries.contains(path))
			catalog.entries.erase(catalog.entries.begin());
		catalog.entries[path] = size;
		++catalog.revision;
	}
	void Forget(const std::string& path)
	{
		std::scoped_lock lock(catalog.mutex);
		if (catalog.protectedPaths.size() < maximumEntries)
			catalog.protectedPaths.insert(path);
		else if (!catalog.protectedPaths.contains(path) && !catalog.protectionLimit) {
			catalog.protectionLimit = true;
			catalog.entries.clear();
			++catalog.revision;
			logger::warn("[TextureStreaming] Remembered-size protection limit reached; reduced loads are suspended until restart");
		}
		if (catalog.entries.erase(path))
			++catalog.revision;
	}
	nlohmann::json Status()
	{
		struct Snapshot
		{
			bool previewLoadGuardActive = false, hookReady = false, ready = false;
			std::size_t entries = 0;
			std::uint64_t loadedReduced = 0, rejectedSources = 0, failures = 0;
			std::size_t activeLoads = 0;
			bool protectionLimit = false, unsaved = false;
			std::string error;
		} snapshot;
		{
			std::scoped_lock lock(catalog.mutex);
			snapshot = { IsPreviewActive(), catalog.hookReady, catalog.ready, catalog.entries.size(),
				catalog.hits, catalog.rejected, catalog.failures, catalog.activeLoads->load(),
				catalog.protectionLimit, catalog.revision != catalog.savedRevision, catalog.error };
		}
		return { { "previewLoadGuardActive", snapshot.previewLoadGuardActive }, { "hookReady", snapshot.hookReady }, { "ready", snapshot.ready }, { "entries", snapshot.entries },
			{ "loadedReduced", snapshot.loadedReduced }, { "rejectedSources", snapshot.rejectedSources }, { "failures", snapshot.failures },
			{ "activeLoads", snapshot.activeLoads }, { "protectionLimit", snapshot.protectionLimit }, { "unsaved", snapshot.unsaved }, { "error", snapshot.error } };
	}
}
