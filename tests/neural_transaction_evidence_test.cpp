#include "Features/Upscaling/NeuralRendering/CharacterPreparationEvidenceJson.h"

#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace NeuralRendering;
	using namespace NeuralRendering::Evidence;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	ExecutionDescriptor Descriptor(RenderingMode mode, bool fovOnly, bool characters)
	{
		ExecutionDescriptor value;
		value.submissionId = 72;
		value.frame = 104;
		value.sourceWorldFrame = 103;
		value.generation = 8;
		value.colorRevision = 9;
		value.inputEpoch = 11;
		value.route = mode == RenderingMode::Foveated ? FeatureSlotRoute::Submit : FeatureSlotRoute::Main;
		value.insertion = mode == RenderingMode::FullResolution ? InsertionPoint::FinalLdrPreUi : InsertionPoint::UpscaledCenter;
		value.logicalEyeCount = 2;
		value.regionCount = characters ? 4 : 2;
		value.context = { .renderingMode = mode, .fovOnly = fovOnly, .captureEpoch = 13, .configurationEpoch = 17, .sourceContext = "admitted_test_route", .sourceTransactionId = 19, .dlssViewportCrop = UpscalingDLSS::ViewportCrop{ { 756, 840 }, { 0, 0, 756, 840 }, { 1512, 1680 }, { 0, 0, 1512, 1680 } }, .jitterPixels = std::array{ 0.25f, -0.125f }, .sourceColorOrigin = std::array{ 64u, 96u }, .sourceGuideOrigin = std::array{ 128u, 192u } };
		const UpscalingDLSS::Extent extent = mode == RenderingMode::ReducedResolution ? UpscalingDLSS::Extent{ 756, 840 } : UpscalingDLSS::Extent{ 1512, 1680 };
		for (uint32_t i = 0; i < value.regionCount; ++i) {
			auto& region = value.regions[i];
			region.eye = i % 2;
			region.region = i / 2;
			region.logicalSlot = region.eye;
			region.physicalSlot = region.eye + region.region * 4;
			region.regionIdentity = 1000 + i;
			region.clusterIdentity = 2000 + region.region;
			region.context = value.context;
			region.characterVisualIsolation = characters;
			region.viewportCrop = { extent, { 0, 0, extent.width, extent.height }, extent, { 0, 0, extent.width, extent.height } };
			region.color = { extent, 26, characters ? ComputeSubrect{ 64, 128, 256, 192 } : ComputeSubrect{ 0, 0, extent.width, extent.height },
				static_cast<uint64_t>(extent.width) * extent.height * 4, std::nullopt };
			region.color.workLogicalBytes = region.color.work.Area() * 4;
			region.output = region.color;
			region.depth = { extent, 41, region.color.work, std::nullopt, std::nullopt };
			region.motion = { extent, 34, region.color.work, 0, 0 };
			region.motionVectorScaleX = static_cast<float>(extent.width);
			region.motionVectorScaleY = static_cast<float>(extent.height);
			value.plannedPhysicalSlotMask |= 1u << region.physicalSlot;
		}
		return value;
	}

	void TestRouteMatrix()
	{
		for (uint32_t mode = 0; mode < 3; ++mode) {
			for (bool fov : { false, true }) {
				for (bool characters : { false, true }) {
					const auto descriptor = Descriptor(static_cast<RenderingMode>(mode), fov, characters);
					const ExecutionEvidence evidence(descriptor);
					const auto value = ExecutionJson(evidence);
					const std::array names{ "full_resolution", "foveated", "reduced_resolution" };
					Check(value["source"]["mode"] == names[mode] && value["source"]["fovOnly"] == fov, "route arrangement lost");
					Check(value["source"]["captureEpoch"] == 13 && value["source"]["configurationEpoch"] == 17 && value["source"]["sourceTransactionId"] == 19,
						"caller transaction epochs lost");
					Check(value["frame"] == 104 && value["sourceWorldFrame"] == 103 && value["generation"] == 8 && value["colorRevision"] == 9 && value["inputEpoch"] == 11,
						"producer identity lost");
					Check(value["route"] == (mode == 1 ? "submit" : "main") && value["logicalEyeCount"] == 2, "physical route lost");
					Check(value["plannedRegionCount"] == (characters ? 4 : 2) && value["actualEvaluationCount"] == 0 && value["activeEvaluationPixels"] == 0,
						"planned regions reported as evaluated work");
					for (uint32_t i = 0; i < descriptor.regionCount; ++i) {
						const auto& region = value["regions"][i];
						Check(region["eye"] == i % 2 && region["region"] == i / 2 && region["regionIdentity"] == 1000 + i && region["clusterIdentity"] == 2000 + i / 2,
							"physical region identity lost");
						Check(region["characterSelection"] == characters && region["source"] == value["source"], "per-region context mismatch");
						Check(region["nrInput"]["capacityGrid"]["width"] == (mode == 2 ? 756 : 1512), "NR capacity confused with DLSS output");
						Check(region["nrInput"]["workPixels"] == descriptor.regions[i].color.work.Area(), "subrect work lost");
						Check(region["nrDepthGuide"]["capacityLogicalBytes"].is_null() && region["nrMotionGuide"]["capacityLogicalBytes"] == 0,
							"unknown logical bytes collapsed to zero");
						Check(region["timing"]["evaluationGpu"]["microseconds"].is_null() && region["timing"]["depthGuide"]["gpu"]["inclusiveMs"].is_null(),
							"missing timing serialized as measured zero");
					}
				}
			}
		}
		ExecutionEvidence unknown({});
		const auto absent = ExecutionJson(unknown);
		Check(absent["source"]["mode"].is_null() && absent["source"]["configurationEpoch"].is_null() && absent["source"]["dlssRouteGrid"].is_null(),
			"absent caller facts invented");
	}

	void TestDelayedAndPartialOutcomes()
	{
		for (uint32_t attempted : { 0u, 1u, 4u }) {
			auto descriptor = Descriptor(RenderingMode::FullResolution, true, true);
			auto evidence = std::make_shared<ExecutionEvidence>(descriptor);
			descriptor.frame = 999;
			descriptor.regions[0].color.work = {};
			evidence->Update([&](auto& state) {
				state.finished = true;
				state.succeeded = attempted == 4;
				state.failureStage = attempted == 4 ? 0 : 6;
				for (uint32_t i = 0; i < attempted; ++i) {
					auto& region = state.regions[i];
					region.runtime.createAttempted = region.runtime.createSucceeded = region.runtime.evaluateAttempted = true;
					region.runtime.evaluateSucceeded = i + 1 < attempted || attempted == 4;
					region.runtime.createCpuMicroseconds = 2;
					region.runtime.evaluateCpuMicroseconds = 3;
					region.evaluationGpu = { ExecutionTimingState::Pending, std::nullopt };
					state.attemptedPhysicalSlotMask |= 1u << evidence->Descriptor().regions[i].physicalSlot;
				}
				state.batchGpu = { ExecutionTimingState::Pending, std::nullopt };
			});
			const auto pending = ExecutionJson(*evidence);
			Check(pending["plannedRegionCount"] == 4 && pending["actualEvaluationCount"] == attempted && pending["activeEvaluationPixels"] == attempted * 256u * 192u,
				"attempted work confused with plan or success");
			Check(pending["frame"] == 104 && pending["regions"][0]["nrInput"]["work"]["width"] == 256, "descriptor changed after construction");
			Check(pending["timing"]["wholeFeatureBatchGpuLegacy"]["state"] == "pending" && pending["timing"]["wholeFeatureBatchGpuLegacy"]["microseconds"].is_null(),
				"pending GPU timing fabricated");
			std::thread completion([evidence, attempted] {
				evidence->Update([&](auto& state) {
					state.batchGpu = { ExecutionTimingState::Complete, 1000 };
					for (uint32_t i = 0; i < attempted; ++i)
						state.regions[i].evaluationGpu = { ExecutionTimingState::Complete, 10 + i };
				});
			});
			completion.join();
			const auto complete = ExecutionJson(*evidence);
			Check(complete["source"] == pending["source"] && complete["regions"][0]["nrViewport"] == pending["regions"][0]["nrViewport"], "delayed timing replaced descriptor identity");
			Check(complete["timing"]["wholeFeatureBatchGpuLegacy"]["microseconds"] == 1000 && pending["timing"]["wholeFeatureBatchGpuLegacy"]["microseconds"].is_null(),
				"whole timer replaced with region sum or sealed snapshot mutated");
			Check(!complete.contains("gpuTotal") && !complete.contains("presented") && !complete.contains("submitted"), "execution snapshot asserts unowned presentation or timing sum");
			if (attempted == 1)
				Check(complete["regions"][0]["evaluationAttempted"] == true && complete["regions"][0]["evaluationSucceeded"] == false && complete["succeeded"] == false,
					"failed evaluation hidden from actual work count");
		}
		ExecutionEvidence privateCommit(Descriptor(RenderingMode::ReducedResolution, false, true));
		privateCommit.Update([](auto& state) {
			state.finished = true;
			state.succeeded = false;
			state.failureStage = 7;
			state.regions[0].privateOutputCommitted = true;
			state.committedPhysicalSlotMask = 1;
			state.regions[0].evaluationGpu = { ExecutionTimingState::Failed, 123 };
		});
		const auto failed = ExecutionJson(privateCommit);
		Check(failed["privateCommittedPhysicalSlotMask"] == 1 && failed["regions"][0]["privateOutputCommitted"] == true && failed["succeeded"] == false,
			"private partial commit treated as visible success");
		Check(failed["regions"][0]["timing"]["evaluationGpu"]["microseconds"].is_null(), "failed timing retained a numeric sample");
	}

	void TestCharacterEmptyAndDelayedCoverage()
	{
		auto evidence = std::make_shared<CharacterPreparationEvidence>();
		evidence->key = { .frame = 77, .sourceWorldFrame = 76, .eye = 1, .featureSlot = 3, .generation = 4, .contentSerial = 5, .settingsKey = 6, .captureEpoch = 7 };
		evidence->prepared = true;
		evidence->requiresEvaluation = false;
		evidence->outcome = "empty_no_work";
		evidence->computeSubrect = { 0, 0, 1512, 1680 };
		evidence->support = std::make_shared<CharacterMaskSupportCapture>(evidence->key);
		evidence->support->Pending();
		const auto pending = CharacterPreparationJson(evidence);
		Check(pending["regions"].empty() && pending["requiresEvaluation"] == false && pending["dispatchedPixels"] == 0, "empty preparation invented full-frame evaluation");
		Check(pending["maskSupport"]["pixels"].is_null() && pending["maskSupport"]["state"] == "pending", "pending mask coverage treated as zero");
		evidence->support->Complete(0);
		evidence->support->Complete(999);
		const auto ready = CharacterPreparationJson(evidence);
		Check(ready["maskSupport"]["state"] == "ready" && ready["maskSupport"]["pixels"] == 0, "measured empty mask lost or overwritten");
		Check(ready["maskSupport"]["producer"]["captureEpoch"] == 7 && ready["maskSupport"]["producer"]["contentSerial"] == 5, "coverage producer identity lost");
		Check(ready["sourceCapture"]["available"] == false && ready["timing"]["explicitWait"]["milliseconds"].is_null(), "unobserved source or wait fabricated");
		Check(!CharacterPreparationJson({})["available"].get<bool>(), "missing preparation marked available");
	}
}

int main()
{
	try {
		TestRouteMatrix();
		TestDelayedAndPartialOutcomes();
		TestCharacterEmptyAndDelayedCoverage();
		std::cout << "Neural transaction evidence JSON tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
