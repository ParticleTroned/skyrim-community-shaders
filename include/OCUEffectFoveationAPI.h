#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Read-only, optional renderer integration. Discover OCU_GetEffectFoveationV1
// from the already loaded openvr_api.dll; do not load a second runtime.
// No OpenXR/OpenVR headers, GPU resources, callbacks, or cross-module ownership.
namespace ocu_effect_foveation {

constexpr std::uint32_t Version = 1;
enum class Mode : std::uint32_t { Disabled = 0, Fixed = 1, EyeTracked = 2 };
enum class Shape : std::uint32_t {
    // eyeUV has (0,0) at this eye's top-left, (1,1) at its bottom-right.
    // Ring distance = 2 * length(eyeUV - centerUV[eye]). Thus radius 1
    // reaches a centered eye's side. This is NOT an angle or pixel circle;
    // it is the current OCU UV-radial profile, not CSX's superellipse.
    UVRadialHalfExtent = 1
};
enum class Result : std::uint32_t {
    Success = 0, UnsupportedVersion = 1, InvalidOutput = 2
};
enum Flags : std::uint32_t { SourceSampleTimeAvailable = 1u << 0 };

struct Snapshot {
    std::uint32_t structSize;
    std::uint32_t version;
    Mode mode;
    Shape shape;
    std::uint64_t frameId;
    std::int64_t publicationQpc;
    std::int64_t qpcFrequency;
    // Opaque OpenXR timeline values, not QPC. Zero source sample time is
    // allowed for a valid pose; consumers must never use it as a validity gate.
    std::int64_t predictedDisplayTime;
    std::int64_t gazeSampleTime;
    float centerUV[2][2];
    float innerRadius;
    float midRadius;
    // Diagnostic projection used for the UV centers: left/right/up/down.
    // These can be cached projection geometry, not same-frame eye poses.
    // Do not reinterpret the UV radii as angular cones using these values.
    float fovTangents[2][4];
    std::uint32_t flags;
    std::uint32_t reserved[3];
};

// Caller supplies writable outputBytes >= sizeof(Snapshot). On success the
// provider copies exactly sizeof(Snapshot), including a disabled snapshot.
// Unsupported versions/short/null outputs are untouched. Query does not sample
// gaze, wait for rendering, create resources, or enable a renderer feature.
// Consumers read once per game frame before their effect dispatch and retain
// that copy for both eyes. Disabled/unsupported snapshots mean native quality.
// Use frameId/publicationQpc for publication freshness, never gazeSampleTime.
// Snapshot is cleared at the WaitGetPoses pre-render boundary, first Submit and
// runtime shutdown.
// Only active game-frame publications may drive a peripheral quality policy.
// The export is optional; absence is normal on SteamVR/older OCU. No hot unload.
#ifdef _WIN32
using QueryFn = std::uint32_t(__cdecl*)(std::uint32_t requestedVersion,
    std::uint32_t outputBytes, Snapshot* output);
#else
using QueryFn = std::uint32_t(*)(std::uint32_t requestedVersion,
    std::uint32_t outputBytes, Snapshot* output);
#endif

static_assert(std::is_standard_layout_v<Snapshot> && std::is_trivially_copyable_v<Snapshot>);
static_assert(sizeof(Snapshot) == 128);
static_assert(alignof(Snapshot) == 8);
static_assert(offsetof(Snapshot, frameId) == 16);
static_assert(offsetof(Snapshot, centerUV) == 56);
static_assert(offsetof(Snapshot, fovTangents) == 80);
static_assert(offsetof(Snapshot, flags) == 112);

} // namespace ocu_effect_foveation
