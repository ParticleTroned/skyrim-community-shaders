#pragma once

#include "Diagnostics/AssetLoadRecord.h"
#include "Features/TextureStreaming/RestorationCache.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <optional>
#include <utility>

#ifdef DEVBENCH_BRIDGE_ENABLED
namespace CSX::Diagnostics::Stutters
{
	class AssetScope
	{
	public:
		AssetScope(AssetKind, std::string_view, std::string_view) noexcept {}
		~AssetScope() noexcept = default;
		AssetScope(const AssetScope&) = delete;
		AssetScope& operator=(const AssetScope&) = delete;
		void Result(bool) noexcept {}
		void Result(bool, std::int64_t) noexcept {}
		void CacheHit(bool) noexcept {}
		void Bytes(std::uint64_t) noexcept {}
	};
}
#endif

namespace StreamingReaderFixture
{
	namespace Policy = TextureStreamingPolicy;
	std::vector<std::uint8_t> file;
	std::uint64_t bytesRead = 0;
	std::atomic_uint64_t physical{ 4 * Policy::RestorationCacheBytes }, commit{ 4 * Policy::RestorationCacheBytes };
	bool available = true, completeRead = true, memoryAvailable = true;
	BOOL FixtureMemoryStatusEx(MEMORYSTATUSEX* value)
	{
		value->ullAvailPhys = physical;
		value->ullAvailPageFile = commit;
		return memoryAvailable;
	}
	namespace RE
	{
		struct BSResourceNiBinaryStream
		{
			struct Size
			{
				std::uint64_t totalSize;
			};
			std::unique_ptr<Size> stream;
			explicit BSResourceNiBinaryStream(const std::string&)
			{
				if (available)
					stream = std::make_unique<Size>(Size{ file.size() });
			}
			bool good() const { return stream != nullptr; }
			bool read(void* output, std::size_t size)
			{
				if (!completeRead || size > file.size())
					return false;
				std::memcpy(output, file.data(), size);
				bytesRead += size;
				return true;
			}
		};
	}
#define GlobalMemoryStatusEx FixtureMemoryStatusEx
#include "streaming_reader_under_test.h"
#undef GlobalMemoryStatusEx

	StreamingTextures::TexturePixels Pixels(DXGI_FORMAT format = DXGI_FORMAT_BC1_UNORM)
	{
		auto pixels = std::make_shared<DirectX::ScratchImage>();
		Check(pixels->Initialize2D(format, 1024, 1024, 1, 11));
		std::memset(pixels->GetPixels(), 37, pixels->GetPixelsSize());
		return pixels;
	}
}

void CheckReadDiagnostics([[maybe_unused]] const StreamingReaderFixture::ReadResult& result, [[maybe_unused]] std::uint64_t bytes, [[maybe_unused]] bool cached)
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	Require(result.sourceBytesRead == bytes && result.cacheHit == cached, "Reader diagnostics disagreed with completed reads");
#endif
}

