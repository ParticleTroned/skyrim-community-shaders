#include "Features/VR/ImGuiVRHelperHostPolicy.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	namespace Policy = ImGuiVRHelperHostPolicy;
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	Policy::PairInput ValidPair()
	{
		Policy::PairInput input{};
		input.isVR = true;
		input.negotiatedCSXHostToken = 17;
		input.activeCSXHostToken = 17;
		input.hasWorldContent = true;
		input.currentPair = { 51, 9, 42, 123 };
		input.completedResourceGeneration = 7;
		input.currentDeviceIdentity = 0x123;
		for (std::uint32_t index = 0; index < 2; ++index) {
			auto& eye = input.eyes[index];
			eye.candidate = Policy::Candidate::CurrentNative;
			eye.scenePair = input.currentPair;
			eye.depthPair = input.currentPair;
			eye.submitLease = { 7, 0x123, true, false, false };
			eye.colorView = { 0x100 + index, 0x123, { 2000, 1000 } };
			eye.colorBounds = { 0.0, 0.0, 1.0, 1.0 };
			eye.colorContract = VRSubmitColorContract::Resolve(true, VRSubmitColorContract::SourceColorSpace::Gamma);
			eye.renderTargetCapabilityProven = true;
			eye.depthView = { 0x200, 0x123, { 2000, 500 } };
			eye.occlusionDepthRetention = { 0x200, 0x123, 7, true };
			eye.depthRect = { index * 1000, 0, 1000, 500 };
			eye.completedOpaqueDepth = true;
			eye.fullEyeDepthCoverage = true;
			eye.trackingToColorClip = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			eye.trackingToDepthClip = eye.trackingToColorClip;
			eye.trackingToDepthViewMetres = { 0, 0, 1, 0 };
			eye.nearPlane = 5;
			eye.farPlane = 100000;
			eye.sourceUnitsToMetres = 0.01;
		}
		return input;
	}

	void RequireReady(const Policy::PairInput& a_input, const char* a_message)
	{
		const auto result = Policy::BuildPair(a_input);
		Require(result.status == Policy::BuildStatus::Ready && result.packet.has_value(), a_message);
	}

	void RequireRejected(const Policy::PairInput& a_input, const char* a_message)
	{
		const auto result = Policy::BuildPair(a_input);
		Require(result.status == Policy::BuildStatus::Rejected && !result.packet.has_value(), a_message);
	}

	void CompatibilityGate()
	{
		for (unsigned bits = 0; bits < 8; ++bits) {
			auto input = ValidPair();
			input.isVR = (bits & 1) != 0;
			input.negotiatedCSXHostToken = (bits & 2) != 0 ? 17 : 0;
			input.activeCSXHostToken = (bits & 4) != 0 ? 17 : 0;
			const auto result = Policy::BuildPair(input);
			const bool expected = bits == 7;
			Require((result.status == Policy::BuildStatus::Ready) == expected, "Only active negotiated CSX in VR may admit a pair");
			Require(result.packet.has_value() == expected, "An inactive host must not receive a packet");
		}
		auto input = ValidPair();
		input.activeCSXHostToken = 18;
		Require(Policy::BuildPair(input).status == Policy::BuildStatus::Inactive, "A token from another registration must not activate hosting");
		input = ValidPair();
		input.hasWorldContent = false;
		input.eyes = {};
		const auto empty = Policy::BuildPair(input);
		Require(empty.status == Policy::BuildStatus::NoContent && !empty.packet, "No content must need no valid graphics descriptors");
	}

	void CurrentImagesOnly()
	{
		for (auto candidate : { Policy::Candidate::CurrentNative, Policy::Candidate::CurrentReconstructed }) {
			auto input = ValidPair();
			for (auto& eye : input.eyes)
				eye.candidate = candidate;
			RequireReady(input, "Current complete native and reconstructed pairs must be admitted");
		}
		for (auto candidate : { Policy::Candidate::Unproven, Policy::Candidate::Protected, Policy::Candidate::Loading,
				 Policy::Candidate::Retained, Policy::Candidate::DeviceLost, static_cast<Policy::Candidate>(255) }) {
			for (std::size_t index = 0; index < 2; ++index) {
				auto input = ValidPair();
				input.eyes[index].candidate = candidate;
				RequireRejected(input, "Unsafe or unknown images must reject the entire pair");
			}
		}
		auto input = ValidPair();
		input.eyes[1].candidate = Policy::Candidate::CurrentReconstructed;
		RequireRejected(input, "Mixed native/reconstructed pair routes are not qualified");
	}

	void ExactProducerAndPublication()
	{
		const auto stalePair = [](auto a_mutate) {
			for (std::size_t index = 0; index < 2; ++index) {
				auto colorMismatch = ValidPair();
				a_mutate(colorMismatch.eyes[index].scenePair);
				RequireRejected(colorMismatch, "Color must match the captured stereo producer");
				auto depthMismatch = ValidPair();
				a_mutate(depthMismatch.eyes[index].depthPair);
				RequireRejected(depthMismatch, "Depth must match the captured stereo producer");
			}
		};
		stalePair([](auto& identity) { ++identity.token; });
		stalePair([](auto& identity) { ++identity.compositorCycle; });
		stalePair([](auto& identity) { ++identity.frame; });
		stalePair([](auto& identity) { ++identity.thread; });

		const auto invalidCurrent = [](auto a_mutate) {
			auto input = ValidPair();
			a_mutate(input);
			for (auto& eye : input.eyes) {
				eye.scenePair = input.currentPair;
				eye.depthPair = input.currentPair;
			}
			RequireRejected(input, "Missing current producer identity cannot become proof by matching itself");
		};
		invalidCurrent([](auto& input) { input.currentPair.token = 0; });
		invalidCurrent([](auto& input) { input.currentPair.compositorCycle = 0; });
		invalidCurrent([](auto& input) { input.currentPair.thread = 0; });
		invalidCurrent([](auto& input) { input.currentPair.frame = std::numeric_limits<std::uint32_t>::max(); });
		invalidCurrent([](auto& input) { input.completedResourceGeneration = 0; });
		invalidCurrent([](auto& input) { input.currentDeviceIdentity = 0; });

		for (std::size_t index = 0; index < 2; ++index) {
			const auto badLease = [index](auto a_mutate) {
				auto input = ValidPair();
				a_mutate(input.eyes[index].submitLease);
				RequireRejected(input, "The full OpenVR payload must be retained under the current publication lease");
			};
			badLease([](auto& lease) { lease.colorTextureRetained = false; });
			badLease([](auto& lease) { lease.depthTextureRequired = true; });
			badLease([](auto& lease) { ++lease.deviceIdentity; });
			badLease([](auto& lease) { ++lease.generation; });
			const auto badOcclusionRetention = [index](auto a_mutate) {
				auto input = ValidPair();
				input.eyes[index].submitLease.depthTextureRequired = true;
				input.eyes[index].submitLease.depthTextureRetained = true;
				a_mutate(input.eyes[index].occlusionDepthRetention);
				RequireRejected(input, "A retained compositor payload must not prove helper occlusion-depth ownership");
			};
			badOcclusionRetention([](auto& retention) { retention.retained = false; });
			badOcclusionRetention([](auto& retention) { ++retention.resourceIdentity; });
			badOcclusionRetention([](auto& retention) { ++retention.deviceIdentity; });
			badOcclusionRetention([](auto& retention) { ++retention.generation; });
		}
		RequireReady(ValidPair(), "Color-only OpenVR payloads can retain separate helper occlusion depth");
		auto extendedPayload = ValidPair();
		for (auto& eye : extendedPayload.eyes) {
			eye.submitLease.depthTextureRequired = true;
			eye.submitLease.depthTextureRetained = true;
		}
		RequireReady(extendedPayload, "Separately retained compositor depth attachments must also be supported");
		auto reusedPointers = ValidPair();
		++reusedPointers.completedResourceGeneration;
		RequireRejected(reusedPointers, "Reused addresses must not rescue a stale publication generation");
	}

	void BothEyesBeforeFirstDraw()
	{
		const auto badEye = [](auto a_mutate) {
			for (std::size_t index = 0; index < 2; ++index) {
				auto input = ValidPair();
				a_mutate(input.eyes[index]);
				const auto result = Policy::BuildPair(input);
				Require(result.status == Policy::BuildStatus::Rejected && !result.packet, "A bad second eye must not leave an admitted first eye");
				Require(result.rejectedEye == index, "The rejected eye must remain diagnosable");
			}
		};
		badEye([](auto& eye) { eye.completedOpaqueDepth = false; });
		badEye([](auto& eye) { eye.fullEyeDepthCoverage = false; });
		badEye([](auto& eye) { eye.renderTargetCapabilityProven = false; });
		badEye([](auto& eye) { eye.colorContract = {}; });
		badEye([](auto& eye) { eye.colorView.resourceIdentity = 0; });
		badEye([](auto& eye) { eye.depthView.resourceIdentity = 0; });
		badEye([](auto& eye) { ++eye.colorView.deviceIdentity; });
		badEye([](auto& eye) { ++eye.depthView.deviceIdentity; });
		badEye([](auto& eye) { eye.colorView.sampleCount = 4; });
		badEye([](auto& eye) { eye.depthView.sampleCount = 4; });
		badEye([](auto& eye) { eye.colorView.arraySize = 2; });
		badEye([](auto& eye) { eye.depthView.arraySize = 2; });
		badEye([](auto& eye) { eye.depthView.mipLevel = 1; });
		badEye([](auto& eye) { eye.depthRect.width = std::numeric_limits<std::uint32_t>::max(); });
		badEye([](auto& eye) { eye.colorBounds.uMax = 0.0; });
		badEye([](auto& eye) { eye.colorBounds.vMin = -0.1; });
		badEye([](auto& eye) { eye.trackingToColorClip[0] = std::numeric_limits<double>::infinity(); });
		badEye([](auto& eye) { eye.trackingToDepthClip[15] = std::numeric_limits<double>::quiet_NaN(); });
		badEye([](auto& eye) { eye.trackingToDepthViewMetres[2] = std::numeric_limits<double>::quiet_NaN(); });
		badEye([](auto& eye) { eye.trackingToColorClip.fill(0); });
		badEye([](auto& eye) { eye.trackingToDepthViewMetres.fill(0); });
		badEye([](auto& eye) { eye.nearPlane = 0; });
		badEye([](auto& eye) { eye.farPlane = eye.nearPlane; });
		badEye([](auto& eye) { eye.sourceUnitsToMetres = -1; });
	}

	void StereoResourceIsolation()
	{
		for (std::size_t color = 0; color < 2; ++color) {
			for (std::size_t depth = 0; depth < 2; ++depth) {
				auto input = ValidPair();
				input.eyes[color].colorView.resourceIdentity = input.eyes[depth].depthView.resourceIdentity;
				RequireRejected(input, "Writing either eye must never corrupt either depth source");
			}
		}
		auto sharedColor = ValidPair();
		sharedColor.eyes[1].colorView = sharedColor.eyes[0].colorView;
		RequireRejected(sharedColor, "Shared color textures require disjoint eye viewports");
		sharedColor.eyes[0].colorBounds.uMax = 0.5;
		sharedColor.eyes[1].colorBounds.uMin = 0.5;
		RequireReady(sharedColor, "Disjoint atlas eye viewports must be supported");
		sharedColor.eyes[1].colorBounds.uMin = 1.0;
		sharedColor.eyes[1].colorBounds.uMax = 0.5;
		RequireReady(sharedColor, "Reversed bounds must preserve the positive atlas region");
		--sharedColor.eyes[1].colorView.extent.width;
		RequireRejected(sharedColor, "One resource identity cannot have contradictory descriptors");

		auto sharedDepth = ValidPair();
		sharedDepth.eyes[1].depthRect.x = 999;
		RequireRejected(sharedDepth, "Shared depth-eye rectangles must not overlap");
		sharedDepth = ValidPair();
		++sharedDepth.eyes[1].depthView.extent.width;
		RequireRejected(sharedDepth, "Shared depth descriptors must agree even if each region fits");
		sharedDepth = ValidPair();
		sharedDepth.eyes[1].depthView.resourceIdentity = 0x201;
		sharedDepth.eyes[1].occlusionDepthRetention.resourceIdentity = 0x201;
		sharedDepth.eyes[1].depthRect.x = 0;
		RequireReady(sharedDepth, "Separate depth resources can use the same pixel coordinates");
	}

	void SnapshotOwnsItsMetadata()
	{
		auto input = ValidPair();
		input.eyes[1].colorBounds = { 0.9, 0.8, 0.1, 0.2 };
		const auto result = Policy::BuildPair(input);
		Require(result.status == Policy::BuildStatus::Ready && result.packet, "A reversed eye must produce a complete snapshot");
		const auto& packet = *result.packet;
		Require(packet.hostToken == 17 && packet.pair.token == 51 && packet.resourceGeneration == 7 && packet.deviceIdentity == 0x123,
			"The output must retain the validated ownership and scene identity");
		Require(packet.eyes[1].viewport.flipX && packet.eyes[1].viewport.flipY, "Orientation must not be lost during packet construction");
		Require(packet.eyes[0].depth.activeRect.x == 0 && packet.eyes[1].depth.activeRect.x == 1000,
			"The snapshot must preserve independent depth-eye rectangles");
		Require(!packet.eyes[0].submitLease.depthTextureRequired && packet.eyes[0].occlusionDepthRetention.retained &&
					packet.eyes[0].occlusionDepthRetention.resourceIdentity == packet.eyes[0].depth.texture.resourceIdentity,
			"Independent occlusion retention must survive without adding an OpenVR depth attachment");
		const auto nearMetres = ImGuiVRHelperScenePacket::DecodeDepthMetres(packet.eyes[0].depthEncoding, 0);
		Require(nearMetres && *nearMetres > 0.04999 && *nearMetres < 0.05001, "Native engine depth must decode with the captured source-unit scale");
		input.eyes[0].depthRect.x = 99;
		input.eyes[0].trackingToColorClip[0] = 17;
		input.eyes[0].trackingToDepthViewMetres[2] = 999;
		input.currentPair.token = 999;
		Require(packet.pair.token == 51 && packet.eyes[0].depth.activeRect.x == 0 &&
					packet.eyes[0].trackingToColorClip[0] == 1 && packet.eyes[0].trackingToDepthViewMetres[2] == 1,
			"Changing later source metadata must not mutate an already captured stereo pair");
	}
}

int main()
{
	const std::array tests{
		std::pair{ "CSX opt-in and empty content", CompatibilityGate },
		std::pair{ "current images only", CurrentImagesOnly },
		std::pair{ "producer and publication identity", ExactProducerAndPublication },
		std::pair{ "pair-wide admission", BothEyesBeforeFirstDraw },
		std::pair{ "stereo resource isolation", StereoResourceIsolation },
		std::pair{ "captured metadata lifetime", SnapshotOwnsItsMetadata },
	};
	try {
		for (const auto& [name, test] : tests) {
			test();
			std::cout << "PASS " << name << '\n';
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
