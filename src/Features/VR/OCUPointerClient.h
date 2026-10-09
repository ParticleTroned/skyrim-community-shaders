#pragma once

#include "OCUPointerAPI.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <windows.h>

namespace OCUPointer
{
	/** Serialized VR-input client; optional exports never load another runtime. */
	class Client
	{
	public:
		enum class Status
		{
			Disabled,
			ProviderUnavailable,
			QueryFailed,
			InvalidOrStale,
			OwnershipDenied,
			Active
		};
		struct Frame
		{
			bool providerAvailable = false;
			bool valid = false;
			ocu_pointer::Snapshot snapshot{};
		};

		Client() noexcept : token(reinterpret_cast<std::uintptr_t>(this)) {}
		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;
		~Client() = default;

		/** Read one shared ray publication per input frame, registering menu visibility as needed. */
		const Frame& ReadForFrame(std::uint64_t a_frame, std::uint32_t a_trackingOrigin,
			float a_pitchDegrees, bool a_enabled) noexcept
		{
			if (!a_enabled) {
				Release();
				frame = {};
				status = Status::Disabled;
				return frame;
			}
			SelectProvider();
			if (trackingOrigin != a_trackingOrigin || pitchDegrees != a_pitchDegrees) {
				Release();
				trackingOrigin = a_trackingOrigin;
				pitchDegrees = a_pitchDegrees;
				++generation;
			}
			if (hasFrame && rendererFrame == a_frame) {
				if (frame.valid && !ValidSnapshot(frame.snapshot)) {
					status = Status::InvalidOrStale;
					Invalidate();
				}
				return frame;
			}
			hasFrame = true;
			rendererFrame = a_frame;
			frame = {};
			frame.providerAvailable = query && updateLease;
			status = Status::ProviderUnavailable;
			if (!frame.providerAvailable)
				return frame;
			status = Status::InvalidOrStale;
			if (trackingOrigin > 2 || !std::isfinite(pitchDegrees) || pitchDegrees < -90.0f || pitchDegrees > 90.0f) {
				Invalidate();
				return frame;
			}
			if (!leaseActive && !BeginObservation())
				return frame;

			ocu_pointer::Snapshot snapshot{};
			try {
				if (query(ocu_pointer::Version, sizeof(snapshot), &snapshot) !=
					static_cast<std::uint32_t>(ocu_pointer::Result::Success)) {
					status = Status::QueryFailed;
					Invalidate();
					return frame;
				}
			} catch (...) {
				status = Status::QueryFailed;
				Invalidate();
				return frame;
			}
			if (!ValidSnapshot(snapshot)) {
				Invalidate();
				return frame;
			}
			if (lastSession != 0 && lastSession != snapshot.sessionId) {
				ownedHands = 0;
				triggerArmed = {};
				++generation;
			}
			lastSession = snapshot.sessionId;
			lastProviderFrame = snapshot.frameId;
			lastPublicationQpc = snapshot.publicationQpc;
			ApplyOwnership(ownedHands & snapshot.ownedHands, snapshot);
			frame.snapshot = snapshot;
			frame.valid = true;
			status = Status::Active;
			return frame;
		}

		/** Renew focus and claim physical hands; hitMask selects the visible laser endpoints. */
		bool UpdateOwnership(std::uint32_t a_handMask, std::uint32_t a_hitMask,
			const float a_hitDistances[2]) noexcept
		{
			if (!frame.valid || !a_hitDistances || (a_handMask & ~3u) != 0 ||
				(a_hitMask & ~a_handMask) != 0)
				return DenyOwnership();
			for (std::uint32_t hand = 0; hand < 2; ++hand) {
				if ((a_handMask & (1u << hand)) != 0 &&
					(frame.snapshot.hands[hand].flags & ocu_pointer::RayValid) == 0)
					return DenyOwnership();
				if ((a_hitMask & (1u << hand)) != 0 &&
					(!std::isfinite(a_hitDistances[hand]) || a_hitDistances[hand] <= 0.0f || a_hitDistances[hand] > ocu_pointer::MaximumHitDistanceMeters))
					return DenyOwnership();
			}
			auto request = MakeRequest();
			request.sessionId = frame.snapshot.sessionId;
			request.frameId = frame.snapshot.frameId;
			request.handMask = a_handMask;
			request.flags |= ((a_hitMask & 1u) ? ocu_pointer::HasLeftHit : 0u) |
			                 ((a_hitMask & 2u) ? ocu_pointer::HasRightHit : 0u);
			for (std::uint32_t hand = 0; hand < 2; ++hand)
				request.hitDistance[hand] = (a_hitMask & (1u << hand)) ? a_hitDistances[hand] : 0.0f;
			ocu_pointer::LeaseResponse response{};
			if (!CallLease(request, response) || response.sessionId != request.sessionId ||
				response.frameId != request.frameId || (response.ownedHands & ~a_handMask) != 0)
				return DenyOwnership();
			leaseActive = true;
			ApplyOwnership(response.ownedHands, frame.snapshot);
			frame.snapshot.ownedHands = ownedHands;
			status = ownedHands == a_handMask ? Status::Active : Status::OwnershipDenied;
			return ownedHands == a_handMask;
		}