void CheckStreamingReader()
{
	using namespace StreamingReaderFixture;
	using namespace StreamingTextures;
	const D3D11_TEXTURE2D_DESC full{ 1024, 1024, 11, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
	const auto pixels = Pixels();
	DirectX::Blob blob;
	Check(DirectX::SaveToDDSMemory(pixels->GetImages(), pixels->GetImageCount(), pixels->GetMetadata(), DirectX::DDS_FLAGS_NONE, blob));
	const auto* begin = static_cast<const std::uint8_t*>(blob.GetBufferPointer());
	file.assign(begin, begin + blob.GetBufferSize());
	const auto original = file;
	RestorationCache cache;
	ReadRequest request{ 123, 77, "textures\\test.dds", full, 2, ReadKind::ValidateSource, cache.GetStatus().generation };
	auto result = ReadDDS(request, cache);
	CheckReadDiagnostics(result, 148, false);
	Require(result.error.empty() && !result.image && bytesRead == 148,
		"A shrink read pixels instead of validating only the DDS header");
	request.kind = ReadKind::Restore;
	result = ReadDDS(request, cache);
	CheckReadDiagnostics(result, file.size(), false);
	Require(result.error.empty() && result.image &&
				cache.GetStatus().bytes == pixels->GetPixelsSize(),
		"First restoration failed to populate bounded immutable pixels");
	const auto retained = result.image;
	const auto readsBeforeHit = bytesRead;
	available = false;
	result = ReadDDS(request, cache);
	CheckReadDiagnostics(result, 0, true);
	Require(result.error.empty() && result.image == retained && bytesRead == readsBeforeHit,
		"A cache hit reopened the source or duplicated pixels");
	request.serial++;
	result = ReadDDS(request, cache);
	Require(!result.error.empty() && !result.image, "A new resource serial reused stale restoration pixels");
	request.serial--;
	request.path = "textures\\different.dds";
	Require(!ReadDDS(request, cache).error.empty(), "A different path reused stale restoration pixels");
	request.path = "textures\\test.dds";
	request.full.Format = DXGI_FORMAT_BC1_UNORM_SRGB;
	Require(!ReadDDS(request, cache).error.empty(), "Different metadata reused stale restoration pixels");
	request.full = full;
	available = true;
	request.kind = ReadKind::ValidateSource;
	file.resize(148);
	Require(!ReadDDS(request, cache).error.empty(), "A truncated payload admitted a GPU-only reduction");
	file = original;
	request.full.Width /= 2;
	Require(!ReadDDS(request, cache).error.empty(), "A changed DDS contract admitted a reduction");
	request.full = full;
	completeRead = false;
	Require(!ReadDDS(request, cache).error.empty(), "An incomplete source header admitted a reduction");
	completeRead = true;

	RestorationCache limited(2 * pixels->GetPixelsSize());
	const auto generation = limited.GetStatus().generation;
	Require(limited.Store(1, "a", full, pixels, generation) && limited.Store(2, "b", full, pixels, generation), "Cache rejected entries within its budget");
	const auto pinned = limited.Find(1, "a", full, generation);
	Require(pinned == pixels && limited.Store(3, "c", full, pixels, generation), "LRU insertion or promotion failed");
	Require(!limited.Find(2, "b", full, generation) && limited.Find(1, "a", full, generation) &&
				limited.GetStatus().bytes <= limited.GetStatus().limitBytes,
		"Cache evicted the wrong entry or exceeded its budget");
	limited.Clear();
	Require(pinned->GetPixels()[0] == 37 && !limited.GetStatus().bytes &&
				!limited.Store(4, "d", full, pixels, generation),
		"Eviction corrupted an active upload or stale work repopulated a cleared cache");
	RestorationCache tooSmall(pixels->GetPixelsSize() - 1);
	Require(!tooSmall.Store(1, "a", full, pixels, tooSmall.GetStatus().generation) && !tooSmall.GetStatus().bytes,
		"An oversized entry bypassed the cache limit");

	request.kind = ReadKind::Restore;
	physical = 0;
	result = ReadDDS(request, cache);
	Require(result.error.empty() && result.image && !cache.GetStatus().bytes,
		"Low physical headroom did not release the optional cache and use bounded restoration");
	physical = 4 * Policy::RestorationCacheBytes;
	commit = 0;
	Require(!ReadDDS(request, cache).error.empty(), "Restoration ignored insufficient commit headroom");
	commit = physical.load();
	memoryAvailable = false;
	Require(!ReadDDS(request, cache).error.empty(), "Restoration allocated without known commit headroom");
	request.kind = ReadKind::ValidateSource;
	Require(ReadDDS(request, cache).error.empty(), "An allocation-free source check required restoration memory");
	memoryAvailable = true;

	for (const auto format : { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB }) {
		const auto image = Pixels(format);
		Check(DirectX::SaveToDDSMemory(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DirectX::DDS_FLAGS_NONE, blob));
		const auto* header = static_cast<const std::uint8_t*>(blob.GetBufferPointer());
		auto descriptor = full;
		descriptor.Format = format;
		Require(MatchesDDSHeader({ header, 148 }, blob.GetBufferSize(), descriptor), "A legacy or DX10 DDS header was rejected");
		Require(!MatchesDDSHeader({ header, 127 }, blob.GetBufferSize(), descriptor), "An incomplete DDS header was accepted");
	}
	file = original;
	Reader reader;
	request.kind = ReadKind::Restore;
	reader.Start(request);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	std::optional<ReadResult> threaded;
	while (!(threaded = reader.Poll()) && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	Require(threaded && threaded->error.empty() && threaded->image && reader.CacheStatus().entries == 1,
		"The worker failed to publish a restoration or capture its cache generation");
	physical = 0;
	const auto idleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (reader.CacheStatus().bytes && std::chrono::steady_clock::now() < idleDeadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	Require(!reader.CacheStatus().bytes, "Idle RAM cache did not release pixels under system memory pressure");
	physical = 4 * Policy::RestorationCacheBytes;
	reader.ClearCache();
	Require(!reader.CacheStatus().bytes && reader.CacheStatus().generation > threaded->request.cacheGeneration,
		"Device/cancellation cache clearing retained the worker generation");
}
