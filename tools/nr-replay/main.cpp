#include "D3D12Interop.h"
#include "Runtime.h"
#include "Utils/CryptoHash.h"
#include "build_identity.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <dxgi1_4.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

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
	TextureData Sentinel(const TextureData& source)
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
				const std::uint32_t value = 0xff000000u | (static_cast<std::uint32_t>(pixel * 2654435761u) & 0x00ffffffu);
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
	struct Variant
	{
		std::string id, axis, pairGroup, history = "static_reset";
		std::vector<ComputeSubrect> rects;
		ComputeSubrect crop;
		bool temporal = false;
		double occupancy = -1;
	};
	std::vector<Variant> Variants(unsigned width, unsigned height, unsigned guideWidth, unsigned guideHeight, const Difference& control, bool temporal)
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
			const unsigned quantumX = width / std::gcd(width, guideWidth);
			const unsigned quantumY = height / std::gcd(height, guideHeight);
			auto capacity = at(quantumX <= 128 ? 128 / quantumX * quantumX : 128,
				quantumY <= 128 ? 128 / quantumY * quantumY : 128);
			capacity.baseX = capacity.baseX / quantumX * quantumX;
			capacity.baseY = capacity.baseY / quantumY * quantumY;
			values.push_back({ "capacity-full", "capacity", "capacity-identical-integer-crop", "static_reset", { capacity }, {} });
			values.push_back({ "capacity-compact", "capacity", "capacity-identical-integer-crop", "static_reset", { { 0, 0, capacity.width, capacity.height } }, capacity });
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
		bool active = false;
		~Session()
		{
			if (!active)
				return;
			if (interop.IsRecording())
				(void)interop.AbortD3D12();
			if (!interop.WaitForIdle() || !Runtime::Instance().Shutdown()) {
				Runtime::Instance().AbandonUnsafe();
				interop.AbandonUnsafe();
			} else {
				(void)interop.Shutdown();
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
			Require(Runtime::Instance().Shutdown(), "case runtime shutdown failed");
			Require(interop.Shutdown(), "case resource retirement failed");
			Require(interop.Initialize(adapter.Get(), device.Get(), context.Get()), interop.LastOperation());
			Require(Runtime::Instance().Initialize(interop.Device()), Runtime::Instance().Detail());
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
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		for (const auto& resource : resources)
			for (const auto* texture : { &resource.color, &resource.depth, &resource.motion, &resource.output }) {
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = texture->resource12.Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				const auto target = texture == &resource.output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				barrier.Transition.StateBefore = begin ? D3D12_RESOURCE_STATE_COMMON : target;
				barrier.Transition.StateAfter = begin ? target : D3D12_RESOURCE_STATE_COMMON;
				barriers.push_back(barrier);
			}
		list->ResourceBarrier(static_cast<unsigned>(barriers.size()), barriers.data());
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
	Json RunCase(Session& session, const Variant& variant, const std::vector<Frame>& frames,
		unsigned warmup, unsigned samples, Clock::time_point deadline, const std::filesystem::path& outputRoot)
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
		Require(count <= 4, "initial replay supports at most two regions per eye");
		Json value{ { "id", variant.id }, { "axis", variant.axis }, { "pairGroup", variant.pairGroup },
			{ "route", std::string(1, char('A' + first.metadata.at("mode").get<unsigned>())) }, { "mode", first.metadata.at("mode") },
			{ "history", variant.history }, { "creationExtent", { width, height } },
			{ "resourceExtents", { { "color", { width, height } }, { "depth", { guideWidth, guideHeight } }, { "motion", { guideWidth, guideHeight } }, { "output", { width, height } } } },
			{ "evaluatedRects", Json::array() }, { "evaluatedSourceRects", Json::array() }, { "evaluatedGuideRects", Json::array() }, { "evaluationsPerSample", count }, { "logicalEyeCount", first.eyes.size() },
			{ "featureUpscaling", firstEye.featureUpscaling }, { "useAutoMask", true }, { "controlMaskPassed", false },
			{ "warmupIterations", warmup }, { "requestedSamples", samples }, { "colorConfiguration", first.metadata.at("colorConfiguration") },
			{ "tuning", first.metadata.at("tuning") }, { "sourceFrameIndices", Json::array() },
			{ "transportBypass", false }, { "applyModelEdit", true }, { "samples", Json::array() },
			{ "characterSelection", first.metadata.value("characterSelection", false) },
			{ "contextIds", Json::array() }, { "initializationFingerprint", "fresh-features-first-source-reset" },
			{ "sourceGuideAlignment", "captured-native-grids-exact-integer-crop-no-resample" },
			{ "temporalSequence", variant.temporal }, { "qualityAssessment", "not_performed" },
			{ "status", "unavailable" }, { "reason", "not_started" } };
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
				value["contextIds"].push_back(std::format("{}:fresh-slot-{}", variant.id, eye + region * 4));
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
					resource.slot = eye + region * 4;
					resource.eye = eye;
					resource.rect = variant.rects[region];
					const auto& source = first.eyes[eye];
					const auto make = [&](const TextureData& input, const char* name, bool guide) {
						const auto cropped = variant.crop.IsValid() ? Crop(input, guide ? guideCrop : variant.crop) : input;
						logicalBytes += cropped.bytes.size();
						Require(logicalBytes <= kResourceBudget, "logical GPU resource budget exceeded");
						return CreateTexture(session, cropped, name);
					};
					resource.color = make(source.color, "NRReplay::Color", false);
					resource.depth = make(source.depth, "NRReplay::Depth", true);
					resource.motion = make(source.motion, "NRReplay::Motion", true);
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
				try {
					if (variant.history == "cold_create")
						session.Retire();
					const auto memoryBefore = session.Memory();
					for (auto& resource : resources) {
						const auto& eye = frame.eyes[resource.eye];
						const auto upload = [&](const TextureData& input, SharedTexture& texture, bool guide) {
							const auto cropped = variant.crop.IsValid() ? Crop(input, guide ? guideCrop : variant.crop) : input;
							session.context->UpdateSubresource(texture.resource11.Get(), 0, nullptr, cropped.bytes.data(), cropped.rowBytes, 0);
						};
						upload(eye.color, resource.color, false);
						upload(eye.depth, resource.depth, true);
						upload(eye.motion, resource.motion, true);
						resource.source = variant.crop.IsValid() ? Crop(eye.color, variant.crop) : eye.color;
						resource.sentinel = Sentinel(resource.source);
						session.context->UpdateSubresource(resource.output.resource11.Get(), 0, nullptr, resource.sentinel.bytes.data(), resource.sentinel.rowBytes, 0);
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
					const bool reset = variant.history != "continuous" || iteration == 0;
					std::uint64_t creationCpu = 0, evaluationCpu = 0;
					unsigned created = 0, evaluated = 0;
					for (unsigned i = 0; i < resources.size(); ++i) {
						const auto& resource = resources[i];
						const auto& eye = frame.eyes[resource.eye];
						RuntimeExecutionEvidence native;
						bool attempted = false;
						const bool succeeded = Runtime::Instance().Execute(list, resource.slot,
							resource.color.resource12.Get(), resource.depth.resource12.Get(), resource.motion.resource12.Get(), resource.output.resource12.Get(), nullptr,
							width, height, guideWidth, guideHeight, width, height, 0, 0, resource.rect,
							eye.motionScale[0], eye.motionScale[1], eye.featureUpscaling, tuning, reset, &attempted, &native, &session.interop, i);
						created += native.createSucceeded ? 1 : 0;
						evaluated += native.evaluateSucceeded ? 1 : 0;
						creationCpu += native.createCpuMicroseconds.value_or(0);
						evaluationCpu += native.evaluateCpuMicroseconds.value_or(0);
						sample["createdFeatureCount"] = created;
						sample["evaluationCount"] = evaluated;
						sample["createCpuMicroseconds"] = creationCpu;
						sample["evalCpuMicroseconds"] = evaluationCpu;
						sample["runtimeCalls"].push_back({ { "slot", resource.slot }, { "createAttempted", native.createAttempted }, { "createSucceeded", native.createSucceeded },
							{ "evaluationAttempted", attempted }, { "evaluationSucceeded", native.evaluateSucceeded },
							{ "createResult", native.createResult ? Json(*native.createResult) : Json(nullptr) },
							{ "evaluateResult", native.evaluateResult ? Json(*native.evaluateResult) : Json(nullptr) } });
						Require(succeeded && attempted && native.evaluateSucceeded, Runtime::Instance().Detail());
					}
					Transition(list, resources, false);
					Require(session.interop.EndFeatureTiming(list), session.interop.LastOperation());
					Require(session.interop.EndD3D12(), session.interop.LastOperation());
					Require(session.interop.WaitForIdle(evidence), "replay submission GPU idle proof failed");
					const auto finished = evidence->Snapshot();
					const auto memoryAfter = session.Memory();
					sample.update(Json{ { "iteration", iteration }, { "warmup", iteration < warmup }, { "success", true }, { "reason", "" },
						{ "reset", reset }, { "createdFeatureCount", created }, { "evaluationCount", evaluated }, { "evaluatedPixels", area },
						{ "gpuMicroseconds", finished.batchGpu.microseconds ? Json(*finished.batchGpu.microseconds) : Json(nullptr) },
						{ "evaluationGpuMicroseconds", Json::array() }, { "createCpuMicroseconds", creationCpu }, { "evalCpuMicroseconds", evaluationCpu },
						{ "memory", { { "localUsageBefore", memoryBefore["local"] }, { "localUsageAfter", memoryAfter["local"] },
										{ "nonlocalUsageBefore", memoryBefore["nonlocal"] }, { "nonlocalUsageAfter", memoryAfter["nonlocal"] } } },
						{ "nonzeroEditPixels", 0 }, { "maximumAbsEdit", 0 }, { "outputFiles", Json::array() }, { "providerFootprint", Json::array() } });
					std::uint64_t edits = 0;
					double maxEdit = 0;
					for (unsigned i = 0; i < resources.size(); ++i) {
						sample["evaluationGpuMicroseconds"].push_back(finished.regions[i].evaluationGpu.microseconds ? Json(*finished.regions[i].evaluationGpu.microseconds) : Json(nullptr));
						const auto pixels = Download(session, resources[i].output, std::min(deadline, Clock::now() + std::chrono::seconds(2)));
						const auto footprint = Footprint(resources[i].sentinel, pixels, resources[i].rect);
						sample["providerFootprint"].push_back(footprint);
						if (footprint.at("unchangedInsidePixels") != 0 || footprint.at("nonfiniteInsidePixels") != 0) {
							sample["success"] = false;
							sample["reason"] = "provider_left_unwritten_or_nonfinite_evaluated_pixels";
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
						if (variant.temporal) {
							const auto name = std::format("{}-{:03}-slot{}.bin", variant.id, iteration, resources[i].slot);
							std::ofstream stream(outputRoot / name, std::ios::binary);
							stream.write(reinterpret_cast<const char*>(pixels.bytes.data()), static_cast<std::streamsize>(pixels.bytes.size()));
							Require(bool(stream), "temporal output write failed");
							sample["outputFiles"].push_back({ { "file", name }, { "sha256", Hash(pixels.bytes) }, { "format", pixels.format },
								{ "width", pixels.width }, { "height", pixels.height }, { "rowBytes", pixels.rowBytes }, { "slot", resources[i].slot } });
						}
					}
					sample["nonzeroEditPixels"] = edits;
					sample["maximumAbsEdit"] = maxEdit;
					sample["elapsedCpuMicroseconds"] = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
					if (!edits || !maxEdit) {
						sample["success"] = false;
						sample["reason"] = "no_nonzero_native_edit_not_a_fast_result";
					}
					value["samples"].push_back(std::move(sample));
				} catch (const std::exception& error) {
					sample["success"] = false;
					sample["reason"] = error.what();
					value["samples"].push_back(std::move(sample));
					throw;
				}
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
		{ "fourEightRegions", "deferred_until_capacity_task" },
		{ "maskOccupancy", "synthetic_binary_composite_only_proxy_no_provider_ControlMask_no_GPU_composite_timing" },
		{ "gpuCapture", "not_requested_no_external_capture_tool_attached" } };
	result["buildIdentity"] = { { "runtimeSourceSha256", kRuntimeSourceHash }, { "interopSourceSha256", kInteropSourceHash },
		{ "ngxHeaderSha256", kNgxHeaderHash }, { "streamlineCoreHeaderSha256", kSlHeaderHash }, { "streamlineSdkVersion", kSlVersion } };
	result["buildIdentity"]["replaySourceSha256"] = Json::parse(kReplaySourceIdentityJson);
	std::filesystem::path manifestPath, outputRoot, runtimeSource;
	unsigned samples = 8, warmup = 3, seconds = 180;
	std::string onlyCase;
	bool validateOnly = false, inspectOnly = false;
	bool ownsOutput = false;
	try {
		for (int i = 1; i < argc; ++i) {
			const std::wstring option = argv[i];
			if (option == L"--validate-input") {
				validateOnly = true;
				continue;
			}
			if (option == L"--inspect") {
				inspectOnly = true;
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
			else
				throw std::runtime_error("unknown option");
		}
		Require((!manifestPath.empty() || inspectOnly) && !outputRoot.empty(), "usage: csx_nr_replay --manifest input.json --output NEW_DIRECTORY [--runtime admitted.dll] [--samples 8] [--warmup 3] [--seconds 180] [--case ID] [--validate-input]; --inspect needs only --output/--runtime");
		Require(samples >= 1 && samples <= 64 && warmup <= 32 && seconds >= 1 && seconds <= 600, "replay bounds: samples1..64 warmup0..32 seconds1..600");
		Require(!std::filesystem::exists(outputRoot), "output directory already exists; preserve earlier evidence");
		std::filesystem::create_directories(outputRoot);
		ownsOutput = true;
		std::array<wchar_t, 32768> executablePath{};
		const auto executableLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		Require(executableLength && executableLength < executablePath.size(), "replay executable identity unavailable");
		result["buildIdentity"]["executableSha256"] = Hash(Read(std::filesystem::path(executablePath.data()), kBundleBudget));
		const auto expectedRuntime = Util::PathHelpers::GetDataPath() / "Shaders/Upscaling/Streamline/nvngx_dlssnr.dll";
		if (inspectOnly) {
			StageRuntime(runtimeSource, expectedRuntime);
			auto& runtime = Runtime::Instance();
			Require(runtime.Probe(), runtime.Detail());
			result["runtime"] = { { "path", runtime.Path().string() }, { "sha256", runtime.Hash() }, { "version", runtime.Version() },
				{ "trust", ToString(runtime.Trust()) }, { "status", ToString(runtime.Status()) }, { "detail", runtime.Detail() } };
			result["status"] = "inspection_complete_no_evaluation";
			WriteJson(outputRoot / "results.json", result);
			return 0;
		}
		const auto manifestBytes = Read(manifestPath, 8 * 1024 * 1024);
		const auto manifest = Json::parse(manifestBytes);
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
		if (validateOnly) {
			result["status"] = "input_validated_no_runtime_measurement";
			WriteJson(outputRoot / "results.json", result);
			return 0;
		}
		const auto deadline = Clock::now() + std::chrono::seconds(seconds);
		StageRuntime(runtimeSource, expectedRuntime, manifest.at("runtime").at("sha256").get<std::string>());
		Session session;
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
		auto& runtime = Runtime::Instance();
		Require(runtime.Probe() && runtime.Initialize(session.interop.Device()), runtime.Detail());
		result["runtime"] = { { "path", runtime.Path().string() }, { "sha256", runtime.Hash() }, { "version", runtime.Version() },
			{ "trust", ToString(runtime.Trust()) }, { "parameterCorePath", runtime.ParameterCorePath().string() },
			{ "parameterCoreSha256", runtime.ParameterCoreHash() } };
		result["status"] = "running";
		for (const auto& variant : Variants(first.color.width, first.color.height, first.depth.width, first.depth.height, control, frames.size() > 1)) {
			if (!onlyCase.empty() && variant.id != onlyCase)
				continue;
			std::cout << variant.id << std::endl;
			auto value = RunCase(session, variant, frames, warmup, samples, deadline, outputRoot);
			value["sourceContentSha256"] = result["sourceContentSha256"];
			value["sourceGuideAlignmentMethod"] = value["sourceGuideAlignment"];
			value["sourceGuideAlignment"] = result["captureManifestSha256"];
			result["cases"].push_back(std::move(value));
			WriteJson(outputRoot / "results.json", result);
			Require(result["cases"].back()["status"] != "failed", "case failed; stopped further native calls and retained evidence");
		}
		Require(!result["cases"].empty(), "no matching cases");
		result["status"] = Clock::now() < deadline ? "complete" : "bounded_deadline";
		WriteJson(outputRoot / "results.json", result);
		return result["status"] == "complete" ? 0 : 2;
	} catch (const std::exception& error) {
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