		/** Release even a stale lease, clear held input and invalidate the current frame copy. */
		void Release() noexcept
		{
			if (leaseActive || ownedHands != 0 || frame.valid)
				++generation;
			ReleaseProviderLease();
			ownedHands = 0;
			triggerArmed = {};
			frame.valid = false;
			frame.snapshot = {};
			hasFrame = false;
		}

		/** Require current ownership and an observed physical release before generating edges. */
		bool TriggerEligible(std::uint32_t a_physicalHand) const noexcept
		{
			if (!frame.valid || a_physicalHand >= 2 || (ownedHands & (1u << a_physicalHand)) == 0 || !triggerArmed[a_physicalHand])
				return false;
			const auto flags = frame.snapshot.hands[a_physicalHand].flags;
			return (flags & (ocu_pointer::InputAvailable | ocu_pointer::SuppressedUntilRelease)) == ocu_pointer::InputAvailable;
		}
		/** Only an owned hand observed released since acquisition can generate a press. */
		bool TriggerDown(std::uint32_t a_physicalHand) const noexcept
		{
			return TriggerEligible(a_physicalHand) && (frame.snapshot.hands[a_physicalHand].flags & ocu_pointer::TriggerDown) != 0;
		}
		/** Raw edge identity supports bounded short-tap recovery within one owned session. */
		std::uint64_t TriggerSequence(std::uint32_t a_physicalHand) const noexcept
		{
			return frame.valid && a_physicalHand < 2 ? frame.snapshot.hands[a_physicalHand].triggerSequence : 0;
		}
		/** Reflect lease updates and invalidation without querying the provider again. */
		const Frame& GetFrame() const noexcept { return frame; }
		std::uint32_t GetOwnedHands() const noexcept { return ownedHands; }
		std::uint64_t GetGeneration() const noexcept { return generation; }
		/** Changes when this hand loses ownership, including loss and reacquisition within one frame. */
		std::uint64_t GetHandGeneration(std::uint32_t a_hand) const noexcept { return a_hand < 2 ? handGenerations[a_hand] : 0; }
		Status GetStatus() const noexcept { return status; }

