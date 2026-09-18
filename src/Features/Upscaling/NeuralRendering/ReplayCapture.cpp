#include "ReplayCapture.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ColorPolicy.h"
#	include "Utils/CryptoHash.h"
#	include "Utils/ResourceName.h"
#	include <Windows.h>
#	include <algorithm>
#	include <atomic>
#	include <chrono>
#	include <cmath>
#	include <cstring>
#	include <deque>
#	include <filesystem>
#	include <fstream>
#	include <future>
#	include <limits>
#	include <mutex>
#	include <optional>
#	include <stdexcept>
#	include <wrl/client.h>

namespace NeuralRendering::Replay
{
	using Microsoft::WRL::ComPtr;
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr std::uint64_t kByteBudget = 512ull * 1024 * 1024;
		constexpr std::size_t kPendingLimit = 2;
		std::atomic_bool armed{ false };
		std::atomic_bool diagnosticFailure{ false };
		struct Image
		{
			ComPtr<ID3D11Texture2D> staging;
			Json metadata;
			std::string role;
			std::size_t eye = 0;
			std::uint32_t rowBytes = 0, height = 0;
		};
		struct Pending
		{
			ComPtr<ID3D11Query> ready;
			Json frame;
			std::vector<Image> images;
		};
		struct Pixels
		{
			Image image;
			std::vector<std::byte> bytes;
		};
		struct State
		{
			std::mutex mutex;
			std::string state = "idle", reason, requestId;
			std::filesystem::path directory, manifest;
			std::uint32_t requested = 0, captured = 0;
			std::optional<std::uint32_t> previousFrame;
			Clock::time_point deadline{};
			std::uint64_t bytes = 0;
			Json runtime, adapter, invariant;
			Json frames = Json::array();
			std::deque<Pending> pending;
			ComPtr<ID3D11DeviceContext> context;
			std::future<Json> writer;
		};
		State& Owner()
		{
			static State state;
			return state;
		}

