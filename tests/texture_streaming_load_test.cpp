#include "Features/TextureStreaming/LoadHooks.h"
#include "Utils/NativeTextureLoading.h"
#include "d3d11_shader_test.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using D3D11ShaderTest::Check;
using Microsoft::WRL::ComPtr;
void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}
namespace logger
{
	template <class... T>
	void warn(const char*, T&&...)
	{}
	template <class... T>
	void error(const char*, T&&...)
	{}
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		explicit scope_exit(F value) : function(std::move(value)) {}
		~scope_exit() { function(); }
	};
}
namespace RE
{
	inline std::vector<std::uint8_t> sourceBytes;
	inline constexpr std::array<std::uintptr_t, 1> VTABLE_BSShaderResourceManager{};
	namespace BSGraphics
	{
		struct Texture
		{
			ID3D11Resource* texture = nullptr;
			ID3D11ShaderResourceView* resourceView = nullptr;
			std::uint32_t width = 4096, height = 2048, mips = 13;
		};
	}
	class NiSourceTexture
	{
	public:
		std::string name;
		int references = 0;
		BSGraphics::Texture* rendererTexture = nullptr;
	};
	template <class T>
	struct NiPointer
	{
		T* value = nullptr;
		NiPointer() = default;
		explicit NiPointer(T* pointer) { reset(pointer); }
		NiPointer(const NiPointer& other) { reset(other.value); }
		~NiPointer() { reset(nullptr); }
		void reset(T* pointer)
		{
			if (pointer)
				++pointer->references;
			if (value)
				--value->references;
			value = pointer;
		}
	};
	struct BSResourceNiBinaryStream
	{
		struct Data
		{
			std::uint64_t totalSize = sourceBytes.size();
		};
		std::unique_ptr<Data> stream = std::make_unique<Data>();
		explicit BSResourceNiBinaryStream(const std::string&) {}
		bool good() const { return true; }
		bool read(void* destination, std::uint32_t count)
		{
			if (count > sourceBytes.size())
				return false;
			std::memcpy(destination, sourceBytes.data(), count);
			return true;
		}
	};
}
std::function<void(RE::NiSourceTexture*)> createBody;
void OriginalCreate(void*, RE::NiSourceTexture* source) { createBody(source); }
namespace REL
{
	inline std::array<std::uintptr_t, Util::NativeTextureLoading::kCreateSlot + 1> slots = [] {
		std::array<std::uintptr_t, Util::NativeTextureLoading::kCreateSlot + 1> result{};
		result[Util::NativeTextureLoading::kCreateSlot] = reinterpret_cast<std::uintptr_t>(&OriginalCreate);
		return result;
	}();
	template <class T>
	struct Relocation
	{
		explicit Relocation(std::uintptr_t) {}
		std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(slots.data()); }
		template <class F>
		std::uintptr_t write_vfunc(std::size_t slot, F function)
		{
			return std::exchange(slots[slot], reinterpret_cast<std::uintptr_t>(function));
		}
	};
}
namespace Util
{
	namespace PathHelpers
	{
		inline std::filesystem::path testPath;
		std::filesystem::path GetCommunityShaderPath() { return testPath; }
	}
	namespace FileHelpers
	{
		bool WriteTextFileAtomic(const std::filesystem::path& path, std::string_view contents, std::string&, bool)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(contents.data(), contents.size());
			return bool(stream);
		}
	}
	class GpuMemoryBudget
	{
	public:
		struct Snapshot
		{
			struct Local
			{
				std::uint64_t Budget = 10000ull << 20, CurrentUsage = 4000ull << 20;
			} local;
			std::uint64_t pendingBytes = 0;
			bool valid = true;
			bool Fresh(std::uint64_t) const { return valid; }
		} snapshot;
		static GpuMemoryBudget& Get()
		{
			static GpuMemoryBudget value;
			return value;
		}
		Snapshot SampleStreaming(ID3D11Device*) { return snapshot; }
		static std::uint64_t Add(std::uint64_t a, std::uint64_t b) { return a + b; }
	};
}
#include "streaming_load_under_test.h"
namespace Policy = TextureStreamingPolicy;
struct TextureStreaming
{
	struct State
	{
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
			unsigned drop = 0, desired = 0;
			std::uint64_t headerHash = 0, fileBytes = 0;
			bool sourceValidated = false, protectedConsumer = false;
			std::shared_ptr<void> loadLifetime;
		};
		std::unordered_map<std::uint64_t, Record> records;
		std::unordered_set<std::uint64_t> reducedRecords;
		void Fail(const char* error) { throw std::runtime_error(error); }
		void AdoptLoads();
	};
};
#include "streaming_adoption_under_test.h"