	private:
		struct Exports
		{
			HMODULE module = nullptr;
			ocu_pointer::QueryFn query = nullptr;
			ocu_pointer::UpdateLeaseFn update = nullptr;
		};
		static Exports Resolve(HMODULE a_module) noexcept
		{
			if (!a_module)
				return {};
			const auto get = reinterpret_cast<ocu_pointer::QueryFn>(GetProcAddress(a_module, "OCU_GetPointerStateV1"));
			const auto update = reinterpret_cast<ocu_pointer::UpdateLeaseFn>(GetProcAddress(a_module, "OCU_UpdatePointerLeaseV1"));
			return get && update ? Exports{ a_module, get, update } : Exports{};
		}
		void SelectProvider() noexcept
		{
			auto selected = Resolve(GetModuleHandleW(L"openvr_api.dll"));
			if (!selected.module)
				selected = Resolve(GetModuleHandleW(L"vrclient_x64.dll"));
			if (selected.module == module && selected.query == query && selected.update == updateLease)
				return;
			Release();
			module = selected.module;
			query = selected.query;
			updateLease = selected.update;
			lastSession = lastProviderFrame = 0;
			lastPublicationQpc = 0;
			++generation;
		}
		ocu_pointer::LeaseRequest MakeRequest() const noexcept
		{
			ocu_pointer::LeaseRequest request{};
			request.structSize = sizeof(request);
			request.version = ocu_pointer::Version;
			request.clientToken = token;
			request.flags = ocu_pointer::MenuVisible;
			request.leaseMilliseconds = ocu_pointer::MaximumLeaseMilliseconds;
			request.trackingOrigin = trackingOrigin;
			request.pitchTrimDegrees = pitchDegrees;
			return request;
		}
		bool CallLease(const ocu_pointer::LeaseRequest& a_request, ocu_pointer::LeaseResponse& a_response) noexcept
		{
			try {
				return updateLease && updateLease(ocu_pointer::Version, sizeof(a_request), &a_request, sizeof(a_response), &a_response) == static_cast<std::uint32_t>(ocu_pointer::Result::Success) &&
				       a_response.structSize == sizeof(a_response) && a_response.version == ocu_pointer::Version &&
				       a_response.reserved == 0;
			} catch (...) {
				status = Status::QueryFailed;
				return false;
			}
		}
		bool BeginObservation() noexcept
		{
			ocu_pointer::LeaseResponse response{};
			if (!CallLease(MakeRequest(), response) || response.ownedHands != 0) {
				status = Status::OwnershipDenied;
				Invalidate();
				return false;
			}
			leaseActive = true;
			return true;
		}
		void ReleaseProviderLease() noexcept
		{
			if (!leaseActive)
				return;
			// A disappearing DLL cannot safely receive a release; its lease expires.
			const auto app = Resolve(GetModuleHandleW(L"openvr_api.dll"));
			const auto system = Resolve(GetModuleHandleW(L"vrclient_x64.dll"));
			if ((app.module == module && app.update == updateLease) ||
				(system.module == module && system.update == updateLease)) {
				auto request = MakeRequest();
				request.flags = 0;
				request.leaseMilliseconds = 0;
				ocu_pointer::LeaseResponse ignored{};
				CallLease(request, ignored);
			}
			leaseActive = false;
		}
		void Invalidate() noexcept
		{
			if (ownedHands != 0 || frame.valid)
				++generation;
			if (leaseActive) {
				// Keep the visible observer alive so the next runtime frame can publish.
				ocu_pointer::LeaseResponse response{};
				leaseActive = CallLease(MakeRequest(), response) && response.ownedHands == 0;
			}
			ownedHands = 0;
			triggerArmed = {};
			frame.valid = false;
			frame.snapshot = {};
		}
		bool DenyOwnership() noexcept
		{
			status = Status::OwnershipDenied;
			Invalidate();
			return false;
		}
		void ApplyOwnership(std::uint32_t a_ownedHands, const ocu_pointer::Snapshot& a_snapshot) noexcept
		{
			for (std::uint32_t hand = 0; hand < 2; ++hand) {
				const auto flags = a_snapshot.hands[hand].flags;
				if ((a_ownedHands & (1u << hand)) == 0 ||
					(flags & (ocu_pointer::RayValid | ocu_pointer::InputAvailable)) !=
						(ocu_pointer::RayValid | ocu_pointer::InputAvailable)) {
					a_ownedHands &= ~(1u << hand);
					triggerArmed[hand] = false;
				} else if ((flags & ocu_pointer::SuppressedUntilRelease) != 0) {
					triggerArmed[hand] = false;
				} else if ((ownedHands & (1u << hand)) == 0) {
					triggerArmed[hand] = (flags & (ocu_pointer::TriggerDown | ocu_pointer::SuppressedUntilRelease)) == 0;
				} else if ((flags & ocu_pointer::TriggerDown) == 0) {
					triggerArmed[hand] = true;
				}
				if ((ownedHands & ~a_ownedHands & (1u << hand)) != 0)
					++handGenerations[hand];
			}
			ownedHands = a_ownedHands;
		}
		bool ValidSnapshot(const ocu_pointer::Snapshot& a_snapshot) const noexcept
		{
			using namespace ocu_pointer;
			LARGE_INTEGER now{}, frequency{};
			if (a_snapshot.structSize != sizeof(Snapshot) || a_snapshot.version != Version ||
				a_snapshot.sessionId == 0 || a_snapshot.frameId == 0 || a_snapshot.clientToken != token ||
				a_snapshot.trackingOrigin != trackingOrigin || (a_snapshot.ownedHands & ~3u) != 0 ||
				a_snapshot.appliedPitchTrimDegrees != pitchDegrees || a_snapshot.reserved != 0 ||
				!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
				a_snapshot.qpcFrequency != frequency.QuadPart || a_snapshot.publicationQpc <= 0 ||
				now.QuadPart < a_snapshot.publicationQpc ||
				static_cast<double>(now.QuadPart - a_snapshot.publicationQpc) / static_cast<double>(frequency.QuadPart) > 0.100)
				return false;
			if (a_snapshot.sessionId == lastSession &&
				(a_snapshot.frameId < lastProviderFrame || a_snapshot.publicationQpc < lastPublicationQpc))
				return false;
			for (const auto* colors : { &a_snapshot.style.beamColor, &a_snapshot.style.dotColor })
				for (const auto& color : *colors)
					for (const float channel : color)
						if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
							return false;
			for (const auto& hand : a_snapshot.hands) {
				if ((hand.flags & ~15u) != 0 || hand.reserved != 0)
					return false;
				if ((hand.flags & RayValid) == 0)
					continue;
				float lengthSquared = 0.0f;
				for (std::uint32_t axis = 0; axis < 3; ++axis) {
					if (!std::isfinite(hand.origin[axis]) || !std::isfinite(hand.direction[axis]))
						return false;
					lengthSquared += hand.direction[axis] * hand.direction[axis];
				}
				if (!std::isfinite(lengthSquared) || std::abs(lengthSquared - 1.0f) > 0.01f)
					return false;
			}
			return true;
		}

		HMODULE module = nullptr;
		ocu_pointer::QueryFn query = nullptr;
		ocu_pointer::UpdateLeaseFn updateLease = nullptr;
		const std::uint64_t token;
		Frame frame;
		std::array<bool, 2> triggerArmed{};
		std::array<std::uint64_t, 2> handGenerations{};
		std::uint64_t rendererFrame = 0;
		std::uint64_t generation = 0;
		std::uint64_t lastSession = 0;
		std::uint64_t lastProviderFrame = 0;
		std::int64_t lastPublicationQpc = 0;
		std::uint32_t trackingOrigin = 1;
		std::uint32_t ownedHands = 0;
		float pitchDegrees = 0.0f;
		bool hasFrame = false;
		bool leaseActive = false;
		Status status = Status::Disabled;
	};
}  // namespace OCUPointer
