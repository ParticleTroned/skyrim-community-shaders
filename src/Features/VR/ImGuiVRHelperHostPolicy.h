#pragma once

#include "../Upscaling/VRSubmitColorContract.h"
#include "ImGuiVRHelperScenePacket.h"
#include "OpenVRSubmitLeasePolicy.h"
#include "VRRenderScaleFrameBoundaryPolicy.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace ImGuiVRHelperHostPolicy
{
	namespace Scene = ImGuiVRHelperScenePacket;
	using PairIdentity = VRRenderScaleFrameBoundaryPolicy::PairIdentity;
	using PublicationLease = OpenVRSubmitLeasePolicy::PublicationLease;

	enum class Candidate : std::uint8_t
	{
		Unproven,
		CurrentNative,
		CurrentReconstructed,
		Protected,
		Loading,
		Retained,
		DeviceLost,
	};

	enum class BuildStatus : std::uint8_t
	{
		Ready,
		NotVR,
		Inactive,
		NoContent,
		Rejected,
	};

	enum class Rejection : std::uint8_t
	{
		None,
		InvalidPair,
		InvalidPublicationIdentity,
		IneligibleCandidate,
		CandidateMismatch,
		ScenePairMismatch,
		DepthPairMismatch,
		DepthNotCompleted,
		DepthNotFullEye,
		MissingDepthRetention,
		DepthRetentionMismatch,
		StalePublication,
		InvalidColorView,
		InvalidDepthView,
		WrongDevice,
		InvalidColorContract,
		TargetNotWritable,
		InvalidColorBounds,
		InvalidDepthRect,
		InvalidCamera,
		InvalidDepthEncoding,
		ColorDepthAlias,
		SharedColorMismatch,
		SharedDepthMismatch,
		OverlappingColorEyes,
		OverlappingDepthEyes,
	};

	/// Retention evidence for the occlusion source, independently of an OpenVR payload depth attachment.
	struct OcclusionDepthRetention
	{
		std::uintptr_t resourceIdentity = 0;
		std::uintptr_t deviceIdentity = 0;
		std::uint64_t generation = 0;
		bool retained = false;
	};

	/// Captured metadata only; the caller retains the referenced resources and their matching contents.
	struct EyeInput
	{
		Candidate candidate = Candidate::Unproven;
		PairIdentity scenePair{};
		PairIdentity depthPair{};
		PublicationLease submitLease{};
		Scene::TextureView colorView{};
		Scene::NormalizedBounds colorBounds{};
		VRSubmitColorContract::Contract colorContract{};
		bool renderTargetCapabilityProven = false;
		Scene::TextureView depthView{};
		OcclusionDepthRetention occlusionDepthRetention{};
		Scene::PixelRect depthRect{};
		bool completedOpaqueDepth = false;
		bool fullEyeDepthCoverage = false;
		Scene::Matrix4x4 trackingToColorClip{};
		Scene::Matrix4x4 trackingToDepthClip{};
		std::array<double, 4> trackingToDepthViewMetres{};
		double nearPlane = 0.0;
		double farPlane = 0.0;
		double sourceUnitsToMetres = 0.0;
	};

	/// Hosting tokens come from explicit CSX negotiation, independently of module presence or hook order.
	struct PairInput
	{
		bool isVR = false;
		std::uint64_t negotiatedCSXHostToken = 0;
		std::uint64_t activeCSXHostToken = 0;
		bool hasWorldContent = false;
		PairIdentity currentPair{};
		std::uint64_t completedResourceGeneration = 0;
		std::uintptr_t currentDeviceIdentity = 0;
		std::array<EyeInput, 2> eyes{};
	};

	/// Validated camera and view metadata for one eye of an admitted completed-opaque pair.
	struct EyePacket
	{
		Candidate candidate = Candidate::Unproven;
		PublicationLease submitLease{};
		Scene::TextureView colorView{};
		Scene::OutputViewport viewport{};
		VRSubmitColorContract::Contract colorContract{};
		Scene::DepthView depth{};
		OcclusionDepthRetention occlusionDepthRetention{};
		Scene::DepthEncoding depthEncoding{};
		Scene::Matrix4x4 trackingToColorClip{};
		Scene::Matrix4x4 trackingToDepthClip{};
		std::array<double, 4> trackingToDepthViewMetres{};
	};

	/// A by-value stereo snapshot; publication still requires the caller's retained resource ownership.
	struct StereoPacket
	{
		std::uint64_t hostToken = 0;
		PairIdentity pair{};
		std::uint64_t resourceGeneration = 0;
		std::uintptr_t deviceIdentity = 0;
		std::array<EyePacket, 2> eyes{};
	};

	struct BuildResult
	{
		BuildStatus status = BuildStatus::Rejected;
		Rejection rejection = Rejection::None;
		std::uint32_t rejectedEye = 2;
		std::optional<StereoPacket> packet;
	};

	namespace Detail
	{
		[[nodiscard]] constexpr bool IsValidPair(const PairIdentity& a_pair) noexcept
		{
			return a_pair.token != 0 && a_pair.compositorCycle != 0 &&
			       a_pair.frame != 0 && a_pair.frame != std::numeric_limits<std::uint32_t>::max() &&
			       a_pair.thread != 0;
		}

		[[nodiscard]] constexpr bool MatchesPair(const PairIdentity& a_left, const PairIdentity& a_right) noexcept
		{
			return a_left.token == a_right.token && a_left.compositorCycle == a_right.compositorCycle &&
			       a_left.frame == a_right.frame && a_left.thread == a_right.thread;
		}

		[[nodiscard]] constexpr bool IsCurrentWorld(Candidate a_candidate) noexcept
		{
			return a_candidate == Candidate::CurrentNative || a_candidate == Candidate::CurrentReconstructed;
		}

		[[nodiscard]] constexpr bool MatchesView(const Scene::TextureView& a_left, const Scene::TextureView& a_right) noexcept
		{
			return a_left.resourceIdentity == a_right.resourceIdentity &&
			       a_left.deviceIdentity == a_right.deviceIdentity &&
			       a_left.extent.width == a_right.extent.width && a_left.extent.height == a_right.extent.height &&
			       a_left.sampleCount == a_right.sampleCount && a_left.arraySize == a_right.arraySize &&
			       a_left.arraySlice == a_right.arraySlice && a_left.mipLevel == a_right.mipLevel;
		}

		[[nodiscard]] constexpr bool Overlaps(const Scene::OutputViewport& a_left, const Scene::OutputViewport& a_right) noexcept
		{
			return a_left.x < a_right.x + a_right.width && a_right.x < a_left.x + a_left.width &&
			       a_left.y < a_right.y + a_right.height && a_right.y < a_left.y + a_left.height;
		}

		[[nodiscard]] constexpr bool Overlaps(const Scene::PixelRect& a_left, const Scene::PixelRect& a_right) noexcept
		{
			return static_cast<std::uint64_t>(a_left.x) < static_cast<std::uint64_t>(a_right.x) + a_right.width &&
			       static_cast<std::uint64_t>(a_right.x) < static_cast<std::uint64_t>(a_left.x) + a_left.width &&
			       static_cast<std::uint64_t>(a_left.y) < static_cast<std::uint64_t>(a_right.y) + a_right.height &&
			       static_cast<std::uint64_t>(a_right.y) < static_cast<std::uint64_t>(a_left.y) + a_left.height;
		}

		[[nodiscard]] inline BuildResult Reject(Rejection a_reason, std::uint32_t a_eye = 2) noexcept
		{
			return { BuildStatus::Rejected, a_reason, a_eye, std::nullopt };
		}
	}

	/// Builds both eyes or neither; inactive and empty callers perform no metadata validation or graphics work.
	[[nodiscard]] inline BuildResult BuildPair(const PairInput& a_input) noexcept
	{
		if (!a_input.isVR)
			return { BuildStatus::NotVR, Rejection::None, 2, std::nullopt };
		if (a_input.negotiatedCSXHostToken == 0 ||
			a_input.activeCSXHostToken != a_input.negotiatedCSXHostToken)
			return { BuildStatus::Inactive, Rejection::None, 2, std::nullopt };
		if (!a_input.hasWorldContent)
			return { BuildStatus::NoContent, Rejection::None, 2, std::nullopt };
		if (!Detail::IsValidPair(a_input.currentPair))
			return Detail::Reject(Rejection::InvalidPair);
		if (a_input.completedResourceGeneration == 0 || a_input.currentDeviceIdentity == 0)
			return Detail::Reject(Rejection::InvalidPublicationIdentity);

		StereoPacket packet{
			.hostToken = a_input.activeCSXHostToken,
			.pair = a_input.currentPair,
			.resourceGeneration = a_input.completedResourceGeneration,
			.deviceIdentity = a_input.currentDeviceIdentity,
		};
		for (std::uint32_t eye = 0; eye < a_input.eyes.size(); ++eye) {
			const auto& source = a_input.eyes[eye];
			if (!Detail::IsCurrentWorld(source.candidate))
				return Detail::Reject(Rejection::IneligibleCandidate, eye);
			if (!Detail::MatchesPair(source.scenePair, a_input.currentPair))
				return Detail::Reject(Rejection::ScenePairMismatch, eye);
			if (!Detail::MatchesPair(source.depthPair, a_input.currentPair))
				return Detail::Reject(Rejection::DepthPairMismatch, eye);
			if (!source.completedOpaqueDepth)
				return Detail::Reject(Rejection::DepthNotCompleted, eye);
			if (!source.fullEyeDepthCoverage)
				return Detail::Reject(Rejection::DepthNotFullEye, eye);
			if (!source.occlusionDepthRetention.retained)
				return Detail::Reject(Rejection::MissingDepthRetention, eye);
			if (!OpenVRSubmitLeasePolicy::CanPublish(source.submitLease,
					a_input.completedResourceGeneration, a_input.currentDeviceIdentity))
				return Detail::Reject(Rejection::StalePublication, eye);
			if (!Scene::IsValid(source.colorView))
				return Detail::Reject(Rejection::InvalidColorView, eye);
			if (!Scene::IsValid(source.depthView))
				return Detail::Reject(Rejection::InvalidDepthView, eye);
			if (source.colorView.deviceIdentity != a_input.currentDeviceIdentity ||
				source.depthView.deviceIdentity != a_input.currentDeviceIdentity)
				return Detail::Reject(Rejection::WrongDevice, eye);
			if (source.occlusionDepthRetention.resourceIdentity != source.depthView.resourceIdentity ||
				source.occlusionDepthRetention.deviceIdentity != a_input.currentDeviceIdentity ||
				source.occlusionDepthRetention.generation != a_input.completedResourceGeneration)
				return Detail::Reject(Rejection::DepthRetentionMismatch, eye);
			if (!VRSubmitColorContract::IsPresentationSupported(source.colorContract))
				return Detail::Reject(Rejection::InvalidColorContract, eye);
			if (!source.renderTargetCapabilityProven)
				return Detail::Reject(Rejection::TargetNotWritable, eye);
			const auto viewport = Scene::MakeOutputViewport(source.colorView.extent, source.colorBounds);
			if (!viewport)
				return Detail::Reject(Rejection::InvalidColorBounds, eye);
			const auto depth = Scene::MakeDepthView(source.depthView, source.depthRect);
			if (!depth)
				return Detail::Reject(Rejection::InvalidDepthRect, eye);
			if (!Scene::IsInvertible(source.trackingToColorClip) || !Scene::IsInvertible(source.trackingToDepthClip) ||
				!Scene::HasAxialDepthDirection(source.trackingToDepthViewMetres))
				return Detail::Reject(Rejection::InvalidCamera, eye);
			const auto encoding = Scene::MakeNativeForwardZ(source.nearPlane, source.farPlane, source.sourceUnitsToMetres);
			if (!encoding)
				return Detail::Reject(Rejection::InvalidDepthEncoding, eye);
			packet.eyes[eye] = {
				.candidate = source.candidate,
				.submitLease = source.submitLease,
				.colorView = source.colorView,
				.viewport = *viewport,
				.colorContract = source.colorContract,
				.depth = *depth,
				.occlusionDepthRetention = source.occlusionDepthRetention,
				.depthEncoding = *encoding,
				.trackingToColorClip = source.trackingToColorClip,
				.trackingToDepthClip = source.trackingToDepthClip,
				.trackingToDepthViewMetres = source.trackingToDepthViewMetres,
			};
		}

		const auto& left = packet.eyes[0];
		const auto& right = packet.eyes[1];
		if (left.candidate != right.candidate)
			return Detail::Reject(Rejection::CandidateMismatch);
		for (const auto& colorEye : packet.eyes) {
			for (const auto& depthEye : packet.eyes) {
				if (colorEye.colorView.resourceIdentity == depthEye.depth.texture.resourceIdentity)
					return Detail::Reject(Rejection::ColorDepthAlias);
			}
		}
		if (left.colorView.resourceIdentity == right.colorView.resourceIdentity) {
			if (!Detail::MatchesView(left.colorView, right.colorView) || left.colorContract != right.colorContract)
				return Detail::Reject(Rejection::SharedColorMismatch);
			if (Detail::Overlaps(left.viewport, right.viewport))
				return Detail::Reject(Rejection::OverlappingColorEyes);
		}
		if (left.depth.texture.resourceIdentity == right.depth.texture.resourceIdentity) {
			if (!Detail::MatchesView(left.depth.texture, right.depth.texture))
				return Detail::Reject(Rejection::SharedDepthMismatch);
			if (Detail::Overlaps(left.depth.activeRect, right.depth.activeRect))
				return Detail::Reject(Rejection::OverlappingDepthEyes);
		}
		return { BuildStatus::Ready, Rejection::None, 2, packet };
	}
}
