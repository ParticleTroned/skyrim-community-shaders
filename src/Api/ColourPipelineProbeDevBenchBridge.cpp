#include "ColourPipelineProbeDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Features/Upscaling.h"
#	include "Features/Upscaling/ColourPipelineProbe.h"
#	include "Features/Upscaling/ColourPipelineProbePolicy.h"
#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>
#	include <array>
#	include <atomic>
#	include <stdexcept>
#	include <string_view>

namespace
{
	using json = nlohmann::json;
	namespace Probe = CSX::Diagnostics::ColourPipelineProbe;
	constexpr std::array kStages{ "fsr_input", "fsr_output", "combined_main", "imagespace_input", "imagespace_output" };
	std::atomic_bool registered{ false };
	std::atomic_bool installing{ false };

	std::uint64_t Unsigned(const json& a_args, const char* a_field)
	{
		if (!a_args.contains(a_field) || !a_args.at(a_field).is_number_unsigned())
			throw std::runtime_error(std::string(a_field) + " must be an unsigned integer");
		return a_args.at(a_field).get<std::uint64_t>();
	}

	std::string CaptureId(const json& a_args)
	{
		if (!a_args.contains("captureId") || !a_args.at("captureId").is_string())
			throw std::runtime_error("captureId must be a string");
		auto id = a_args.at("captureId").get<std::string>();
		if (id.empty() || id.size() > Probe::Policy::kMaximumCaptureIdBytes)
			throw std::runtime_error("captureId must contain 1-128 UTF-8 bytes");
		return id;
	}

	json BuildResult(const json& a_args)
	{
		const auto action = a_args.value("action", std::string("status"));
		for (const auto& [key, value] : a_args.items()) {
			const bool common = key == "action" || key == "expectedBuildId";
			const bool identity = key == "captureId" || key == "generation";
			if (!(common || (action == "arm" && (key == "captureId" || key == "expectedRevision" || key == "metadata")) ||
					(action == "read" && (identity || key == "stage" || key == "eye")) ||
					(action == "reset" && identity)))
				throw std::runtime_error("unknown parameter for the selected action: " + key);
		}
		if (action == "status")
			return Probe::BuildStatus();
		if (action == "arm" || action == "reset") {
			if (!a_args.contains("expectedBuildId"))
				throw std::runtime_error("mutation requires expectedBuildId");
			const auto id = CaptureId(a_args);
			std::uint64_t generation = 0;
			std::string error;
			bool accepted = false;
			if (action == "arm") {
				const auto revision = Unsigned(a_args, "expectedRevision");
				const auto status = Upscaling::fidelityFX.GetDevBenchFsrColorContractStatusSnapshot();
				if (!revision || revision != status.contract.revision)
					return { { "accepted", false }, { "error", "FSR request revision did not match" }, { "observedRevision", status.contract.revision } };
				accepted = Probe::Arm(id, revision, a_args.value("metadata", json::object()), generation, error);
			} else {
				accepted = Probe::Reset(id, Unsigned(a_args, "generation"), generation, error);
			}
			return { { "action", action }, { "accepted", accepted }, { "captureId", id },
				{ "generation", generation }, { "error", error.empty() ? json(nullptr) : json(error) } };
		}
		if (action == "read") {
			const auto stage = a_args.at("stage").get<std::string>();
			const auto eye = Unsigned(a_args, "eye");
			if (eye >= 2)
				throw std::runtime_error("eye must be 0 or 1");
			for (std::size_t index = 0; index < kStages.size(); ++index)
				if (stage == kStages[index])
					return Probe::BuildCapture(CaptureId(a_args), Unsigned(a_args, "generation"),
						static_cast<Probe::Stage>(index), static_cast<std::uint32_t>(eye));
			throw std::runtime_error("unsupported probe stage");
		}
		throw std::runtime_error("action must be status, arm, read or reset");
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		if (!a_write)
			return;
		try {
			json output;
			try {
				const std::string_view input = a_argsJson && *a_argsJson ? a_argsJson : "{}";
				if (input.size() > 64 * 1024)
					throw std::runtime_error("probe request exceeds 64 KiB");
				const auto args = json::parse(input);
				if (!args.is_object())
					throw std::runtime_error("arguments must be an object");
				if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
					output = std::move(*mismatch);
				else
					output = BuildResult(args);
			} catch (const std::exception& error) {
				output = { { "error", "invalid probe request" }, { "detail", error.what() } };
			} catch (...) {
				output = { { "error", "unknown probe handler failure" } };
			}
			BuildProvenance::AttachProducer(output);
			const auto serialized = output.dump();
			if (serialized.size() > 128 * 1024)
				a_write(a_sink, R"({"error":"probe response exceeds 128 KiB"})");
			else
				a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"probe response serialization failed"})");
		}
	}
}

