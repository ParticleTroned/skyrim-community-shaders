#include "CharacterComputeSubrect.h"
#include "CharacterMaskWorkPolicy.h"
#include "D3D12Interop.h"
#include "FeatureCreationObserver.h"
#include "GpuCapture.h"
#include "KernelChainCommandList.h"
#include "KernelCommandCapture.h"
#include "KernelScheduleReadback.h"
#include "ProviderFloorProbe.h"
#include "ProviderKernelChainProbe.h"
#include "Runtime.h"
#include "SourceTransport.h"
#include "Utils/CryptoHash.h"
#include "build_identity.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dxgi1_4.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <nvsdk_ngx.h>
#include <thread>
#include <tuple>

namespace
{
	using Json = nlohmann::json;
	using Microsoft::WRL::ComPtr;
	using namespace NeuralRendering;
	using Clock = std::chrono::steady_clock;
	constexpr std::uint64_t kBundleBudget = 512ull * 1024 * 1024;
	constexpr std::uint64_t kResourceBudget = 2ull * 1024 * 1024 * 1024;

	void Require(bool condition, std::string_view message)
	{
		if (!condition)
			throw std::runtime_error(std::string(message));
	}
	void Check(HRESULT result, std::string_view operation)
	{
		if (FAILED(result))
			throw std::runtime_error(std::format("{}: 0x{:08x}", operation, static_cast<unsigned>(result)));
	}
	std::string Hash(std::span<const std::uint8_t> bytes)
	{
		return Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(bytes)));
	}
	std::vector<std::uint8_t> Read(const std::filesystem::path& path, std::uint64_t maximum)
	{
		const auto size = std::filesystem::file_size(path);
		Require(size <= maximum, "file exceeds bounded replay budget");
		std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
		std::ifstream stream(path, std::ios::binary);
		Require(bool(stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))), "input read failed");
		return result;
	}
	void WriteJson(const std::filesystem::path& path, const Json& value)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << value.dump(2) << '\n';
		Require(bool(stream), "result write failed");
	}
	std::string Lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}
	std::string Utf8(const std::wstring& value)
	{
		const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		Require(count > 0, "adapter description UTF8 conversion");
		std::string result(static_cast<std::size_t>(count), '\0');
		Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) == count, "adapter description UTF8 conversion");
		return result;
	}
	void StageRuntime(const std::filesystem::path& source, const std::filesystem::path& expected, const std::string& requiredHash = {})
	{
		if (!source.empty()) {
			const auto hash = Hash(Read(source, 1024ull * 1024 * 1024));
			Require(requiredHash.empty() || hash == Lower(requiredHash), "runtime differs from capture identity");
			if (std::filesystem::exists(expected))
				Require(Hash(Read(expected, 1024ull * 1024 * 1024)) == hash, "existing staged runtime differs; no overwrite");
			else {
				std::filesystem::create_directories(expected.parent_path());
				std::filesystem::copy_file(source, expected);
			}
		}
		Require(std::filesystem::is_regular_file(expected), "admitted runtime missing from replay executable Data layout; supply --runtime physical provider");
		Require(requiredHash.empty() || Hash(Read(expected, 1024ull * 1024 * 1024)) == Lower(requiredHash), "staged runtime hash differs from captured runtime");
	}
	unsigned BytesPerPixel(unsigned format)
	{
		switch (format) {
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return 8;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return 16;
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R16G16_FLOAT:
			return 4;
		default:
			throw std::runtime_error("capture format unsupported by replay decoder");
		}
	}
	double SmallFloat(unsigned value, unsigned mantissaBits, bool signedValue)
	{
		const unsigned exponent = (value >> mantissaBits) & 31u;
		const unsigned mantissa = value & ((1u << mantissaBits) - 1u);
		const double sign = signedValue && (value & (1u << (mantissaBits + 5u))) ? -1.0 : 1.0;
		if (exponent == 31u)
			return std::numeric_limits<double>::quiet_NaN();
		return sign * (exponent ? std::ldexp(1.0 + std::ldexp(double(mantissa), -int(mantissaBits)), int(exponent) - 15) :
								  std::ldexp(double(mantissa), -14 - int(mantissaBits)));
	}
	std::array<double, 3> Decode(const std::uint8_t* bytes, unsigned format)
	{
		if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
			std::uint32_t packed;
			std::memcpy(&packed, bytes, sizeof(packed));
			return { SmallFloat(packed & 2047u, 6, false), SmallFloat((packed >> 11) & 2047u, 6, false), SmallFloat(packed >> 22, 5, false) };
		}
		if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
			std::array<std::uint16_t, 4> values;
			std::memcpy(values.data(), bytes, sizeof(values));
			return { SmallFloat(values[0], 10, true), SmallFloat(values[1], 10, true), SmallFloat(values[2], 10, true) };
		}
		if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
			std::array<float, 4> values;
			std::memcpy(values.data(), bytes, sizeof(values));
			return { values[0], values[1], values[2] };
		}
		Require(format == DXGI_FORMAT_R8G8B8A8_UNORM, "unsupported colour decoder");
		return { bytes[0] / 255.0, bytes[1] / 255.0, bytes[2] / 255.0 };
	}
	struct TextureData
	{
		unsigned width = 0, height = 0, format = 0, rowBytes = 0;
		std::vector<std::uint8_t> bytes;
	};
	struct Eye
	{
		TextureData color, depth, motion, output;
		std::array<float, 2> motionScale{};
		bool featureUpscaling = false;
	};
	struct Frame
	{
		Json metadata;
		std::vector<Eye> eyes;
	};
	TextureData LoadTexture(const Json& value, const std::filesystem::path& root, std::uint64_t& budget)
	{
		TextureData result;
		result.width = value.at("width").get<unsigned>();
		result.height = value.at("height").get<unsigned>();
		result.format = value.at("format").get<unsigned>();
		result.rowBytes = value.at("rowBytes").get<unsigned>();
		Require(result.width && result.height && result.width <= 16384 && result.height <= 16384, "invalid capture extent");
		Require(result.rowBytes == std::uint64_t(result.width) * BytesPerPixel(result.format), "capture rows are not tightly packed");
		const auto relative = std::filesystem::path(value.at("file").get<std::string>());
		Require(!relative.is_absolute() && !relative.has_root_path(), "capture resource must be relative");
		for (const auto& component : relative)
			Require(component != "..", "capture resource escapes bundle");
		const auto path = std::filesystem::weakly_canonical(root / relative);
		const auto containment = path.lexically_relative(std::filesystem::weakly_canonical(root));
		Require(!containment.empty() && *containment.begin() != "..", "capture resource resolves outside bundle");
		result.bytes = Read(path, budget);
		Require(result.bytes.size() == std::uint64_t(result.rowBytes) * result.height, "capture resource length mismatch");
		budget -= result.bytes.size();
		Require(Hash(result.bytes) == Lower(value.at("sha256").get<std::string>()), "capture resource hash mismatch");
		return result;
	}
	std::vector<Frame> LoadFrames(const Json& manifest, const std::filesystem::path& root)
	{
		Require(manifest.at("schema") == "csx-nr-replay-input-v1", "unsupported native replay schema");
		Require(manifest.at("complete") == true && manifest.at("state") == "complete", "native input capture is incomplete");
		const auto& frames = manifest.at("frames");
		Require(frames.is_array() && !frames.empty() && frames.size() <= 32, "capture must contain 1..32 frames");
		std::uint64_t budget = kBundleBudget;
		std::vector<Frame> result;
		for (const auto& source : frames) {
			Frame frame{ source, {} };
			Require(source.at("mode").get<unsigned>() <= 2, "invalid captured route");
			Require(source.at("tuning").at("useAutoMask") == true && source.at("tuning").at("uiCorrection") == false, "only native auto-mask supported");
			Require(source.at("eyes").size() >= 1 && source.at("eyes").size() <= 2, "invalid captured eye count");
			for (const auto& value : source.at("eyes")) {
				Eye eye;
				eye.color = LoadTexture(value.at("color"), root, budget);
				eye.depth = LoadTexture(value.at("depth"), root, budget);
				eye.motion = LoadTexture(value.at("motion"), root, budget);
				eye.output = LoadTexture(value.at("output"), root, budget);
				eye.motionScale = value.at("motionVectorScale").get<std::array<float, 2>>();
				eye.featureUpscaling = value.at("featureUpscaling").get<bool>();
				for (auto scale : eye.motionScale)
					Require(std::isfinite(scale) && scale > 0, "invalid motion scale");
				Require(eye.depth.width == eye.motion.width && eye.depth.height == eye.motion.height, "depth/motion guide grids differ");
				Require(eye.output.width == eye.color.width && eye.output.height == eye.color.height, "colour/output grids differ");
				Require(eye.output.format == eye.color.format, "initial replay requires matching colour/output formats");
				const auto& rect = value.at("outputSubrect");
				Require(rect.at("baseX") == 0 && rect.at("baseY") == 0 && rect.at("width") == eye.color.width && rect.at("height") == eye.color.height,
					"capture must contain full initialized native resource domain");
				frame.eyes.push_back(std::move(eye));
			}
			for (const auto& eye : frame.eyes) {
				const auto& reference = frame.eyes.front();
				Require(eye.featureUpscaling == reference.featureUpscaling, "stereo feature mode differs");
				for (const auto member : { &Eye::color, &Eye::depth, &Eye::motion, &Eye::output }) {
					const auto& a = eye.*member;
					const auto& b = reference.*member;
					Require(a.width == b.width && a.height == b.height && a.format == b.format, "stereo resources differ");
				}
			}
			if (!result.empty()) {
				const auto& first = result.front();
				for (const char* key : { "mode", "insertionPoint", "generation", "colorRevision", "inputEpoch", "tuning", "colorConfiguration" })
					Require(first.metadata.at(key) == source.at(key), "capture changes matched replay configuration");
				Require(frame.eyes.size() == first.eyes.size(), "capture eye count changes");
				Require(source.at("sourceWorldFrame").get<std::uint32_t>() == result.back().metadata.at("sourceWorldFrame").get<std::uint32_t>() + 1u,
					"temporal capture has missing/repeated source frames");
				for (std::size_t i = 0; i < frame.eyes.size(); ++i) {
					Require(frame.eyes[i].featureUpscaling == first.eyes[i].featureUpscaling && frame.eyes[i].motionScale == first.eyes[i].motionScale,
						"temporal feature or motion scale changes");
					for (const auto member : { &Eye::color, &Eye::depth, &Eye::motion, &Eye::output }) {
						const auto& a = frame.eyes[i].*member;
						const auto& b = first.eyes[i].*member;
						Require(a.width == b.width && a.height == b.height && a.format == b.format, "temporal resource contract changes");
					}
				}
			}
			result.push_back(std::move(frame));
		}
		return result;
	}
	struct Difference
	{
		std::uint64_t pixels = 0;
		double maximum = 0;
		unsigned peakX = 0, peakY = 0;
	};
	Difference Compare(const TextureData& source, const TextureData& output, const ComputeSubrect& rect)
	{
		Difference result;
		Require(rect.Fits(source.width, source.height) && rect.Fits(output.width, output.height), "comparison bounds");
		for (unsigned y = rect.baseY; y < rect.baseY + rect.height; ++y)
			for (unsigned x = rect.baseX; x < rect.baseX + rect.width; ++x) {
				const auto a = Decode(source.bytes.data() + std::uint64_t(y) * source.rowBytes + x * BytesPerPixel(source.format), source.format);
				const auto b = Decode(output.bytes.data() + std::uint64_t(y) * output.rowBytes + x * BytesPerPixel(output.format), output.format);
				double delta = 0;
				for (unsigned channel = 0; channel < 3; ++channel) {
					Require(std::isfinite(a[channel]) && std::isfinite(b[channel]), "nonfinite replay colour");
					delta = std::max(delta, std::abs(a[channel] - b[channel]));
				}
				if (delta > 0)
					++result.pixels;
				if (delta > result.maximum) {
					result.maximum = delta;
					result.peakX = x;
					result.peakY = y;
				}
			}
		return result;
	}
	TextureData Crop(const TextureData& source, const ComputeSubrect& crop)
	{
		Require(crop.Fits(source.width, source.height), "crop outside captured texture");
		TextureData result{ crop.width, crop.height, source.format, crop.width * BytesPerPixel(source.format), {} };
		result.bytes.resize(std::uint64_t(result.rowBytes) * result.height);
		for (unsigned y = 0; y < crop.height; ++y)
			std::memcpy(result.bytes.data() + std::uint64_t(y) * result.rowBytes,
				source.bytes.data() + std::uint64_t(y + crop.baseY) * source.rowBytes + crop.baseX * BytesPerPixel(source.format), result.rowBytes);
		return result;
	}
	TextureData Sentinel(const TextureData& source, bool alternate)
	{
		auto result = source;
		const auto stride = BytesPerPixel(source.format);
		for (std::uint64_t pixel = 0; pixel < std::uint64_t(source.width) * source.height; ++pixel) {
			auto* destination = result.bytes.data() + pixel * stride;
			if (source.format == DXGI_FORMAT_R11G11B10_FLOAT) {
				const std::uint32_t value = 0x7c1u | (0x7c1u << 11) | (0x3e1u << 22);
				std::memcpy(destination, &value, 4);
			} else if (source.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
				const std::array<std::uint16_t, 4> values{ 0x7e01, 0x7e01, 0x7e01, 0x3c00 };
				std::memcpy(destination, values.data(), stride);
			} else if (source.format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
				const std::array<std::uint32_t, 4> values{ 0x7fc00001, 0x7fc00001, 0x7fc00001, 0x3f800000 };
				std::memcpy(destination, values.data(), stride);
			} else {
				Require(source.format == DXGI_FORMAT_R8G8B8A8_UNORM, "sentinel colour format");
				const std::uint32_t pattern = static_cast<std::uint32_t>(pixel * 2654435761u) ^ (alternate ? 0x00ffffffu : 0u);
				const std::uint32_t value = 0xff000000u | (pattern & 0x00ffffffu);
				std::memcpy(destination, &value, 4);
			}
		}
		return result;
	}
	Json Footprint(const TextureData& sentinel, const TextureData& actual, const ComputeSubrect& rect)
	{
		std::uint64_t changedInside = 0, changedOutside = 0, unchangedInside = 0, nonfiniteInside = 0;
		const auto stride = BytesPerPixel(actual.format);
		for (unsigned y = 0; y < actual.height; ++y)
			for (unsigned x = 0; x < actual.width; ++x) {
				const auto offset = std::uint64_t(y) * actual.rowBytes + x * stride;
				const bool changed = std::memcmp(sentinel.bytes.data() + offset, actual.bytes.data() + offset, stride) != 0;
				if (x >= rect.baseX && x - rect.baseX < rect.width && y >= rect.baseY && y - rect.baseY < rect.height) {
					changedInside += changed;
					unchangedInside += !changed;
					const auto rgb = Decode(actual.bytes.data() + offset, actual.format);
					nonfiniteInside += !std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]);
				} else {
					changedOutside += changed;
				}
			}
		return { { "modifiedInsidePixels", changedInside }, { "modifiedOutsidePixels", changedOutside },
			{ "unchangedInsidePixels", unchangedInside }, { "nonfiniteInsidePixels", nonfiniteInside },
			{ "sentinel", actual.format == DXGI_FORMAT_R8G8B8A8_UNORM ? "deterministic_byte_pattern" : "raw_quiet_NaN_RGB" } };
	}
	Json RectJson(const ComputeSubrect& rect)
	{
		return { { "baseX", rect.baseX }, { "baseY", rect.baseY }, { "width", rect.width }, { "height", rect.height } };
	}
	void WriteFinitePattern(std::uint8_t* destination, unsigned format, bool alternate)
	{
		const float a = alternate ? 1.0f : 0.0f, b = 1.0f - a;
		const std::uint16_t halfA = alternate ? 0x3c00 : 0, halfB = alternate ? 0 : 0x3c00;
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			{
				const std::array<std::uint8_t, 4> pixel{ static_cast<std::uint8_t>(255 * a), static_cast<std::uint8_t>(255 * b), static_cast<std::uint8_t>(255 * a), 255 };
				std::memcpy(destination, pixel.data(), sizeof(pixel));
				break;
			}
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			{
				const std::array<std::uint16_t, 4> pixel{ halfA, halfB, halfA, 0x3c00 };
				std::memcpy(destination, pixel.data(), sizeof(pixel));
				break;
			}
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			{
				const std::array<float, 4> pixel{ a, b, a, 1.0f };
				std::memcpy(destination, pixel.data(), sizeof(pixel));
				break;
			}
		case DXGI_FORMAT_R11G11B10_FLOAT:
			{
				const std::uint32_t pixel = alternate ? 0x3c0u | (0x1e0u << 22) : 0x3c0u << 11;
				std::memcpy(destination, &pixel, sizeof(pixel));
				break;
			}
		case DXGI_FORMAT_R32_FLOAT:
			{
				const float pixel = alternate ? 0.25f : 0.75f;
				std::memcpy(destination, &pixel, sizeof(pixel));
				break;
			}
		case DXGI_FORMAT_R16G16_FLOAT:
			{
				const std::array<std::uint16_t, 2> pixel{ alternate ? std::uint16_t(0x3c00) : std::uint16_t(0xbc00), alternate ? std::uint16_t(0xbc00) : std::uint16_t(0x3c00) };
				std::memcpy(destination, pixel.data(), sizeof(pixel));
				break;
			}
		default:
			throw std::runtime_error("unsupported finite storage pattern format");
		}
	}
	Json ApplyInputStorage(TextureData& data, const ComputeSubrect& valid, const ComputeSubrect& preserved, std::string_view policy)
	{
		Require(valid.Fits(data.width, data.height), "input storage rectangle outside resource");
		Require(preserved.Fits(data.width, data.height) && ContainsComputeSubrect(preserved, valid), "input context must contain requested rectangle");
		Require(policy == "captured" || policy == "zero" || policy == "finite_pattern", "unknown input storage policy");
		const auto validHash = Hash(Crop(data, valid).bytes);
		const auto preservedHash = Hash(Crop(data, preserved).bytes);
		const auto capturedHash = Hash(data.bytes);
		std::uint64_t changed = 0;
		const auto stride = BytesPerPixel(data.format);
		if (policy != "captured")
			for (unsigned y = 0; y < data.height; ++y)
				for (unsigned x = 0; x < data.width; ++x) {
					if (x >= preserved.baseX && x - preserved.baseX < preserved.width && y >= preserved.baseY && y - preserved.baseY < preserved.height)
						continue;
					auto* destination = data.bytes.data() + std::uint64_t(y) * data.rowBytes + x * stride;
					std::array<std::uint8_t, 16> pixel{};
					if (policy == "finite_pattern")
						WriteFinitePattern(pixel.data(), data.format, ((x ^ y) & 1u) != 0);
					changed += std::memcmp(destination, pixel.data(), stride) != 0;
					std::memcpy(destination, pixel.data(), stride);
				}
		Require(Hash(Crop(data, valid).bytes) == validHash, "input storage treatment changed requested input pixels");
		Require(Hash(Crop(data, preserved).bytes) == preservedHash, "input storage treatment changed preserved context");
		return { { "policy", policy }, { "format", data.format }, { "width", data.width }, { "height", data.height },
			{ "validRect", RectJson(valid) }, { "validSha256", validHash }, { "capturedSha256", capturedHash },
			{ "preservedRect", RectJson(preserved) }, { "preservedSha256", preservedHash },
			{ "uploadedSha256", Hash(data.bytes) }, { "changedOutsidePixels", changed } };
	}
	struct Variant
	{
		std::string id, axis, pairGroup, history = "static_reset";
		std::vector<ComputeSubrect> rects;
		ComputeSubrect crop;
		bool temporal = false;
		double occupancy = -1;
		std::string inputStorage = "captured";
		std::string inputStorageResource = "all";
		unsigned inputStorageHaloPixels = 0;
		bool sharedInputs = false;
		bool reuseNativeHandles = false;
	};
	Variant CustomRegions(const std::string& encoded, const Eye& eye, unsigned mode)
	{
		Require(mode == 2, "custom regions require stateless C input");
		const auto rectangles = Json::parse(encoded);
		Require(rectangles.is_array() && !rectangles.empty() && rectangles.size() <= kMaximumRegionsPerEye,
			"custom regions require 1..8 rectangles");
		Variant variant{ "custom-regions", "packed_region_experiment", "immutable-owned-pixels", "static_reset", {}, {} };
		variant.sharedInputs = true;
		for (const auto& item : rectangles) {
			Require(item.is_array() && item.size() == 4, "custom rectangle must be [x,y,width,height]");
			for (const auto& coordinate : item)
				Require(coordinate.is_number_unsigned() && coordinate.get<std::uint64_t>() <= 16384,
					"custom rectangle coordinates must be unsigned integers <=16384");
			const ComputeSubrect rect{ item[0].get<unsigned>(), item[1].get<unsigned>(), item[2].get<unsigned>(), item[3].get<unsigned>() };
			Require(rect.width >= 64 && rect.height >= 64 && rect.Fits(eye.color.width, eye.color.height),
				"custom rectangles must fit captured resources and have both extents >=64");
			Require(rectangles.size() <= kDefaultRegionsPerEye || QualifiedExperimentalContextGeometry(rect),
				"higher-count custom regions require the qualified 128-pixel extent floor");
			for (const auto& previous : variant.rects)
				Require(rect.baseX + rect.width <= previous.baseX || previous.baseX + previous.width <= rect.baseX ||
							rect.baseY + rect.height <= previous.baseY || previous.baseY + previous.height <= rect.baseY,
					"custom output ownership rectangles overlap");
			variant.rects.push_back(rect);
		}
		return variant;
	}
	void ConfigureNativeHandleExperiment(Variant& variant, const Frame& frame, std::string_view policy)
	{
		Require(policy == "independent" || policy == "per-eye", "native handle policy must be independent or per-eye");
		Require(frame.metadata.at("mode") == 2 && variant.history == "static_reset" && !variant.temporal &&
					!variant.crop.IsValid() && variant.inputStorage == "captured" && variant.sharedInputs,
			"native handle experiment requires unchanged stateless C custom inputs");
		Require(variant.rects.size() >= 2 && variant.rects.size() <= 4 &&
					std::ranges::all_of(variant.rects, [](const auto& rect) { return QualifiedExperimentalContextGeometry(rect); }),
			"native handle experiment requires two to four regions with extents >=128");
		for (const auto& eye : frame.eyes)
			Require(!eye.featureUpscaling && eye.color.format == 28 && eye.depth.format == 41 && eye.motion.format == 34 && eye.depth.width == eye.color.width &&
						eye.depth.height == eye.color.height && eye.motion.width == eye.color.width && eye.motion.height == eye.color.height,
				"native handle experiment requires RGBA8 C with equal input grids");
		variant.axis = "native_handle_reuse";
		variant.pairGroup = "unchanged-independent-contexts";
		variant.reuseNativeHandles = policy == "per-eye";
	}
	void ValidateIndependentProviderContexts(const Variant& variant, const std::vector<Frame>& frames)
	{
		Require(frames.size() == 1 && frames.front().metadata.at("mode") == 2 && variant.history == "static_reset" &&
					!variant.temporal && !variant.crop.IsValid() && variant.inputStorage == "captured" && variant.sharedInputs && !variant.reuseNativeHandles,
			"provider experiment requires one immutable stateless C capture and independent custom contexts");
		Require(variant.rects.size() >= 1 && variant.rects.size() <= 4 &&
					std::ranges::all_of(variant.rects, [](const auto& rect) { return QualifiedExperimentalContextGeometry(rect); }),
			"provider experiment requires one to four regions with extents >=128");
		for (const auto& eye : frames.front().eyes)
			Require(!eye.featureUpscaling && eye.color.format == 28 && eye.depth.format == 41 && eye.motion.format == 34 &&
						eye.depth.width == eye.color.width && eye.depth.height == eye.color.height &&
						eye.motion.width == eye.color.width && eye.motion.height == eye.color.height &&
						std::ranges::all_of(variant.rects, [&](const auto& rect) { return rect.Fits(eye.color.width, eye.color.height); }),
				"provider experiment requires RGBA8 C with equal input grids and bounded contexts in every eye");
	}
	void ConfigureProviderFloorExperiment(Variant& variant, const std::vector<Frame>& frames, std::string_view providerHash, Json& receipt)
	{
		namespace Floor = NrReplay::ProviderFloor;
		Floor::ValidateIdentity(256, providerHash, Floor::kCodeSha256);
		ValidateIndependentProviderContexts(variant, frames);
		receipt.update({ { "requested", true }, { "originalFloor", 320 }, { "experimentalFloor", 256 },
			{ "providerDiskSha256", providerHash }, { "inMemoryState", "not_loaded" }, { "transitions", Json::array() },
			{ "qualityQualified", false }, { "productionPerformanceQualified", false }, { "modeledShapes", Json::array() } });
		for (const auto& rect : variant.rects) {
			const auto original = Floor::PaddedShape(rect.width, rect.height, 320);
			const auto candidate = Floor::PaddedShape(rect.width, rect.height, 256);
			receipt["modeledShapes"].push_back({ { "active", { rect.width, rect.height } },
				{ "original", { original.width, original.height } }, { "candidate", { candidate.width, candidate.height } },
				{ "scope", "observed_padding_branch_model_not_measured_dispatch" } });
		}
	}
	void ConfigureKernelChainExperiment(const Variant& variant, const std::vector<Frame>& frames,
		std::string_view providerHash, std::string_view mode, Json& receipt)
	{
		Require(mode == "forward" || mode == "group", "kernel chain mode must be forward or group");
		Require(NrReplay::ProviderFloor::CanonicalSha256(providerHash) == NrReplay::ProviderFloor::kProviderSha256,
			"kernel chain experiment requires the pinned provider disk SHA256");
		ValidateIndependentProviderContexts(variant, frames);
		Require(std::ranges::all_of(variant.rects, [&](const auto& rect) {
			return rect.width == variant.rects.front().width && rect.height == variant.rects.front().height;
		}),
			"kernel chain experiment requires equal region shapes");
		receipt.update({ { "requested", true }, { "mode", mode }, { "providerDiskSha256", providerHash },
			{ "inMemoryState", "not_loaded" }, { "transitions", Json::array() },
			{ "qualityQualified", false }, { "productionPerformanceQualified", false } });
	}
	void ValidateKernelPairExperiment(const Variant& variant, const std::vector<Frame>& frames)
	{
		ValidateIndependentProviderContexts(variant, frames);
		Require(frames.front().eyes.size() == 2 && variant.rects.size() == 2 &&
					std::ranges::all_of(variant.rects, [](const auto& rect) { return rect.width == 192 && rect.height == 256; }),
			"kernel pair experiment requires two independent 192x256 regions in each eye");
		for (const auto& eye : frames.front().eyes)
			Require(eye.color.width == 1008 && eye.color.height == 1120,
				"kernel pair experiment requires the qualified 1008x1120 input grid");
	}
	Json NativeHandlePlan(const Variant& variant, unsigned eyes)
	{
		Json slots = Json::array();
		unsigned mask = 0;
		for (unsigned eye = 0; eye < eyes; ++eye)
			for (unsigned region = 0; region < variant.rects.size(); ++region) {
				const auto slot = PhysicalRegionFeatureSlot(eye, variant.reuseNativeHandles ? 0u : region);
				slots.push_back(slot);
				mask |= 1u << slot;
			}
		return { { "nativeHandlePolicy", variant.reuseNativeHandles ? "per-eye" : "independent" },
			{ "nativeHandleSlots", slots }, { "nativeHandleMask", mask }, { "nativeHandleCount", std::popcount(mask) },
			{ "nativeHandleReuseBarriers", variant.reuseNativeHandles ? eyes * (variant.rects.size() - 1) : 0 },
			{ "nativeHandleBarrierPolicy", "global_uav_between_reused_handle_evaluations" } };
	}
	Json ApplyVariantInputStorage(TextureData& data, const ComputeSubrect& outputRect, unsigned width, unsigned height,
		const Variant& variant, std::string_view resource)
	{
		const auto valid = MapComputeSubrect(outputRect, width, height, data.width, data.height);
		const auto context = ExpandCharacterWorkRect(outputRect, width, height, variant.inputStorageHaloPixels);
		const auto preserved = MapComputeSubrect(context, width, height, data.width, data.height);
		const auto policy = variant.inputStorageResource == "all" || variant.inputStorageResource == resource ? variant.inputStorage : "captured";
		return ApplyInputStorage(data, valid, preserved, policy);
	}
	std::vector<Variant> Variants(unsigned width, unsigned height, unsigned guideWidth, unsigned guideHeight, const Difference& control, bool temporal, unsigned capacitySize = 128)
	{
		const auto at = [&](unsigned w, unsigned h) {
			w = std::min(w, width);
			h = std::min(h, height);
			return ComputeSubrect{ std::min(control.peakX > w / 2 ? control.peakX - w / 2 : 0u, width - w),
				std::min(control.peakY > h / 2 ? control.peakY - h / 2 : 0u, height - h), w, h };
		};
		std::vector<Variant> values{ { "nonzero-control", "history", "control", "static_reset", { { 0, 0, width, height } }, {} } };
		if (width >= 256 && height >= 256) {
			const auto fixedOrigin = at(256, 256);
			for (auto shape : { std::array{ 64u, 256u }, std::array{ 128u, 128u }, std::array{ 256u, 64u } })
				values.push_back({ std::format("aspect-{}x{}", shape[0], shape[1]), "aspect_ratio", "aspect-equal-area", "static_reset", { { fixedOrigin.baseX, fixedOrigin.baseY, shape[0], shape[1] } }, {} });
			for (unsigned extent : { 64u, 128u, 256u }) {
				values.push_back({ std::format("width-{}", extent), "width", "width-fixed-height", "static_reset", { { fixedOrigin.baseX, fixedOrigin.baseY, extent, 128 } }, {} });
				values.push_back({ std::format("height-{}", extent), "height", "height-fixed-width", "static_reset", { { fixedOrigin.baseX, fixedOrigin.baseY, 128, extent } }, {} });
			}
			const auto rectangle = at(256, 128);
			values.push_back({ "calls-one", "call_count", "calls-equal-area", "static_reset", { rectangle }, {} });
			values.push_back({ "calls-two", "call_count", "calls-equal-area", "static_reset",
				{ { rectangle.baseX, rectangle.baseY, 128, 128 }, { rectangle.baseX + 128, rectangle.baseY, 128, 128 } }, {} });
			for (unsigned regions : { 1u, 2u, 4u, 8u }) {
				const auto duplicate = at(128, 128);
				for (bool shared : { false, true }) {
					Variant value{ std::format("duplicate-{}-{}", regions, shared ? "shared" : "private"), "native_context_count", "duplicate-geometry", "static_reset", std::vector<ComputeSubrect>(regions, duplicate), {} };
					value.sharedInputs = shared;
					values.push_back(std::move(value));
				}
			}
			for (unsigned regions : { 4u }) {
				Variant value{ std::format("calls-{}", regions == 4 ? "four" : "eight"), "call_count", "calls-equal-area", "static_reset", {}, {} };
				const unsigned columns = regions / 2;
				for (unsigned i = 0; i < regions; ++i)
					value.rects.push_back({ rectangle.baseX + (i % columns) * (256 / columns), rectangle.baseY + (i / columns) * 64, 256 / columns, 64 });
				values.push_back(std::move(value));
			}
			for (unsigned count : { 1u, 2u, 4u, 8u }) {
				const auto base = at(512, 256);
				const unsigned columns = count <= 2 ? count : 4;
				const unsigned rows = count / columns;
				Variant value{ std::format("capacity-calls-{}", count), "call_count", "capacity-calls-equal-area", "static_reset", {}, {} };
				for (unsigned i = 0; i < count; ++i)
					value.rects.push_back({ base.baseX + (i % columns) * (512 / columns), base.baseY + (i / columns) * (256 / rows), 512 / columns, 256 / rows });
				values.push_back(std::move(value));
			}
			for (bool shared : { false, true }) {
				Variant value{ std::format("transport-{}", shared ? "shared" : "private"), "source_transport", "transport-identical-contexts", "static_reset",
					{ { fixedOrigin.baseX, fixedOrigin.baseY, 128, 64 }, { fixedOrigin.baseX + 128, fixedOrigin.baseY + 128, 64, 128 } }, {} };
				value.sharedInputs = shared;
				values.push_back(std::move(value));
			}
			const unsigned quantumX = width / std::gcd(width, guideWidth);
			const unsigned quantumY = height / std::gcd(height, guideHeight);
			auto capacity = at(quantumX <= capacitySize ? capacitySize / quantumX * quantumX : capacitySize,
				quantumY <= capacitySize ? capacitySize / quantumY * quantumY : capacitySize);
			capacity.baseX = capacity.baseX / quantumX * quantumX;
			capacity.baseY = capacity.baseY / quantumY * quantumY;
			values.push_back({ "capacity-full", "capacity", "capacity-identical-integer-crop", "static_reset", { capacity }, {} });
			values.push_back({ "capacity-compact", "capacity", "capacity-identical-integer-crop", "static_reset", { { 0, 0, capacity.width, capacity.height } }, capacity });
			for (const auto* policy : { "captured", "zero", "finite_pattern" })
				values.push_back({ std::format("input-storage-{}", policy), "input_storage", "input-storage-identical-valid-rect", "static_reset", { capacity }, {}, false, -1, policy });
			for (const auto* resource : { "all", "color", "depth", "motion" })
				for (const auto* policy : { "zero", "finite_pattern" })
					for (unsigned halo : { 0u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u, 16384u })
						values.push_back({ std::format("input-context-{}-{}-{}", resource, policy, halo), "input_storage", "input-storage-identical-valid-rect", "static_reset", { capacity }, {}, false, -1, policy, resource, halo });
			for (auto offset : { std::array{ 0u, 0u }, std::array{ 1u, 1u }, std::array{ width - 128, height - 128 } })
				values.push_back({ std::format("offset-{}-{}", offset[0], offset[1]), "offset", "offset-fixed-shape", "static_reset", { { offset[0], offset[1], 128, 128 } }, {} });
		}
		values.push_back({ "cold-creation", "history", "control", "cold_create", { { 0, 0, width, height } }, {} });
		if (temporal) {
			values.push_back({ "temporal-continuous", "history", "temporal-pair", "continuous", { { 0, 0, width, height } }, {}, true });
			values.push_back({ "temporal-reset", "history", "temporal-pair", "static_reset", { { 0, 0, width, height } }, {}, true });
		}
		for (const auto fraction : { 0.01, 0.25, 1.0 })
			values.push_back({ std::format("occupancy-{}", fraction), "mask_occupancy", "composite-only-occupancy", "static_reset",
				{ { 0, 0, width, height } }, {}, false, fraction });
		const auto minimumOrigin = at(std::min({ width, height, 256u }), std::min({ width, height, 256u }));
		for (unsigned side : { 31u, 32u, 63u, 64u, 65u, 128u, 256u })
			if (side <= width && side <= height)
				values.push_back({ std::format("minimum-shape-{}", side), "minimum_shape", "minimum-shape", "static_reset",
					{ { minimumOrigin.baseX, minimumOrigin.baseY, side, side } }, {} });
		return values;
	}
	Json ValidateInputStorage(const std::vector<Frame>& frames, const Difference& control, std::string_view onlyCase)
	{
		Json result = Json::array();
		const auto& first = frames.front().eyes.front();
		for (const auto& variant : Variants(first.color.width, first.color.height, first.depth.width, first.depth.height, control, false)) {
			if (variant.axis != "input_storage")
				continue;
			if (onlyCase.empty() ? variant.id.starts_with("input-context-") : variant.id != onlyCase)
				continue;
			Json inputs = Json::array();
			for (unsigned eyeIndex = 0; eyeIndex < frames.front().eyes.size(); ++eyeIndex) {
				const auto& eye = frames.front().eyes[eyeIndex];
				for (const auto& [name, source] : { std::pair{ "color", &eye.color }, { "depth", &eye.depth }, { "motion", &eye.motion } }) {
					auto data = *source;
					auto proof = ApplyVariantInputStorage(data, variant.rects.front(), first.color.width, first.color.height, variant, name);
					proof["eye"] = eyeIndex;
					proof["resource"] = name;
					inputs.push_back(std::move(proof));
				}
			}
			result.push_back({ { "id", variant.id }, { "inputs", std::move(inputs) } });
		}
		Require(!result.empty(), "input storage validation requires at least 256x256 native colour and a known storage case");
		return result;
	}
	Tuning ReadTuning(const Json& json)
	{
		Tuning tuning;
		tuning.intensity = json.at("intensity").get<float>();
		tuning.localToneStrength = json.at("localToneStrength").get<float>();
		tuning.localStructureStrength = json.at("localStructureStrength").get<float>();
		tuning.skinStructureStrength = json.at("skinStructureStrength").get<float>();
		tuning.style = json.at("style").get<unsigned>();
		tuning.useAutoMask = true;
		tuning.uiCorrection = false;
		tuning.singleSubrectScale = 1.0f;
		return tuning;
	}
	struct Session
	{
		ComPtr<IDXGIAdapter3> adapter;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		D3D12Interop interop;
		std::unique_ptr<NrReplay::ProviderFloorProbe> providerFloor;
		std::unique_ptr<NrReplay::ProviderKernelChainProbe> kernelChain;
		bool active = false;
		bool ngxInitialized = false;
		~Session()
		{
			if (!Close())
				logger::error("Replay session shutdown failed");
		}
		bool Close()
		{
			if (!active)
				return true;
			active = false;
			if (interop.IsRecording())
				(void)interop.AbortD3D12();
			logger::info("Replay shutdown: waiting for native GPU idle and retiring runtime");
			const bool idle = interop.WaitForIdle();
			const bool chainRestored = !kernelChain || kernelChain->Restore(idle);
			if (!chainRestored)
				(void)kernelChain.release();
			const bool restored = (!providerFloor || providerFloor->Restore(idle)) && chainRestored;
			if (!idle || !restored || !Runtime::Instance().Shutdown()) {
				Runtime::Instance().AbandonUnsafe();
				interop.AbandonUnsafe();
				return false;
			} else {
				bool closed = true;
				if (ngxInitialized) {
					logger::info("Replay shutdown: retiring NGX SDK bootstrap");
					const auto status = NVSDK_NGX_D3D11_Shutdown1(nullptr);
					closed = NVSDK_NGX_SUCCEED(status);
					if (!closed)
						logger::error("Replay NGX bootstrap shutdown failed: 0x{:08x}", static_cast<unsigned>(status));
				}
				logger::info("Replay shutdown: retiring interop resources");
				return interop.Shutdown() && closed;
			}
		}
		void Retire()
		{
			Require(interop.WaitForIdle(), "GPU idle proof failed");
			Require(Runtime::Instance().ResetFeatures(), "feature retirement failed");
		}
		void Restart()
		{
			Retire();
			Require(!kernelChain || kernelChain->Restore(true), "case kernel chain restoration failed");
			Require(!providerFloor || providerFloor->Restore(true), "case provider floor restoration failed");
			Require(Runtime::Instance().Shutdown(), "case runtime shutdown failed");
			Require(interop.Shutdown(), "case resource retirement failed");
			Require(interop.Initialize(adapter.Get(), device.Get(), context.Get()), interop.LastOperation());
			if (!Runtime::Instance().Initialize(interop.Device()))
				throw std::runtime_error(Runtime::Instance().Detail());
			if (providerFloor)
				providerFloor->Apply(Runtime::Instance().Path(), Runtime::Instance().Hash());
			if (kernelChain)
				kernelChain->Apply(Runtime::Instance().Path(), Runtime::Instance().Hash());
		}
		Json Memory()
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
			const auto localResult = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
			const auto nonlocalResult = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
			return { { "local", SUCCEEDED(localResult) ? Json(local.CurrentUsage) : Json(nullptr) },
				{ "nonlocal", SUCCEEDED(nonlocalResult) ? Json(nonlocal.CurrentUsage) : Json(nullptr) },
				{ "localBudget", SUCCEEDED(localResult) ? Json(local.Budget) : Json(nullptr) } };
		}
	};
	struct Resources
	{
		SharedTexture color, depth, motion, output;
		unsigned slot = 0;
		unsigned eye = 0;
		ComputeSubrect rect;
		TextureData source;
		TextureData sentinel;
	};
	SharedTexture CreateTexture(Session& session, const TextureData& data, const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = data.width;
		desc.Height = data.height;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = static_cast<DXGI_FORMAT>(data.format);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		SharedTexture texture;
		Require(session.interop.CreateSharedTexture(desc, texture, name), session.interop.LastOperation());
		return texture;
	}
	void Transition(ID3D12GraphicsCommandList* list, const std::vector<Resources>& resources, bool begin)
	{
		struct SourceState
		{
			ID3D12Resource* resource;
			D3D12_RESOURCE_STATES featureState;
		};
		std::array<SourceState, kMaximumRegionEvaluations * 4> states{};
		std::size_t count = 0;
		for (const auto& resource : resources)
			for (const auto* texture : { &resource.color, &resource.depth, &resource.motion, &resource.output })
				Require(AddSourceTransition(states, count, SourceState{ texture->resource12.Get(), texture == &resource.output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE }), "conflicting or oversized source transition batch");
		std::array<D3D12_RESOURCE_BARRIER, states.size()> barriers{};
		for (std::size_t index = 0; index < count; ++index) {
			auto& barrier = barriers[index];
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = states[index].resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = begin ? D3D12_RESOURCE_STATE_COMMON : states[index].featureState;
			barrier.Transition.StateAfter = begin ? states[index].featureState : D3D12_RESOURCE_STATE_COMMON;
		}
		list->ResourceBarrier(static_cast<unsigned>(count), barriers.data());
	}
	TextureData Download(Session& session, const SharedTexture& texture, Clock::time_point deadline)
	{
		auto desc = texture.desc;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = desc.MiscFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		Check(session.device->CreateTexture2D(&desc, nullptr, &staging), "output staging texture");
		Util::SetResourceName(staging.Get(), "NRReplay::OutputReadback");
		session.context->CopyResource(staging.Get(), texture.resource11.Get());
		session.context->Flush();
		D3D11_MAPPED_SUBRESOURCE mapped{};
		HRESULT result;
		do {
			result = session.context->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (result == DXGI_ERROR_WAS_STILL_DRAWING)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (result == DXGI_ERROR_WAS_STILL_DRAWING && Clock::now() < deadline);
		Check(result, "bounded output readback");
		TextureData data{ desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.Width * BytesPerPixel(desc.Format), {} };
		data.bytes.resize(std::uint64_t(data.rowBytes) * data.height);
		for (unsigned y = 0; y < data.height; ++y)
			std::memcpy(data.bytes.data() + std::uint64_t(y) * data.rowBytes,
				static_cast<const std::uint8_t*>(mapped.pData) + std::uint64_t(y) * mapped.RowPitch, data.rowBytes);
		session.context->Unmap(staging.Get(), 0);
		return data;
	}
	Json SaveTextureEvidence(const std::filesystem::path& root, const std::string& name, const TextureData& pixels, unsigned slot, std::string_view scope)
	{
		std::ofstream stream(root / name, std::ios::binary);
		stream.write(reinterpret_cast<const char*>(pixels.bytes.data()), static_cast<std::streamsize>(pixels.bytes.size()));
		Require(bool(stream), "native output evidence write failed");
		return { { "file", name }, { "sha256", Hash(pixels.bytes) }, { "format", pixels.format },
			{ "width", pixels.width }, { "height", pixels.height }, { "rowBytes", pixels.rowBytes }, { "slot", slot }, { "scope", scope } };
	}
	void SaveScheduleRepetitions(Json& sample, NrReplay::KernelScheduleReadback& readback, const ExecutionSnapshot& finished,
		std::span<const Resources> resources, std::span<const std::array<TextureData, 2>> sentinels,
		const std::filesystem::path& root, const std::string& variant, unsigned iteration, unsigned repetitions, bool alternateSentinel)
	{
		sample["kernelScheduleRepetitions"] = Json::array();
		sample["kernelScheduleTimingScope"] = "complete_frozen_schedule_excludes_sentinel_reset_output_copy_and_between_repetition_uav_join";
		for (unsigned repetition = 0; repetition < repetitions; ++repetition) {
			const auto& gpu = finished.regions[repetition].evaluationGpu;
			Require(gpu.state == ExecutionTimingState::Complete && gpu.microseconds && *gpu.microseconds > 0,
				"kernel schedule repetition GPU timing unavailable");
			Json repeated{ { "repetition", repetition }, { "gpuMicroseconds", *gpu.microseconds }, { "outputs", Json::array() } };
			for (std::size_t i = 0; i < resources.size(); ++i) {
				auto pixels = resources[i].source;
				pixels.bytes = readback.Read(repetition, static_cast<unsigned>(i));
				const auto& sentinel = sentinels[i][repetition & 1u];
				const auto name = std::format("{}-{:03}-repeat{}-slot{}-full.bin", variant, iteration, repetition, resources[i].slot);
				auto output = SaveTextureEvidence(root, name, pixels, resources[i].slot, "full_output_resource");
				output.update({ { "ownedRect", RectJson(resources[i].rect) }, { "ownedSha256", Hash(Crop(pixels, resources[i].rect).bytes) },
					{ "sentinelSha256", Hash(sentinel.bytes) }, { "sentinelPattern", repetition & 1u },
					{ "alternateBytePattern", (repetition & 1u) ? !alternateSentinel : alternateSentinel },
					{ "footprint", Footprint(sentinel, pixels, resources[i].rect) } });
				repeated["outputs"].push_back(std::move(output));
			}
			sample["kernelScheduleRepetitions"].push_back(std::move(repeated));
		}
		sample["kernelScheduleReadback"] = readback.Receipt();
	}
	Json RunCase(Session& session, const Variant& variant, const std::vector<Frame>& frames,
		unsigned warmup, unsigned samples, Clock::time_point deadline, const std::filesystem::path& outputRoot, bool alternateSentinel,
		NrReplay::GpuCapture& capture, bool batchTimingOnly, bool captureKernelModules, bool kernelPair, unsigned scheduleRepetitions)
	{
		const auto& first = frames.front();
		const auto width = variant.crop.IsValid() ? variant.crop.width : first.eyes.front().color.width;
		const auto height = variant.crop.IsValid() ? variant.crop.height : first.eyes.front().color.height;
		const auto& firstEye = first.eyes.front();
		const auto guideCrop = variant.crop.IsValid() ? MapComputeSubrect(variant.crop,
															firstEye.color.width, firstEye.color.height, firstEye.depth.width, firstEye.depth.height) :
		                                                ComputeSubrect{};
		const unsigned guideWidth = guideCrop.IsValid() ? guideCrop.width : firstEye.depth.width;
		const unsigned guideHeight = guideCrop.IsValid() ? guideCrop.height : firstEye.depth.height;
		const auto count = static_cast<unsigned>(variant.rects.size() * first.eyes.size());
		Require(!variant.rects.empty() && variant.rects.size() <= kMaximumRegionsPerEye && count <= kMaximumRegionEvaluations, "replay exceeds bounded region capacity");
		Json value{ { "id", variant.id }, { "axis", variant.axis }, { "pairGroup", variant.pairGroup },
			{ "route", std::string(1, char('A' + first.metadata.at("mode").get<unsigned>())) }, { "mode", first.metadata.at("mode") },
			{ "history", variant.history }, { "creationExtent", { width, height } },
			{ "resourceExtents", { { "color", { width, height } }, { "depth", { guideWidth, guideHeight } }, { "motion", { guideWidth, guideHeight } }, { "output", { width, height } } } },
			{ "evaluatedRects", Json::array() }, { "evaluatedSourceRects", Json::array() }, { "evaluatedGuideRects", Json::array() }, { "evaluationsPerSample", count }, { "logicalEyeCount", first.eyes.size() },
			{ "featureUpscaling", firstEye.featureUpscaling }, { "useAutoMask", true }, { "controlMaskPassed", false },
			{ "warmupIterations", warmup }, { "requestedSamples", samples }, { "colorConfiguration", first.metadata.at("colorConfiguration") },
			{ "tuning", first.metadata.at("tuning") }, { "sourceFrameIndices", Json::array() },
			{ "sharedInputs", variant.sharedInputs }, { "transportBypass", false }, { "applyModelEdit", true }, { "samples", Json::array() },
			{ "characterSelection", first.metadata.value("characterSelection", false) },
			{ "contextIds", Json::array() }, { "initializationFingerprint", "fresh-features-first-source-reset" },
			{ "sourceGuideAlignment", "captured-native-grids-exact-integer-crop-no-resample" },
			{ "inputStoragePolicy", variant.inputStorage }, { "inputStorageDomain", "outside_requested_native_rectangles_not_a_proven_read_footprint" },
			{ "inputStorageResource", variant.inputStorageResource }, { "inputStorageHaloPixels", variant.inputStorageHaloPixels },
			{ "temporalSequence", variant.temporal }, { "qualityAssessment", "not_performed" },
			{ "status", "unavailable" }, { "reason", "not_started" } };
		value.update(NativeHandlePlan(variant, static_cast<unsigned>(first.eyes.size())));
		value["batchTimingOnly"] = batchTimingOnly;
		if (variant.occupancy >= 0) {
			value["maskOccupancyFraction"] = variant.occupancy;
			value["maskPolicy"] = "synthetic_CSX_composite_only_binary_occupancy_proxy_after_native_inference_not_GPU_composite_cost";
		}
		for (std::size_t eye = 0; eye < first.eyes.size(); ++eye)
			for (std::size_t region = 0; region < variant.rects.size(); ++region) {
				value["evaluatedRects"].push_back(RectJson(variant.rects[region]));
				auto sourceRect = variant.rects[region];
				if (variant.crop.IsValid()) {
					sourceRect.baseX += variant.crop.baseX;
					sourceRect.baseY += variant.crop.baseY;
				}
				value["evaluatedSourceRects"].push_back(RectJson(sourceRect));
				value["evaluatedGuideRects"].push_back(RectJson(MapComputeSubrect(variant.rects[region], width, height, guideWidth, guideHeight)));
				value["contextIds"].push_back(std::format("{}:fresh-slot-{}", variant.id,
					PhysicalRegionFeatureSlot(static_cast<unsigned>(eye), variant.reuseNativeHandles ? 0u : static_cast<unsigned>(region))));
			}
		if (variant.temporal && frames.size() < warmup + samples) {
			value["reason"] = "insufficient_consecutive_captured_frames_for_matched_warmup_and_samples";
			return value;
		}
		if (variant.crop.IsValid()) {
			const auto exact = [](unsigned origin, unsigned extent, unsigned source, unsigned target) {
				return std::uint64_t(origin) * target % source == 0 && std::uint64_t(extent) * target % source == 0;
			};
			if (!exact(variant.crop.baseX, variant.crop.width, firstEye.color.width, firstEye.depth.width) ||
				!exact(variant.crop.baseY, variant.crop.height, firstEye.color.height, firstEye.depth.height)) {
				value["reason"] = "compact_crop_cannot_preserve_integer_source_guide_phase";
				return value;
			}
		}
		try {
			session.Restart();
			std::vector<Resources> resources;
			std::uint64_t logicalBytes = 0;
			for (unsigned eye = 0; eye < first.eyes.size(); ++eye)
				for (unsigned region = 0; region < variant.rects.size(); ++region) {
					Resources resource;
					resource.slot = PhysicalRegionFeatureSlot(eye, region);
					Require(resource.slot < Runtime::kFeatureSlotCount, "region capacity exceeds native slot envelope");
					resource.eye = eye;
					resource.rect = variant.rects[region];
					const auto& source = first.eyes[eye];
					const auto make = [&](const TextureData& input, const char* name, bool guide) {
						const auto cropped = variant.crop.IsValid() ? Crop(input, guide ? guideCrop : variant.crop) : input;
						logicalBytes += cropped.bytes.size();
						Require(logicalBytes <= kResourceBudget, "logical GPU resource budget exceeded");
						return CreateTexture(session, cropped, name);
					};
					if (variant.sharedInputs && region) {
						const auto& owner = resources[eye * variant.rects.size()];
						resource.color = owner.color;
						resource.depth = owner.depth;
						resource.motion = owner.motion;
					} else {
						resource.color = make(source.color, "NRReplay::Color", false);
						resource.depth = make(source.depth, "NRReplay::Depth", true);
						resource.motion = make(source.motion, "NRReplay::Motion", true);
					}
					resource.output = make(source.color, "NRReplay::PrivateOutput", false);
					resources.push_back(std::move(resource));
				}
			value["logicalResourceBytes"] = logicalBytes;
			const auto tuning = ReadTuning(first.metadata.at("tuning"));
			for (unsigned iteration = 0; iteration < warmup + samples; ++iteration) {
				if (Clock::now() >= deadline) {
					value["reason"] = "bounded_campaign_deadline";
					return value;
				}
				const auto started = Clock::now();
				const auto sourceIndex = variant.temporal ? iteration : 0u;
				value["sourceFrameIndices"].push_back(sourceIndex);
				const auto& frame = frames[sourceIndex];
				Json sample{ { "iteration", iteration }, { "warmup", iteration < warmup }, { "success", false }, { "reason", "incomplete_submission" },
					{ "reset", variant.history != "continuous" || iteration == 0 },
					{ "gpuMicroseconds", nullptr }, { "evaluationGpuMicroseconds", Json::array() }, { "runtimeCalls", Json::array() },
					{ "createdFeatureCount", 0 }, { "evaluationCount", 0 }, { "nonzeroEditPixels", 0 }, { "maximumAbsEdit", 0 } };
				std::unique_ptr<NrReplay::KernelScheduleReadback> scheduleReadback;
				std::vector<std::array<TextureData, 2>> scheduleSentinels;
				try {
					const bool captureSample = capture.Enabled() && iteration == warmup;
					sample["externalGpuCapture"] = captureSample;
					if (captureSample)
						capture.Begin(session.interop.Device());
					if (variant.history == "cold_create")
						session.Retire();
					const auto memoryBefore = session.Memory();
					for (auto& resource : resources) {
						const auto& eye = frame.eyes[resource.eye];
						const auto upload = [&](const TextureData& input, SharedTexture& texture, bool guide, const char* name) {
							auto cropped = variant.crop.IsValid() ? Crop(input, guide ? guideCrop : variant.crop) : input;
							if (variant.axis == "input_storage" || variant.axis == "capacity") {
								auto proof = ApplyVariantInputStorage(cropped, resource.rect, width, height, variant, name);
								proof["slot"] = resource.slot;
								proof["resource"] = name;
								if (!sample.contains("inputStorage"))
									sample["inputStorage"] = Json::array();
								sample["inputStorage"].push_back(std::move(proof));
							}
							session.context->UpdateSubresource(texture.resource11.Get(), 0, nullptr, cropped.bytes.data(), cropped.rowBytes, 0);
						};
						if (!variant.sharedInputs || resource.slot < kLogicalFeatureSlotCount) {
							upload(eye.color, resource.color, false, "color");
							upload(eye.depth, resource.depth, true, "depth");
							upload(eye.motion, resource.motion, true, "motion");
						}
						resource.source = variant.crop.IsValid() ? Crop(eye.color, variant.crop) : eye.color;
						resource.sentinel = Sentinel(resource.source, alternateSentinel);
						session.context->UpdateSubresource(resource.output.resource11.Get(), 0, nullptr, resource.sentinel.bytes.data(), resource.sentinel.rowBytes, 0);
					}
					if (scheduleRepetitions > 1 && iteration >= warmup) {
						std::vector<NrReplay::KernelScheduleReadback::Input> inputs;
						scheduleSentinels.reserve(resources.size());
						for (const auto& resource : resources) {
							scheduleSentinels.push_back({ resource.sentinel, Sentinel(resource.source, !alternateSentinel) });
							const auto& patterns = scheduleSentinels.back();
							inputs.push_back({ resource.output.resource12.Get(), resource.source.width, resource.source.height, resource.source.rowBytes,
								{ patterns[0].bytes, patterns[1].bytes } });
						}
						scheduleReadback = std::make_unique<NrReplay::KernelScheduleReadback>(session.interop.Device(), inputs, scheduleRepetitions);
					}
					ID3D12GraphicsCommandList* list = nullptr;
					Require(session.interop.BeginD3D12(&list), session.interop.LastOperation());
					unsigned slotMask = 0;
					std::uint64_t area = 0;
					for (const auto& resource : resources) {
						slotMask |= 1u << resource.slot;
						area += resource.rect.Area();
					}
					const auto evidence = std::make_shared<ExecutionEvidence>(ExecutionDescriptor{});
					D3D12InteropSubmissionTiming timing;
					timing.frameId = iteration;
					timing.pixelCount = area;
					timing.evaluationCount = count;
					timing.featureSlotMask = slotMask;
					timing.logicalEyeCount = static_cast<unsigned>(first.eyes.size());
					timing.insertionPoint = static_cast<InsertionPoint>(first.metadata.at("insertionPoint").get<unsigned>());
					timing.execution = evidence;
					Require(session.interop.BeginFeatureTiming(list, timing), session.interop.LastOperation());
					Transition(list, resources, true);
					std::unique_ptr<NrReplay::KernelCommandCapture> commandCapture;
					std::unique_ptr<NrReplay::KernelChainCommandList, NrReplay::KernelChainCommandList::Deleter> chainList;
					if (session.kernelChain) {
						session.kernelChain->BeginSample(iteration, iteration < warmup);
						NrReplay::KernelChainCommandList::BarrierCallback barriers;
						NrReplay::KernelChainCommandList::HeapCallback heaps;
						NrReplay::KernelChainCommandList::BarrierDispositionCallback barrierDisposition;
						NrReplay::KernelChainCommandList::HeapDispositionCallback heapDisposition;
						if (captureKernelModules) {
							commandCapture = std::make_unique<NrReplay::KernelCommandCapture>(sample, true);
							barriers = [&](UINT barrierCount, const D3D12_RESOURCE_BARRIER* values) {
								commandCapture->Barriers(barrierCount, values);
							};
							heaps = [&](UINT heapCount, ID3D12DescriptorHeap* const* values) {
								commandCapture->Heaps(heapCount, values);
							};
						}
						if (kernelPair) {
							barrierDisposition = [&](UINT barrierCount, const D3D12_RESOURCE_BARRIER* values) {
								return session.kernelChain->BarrierDisposition(barrierCount, values);
							};
							heapDisposition = [&](UINT heapCount, ID3D12DescriptorHeap* const* values) {
								return session.kernelChain->HeapDisposition(heapCount, values);
							};
						}
						chainList.reset(new NrReplay::KernelChainCommandList(list, [&](std::string_view name) {
							if (commandCapture)
								commandCapture->BeforeCommand();
							return session.kernelChain->BeforeCommand(name); }, [&](std::string_view reason) {
							if (commandCapture)
								commandCapture->RecordFailure(reason);
							logger::error("Kernel chain command-list rejection: {}", reason); }, std::move(barriers), std::move(heaps), std::move(barrierDisposition), std::move(heapDisposition)));
					}
					auto* evaluationList = chainList ? static_cast<ID3D12GraphicsCommandList*>(chainList.get()) : list;
					const bool reset = variant.history != "continuous" || iteration == 0;
					std::uint64_t creationCpu = 0, evaluationCpu = 0;
					unsigned created = 0, evaluated = 0;
					const auto submissionStarted = Clock::now();
					for (unsigned i = 0; i < resources.size(); ++i) {
						const auto& resource = resources[i];
						if (commandCapture)
							commandCapture->SetEvaluation(resource.eye, i % static_cast<unsigned>(variant.rects.size()));
						const auto& eye = frame.eyes[resource.eye];
						const auto nativeSlot = value["nativeHandleSlots"][i].get<unsigned>();
						if (variant.reuseNativeHandles && i % variant.rects.size() != 0) {
							// Shared handle scratch must finish before the next independent call.
							D3D12_RESOURCE_BARRIER barrier{};
							barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
							list->ResourceBarrier(1, &barrier);
						}
						RuntimeExecutionEvidence native;
						bool attempted = false;
						const auto nativeLayout = BuildNativeEvaluationLayout(
							{ width, height }, { guideWidth, guideHeight }, { width, height }, {}, resource.rect,
							{ true, eye.motionScale[0], eye.motionScale[1] }, eye.featureUpscaling);
						if (session.kernelChain)
							session.kernelChain->BeginEvaluation(evaluationList, list, resource.eye, i % static_cast<unsigned>(variant.rects.size()));
						if (session.kernelChain && (Runtime::Instance().GetResidentFeatureMask() & (1u << nativeSlot)) == 0)
							Require(session.kernelChain->BeforeCommand("native_feature_creation"), "kernel chain feature-creation boundary failed");
						if (session.kernelChain && !batchTimingOnly) {
							Require(session.kernelChain->BeforeCommand("ReplayBeginEvaluationTiming"), "kernel chain pre-evaluation flush failed");
							session.interop.BeginEvaluationTiming(list, i);
						}
						const bool succeeded = Runtime::Instance().Execute(evaluationList, nativeSlot,
							resource.color.resource12.Get(), resource.depth.resource12.Get(), resource.motion.resource12.Get(), resource.output.resource12.Get(), nullptr,
							nativeLayout, tuning, reset, &attempted, &native, (session.kernelChain || batchTimingOnly) ? nullptr : &session.interop, i);
						if (session.kernelChain) {
							if (!batchTimingOnly) {
								Require(session.kernelChain->BeforeCommand("ReplayEndEvaluationTiming"), "kernel chain post-evaluation flush failed");
								session.interop.EndEvaluationTiming(list, i);
							}
							session.kernelChain->EndEvaluation();
							session.kernelChain->ThrowIfFailed();
							Require(chainList->Healthy(), chainList->FailureReason());
						}
						created += native.createSucceeded ? 1 : 0;
						evaluated += native.evaluateSucceeded ? 1 : 0;
						creationCpu += native.createCpuMicroseconds.value_or(0);
						evaluationCpu += native.evaluateCpuMicroseconds.value_or(0);
						sample["createdFeatureCount"] = created;
						sample["evaluationCount"] = evaluated;
						sample["createCpuMicroseconds"] = creationCpu;
						sample["evalCpuMicroseconds"] = evaluationCpu;
						sample["runtimeCalls"].push_back({ { "slot", resource.slot }, { "nativeHandleSlot", nativeSlot }, { "createAttempted", native.createAttempted }, { "createSucceeded", native.createSucceeded },
							{ "evaluationAttempted", attempted }, { "evaluationSucceeded", native.evaluateSucceeded },
							{ "createResult", native.createResult ? Json(*native.createResult) : Json(nullptr) },
							{ "evaluateResult", native.evaluateResult ? Json(*native.evaluateResult) : Json(nullptr) } });
						Require(succeeded && attempted && native.evaluateSucceeded, Runtime::Instance().Detail());
					}
					if (session.kernelChain) {
						Require(!kernelPair || iteration < warmup || created == 0,
							"kernel pair steady sample unexpectedly created a native feature");
						if (scheduleReadback) {
							session.kernelChain->FinishCommandList([&](unsigned repetition) {
								scheduleReadback->RecordBefore(list, repetition);
								session.interop.BeginEvaluationTiming(list, repetition); }, [&](unsigned repetition) {
								session.interop.EndEvaluationTiming(list, repetition);
								scheduleReadback->RecordAfter(list, repetition); });
							for (std::size_t i = 0; i < resources.size(); ++i)
								resources[i].sentinel = scheduleSentinels[i][(scheduleRepetitions - 1) & 1u];
						} else {
							session.kernelChain->FinishCommandList();
						}
						session.kernelChain->ThrowIfFailed();
						Require(chainList->Healthy() && chainList->OutstandingReferences() == 1,
							"kernel chain proxy failed or provider retained an unexpected command-list reference");
					}
					sample["submissionCpuMicroseconds"] = std::chrono::duration<double, std::micro>(Clock::now() - submissionStarted).count();
					sample["submissionCpuScope"] = scheduleReadback ?
					                                   "one_native_recording_and_repeated_schedule_with_copy_commands_excludes_receipt_encoding_gpu_wait_and_cpu_readback" :
					                                   "native_evaluation_loop_and_final_chain_flush_excludes_receipt_encoding_gpu_wait_and_readback";
					if (commandCapture)
						commandCapture->Complete();
					if (session.kernelChain)
						sample["kernelChain"] = session.kernelChain->SampleReceipt();
					sample["retainedKernelChainProxiesUntilExit"] = NrReplay::KernelChainCommandList::RetainedUntilExitCount();
					if (session.kernelChain && !batchTimingOnly)
						sample["kernelChainEvaluationGpuScope"] = "execute_including_feature_creation_on_first_warmup_only";
					if (variant.axis == "native_handle_reuse") {
						sample["residentNativeHandleMask"] = Runtime::Instance().GetResidentFeatureMask();
						sample["nativeHandleReuseBarriers"] = value["nativeHandleReuseBarriers"];
						Require(sample["residentNativeHandleMask"] == value["nativeHandleMask"] &&
									created == (iteration == 0 ? value["nativeHandleCount"].get<unsigned>() : 0u),
							"native handle residency or creation differs from the admitted plan");
					}
					Transition(list, resources, false);
					Require(session.interop.EndFeatureTiming(list), session.interop.LastOperation());
					Require(session.interop.EndD3D12(), session.interop.LastOperation());
					Require(session.interop.WaitForIdle(evidence), "replay submission GPU idle proof failed");
					if (scheduleReadback)
						scheduleReadback->ConfirmIdle();
					const auto finished = evidence->Snapshot();
					const auto memoryAfter = session.Memory();
					sample.update(Json{ { "iteration", iteration }, { "warmup", iteration < warmup }, { "success", true }, { "reason", "" },
						{ "reset", reset }, { "createdFeatureCount", created }, { "evaluationCount", evaluated }, { "evaluatedPixels", area },
						{ "gpuMicroseconds", finished.batchGpu.microseconds ? Json(*finished.batchGpu.microseconds) : Json(nullptr) },
						{ "evaluationGpuMicroseconds", Json::array() }, { "createCpuMicroseconds", creationCpu }, { "evalCpuMicroseconds", evaluationCpu },
						{ "memory", { { "localUsageBefore", memoryBefore["local"] }, { "localUsageAfter", memoryAfter["local"] },
										{ "nonlocalUsageBefore", memoryBefore["nonlocal"] }, { "nonlocalUsageAfter", memoryAfter["nonlocal"] } } },
						{ "nonzeroEditPixels", 0 }, { "maximumAbsEdit", 0 }, { "outputFiles", Json::array() }, { "providerFootprint", Json::array() } });
					if (scheduleReadback) {
						sample["kernelScheduleRepeatCount"] = scheduleRepetitions;
						sample["executedSchedulePixels"] = area * scheduleRepetitions;
						SaveScheduleRepetitions(sample, *scheduleReadback, finished, resources, scheduleSentinels,
							outputRoot, variant.id, iteration, scheduleRepetitions, alternateSentinel);
					}
					std::uint64_t edits = 0;
					double maxEdit = 0;
					for (unsigned i = 0; i < resources.size(); ++i) {
						sample["evaluationGpuMicroseconds"].push_back(!scheduleReadback && finished.regions[i].evaluationGpu.microseconds ? Json(*finished.regions[i].evaluationGpu.microseconds) : Json(nullptr));
						const auto pixels = Download(session, resources[i].output, std::min(deadline, Clock::now() + std::chrono::seconds(2)));
						auto footprint = Footprint(resources[i].sentinel, pixels, resources[i].rect);
						footprint["alternateBytePattern"] = scheduleReadback && ((scheduleRepetitions - 1) & 1u) ? !alternateSentinel : alternateSentinel;
						if (variant.sharedInputs && i % variant.rects.size() == 0) {
							const auto prepared = Download(session, resources[i].color, std::min(deadline, Clock::now() + std::chrono::seconds(2)));
							Require(prepared.bytes == resources[i].source.bytes, "native evaluation modified immutable prepared colour");
							if (variant.axis == "native_handle_reuse" || variant.axis == "packed_region_experiment") {
								const auto& eye = frame.eyes[resources[i].eye];
								sample["immutableInputChecks"].push_back({ { "eye", resources[i].eye }, { "resource", "color" },
									{ "sha256", Hash(prepared.bytes) } });
								for (const auto& [name, texture, input] : {
										 std::tuple{ "depth", &resources[i].depth, &eye.depth },
										 std::tuple{ "motion", &resources[i].motion, &eye.motion } }) {
									const auto unchanged = Download(session, *texture, std::min(deadline, Clock::now() + std::chrono::seconds(2)));
									Require(unchanged.bytes == input->bytes, "native evaluation modified immutable guide input");
									sample["immutableInputChecks"].push_back({ { "eye", resources[i].eye }, { "resource", name },
										{ "sha256", Hash(unchanged.bytes) } });
								}
							}
						}
						if ((variant.axis == "native_handle_reuse" || variant.axis == "packed_region_experiment") && footprint.at("modifiedOutsidePixels") != 0) {
							sample["success"] = false;
							sample["reason"] = "context_probe_wrote_outside_evaluation";
						}
						sample["providerFootprint"].push_back(footprint);
						if (footprint.at("unchangedInsidePixels") != 0 || footprint.at("nonfiniteInsidePixels") != 0) {
							sample["success"] = false;
							sample["reason"] = footprint.at("nonfiniteInsidePixels") != 0 ? "nonfinite_evaluated_pixels" :
							                                                                "ambiguous_output_sentinel_match";
						} else {
							const auto difference = Compare(resources[i].source, pixels, resources[i].rect);
							edits += difference.pixels;
							maxEdit = std::max(maxEdit, difference.maximum);
						}
						if (variant.occupancy >= 0) {
							auto composite = resources[i].source;
							const auto& rect = resources[i].rect;
							const auto selected = static_cast<std::uint64_t>(std::floor(rect.Area() * variant.occupancy));
							const auto pixelBytes = BytesPerPixel(pixels.format);
							std::uint64_t copied = 0;
							for (unsigned y = rect.baseY; y < rect.baseY + rect.height; ++y)
								for (unsigned x = rect.baseX; x < rect.baseX + rect.width; ++x) {
									if (copied++ >= selected)
										continue;
									const auto offset = std::uint64_t(y) * pixels.rowBytes + x * pixelBytes;
									std::memcpy(composite.bytes.data() + offset, pixels.bytes.data() + offset, pixelBytes);
								}
							if (!sample.contains("maskComposites"))
								sample["maskComposites"] = Json::array();
							sample["maskComposites"].push_back({ { "maskSelectedPixels", selected }, { "maskTotalPixels", rect.Area() },
								{ "sha256", Hash(composite.bytes) }, { "nativeInputsUnmodified", true }, { "excludedFromGpuTiming", true } });
						}
						if (variant.temporal || variant.axis == "capacity" || variant.axis == "input_storage" || variant.axis == "source_transport" || variant.axis == "native_context_count" || variant.axis == "call_count" || variant.axis == "packed_region_experiment" || variant.axis == "native_handle_reuse") {
							const bool retainFullResource = variant.temporal && variant.axis != "capacity";
							const auto retained = retainFullResource ? pixels : Crop(pixels, resources[i].rect);
							const auto name = std::format("{}-{:03}-slot{}.bin", variant.id, iteration, resources[i].slot);
							sample["outputFiles"].push_back(SaveTextureEvidence(outputRoot, name, retained, resources[i].slot,
								retainFullResource ? "full_output_resource" : "evaluated_rectangle"));
						}
						if (scheduleReadback) {
							const auto ownedHash = Hash(Crop(pixels, resources[i].rect).bytes);
							for (const auto& repeated : sample.at("kernelScheduleRepetitions")) {
								const auto& output = repeated.at("outputs").at(i);
								const auto& repeatedFootprint = output.at("footprint");
								Require(output.at("ownedSha256") == ownedHash && repeatedFootprint.at("modifiedOutsidePixels") == 0 &&
											repeatedFootprint.at("unchangedInsidePixels") == 0 && repeatedFootprint.at("nonfiniteInsidePixels") == 0,
									"repeated schedule output differs or violates its write footprint");
							}
							Require(Hash(pixels.bytes) == sample.at("kernelScheduleRepetitions").back().at("outputs").at(i).at("sha256").get<std::string>(),
								"final output differs from the final recorded repetition");
						}
					}
					sample["nonzeroEditPixels"] = edits;
					sample["maximumAbsEdit"] = maxEdit;
					if (captureSample)
						capture.End();
					sample["elapsedCpuMicroseconds"] = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
					if (!edits || !maxEdit) {
						sample["success"] = false;
						sample["reason"] = "no_nonzero_native_edit_not_a_fast_result";
					}
					value["samples"].push_back(std::move(sample));
				} catch (const std::exception& error) {
					if (scheduleReadback)
						sample["kernelScheduleReadback"] = scheduleReadback->Receipt();
					sample["success"] = false;
					sample["reason"] = error.what();
					if (session.kernelChain)
						sample["kernelChain"] = session.kernelChain->SampleReceipt();
					sample["retainedKernelChainProxiesUntilExit"] = NrReplay::KernelChainCommandList::RetainedUntilExitCount();
					sample["interopFailure"] = { { "operation", session.interop.LastOperation() }, { "result", static_cast<std::uint32_t>(session.interop.LastError()) },
						{ "deviceRemovedReason", static_cast<std::uint32_t>(session.device->GetDeviceRemovedReason()) } };
					value["samples"].push_back(std::move(sample));
					throw;
				}
				if (variant.axis == "native_handle_reuse" || variant.axis == "packed_region_experiment")
					Require(value["samples"].back()["success"] == true, "context probe rejected a sample; stopped further calls");
			}
			value["status"] = "complete";
			value["reason"] = "";
			session.Retire();
		} catch (const std::exception& error) {
			value["status"] = "failed";
			value["reason"] = error.what();
			value["runtimeFailure"] = { { "ngxResult", Runtime::Instance().NgxResult() }, { "stage", ToString(Runtime::Instance().FailureStage()) } };
		}
		return value;
	}
}