int main()
{
	using namespace StreamingTextures;
	namespace Hooks = StreamingTextures::LoadHooks;
	try {
		Require(Hooks::NeedsLearningRefresh(), "An unknown catalog allowed deferred learning");
		ComPtr<ID3D11Device> device;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
		DirectX::ScratchImage pixels;
		Check(pixels.Initialize2D(DXGI_FORMAT_BC1_UNORM, 4096, 2048, 1, 13));
		DirectX::Blob dds;
		Check(DirectX::SaveToDDSMemory(pixels.GetImages(), pixels.GetImageCount(), pixels.GetMetadata(), DirectX::DDS_FLAGS_NONE, dds));
		const auto* bytes = static_cast<const std::uint8_t*>(dds.GetBufferPointer());
		RE::sourceBytes.assign(bytes, bytes + dds.GetBufferSize());
		// Exercise persistence with a real DDS header hash outside the signed range.
		for (unsigned reserved = 0; reserved <= UINT8_MAX; ++reserved) {
			RE::sourceBytes[32] = static_cast<std::uint8_t>(reserved);
			if (HeaderHash(std::span(RE::sourceBytes.data(), 148)) > INT64_MAX)
				break;
		}
		Require(HeaderHash(std::span(RE::sourceBytes.data(), 148)) > INT64_MAX, "DDS fixture did not cover unsigned source hashes");
		RememberedSize entry;
		entry.full = { 4096, 2048, 13, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		entry.drop = 2;
		entry.headerHash = HeaderHash(std::span(RE::sourceBytes.data(), 148));
		entry.fileBytes = RE::sourceBytes.size();
		const std::string path = "textures\\architecture\\remember.dds";
		Require(ParseRememberedSize(path, SerializeRememberedSize(entry)) == entry, "Persisted source metadata did not round-trip");
		for (const auto hash : { std::uint64_t{ 1 }, std::uint64_t{ INT64_MAX }, std::uint64_t{ INT64_MAX } + 1, std::uint64_t{ UINT64_MAX } }) {
			auto boundary = entry;
			boundary.headerHash = hash;
			const auto persisted = nlohmann::json::parse(SerializeRememberedSize(boundary).dump());
			Require(ParseRememberedSize(path, persisted) == boundary, "Persisted unsigned source hash did not survive restart");
		}
		for (std::size_t field = 0; field < 8; ++field) {
			auto malformed = SerializeRememberedSize(entry);
			malformed[field] = -1;
			Require(!ParseRememberedSize(path, malformed), "Negative persisted metadata was admitted");
			malformed[field] = 1.5;
			Require(!ParseRememberedSize(path, malformed), "Fractional persisted metadata was admitted");
		}
		Require(!ParseRememberedSize("textures\\actors\\face.dds", SerializeRememberedSize(entry)), "Persisted actor bypassed protection");
		auto invalid = SerializeRememberedSize(entry);
		invalid[0] = UINT64_MAX;
		Require(!ParseRememberedSize(path, invalid), "Persisted integer overflow was admitted");
		invalid = SerializeRememberedSize(entry);
		invalid[4] = 9;
		Require(!ParseRememberedSize(path, invalid), "Persisted drop escaped bounded mip validation");
		Options options;
		Require(entry.LoadDrop(options, {}, 1, true) == 1, "Remembered size bypassed the current mip limit");
		Require(entry.LoadDrop(options, {}, 3, false) == 0, "Healthy pressure mode loaded a remembered reduction");
		options.maximumSize = 1024;
		options.rememberedSizes = false;
		Require(entry.LoadDrop(options, {}, 1, false) == 2, "Explicit cap depended on pressure or the distance mip limit");
		entry.categories = Terrain;
		Require(entry.LoadDrop(options, {}, 3, true) > 0, "Default terrain coverage could not use remembered sizes");
		Require(entry.LoadDrop(options, Categories{ false, false, false, false, false }, 3, true) == 0, "Remembered size bypassed disabled coverage");
		entry.categories = Terrain | Parallax;
		Require(ParseRememberedSize(path, SerializeRememberedSize(entry)) == entry, "Remembered parallax consent was not retained");
		Categories allowedParallax{ false, false, false, true, false };
		Require(entry.LoadDrop(options, allowedParallax, 3, true) == 0, "Remembered load bypassed disabled parallax consent");
		allowedParallax.parallax = true;
		Require(entry.LoadDrop(options, allowedParallax, 3, true) > 0, "Consented terrain/parallax could not use remembered sizes");
		entry.categories = 0;
		Util::PathHelpers::testPath = std::filesystem::temp_directory_path() / ("csx-streaming-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		std::filesystem::create_directory(Util::PathHelpers::testPath);
		const auto cachePath = Util::PathHelpers::testPath / "TextureStreamingSizes.json";
		const std::string earlyPath = "textures\\architecture\\early.dds";
		Hooks::Remember(earlyPath, entry);
		Require(Hooks::NeedsLearningRefresh(), "Learning resumed before the persistent catalog was ready");
		{
			std::ofstream file(cachePath);
			file << nlohmann::json{ { "version", 1 }, { "entries", { { path, SerializeRememberedSize(entry) }, { earlyPath, SerializeRememberedSize(entry) } } } }.dump();
		}
		Hooks::Install();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!Hooks::Status().at("ready").get<bool>() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		Require(Hooks::Status().at("entries") == 2, "Worker did not merge the previous session's validated size");
		Hooks::Forget(earlyPath);
		Require(Hooks::NeedsLearningRefresh(), "A persisted reduction did not retain normal learning cadence");
		auto fullSize = entry;
		fullSize.drop = 0;
		Hooks::Remember(path, entry);
		Hooks::Remember(path, fullSize);
		Require(!Hooks::NeedsLearningRefresh(), "Replacing the last reduction retained a stale refresh requirement");
		Hooks::Remember(path, fullSize);
		Hooks::Remember(path, entry);
		const std::string secondPath = "textures\\architecture\\second.dds";
		Hooks::Remember(secondPath, entry);
		Hooks::Forget(secondPath);
		Require(Hooks::NeedsLearningRefresh(), "Removing one reduction hid another eligible entry");
		Hooks::Configure(true, { false, true }, {}, {}, 2);
		RE::NiSourceTexture source{ path };
		ComPtr<ID3D11Texture2D> reduced;
		ComPtr<ID3D11ShaderResourceView> reducedView;
		RE::BSGraphics::Texture engine;
		source.rendererTexture = &engine;
		createBody = [&](RE::NiSourceTexture*) {
			auto request = Hooks::Prepare(device.Get(), 0);
			Require(request && request->size.drop == 2, "Known texture did not load at remembered size");
			auto desc = entry.full;
			desc.Width >>= 2;
			desc.Height >>= 2;
			desc.MipLevels -= 2;
			Check(device->CreateTexture2D(&desc, nullptr, &reduced));
			Util::SetResourceName(reduced.Get(), "StreamingTest::Remembered");
			Check(device->CreateShaderResourceView(reduced.Get(), nullptr, &reducedView));
			Util::SetResourceName(reducedView.Get(), "StreamingTest::Remembered.SRV");
			engine = { reduced.Get(), reducedView.Get() };
			Check(WriteOrigin(reduced.Get(), { 10, false }));
			Hooks::Complete(std::move(*request), reduced.Get(), true);
		};
		Hooks::Create(nullptr, &source);
		Origin origin;
		Require(ReadOrigin(reduced.Get(), origin) && origin.preloaded, "Reduced load lost its ownership marker");
		Require(source.references == 1 && Hooks::Status().at("activeLoads") == 1, "Pending reduced texture was not retained");
		Require(Hooks::HasPending(), "Queued source ownership was invisible to a disabled streaming tick");
		auto loaded = Hooks::Poll();
		Require(loaded && loaded->size.full.Width == 4096 && loaded->size.drop == 2 && loaded->serial == 10, "Full restoration contract was lost at load handoff");
		loaded.reset();
		Require(source.references == 0 && Hooks::Status().at("activeLoads") == 0 && !Hooks::Poll() && !Hooks::HasPending(), "Load handoff leaked ownership or duplicated a source");
		{
			const std::scoped_lock lock(Hooks::catalog.mutex);
			Require(!Hooks::HasPending() && !Hooks::Poll(), "Empty queue attempted a catalog lock");
		}
		Hooks::Create(nullptr, &source);
		{
			TextureStreaming::State state;
			state.AdoptLoads();
			const auto& record = state.records.at(10);
			Require(record.full.Width == 4096 && record.view.Texture2D.MipLevels == 13 && record.drop == 2 &&
						record.bytes[0] > record.bytes[2] && record.sourceValidated && state.reducedRecords.contains(10) &&
						engine.width == 1024 && engine.height == 512 && engine.mips == 11,
				"Adoption lost the full restoration descriptor or reduced-record registration");
		}
		Require(source.references == 0 && Hooks::Status().at("activeLoads") == 0, "Adopted record leaked load ownership");
		createBody = [&](RE::NiSourceTexture*) {
			Require(!Hooks::Prepare(device.Get(), 2048), "Remembered load overrode another mod's size request");
			Hooks::Configure(false, {}, {}, {}, 2);
			Require(!Hooks::Prepare(device.Get(), 0), "Disabled feature still reduced at load");
			Hooks::Configure(true, {}, {}, {}, 2);
			Require(!Hooks::Prepare(device.Get(), 0), "Healthy budget applied a remembered reduction");
			Util::GpuMemoryBudget::Get().snapshot.local.CurrentUsage = 9500ull << 20;
			Require(Hooks::Prepare(device.Get(), 0).has_value(), "Pressure failed to enable remembered loads");
			Hooks::SetPreviewActive(true);
			Require(Hooks::Status().at("previewLoadGuardActive") && !Hooks::Prepare(device.Get(), 0), "Inventory preview inherited a remembered reduction under pressure");
			Hooks::SetPreviewActive(true, 2);
			Hooks::SetPreviewActive(true, 4);
			Hooks::SetPreviewActive(false);
			Hooks::SetPreviewActive(false, 2);
			Require(!Hooks::Prepare(device.Get(), 0), "Closing one preview owner cleared another owner's guard");
			Hooks::SetPreviewActive(false, 4);
			Require(Hooks::Prepare(device.Get(), 0).has_value(), "Closing the preview did not resume remembered world loads");
			Hooks::Configure(true, {}, options, {}, 1);
			Util::GpuMemoryBudget::Get().snapshot.local.CurrentUsage = 4000ull << 20;
			auto capped = Hooks::Prepare(device.Get(), 0);
			Require(capped && capped->size.drop == 2, "Size cap was not independent of pressure");
			capped.reset();
			RE::sourceBytes[12] ^= 1;
			Require(!Hooks::Prepare(device.Get(), 0), "Changed source header reused stale size metadata");
		};
		Hooks::Create(nullptr, &source);
		Require(Hooks::Status().at("rejectedSources") == 1 && Hooks::Status().at("entries") == 0, "Changed DDS was not invalidated");
		Require(!Hooks::NeedsLearningRefresh(), "Source invalidation retained the last remembered reduction");
		Hooks::Remember(path, entry);
		Require(Hooks::Status().at("entries") == 0, "A protected source was relearned in the same session");
		for (std::size_t i = 0; i < Hooks::maximumEntries; ++i)
			Hooks::Remember("textures\\architecture\\catalog-" + std::to_string(i) + ".dds", fullSize);
		Require(!Hooks::NeedsLearningRefresh(), "Full-size entries unnecessarily required normal learning cadence");
		std::string evicted;
		{
			const std::scoped_lock lock(Hooks::catalog.mutex);
			evicted = Hooks::catalog.entries.begin()->first;
		}
		Hooks::Remember(evicted, entry);
		Require(Hooks::NeedsLearningRefresh(), "Replacing a full-size entry lost its reduction");
		Hooks::Remember("textures\\architecture\\replacement.dds", fullSize);
		Require(!Hooks::NeedsLearningRefresh(), "Evicting the last reduction retained a stale refresh requirement");
		Hooks::Remember("textures\\architecture\\replacement.dds", entry);
		for (std::size_t i = 0; i < Hooks::maximumEntries; ++i)
			Hooks::Forget("textures\\architecture\\protected-" + std::to_string(i) + ".dds");
		Require(Hooks::Status().at("protectionLimit") && Hooks::Status().at("entries") == 0 && !Hooks::NeedsLearningRefresh(),
			"Protection-limit clearing left remembered reductions eligible");
		Hooks::catalog.worker.request_stop();
		Hooks::catalog.wake.notify_all();
		Hooks::catalog.worker.join();
		nlohmann::json saved;
		{
			std::ifstream file(cachePath);
			file >> saved;
		}
		Require(saved.at("entries").empty(), "Invalidation was not persisted on shutdown");
		std::stop_source stopped;
		stopped.request_stop();
		{
			Hooks::Catalog empty;
			empty.path = cachePath;
			empty.Run(stopped.get_token());
			Require(empty.ready && !empty.learningRefreshRequired.load() && !empty.rememberedReductions,
				"An empty persisted catalog retained an unknown refresh requirement");
		}
		{
			std::ofstream file(cachePath, std::ios::trunc);
			file << "malformed";
		}
		{
			Hooks::Catalog failed;
			failed.path = cachePath;
			failed.Run(stopped.get_token());
			Require(failed.ready && failed.failures == 1 && !failed.learningRefreshRequired.load() && !failed.rememberedReductions,
				"A rejected catalog retained unknown or phantom reductions");
		}
		std::filesystem::remove(cachePath);
		std::filesystem::remove(Util::PathHelpers::testPath);
		std::cout << "PASS: remembered source validation, load hooks, cap and reserve controls, persistence and ownership transfer\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