void CSX::Api::ColourPipelineProbeDevBenchBridge::Install()
{
	if (registered.load(std::memory_order_acquire))
		return;
	auto* api = DevBenchAPI::GetDevBenchInterface001();
	if (!api || installing.exchange(true))
		return;
	struct InstallationGuard
	{
		~InstallationGuard() { installing.store(false, std::memory_order_release); }
	} guard;
	static const auto descriptor = json{
		{ "description", "Bounded developer-only VR FSR five-seam raw sample capture. arm and reset require exact build identity; arm binds the expected FSR colour request revision. Read one stage and eye per page with the arm receipt's captureId/generation. Combined-main observes kMAIN; ImageSpace seams observe the kVR_FRAMEBUFFER destination before and after the original call. Status retains every named stage-eye queued/mapped flag and the unqueued missingStageEyeSlots, including terminal failures. Storage values are retained without gamma conversion; ambiguous typeless numeric decoding is withheld. Captures require the main-pass stereo FSR path, expire after 15 seconds or 120 readback frames, and never imply render-scale, headset or visual qualification. Limits: 8192 per staging dimension, 256 MiB per slot, 1 GiB total staging payload (driver overhead unmeasured), 16 KiB serialized metadata, 64 KiB request and 128 KiB response. Page dispatch.submittedInputs schema 1 retains actual submitted reset, jitterOffsetPixels in X/Y order and frameTimeDeltaMilliseconds only after same-frame/revision/eye successful attribution. Unavailable input values are null; availability is not internal exposure/history convergence. Additive page sceneObservation schema 1 retains sequential CPU-frame/eye/QPC observations before each stage-eye copy: finite native cached camera matrices and adjusted positions, world-render markers, and typed image-space HDR/cinematic/tint parameters. Cached-camera content frame/QPC/initialization and internal adaptive exposure remain unknown/null; scene reads are not an atomic render snapshot or continuous world-camera equivalence. FSR colour mutations use communityshaders.fsr_color_contract separately." },
		{ "inputSchema", { { "type", "object" }, { "additionalProperties", false }, { "properties", {
																										{ "action", { { "type", "string" }, { "enum", { "status", "arm", "read", "reset" } }, { "default", "status" }, { "description", "Status includes all ten stageEyeSlots and unqueued missingStageEyeSlots for the current capture identity; failure retains them until reset or a new arm. Read pages add versioned dispatch.submittedInputs for the attributed successful eye, with explicit availability and nullable reset/jitterOffsetPixels/frameTimeDeltaMilliseconds. Pages also add sceneObservation schema 1 with CPU observation bracket, finite native cached per-eye camera values and typed image-space parameters; unavailable groups are null, cached content frame and internal adaptive exposure remain unknown." } } },
																										{ "expectedBuildId", { { "type", "string" } } },
																										{ "captureId", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } },
																										{ "expectedRevision", { { "type", "integer" }, { "minimum", 1 } } },
																										{ "generation", { { "type", "integer" }, { "minimum", 1 } } },
																										{ "stage", { { "type", "string" }, { "enum", kStages } } },
																										{ "eye", { { "type", "integer" }, { "enum", { 0, 1 } } } },
																										{ "metadata", { { "type", "object" } } },
																									} } } },
	}
	                                   .dump();
	api->RegisterTool("communityshaders.colour_pipeline_probe", descriptor.c_str(), &ToolHandler, nullptr);
	registered.store(true, std::memory_order_release);
	logger::info("ColourPipelineProbe: registered bounded VR capture controls");
}

#else

void CSX::Api::ColourPipelineProbeDevBenchBridge::Install() {}

#endif