int wmain(int argc, wchar_t** argv)
{
	Json result{ { "schema", "csx-nr-replay-results-v1" }, { "cases", Json::array() },
		{ "status", "unavailable" }, { "scope", "native_provider_microbenchmark_not_end_to_end_route_cost" },
		{ "featureRequirements", { { "queried", false }, { "reason", "NR_Streamline_plugin_not_loaded_by_direct_admitted_NGX_runtime" } } },
		{ "fourEightRegions", "explicit_bounded_qualification_only_64x64_eight_region_probe_rejected" },
		{ "minimumShapeProbes", "explicit_case_only_not_in_default_matrix" },
		{ "inputStorageProbes", "explicit_case_only_finite_patterns_outside_requested_native_rectangles" },
		{ "maskOccupancy", "synthetic_binary_composite_only_proxy_no_provider_ControlMask_no_GPU_composite_timing" },
		{ "gpuCapture", "not_requested_no_external_capture_tool_attached" } };
	result["providerFloorExperiment"] = { { "requested", false } };
	result["buildIdentity"] = { { "runtimeSourceSha256", kRuntimeSourceHash }, { "interopSourceSha256", kInteropSourceHash },
		{ "ngxHeaderSha256", kNgxHeaderHash }, { "streamlineCoreHeaderSha256", kSlHeaderHash }, { "streamlineSdkVersion", kSlVersion } };
	result["buildIdentity"]["replaySourceSha256"] = Json::parse(kReplaySourceIdentityJson);
	result["buildIdentity"]["ngxLibrarySha256"] = kNgxLibraryHash;
	std::filesystem::path manifestPath, outputRoot, runtimeSource, renderdocPath, kernelReplacementManifest, modelReplacementManifest;
	unsigned samples = 8, warmup = 3, seconds = 180;
	unsigned capacitySize = 128;
	bool capacityTemporal = false;
	bool alternateSentinel = false;
	bool experimentalProviderFloor = false;
	bool batchTimingOnly = false;
	bool captureKernelModules = false;
	std::optional<unsigned> modelBatchStages;
	std::optional<unsigned> scheduleRepetitions;
	std::string kernelChainMode, kernelPairMode, repetitionControl;
	std::string onlyCase;
	std::string customRects;
	std::string nativeHandlePolicy;
	std::string captureLibraryHash;
	std::unique_ptr<NrReplay::GpuCapture> capture;
	bool validateOnly = false, validateStorage = false, inspectOnly = false;
	bool ownsOutput = false;
	try {
		for (int i = 1; i < argc; ++i) {
			const std::wstring option = argv[i];
			if (option == L"--validate-input") {
				validateOnly = true;
				continue;
			}
			if (option == L"--validate-storage") {
				validateStorage = true;
				continue;
			}
			if (option == L"--inspect") {
				inspectOnly = true;
				continue;
			}
			if (option == L"--capacity-temporal") {
				capacityTemporal = true;
				continue;
			}
			if (option == L"--alternate-output-sentinel") {
				alternateSentinel = true;
				continue;
			}
			if (option == L"--batch-timing-only") {
				Require(!batchTimingOnly, "batch timing flag must be specified once");
				batchTimingOnly = true;
				continue;
			}
			if (option == L"--capture-kernel-modules") {
				Require(!captureKernelModules, "kernel module capture flag must be specified once");
				captureKernelModules = true;
				continue;
			}
			Require(i + 1 < argc, "option requires value");
			const std::filesystem::path argument = argv[++i];
			if (option == L"--manifest")
				manifestPath = argument;
			else if (option == L"--output")
				outputRoot = argument;
			else if (option == L"--runtime")
				runtimeSource = argument;
			else if (option == L"--samples")
				samples = std::stoul(argument.string());
			else if (option == L"--warmup")
				warmup = std::stoul(argument.string());
			else if (option == L"--seconds")
				seconds = std::stoul(argument.string());
			else if (option == L"--case")
				onlyCase = argument.string();
			else if (option == L"--rects")
				customRects = argument.string();
			else if (option == L"--native-handle-policy")
				nativeHandlePolicy = argument.string();
			else if (option == L"--experimental-provider-floor") {
				Require(!experimentalProviderFloor && argument.string() == "256", "experimental provider floor must be exactly 256 and specified once");
				experimentalProviderFloor = true;
			} else if (option == L"--experimental-kernel-chain") {
				Require(kernelChainMode.empty() && (argument.string() == "forward" || argument.string() == "group"),
					"kernel chain mode must be forward or group and specified once");
				kernelChainMode = argument.string();
			} else if (option == L"--experimental-kernel-replacement") {
				Require(kernelReplacementManifest.empty(), "kernel replacement manifest must be specified once");
				kernelReplacementManifest = argument;
			} else if (option == L"--experimental-model-replacement") {
				Require(modelReplacementManifest.empty(), "model replacement manifest must be specified once");
				modelReplacementManifest = argument;
			} else if (option == L"--experimental-model-batch-stages") {
				const auto value = argument.string();
				Require(!modelBatchStages && !value.empty() && value.size() <= 3 &&
							std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; }),
					"model batch stages must be one decimal count, specified once");
				modelBatchStages = std::stoul(value);
				Require(*modelBatchStages >= 1 && *modelBatchStages <= NrReplay::KernelPair::kStages,
					"model batch stages must be within 1..158");
			} else if (option == L"--experimental-kernel-repetitions") {
				const auto value = argument.string();
				Require(!scheduleRepetitions && (value == "2" || value == "3" || value == "4"),
					"kernel repetitions must be exactly 2, 3 or 4 and specified once");
				scheduleRepetitions = std::stoul(value);
			} else if (option == L"--experimental-kernel-comparison") {
				const auto value = argument.string();
				Require(repetitionControl.empty() && (value == "original" || value == "layer-control"),
					"kernel comparison must be original or layer-control and specified once");
				repetitionControl = value;
			} else if (option == L"--experimental-kernel-pair") {
				Require(kernelPairMode.empty() && (argument.string() == "original" || argument.string() == "control" || argument.string() == "layer-control" || argument.string() == "batch" || argument.string() == "model-batch"),
					"kernel pair mode must be original, control, layer-control, batch or model-batch and specified once");
				kernelPairMode = argument.string();
			} else if (option == L"--renderdoc")
				renderdocPath = argument;
			else if (option == L"--capacity-size") {
				const auto size = argument.string();
				Require(size == "128" || size == "256" || size == "512" || size == "768", "capacity-size must be 128, 256, 512 or 768");
				capacitySize = std::stoul(size);
			} else
				throw std::runtime_error("unknown option");
		}
		Require((!manifestPath.empty() || inspectOnly) && !outputRoot.empty(), "usage: csx_nr_replay --manifest input.json --output NEW_DIRECTORY [--runtime admitted.dll] [--samples 8] [--warmup 3] [--seconds 180] [--case ID] [--validate-input | --validate-storage]; --inspect needs only --output/--runtime");
		Require(!inspectOnly || (!validateOnly && !validateStorage), "runtime inspection and input-only validation cannot be combined");
		Require(capacitySize == 128 || (!inspectOnly && !validateStorage && (onlyCase == "capacity-full" || onlyCase == "capacity-compact")), "capacity-size requires an explicit capacity-full or capacity-compact case");
		Require(!capacityTemporal || (!inspectOnly && !validateOnly && !validateStorage && (onlyCase == "capacity-full" || onlyCase == "capacity-compact")), "capacity-temporal requires an explicit native capacity case");
		Require(customRects.empty() || (!inspectOnly && !validateStorage && !capacityTemporal && capacitySize == 128 && onlyCase.empty()),
			"custom rects cannot be combined with another case or capacity/storage/inspection mode");
		Require(renderdocPath.empty() || (!inspectOnly && !validateOnly && !validateStorage && (!onlyCase.empty() || !customRects.empty())),
			"RenderDoc capture requires one explicit native case");
		Require(samples >= 1 && samples <= 64 && warmup <= 32 && seconds >= 1 && seconds <= 600, "replay bounds: samples1..64 warmup0..32 seconds1..600");
		Require(!std::filesystem::exists(outputRoot), "output directory already exists; preserve earlier evidence");
		std::filesystem::create_directories(outputRoot);
		ownsOutput = true;
		Require(!experimentalProviderFloor || (!customRects.empty() && nativeHandlePolicy.empty() && renderdocPath.empty() &&
												  !inspectOnly && !validateStorage && !capacityTemporal && capacitySize == 128 && onlyCase.empty() && kernelChainMode.empty()),
			"experimental provider floor requires custom rectangles without handle, capture, capacity, temporal, storage or inspection options");
		Require(kernelChainMode.empty() || (!customRects.empty() && nativeHandlePolicy.empty() && renderdocPath.empty() &&
											   !experimentalProviderFloor && !inspectOnly && !validateStorage && !capacityTemporal && capacitySize == 128 && onlyCase.empty()),
			"kernel chain experiment requires custom rectangles without floor, handle, capture, capacity, temporal, storage or inspection options");
		result["kernelChainExperiment"] = { { "requested", false } };
		Require(!captureKernelModules || kernelChainMode == "forward",
			"kernel module capture requires the forward kernel-chain probe");
		result["captureKernelModules"] = captureKernelModules;
		Require(kernelReplacementManifest.empty() || (captureKernelModules && kernelChainMode == "forward" && !validateOnly),
			"kernel replacement requires live forward identity capture; parser-only validation cannot qualify its manifest");
		Require(kernelPairMode.empty() || (captureKernelModules && kernelChainMode == "forward" && batchTimingOnly && warmup >= 1 && !validateOnly),
			"kernel pair mode requires live forward identity capture, batch timing and at least one unchanged warmup");
		Require(kernelPairMode.empty() || ((kernelPairMode == "batch") == !kernelReplacementManifest.empty()),
			"kernel pair batch requires an N2 manifest; original/control require no replacement manifest");
		Require(modelReplacementManifest.empty() || (kernelReplacementManifest.empty() && (kernelPairMode == "original" || kernelPairMode == "layer-control" || kernelPairMode == "model-batch")),
			"model replacement requires the original or layer-control pair schedule without another replacement");
		Require(kernelPairMode != "model-batch" || !modelReplacementManifest.empty(), "model batch requires a pinned replacement manifest");
		Require(!modelBatchStages || kernelPairMode == "model-batch", "model batch stages requires the model-batch pair schedule");
		Require(!scheduleRepetitions || ((kernelPairMode == "original" || kernelPairMode == "layer-control" || kernelPairMode == "model-batch") &&
											modelBatchStages.value_or(NrReplay::KernelPair::kStages) == NrReplay::KernelPair::kStages &&
											(kernelPairMode == "model-batch" || modelReplacementManifest.empty())),
			"kernel repetitions requires a complete original, layer-control or model-batch schedule without N1 replacement");
		NrReplay::KernelPair::ValidateComparison(repetitionControl, scheduleRepetitions.value_or(1), kernelPairMode,
			modelBatchStages.value_or(NrReplay::KernelPair::kStages));
		if (scheduleRepetitions)
			result["kernelScheduleRepetitions"] = *scheduleRepetitions;
		if (!repetitionControl.empty())
			result["kernelScheduleComparisonControl"] = repetitionControl;
		result["modelReplacementRequested"] = !modelReplacementManifest.empty();
		result["modelBatchStageLimit"] = kernelPairMode == "model-batch" ? Json(modelBatchStages.value_or(NrReplay::KernelPair::kStages)) : Json(nullptr);
		result["kernelPairMode"] = kernelPairMode;
		Require(!batchTimingOnly || (!customRects.empty() && nativeHandlePolicy.empty() && renderdocPath.empty() &&
										!experimentalProviderFloor && !inspectOnly && !validateStorage && !capacityTemporal && capacitySize == 128 && onlyCase.empty()),
			"batch-only timing requires independent custom contexts without other experiments");
		result["batchTimingOnly"] = batchTimingOnly;
		Require(nativeHandlePolicy.empty() || (!customRects.empty() && !capacityTemporal && capacitySize == 128 &&
												  !inspectOnly && !validateStorage && onlyCase.empty()),
			"native handle policy requires explicit custom rectangles without capacity, temporal, inspection or storage options");
		std::array<wchar_t, 32768> executablePath{};
		const auto executableLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		Require(executableLength && executableLength < executablePath.size(), "replay executable identity unavailable");
		result["buildIdentity"]["executableSha256"] = Hash(Read(std::filesystem::path(executablePath.data()), kBundleBudget));
		const auto expectedRuntime = Util::PathHelpers::GetDataPath() / "Shaders/Upscaling/Streamline/nvngx_dlssnr.dll";
		if (inspectOnly) {
			StageRuntime(runtimeSource, expectedRuntime);
			auto& runtime = Runtime::Instance();
			if (!runtime.Probe())
				throw std::runtime_error(runtime.Detail());
			result["runtime"] = { { "path", runtime.Path().string() }, { "sha256", runtime.Hash() }, { "version", runtime.Version() },
				{ "trust", ToString(runtime.Trust()) }, { "status", ToString(runtime.Status()) }, { "detail", runtime.Detail() } };
			result["status"] = "inspection_complete_no_evaluation";
			WriteJson(outputRoot / "results.json", result);
			return 0;
		}
		const auto manifestBytes = Read(manifestPath, 8 * 1024 * 1024);
		const auto manifest = Json::parse(manifestBytes);
		Require((kernelReplacementManifest.empty() && kernelPairMode.empty()) || (manifest.at("adapter").at("vendorId") == 0x10de &&
																					 manifest.at("adapter").at("deviceId") == 0x2f58),
			"kernel replacement requires the admitted SM120 experiment adapter");
		result["captureManifest"] = std::filesystem::absolute(manifestPath).string();
		result["captureManifestSha256"] = Hash(manifestBytes);
		const auto frames = LoadFrames(manifest, manifestPath.parent_path());
		std::string contentHashes;
		for (const auto& frame : frames)
			for (const auto& eye : frame.eyes)
				for (const auto* texture : { &eye.color, &eye.depth, &eye.motion, &eye.output })
					contentHashes += Hash(texture->bytes);
		result["sourceContentSha256"] = Hash({ reinterpret_cast<const std::uint8_t*>(contentHashes.data()), contentHashes.size() });
		const auto& first = frames.front().eyes.front();
		const auto control = Compare(first.color, first.output, { 0, 0, first.color.width, first.color.height });
		Require(control.pixels && control.maximum > 0, "capture lacks known nonzero native NR edit control");
		result["capturedControl"] = { { "nonzeroEditPixels", control.pixels }, { "maximumAbsEdit", control.maximum } };
		std::vector<Variant> customVariants;
		if (!customRects.empty()) {
			customVariants.push_back(CustomRegions(customRects, first, frames.front().metadata.at("mode").get<unsigned>()));
			if (batchTimingOnly)
				ValidateIndependentProviderContexts(customVariants.back(), frames);
			if (!nativeHandlePolicy.empty()) {
				ConfigureNativeHandleExperiment(customVariants.back(), frames.front(), nativeHandlePolicy);
				result["nativeHandlePlan"] = NativeHandlePlan(customVariants.back(), static_cast<unsigned>(frames.front().eyes.size()));
			}
			if (experimentalProviderFloor)
				ConfigureProviderFloorExperiment(customVariants.back(), frames, Lower(manifest.at("runtime").at("sha256").get<std::string>()), result["providerFloorExperiment"]);
			if (!kernelChainMode.empty())
				ConfigureKernelChainExperiment(customVariants.back(), frames, Lower(manifest.at("runtime").at("sha256").get<std::string>()), kernelChainMode, result["kernelChainExperiment"]);
			if (!kernelPairMode.empty())
				ValidateKernelPairExperiment(customVariants.back(), frames);
			result["customRegions"] = Json::parse(customRects);
		}
		if (validateOnly || validateStorage) {
			if (validateStorage)
				result["inputStorageValidation"] = ValidateInputStorage(frames, control, onlyCase);
			result["status"] = "input_validated_no_runtime_measurement";
			WriteJson(outputRoot / "results.json", result);
			return 0;
		}
		const auto deadline = Clock::now() + std::chrono::seconds(seconds);
		StageRuntime(runtimeSource, expectedRuntime, manifest.at("runtime").at("sha256").get<std::string>());
		if (!renderdocPath.empty()) {
			captureLibraryHash = Hash(Read(renderdocPath, kBundleBudget));
			result["gpuCapture"] = { { "requested", true }, { "state", "initializing" }, { "timingsInstrumented", true },
				{ "library", std::filesystem::absolute(renderdocPath).string() }, { "librarySha256", captureLibraryHash } };
		}
		capture = std::make_unique<NrReplay::GpuCapture>(renderdocPath, outputRoot / "native-workload");
		Session session;
		if (experimentalProviderFloor)
			session.providerFloor = std::make_unique<NrReplay::ProviderFloorProbe>(result["providerFloorExperiment"]);
		if (!kernelChainMode.empty())
			session.kernelChain = std::make_unique<NrReplay::ProviderKernelChainProbe>(result["kernelChainExperiment"], kernelChainMode,
				captureKernelModules ? outputRoot / "kernel-modules" : std::filesystem::path{}, kernelReplacementManifest, kernelPairMode, modelReplacementManifest,
				modelBatchStages.value_or(NrReplay::KernelPair::kStages), scheduleRepetitions.value_or(1), repetitionControl);
		std::unique_ptr<NrReplay::FeatureCreationObserver> creationObserver;
		if (!kernelPairMode.empty()) {
			result["kernelPairAllocationParameters"] = Json::array();
			creationObserver = std::make_unique<NrReplay::FeatureCreationObserver>([&](const NVSDK_NGX_Parameter* parameters) {
				Json values = Json::array();
				bool admitted = true;
				for (const auto* key : { NVSDK_NGX_Parameter_ResourceAllocCallback, NVSDK_NGX_Parameter_ResourceReleaseCallback,
						 NVSDK_NGX_EParameter_ResourceAllocCallback, NVSDK_NGX_EParameter_ResourceReleaseCallback }) {
					void* callback = nullptr;
					const auto status = parameters->Get(key, &callback);
					const bool absent = !callback && (status == NVSDK_NGX_Result_Success || status == NVSDK_NGX_Result_FAIL_UnsupportedParameter);
					values.push_back({ { "key", key }, { "result", static_cast<unsigned>(status) },
						{ "callback", reinterpret_cast<std::uintptr_t>(callback) }, { "absentOrNull", absent } });
					admitted = admitted && absent;
				}
				result["kernelPairAllocationParameters"].push_back({ { "parameters", values }, { "admitted", admitted } });
				Require(admitted, "kernel pair allocation callbacks are present or unavailable");
				session.kernelChain->ConfirmDefaultAllocationParameters();
			});
		}
		ComPtr<IDXGIFactory1> factory;
		Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
		for (unsigned index = 0;; ++index) {
			ComPtr<IDXGIAdapter1> adapter;
			if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
				break;
			DXGI_ADAPTER_DESC1 desc{};
			Check(adapter->GetDesc1(&desc), "adapter identity");
			if (desc.VendorId == manifest.at("adapter").at("vendorId") && desc.DeviceId == manifest.at("adapter").at("deviceId")) {
				Require(!session.adapter, "multiple matching adapters require an unambiguous capture identity");
				Check(adapter.As(&session.adapter), "IDXGIAdapter3 memory accounting");
				LARGE_INTEGER version{};
				const auto versionResult = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
				const std::wstring name = desc.Description;
				result["adapter"] = { { "description", Utf8(name) }, { "vendorId", desc.VendorId }, { "deviceId", desc.DeviceId },
					{ "luid", { { "low", desc.AdapterLuid.LowPart }, { "high", desc.AdapterLuid.HighPart } } },
					{ "driverVersion", SUCCEEDED(versionResult) ? Json(version.QuadPart) : Json(nullptr) } };
			}
		}
		Require(bool(session.adapter), "captured GPU adapter unavailable");
		if (manifest.at("adapter").contains("luid"))
			Require(result["adapter"]["luid"] == manifest.at("adapter").at("luid"), "captured GPU LUID differs from replay adapter");
		Require(manifest.at("adapter").contains("driverVersion") && !manifest.at("adapter").at("driverVersion").is_null(), "capture lacks GPU driver identity");
		Require(result["adapter"]["driverVersion"] == manifest.at("adapter").at("driverVersion"), "captured GPU driver version differs from replay driver");
		const D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL obtained{};
		Check(D3D11CreateDevice(session.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				  levels, 2, D3D11_SDK_VERSION, &session.device, &obtained, &session.context),
			"D3D11 hardware replay device");
		Require(session.interop.Initialize(session.adapter.Get(), session.device.Get(), session.context.Get()), session.interop.LastOperation());
		session.active = true;
		// The game loads the driver core through NGX before direct NR admission.
		const auto bootstrap = NVSDK_NGX_D3D11_Init_with_ProjectID(kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
			kNgxEngineVersion, outputRoot.c_str(), session.device.Get());
		result["ngxBootstrap"] = { { "api", "D3D11_Init_with_ProjectID" }, { "result", static_cast<unsigned>(bootstrap) },
			{ "projectId", kNgxProjectId }, { "engineVersion", kNgxEngineVersion } };
		session.ngxInitialized = NVSDK_NGX_SUCCEED(bootstrap);
		Require(session.ngxInitialized, std::format("NGX driver bootstrap failed: 0x{:08x}", static_cast<unsigned>(bootstrap)));
		auto& runtime = Runtime::Instance();
		if (!runtime.Probe() || !runtime.Initialize(session.interop.Device()))
			throw std::runtime_error(runtime.Detail());
		result["runtime"] = { { "path", runtime.Path().string() }, { "sha256", runtime.Hash() }, { "version", runtime.Version() },
			{ "trust", ToString(runtime.Trust()) }, { "parameterCorePath", runtime.ParameterCorePath().string() },
			{ "parameterCoreSha256", runtime.ParameterCoreHash() } };
		result["status"] = "running";
		result["alternateOutputSentinel"] = alternateSentinel;
		for (auto variant : customVariants.empty() ? Variants(first.color.width, first.color.height, first.depth.width, first.depth.height, control, frames.size() > 1, capacitySize) : customVariants) {
			if (customVariants.empty() && onlyCase.empty() && (variant.axis == "minimum_shape" || variant.axis == "input_storage" || variant.axis == "native_context_count" || variant.axis == "source_transport" || variant.rects.size() > kDefaultRegionsPerEye || variant.pairGroup == "capacity-calls-equal-area"))
				continue;
			if (!onlyCase.empty() && variant.id != onlyCase)
				continue;
			if (capacityTemporal)
				variant.temporal = true;
			std::cout << variant.id << std::endl;
			auto value = RunCase(session, variant, frames, warmup, samples, deadline, outputRoot, alternateSentinel, *capture, batchTimingOnly, captureKernelModules, !kernelPairMode.empty(), scheduleRepetitions.value_or(1));
			result["gpuCapture"] = capture->Report();
			result["gpuCapture"]["librarySha256"] = captureLibraryHash;
			value["sourceContentSha256"] = result["sourceContentSha256"];
			value["sourceGuideAlignmentMethod"] = value["sourceGuideAlignment"];
			value["sourceGuideAlignment"] = result["captureManifestSha256"];
			result["cases"].push_back(std::move(value));
			WriteJson(outputRoot / "results.json", result);
			Require(result["cases"].back()["status"] != "failed", "case failed; stopped further native calls and retained evidence");
		}
		Require(!result["cases"].empty(), "no matching cases");
		result["sessionClosed"] = session.Close();
		Require(result["sessionClosed"].get<bool>(), "replay session shutdown failed");
		result["status"] = Clock::now() < deadline ? "complete" : "bounded_deadline";
		WriteJson(outputRoot / "results.json", result);
		return result["status"] == "complete" ? 0 : 2;
	} catch (const std::exception& error) {
		if (capture) {
			result["gpuCapture"] = capture->Report();
			result["gpuCapture"]["librarySha256"] = captureLibraryHash;
		}
		result["status"] = "failed";
		result["reason"] = error.what();
		if (ownsOutput) {
			try {
				WriteJson(outputRoot / "results.json", result);
			} catch (...) {
				std::cerr << "Could not write failure evidence\n";
			}
		}
		std::cerr << error.what() << '\n';
		return 1;
	}
}
