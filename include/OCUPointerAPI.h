#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

/** Optional in-process pointer integration with the already loaded OCU runtime. */
namespace ocu_pointer
{
	constexpr std::uint32_t Version = 1;
	constexpr std::uint32_t MaximumLeaseMilliseconds = 100;
	constexpr float MaximumHitDistanceMeters = 100.0f;
	enum class Result : std::uint32_t
	{
		Success = 0,
		UnsupportedVersion = 1,
		InvalidArgument = 2,
		Busy = 3,
		Stale = 4
	};
	enum HandFlags : std::uint32_t
	{
		RayValid = 1u << 0,
		TriggerDown = 1u << 1,
		InputAvailable = 1u << 2,
		SuppressedUntilRelease = 1u << 3
	};
	enum LeaseFlags : std::uint32_t
	{
		MenuVisible = 1u << 0,
		HasLeftHit = 1u << 1,
		HasRightHit = 1u << 2
	};

	/** Physical left/right hands, independent of the game's handedness setting. */
	struct Hand
	{
		// Metres in trackingOrigin, right-handed OpenVR axes: +Y up, -Z forward.
		// Direction is normalized, calibrated and smoothed; consumers add no trim.
		float origin[3];
		float direction[3];
		std::uint64_t triggerSequence;
		std::uint32_t flags;
		std::uint32_t reserved;
	};

	/** Authoritative nominal colors: state 0 idle, state 1 pressed; straight-alpha sRGB RGBA. */
	struct Style
	{
		float beamColor[2][4];
		float dotColor[2][4];
	};

	/** One coherent runtime-frame publication, also used to draw the OCU laser. */
	struct Snapshot
	{
		std::uint32_t structSize;
		std::uint32_t version;
		std::uint64_t sessionId;
		std::uint64_t frameId;
		std::uint64_t clientToken;
		std::int64_t publicationQpc;
		std::int64_t qpcFrequency;
		std::int64_t predictedDisplayTime;
		// OpenVR ETrackingUniverseOrigin numeric value. Never mix tracking spaces.
		std::uint32_t trackingOrigin;
		std::uint32_t ownedHands;
		float appliedPitchTrimDegrees;
		std::uint32_t reserved;
		Hand hands[2];
		Style style;
	};

	/** Renewable exclusive menu focus, with independently owned physical hands. */
	struct LeaseRequest
	{
		std::uint32_t structSize;
		std::uint32_t version;
		std::uint64_t clientToken;
		std::uint64_t sessionId;
		std::uint64_t frameId;
		std::uint32_t handMask;
		std::uint32_t flags;
		std::uint32_t leaseMilliseconds;
		std::uint32_t trackingOrigin;
		float pitchTrimDegrees;
		float hitDistance[2];
		std::uint32_t reserved;
	};

	/** Effective ownership after keyboard/menu arbitration; denied hands stay released. */
	struct LeaseResponse
	{
		std::uint32_t structSize;
		std::uint32_t version;
		std::uint64_t sessionId;
		std::uint64_t frameId;
		std::uint32_t ownedHands;
		std::uint32_t reserved;
	};

	// Query copies the latest publication without waiting, sampling poses or calling
	// OpenXR. Unavailable hands have RayValid/InputAvailable clear. A session epoch
	// changes on restart/recenter/space change; frameId and triggerSequence are
	// monotonic within that epoch. Sequence counts every physical trigger edge.
	// Validate QPC freshness (at most 100ms); repeated queries do not refresh it.
	// Consumers read once per frame and use one ray for hit testing and display.
	// Unsupported versions and invalid output buffers are untouched.
	//
	// A visible zero-hand lease with zero session/frame bootstraps publication even
	// with OCU's own menu closed. Later claims cite a fresh snapshot. The requested
	// pitch is applied exactly once before OCU smoothing and shared beam rendering.
	// HasLeftHit/HasRightHit select visible beams ending at those metre distances;
	// ownership may retain a dragging hand after it leaves the surface. OCU omits
	// its cursor dot on externally owned hands; the consumer draws the hit cursor.
	//
	// Lease updates are bounded CPU-only calls, serialized by the consumer. Use a
	// nonzero process-unique token, renew within MaximumLeaseMilliseconds, and send
	// flags=0 to release. A matching token can release despite stale session/frame.
	// Expiry, menu close, tracking loss and keyboard preemption release ownership.
	// A trigger held across acquisition/loss stays suppressed until physical release.
	// Consumers must also cancel queued/held input on invalid samples or lost leases.
	// Export absence is normal on SteamVR/older OCU. Discover both exports from one
	// already loaded openvr_api.dll or vrclient_x64.dll; never load another runtime.
#ifdef _WIN32
	using QueryFn = std::uint32_t(__cdecl*)(std::uint32_t requestedVersion,
		std::uint32_t outputBytes, Snapshot* output);
	using UpdateLeaseFn = std::uint32_t(__cdecl*)(std::uint32_t requestedVersion,
		std::uint32_t requestBytes, const LeaseRequest* request,
		std::uint32_t outputBytes, LeaseResponse* output);
#else
	using QueryFn = std::uint32_t (*)(std::uint32_t requestedVersion,
		std::uint32_t outputBytes, Snapshot* output);
	using UpdateLeaseFn = std::uint32_t (*)(std::uint32_t requestedVersion,
		std::uint32_t requestBytes, const LeaseRequest* request,
		std::uint32_t outputBytes, LeaseResponse* output);
#endif

	static_assert(std::is_standard_layout_v<Hand> && std::is_trivially_copyable_v<Hand>);
	static_assert(std::is_standard_layout_v<Snapshot> && std::is_trivially_copyable_v<Snapshot>);
	static_assert(std::is_standard_layout_v<LeaseRequest> && std::is_trivially_copyable_v<LeaseRequest>);
	static_assert(std::is_standard_layout_v<LeaseResponse> && std::is_trivially_copyable_v<LeaseResponse>);
	static_assert(sizeof(Hand) == 40 && alignof(Hand) == 8);
	static_assert(sizeof(Snapshot) == 216 && alignof(Snapshot) == 8);
	static_assert(sizeof(LeaseRequest) == 64 && alignof(LeaseRequest) == 8);
	static_assert(sizeof(LeaseResponse) == 32 && alignof(LeaseResponse) == 8);
	static_assert(offsetof(Hand, triggerSequence) == 24);
	static_assert(offsetof(Snapshot, hands) == 72);
	static_assert(offsetof(Snapshot, style) == 152 && sizeof(Style) == 64);
	static_assert(offsetof(LeaseRequest, hitDistance) == 52);
}  // namespace ocu_pointer