		void WriteFile(const std::filesystem::path& path, const void* data, std::size_t bytes)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.exceptions(std::ios::failbit | std::ios::badbit);
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
			file.close();
		}
		void Manifest(State& state, bool complete)
		{
			Json manifest = {
				{ "schema", "csx-nr-replay-input-v1" }, { "complete", complete },
				{ "requestId", state.requestId }, { "replayScope", "provider_inputs_and_native_output_not_final_stereo_publication" },
				{ "state", state.state }, { "reason", state.reason },
				{ "requestedFrames", state.requested }, { "capturedFrames", state.captured },
				{ "payloadBytes", state.bytes }, { "byteBudget", kByteBudget },
				{ "byteBudgetAccounting", "twice_row_packed_payload_excludes_driver_allocation_padding" },
				{ "captureIntrusive", true }, { "captureTimingIsPerformanceEvidence", false },
				{ "unreferencedPartialPayloadsMayExist", !complete },
				{ "runtime", state.runtime }, { "adapter", state.adapter }, { "frames", state.frames }
			};
			const auto target = state.directory / (complete ? "manifest.json" : "incomplete.json");
			const auto temporary = state.directory / "manifest.tmp";
			const auto text = manifest.dump(2);
			WriteFile(temporary, text.data(), text.size());
			std::filesystem::rename(temporary, target);
			state.manifest = target;
		}
		void Stop(State& state, const char* result, std::string_view reason) noexcept
		{
			armed.store(false, std::memory_order_release);
			state.pending.clear();
			state.context.Reset();
			try {
				state.state = result;
				state.reason = reason;
				Manifest(state, false);
			} catch (const std::exception& error) {
				diagnosticFailure.store(true, std::memory_order_release);
				try {
					state.reason += "; failed to preserve incomplete manifest: ";
					state.reason += error.what();
				} catch (...) {
				}
			} catch (...) {
				diagnosticFailure.store(true, std::memory_order_release);
			}
		}
		void PollWriter(State& state)
		{
			if (state.writer.valid() && state.writer.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				try {
					auto frame = state.writer.get();
					if (state.state == "capturing")
						state.frames.push_back(std::move(frame));
				} catch (const std::exception& error) {
					if (state.state == "capturing")
						Stop(state, "failed", error.what());
				}
			}
			if (state.state != "capturing")
				return;
			if (Clock::now() >= state.deadline) {
				Stop(state, "failed", "capture deadline expired; no blocking GPU readback or flush attempted");
				return;
			}
			if (state.frames.size() == state.requested && state.pending.empty() && !state.writer.valid()) {
				state.state = "complete";
				armed.store(false, std::memory_order_release);
				state.context.Reset();
				try {
					Manifest(state, true);
				} catch (const std::exception& error) {
					Stop(state, "failed", error.what());
				}
			}
		}
		Json Snapshot(const State& state)
		{
			return {
				{ "requestId", state.requestId },
				{ "ok", state.state != "failed" && !diagnosticFailure.load(std::memory_order_acquire) }, { "state", state.state }, { "reason", state.reason },
				{ "diagnosticFailure", diagnosticFailure.load(std::memory_order_acquire) },
				{ "requestedFrames", state.requested }, { "capturedFrames", state.captured },
				{ "savedFrames", state.frames.size() }, { "pendingFrames", state.pending.size() },
				{ "writerActive", state.writer.valid() }, { "payloadBytes", state.bytes },
				{ "byteBudget", kByteBudget }, { "directory", state.directory.string() },
				{ "byteBudgetAccounting", "twice_row_packed_payload_excludes_driver_allocation_padding" },
				{ "manifest", state.manifest.empty() ? Json(nullptr) : Json(state.manifest.string()) },
				{ "captureTimingIsPerformanceEvidence", false }
			};
		}
		std::uint32_t BytesPerPixel(DXGI_FORMAT format)
		{
			switch (format) {
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				return 8;
			case DXGI_FORMAT_R32_FLOAT:
			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_R16G16_FLOAT:
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return 4;
			default:
				throw std::runtime_error("native replay capture does not support this texture format");
			}
		}
		Json Invariant(const Batch& batch)
		{
			Json result = Json::object();
			for (const auto* name : { "generation", "insertionPoint", "mode", "arrangement", "colorRevision", "inputEpoch", "colorConfiguration", "tuning" })
				result[name] = batch.metadata.at(name);
			result["runtime"] = batch.runtime;
			result["adapter"] = batch.adapter;
			result["eyes"] = Json::array();
			for (const auto& eye : batch.eyes) {
				Json item = { { "slot", eye.slot }, { "motionVectorScale", eye.motionVectorScale }, { "featureUpscaling", eye.featureUpscaling } };
				item["resources"] = Json::array();
				for (auto* resource : { eye.color, eye.depth, eye.motion, eye.output }) {
					D3D11_TEXTURE2D_DESC description{};
					if (resource)
						resource->GetDesc(&description);
					item["resources"].push_back({ description.Width, description.Height, description.Format });
				}
				result["eyes"].push_back(std::move(item));
			}
			return result;
		}

		void ReadReady(State& state, ID3D11DeviceContext* context)
		{
			if (state.pending.empty() || state.writer.valid())
				return;
			auto& pending = state.pending.front();
			const auto ready = context->GetData(pending.ready.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (ready == S_FALSE)
				return;
			if (FAILED(ready))
				throw std::runtime_error("native replay capture GPU readiness query failed");
			std::vector<Pixels> images;
			images.reserve(pending.images.size());
			for (const auto& image : pending.images) {
				Pixels pixels{ image, std::vector<std::byte>(static_cast<std::size_t>(image.rowBytes) * image.height) };
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto result = context->Map(image.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (result == DXGI_ERROR_WAS_STILL_DRAWING)
					return;
				if (FAILED(result))
					throw std::runtime_error("native replay capture nonblocking texture map failed");
				if (!mapped.pData || mapped.RowPitch < image.rowBytes) {
					context->Unmap(image.staging.Get(), 0);
					throw std::runtime_error("native replay capture invalid mapped row pitch");
				}
				for (std::uint32_t row = 0; row < image.height; ++row)
					std::memcpy(pixels.bytes.data() + static_cast<std::size_t>(row) * image.rowBytes,
						static_cast<const std::byte*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, image.rowBytes);
				context->Unmap(image.staging.Get(), 0);
				pixels.image.staging.Reset();
				images.push_back(std::move(pixels));
			}
			auto frame = std::move(pending.frame);
			state.pending.pop_front();
			state.writer = std::async(std::launch::async, [directory = state.directory, frame = std::move(frame), images = std::move(images)]() mutable {
				for (auto& pixels : images) {
					const auto name = pixels.image.metadata.at("file").get<std::string>();
					const auto finalPath = directory / name;
					auto temporary = finalPath;
					temporary += ".tmp";
					WriteFile(temporary, pixels.bytes.data(), pixels.bytes.size());
					pixels.image.metadata["sha256"] = Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(pixels.bytes));
					std::filesystem::rename(temporary, finalPath);
					frame["eyes"][pixels.image.eye][pixels.image.role] = std::move(pixels.image.metadata);
				}
				return frame;
			});
		}

		void Capture(State& state, ID3D11Device* device, ID3D11DeviceContext* context, const Batch& batch)
		{
			if (!batch.supported)
				throw std::runtime_error(batch.unsupportedReason.empty() ? "renderer did not prove native replay support" : batch.unsupportedReason);
			if (batch.eyes.empty() || batch.eyes.size() > 2)
				throw std::runtime_error("native replay requires one complete resource set per eye, at most two");
			const auto hash = batch.runtime.at("sha256").get<std::string>();
			if (batch.runtime.at("path").get<std::string>().empty() || batch.runtime.at("version").get<std::string>().empty() ||
				hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
					return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
				}))
				throw std::runtime_error("native replay requires the admitted runtime path, version and SHA-256 identity");
			(void)batch.metadata.at("frame").get<std::uint32_t>();
			const auto sourceFrame = batch.metadata.at("sourceWorldFrame").get<std::uint32_t>();
			if (sourceFrame == std::numeric_limits<std::uint32_t>::max())
				throw std::runtime_error("native replay requires attributed source world frames");
			if (state.previousFrame && sourceFrame == *state.previousFrame)
				return;
			if (state.previousFrame && sourceFrame != *state.previousFrame + 1u)
				throw std::runtime_error("source world frame gap invalidates consecutive temporal replay");
			const auto invariant = Invariant(batch);
			if (state.captured && state.invariant != invariant)
				throw std::runtime_error("runtime, route, resource, colour or tuning changed within replay sequence");
			if (state.pending.size() >= kPendingLimit)
				throw std::runtime_error("readback queue is full; refusing to skip a temporal source frame");
			Pending pending;
			std::vector<D3D11_TEXTURE2D_DESC> stagingDescriptions;
			pending.frame = batch.metadata;
			pending.frame["eyes"] = Json::array();
			std::uint64_t frameBytes = 0;
			std::uint32_t slotMask = 0;
			for (std::size_t index = 0; index < batch.eyes.size(); ++index) {
				const auto& eye = batch.eyes[index];
				if (eye.slot >= 8 || (slotMask & (1u << eye.slot)) ||
					!Color::Finite(eye.motionVectorScale[0]) || !Color::Finite(eye.motionVectorScale[1]))
					throw std::runtime_error("native replay requires unique slots and finite motion scales");
				slotMask |= 1u << eye.slot;
				Json eyeJson = eye.metadata;
				eyeJson.update({ { "slot", eye.slot }, { "featureUpscaling", eye.featureUpscaling }, { "motionVectorScale", eye.motionVectorScale },
					{ "outputSubrect", { { "baseX", eye.outputSubrect.baseX }, { "baseY", eye.outputSubrect.baseY },
										   { "width", eye.outputSubrect.width }, { "height", eye.outputSubrect.height } } } });
				pending.frame["eyes"].push_back(std::move(eyeJson));
				std::uint32_t colorWidth = 0, colorHeight = 0, guideWidth = 0, guideHeight = 0;
				const std::array<ID3D11Texture2D*, 4> resources{ eye.color, eye.depth, eye.motion, eye.output };
				constexpr std::array<const char*, 4> roles{ "color", "depth", "motion", "output" };
				for (std::size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
					auto* source = resources[resourceIndex];
					if (!source)
						throw std::runtime_error("native replay texture is missing");
					ComPtr<ID3D11Device> sourceDevice;
					source->GetDevice(&sourceDevice);
					D3D11_TEXTURE2D_DESC description{};
					source->GetDesc(&description);
					if (sourceDevice.Get() != device || !description.Width || !description.Height ||
						description.ArraySize != 1 || description.MipLevels != 1 || description.SampleDesc.Count != 1)
						throw std::runtime_error("native replay requires single-sample, single-level textures on the render device");
					if (!resourceIndex) {
						colorWidth = description.Width;
						colorHeight = description.Height;
						if (eye.outputSubrect.baseX || eye.outputSubrect.baseY ||
							eye.outputSubrect.width != colorWidth || eye.outputSubrect.height != colorHeight)
							throw std::runtime_error("native replay requires a fully evaluated output canvas; partial provider output is undefined");
					}
					if (resourceIndex == 1) {
						guideWidth = description.Width;
						guideHeight = description.Height;
					}
					const bool guide = resourceIndex == 1 || resourceIndex == 2;
					if (description.Width != (guide ? guideWidth : colorWidth) ||
						description.Height != (guide ? guideHeight : colorHeight))
						throw std::runtime_error("native replay source/guide/output extents or evaluated rectangle are incompatible");
					const auto rowBytes = static_cast<std::uint64_t>(description.Width) * BytesPerPixel(description.Format);
					const auto bytes = rowBytes * description.Height;
					if (rowBytes > std::numeric_limits<std::uint32_t>::max() || bytes > kByteBudget / 2 || frameBytes > kByteBudget / 2 - bytes)
						throw std::runtime_error("native replay frame exceeds the bounded staging and CPU memory budget");
					frameBytes += bytes;
					Image image;
					image.eye = index;
					image.role = roles[resourceIndex];
					image.rowBytes = static_cast<std::uint32_t>(rowBytes);
					image.height = description.Height;
					const auto name = "frame-" + std::to_string(state.captured) + "-eye-" + std::to_string(index) + "-" + image.role + ".bin";
					image.metadata = { { "file", name }, { "width", description.Width }, { "height", description.Height },
						{ "format", description.Format }, { "rowBytes", rowBytes }, { "bytes", bytes } };
					description.Usage = D3D11_USAGE_STAGING;
					description.BindFlags = description.MiscFlags = 0;
					description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					stagingDescriptions.push_back(description);
					pending.images.push_back(std::move(image));
				}
			}
			// Reserve the entire requested sequence before recording the first GPU copy.
			if (frameBytes > kByteBudget / 2 / state.requested)
				throw std::runtime_error("requested replay sequence exceeds 512 MiB staging plus CPU budget; request fewer frames");
			for (std::size_t index = 0; index < pending.images.size(); ++index) {
				if (FAILED(device->CreateTexture2D(&stagingDescriptions[index], nullptr, &pending.images[index].staging)))
					throw std::runtime_error("native replay staging texture allocation failed");
				Util::SetResourceName(pending.images[index].staging.Get(), "NeuralRendering::ReplayCaptureReadback");
			}
			D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
			if (FAILED(device->CreateQuery(&query, &pending.ready)))
				throw std::runtime_error("native replay readiness query allocation failed");
			Util::SetResourceName(pending.ready.Get(), "NeuralRendering::ReplayCaptureReady");
			for (std::size_t index = 0; index < batch.eyes.size(); ++index) {
				const auto& eye = batch.eyes[index];
				const std::array<ID3D11Texture2D*, 4> sources{ eye.color, eye.depth, eye.motion, eye.output };
				for (std::size_t resource = 0; resource < sources.size(); ++resource)
					context->CopyResource(pending.images[index * sources.size() + resource].staging.Get(), sources[resource]);
			}
			context->End(pending.ready.Get());
			state.runtime = batch.runtime;
			state.adapter = batch.adapter;
			state.invariant = invariant;
			state.previousFrame = sourceFrame;
			state.bytes += frameBytes;
			state.pending.push_back(std::move(pending));
			++state.captured;
		}
	}

	bool IsArmed() noexcept { return armed.load(std::memory_order_acquire); }
	Json Request(std::uint32_t frameCount, std::uint32_t timeoutMs, const std::filesystem::path& evidenceRoot)
	{
		auto& state = Owner();
		std::lock_guard lock(state.mutex);
		PollWriter(state);
		if (!frameCount || frameCount > 32 || !timeoutMs || timeoutMs > 30000)
			return { { "ok", false }, { "reason", "frameCount must be 1..32 and timeoutMs 1..30000" } };
		if (state.state == "capturing" || state.writer.valid())
			return { { "ok", false }, { "reason", "a replay capture or its file writer still owns the capture slot" } };
		try {
			const auto root = std::filesystem::absolute(evidenceRoot);
			std::filesystem::create_directories(root);
			static std::uint64_t serial = 0;
			const auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			const auto directory = root / ("capture-" + std::to_string(time) + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial));
			if (!std::filesystem::create_directory(directory))
				throw std::runtime_error("native replay evidence directory already exists");
			state.directory = directory;
			state.requestId = directory.filename().string();
			state.manifest.clear();
			state.requested = frameCount;
			state.captured = 0;
			state.bytes = 0;
			state.previousFrame.reset();
			state.frames = Json::array();
			state.pending.clear();
			state.runtime = state.adapter = state.invariant = Json::object();
			state.context.Reset();
			state.deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			state.reason.clear();
			state.state = "capturing";
			diagnosticFailure.store(false, std::memory_order_release);
			armed.store(true, std::memory_order_release);
		} catch (const std::exception& error) {
			return { { "ok", false }, { "reason", error.what() } };
		}
		return Snapshot(state);
	}
	Json Cancel(std::string_view requestId)
	{
		auto& state = Owner();
		std::lock_guard lock(state.mutex);
		PollWriter(state);
		if (requestId.empty() || requestId != state.requestId)
			return { { "ok", false }, { "reason", "requestId does not own this capture" } };
		if (state.state == "capturing")
			Stop(state, "cancelled", "cancelled by the capture owner");
		return Snapshot(state);
	}
	void Fail(std::string_view reason) noexcept
	{
		try {
			auto& state = Owner();
			std::lock_guard lock(state.mutex);
			if (state.state == "capturing")
				Stop(state, "failed", reason);
		} catch (...) {
			diagnosticFailure.store(true, std::memory_order_release);
			armed.store(false, std::memory_order_release);
		}
	}
	Json Status()
	{
		auto& state = Owner();
		std::lock_guard lock(state.mutex);
		PollWriter(state);
		return Snapshot(state);
	}
	void OfferBatch(ID3D11Device* device, ID3D11DeviceContext* context, const Batch& batch) noexcept
	{
		if (!IsArmed())
			return;
		try {
			auto& state = Owner();
			std::lock_guard lock(state.mutex);
			try {
				PollWriter(state);
				if (state.state != "capturing")
					return;
				if (!device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
					throw std::runtime_error("native replay requires the owning immediate render context");
				ComPtr<ID3D11Device> contextDevice;
				context->GetDevice(&contextDevice);
				if (contextDevice.Get() != device || (state.context && state.context.Get() != context))
					throw std::runtime_error("native replay render context changed during acquisition");
				state.context = context;
				ReadReady(state, context);
				if (state.captured < state.requested)
					Capture(state, device, context, batch);
			} catch (const std::exception& error) {
				Stop(state, "failed", error.what());
			} catch (...) {
				Stop(state, "failed", "unknown native replay capture error");
			}
		} catch (...) {
			diagnosticFailure.store(true, std::memory_order_release);
			armed.store(false, std::memory_order_release);
		}
	}
}
#endif
