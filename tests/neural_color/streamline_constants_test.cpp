#include "Features/Upscaling/StreamlineConstants.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using UpscalingDLSS::SameStreamlineConstants;
static void Require(bool value)
{
	if (!value)
		std::abort();
}

int main()
{
	const sl::Constants original{};
	Require(SameStreamlineConstants(original, original));
	for (auto member : { &sl::Constants::cameraViewToClip, &sl::Constants::clipToCameraView,
			 &sl::Constants::clipToLensClip, &sl::Constants::clipToPrevClip, &sl::Constants::prevClipToClip }) {
		for (unsigned row = 0; row < 4; ++row) {
			for (auto component : { &sl::float4::x, &sl::float4::y, &sl::float4::z, &sl::float4::w }) {
				auto changed = original;
				(changed.*member)[row].*component = 0;
				Require(!SameStreamlineConstants(original, changed));
			}
		}
	}
	for (auto member : { &sl::Constants::jitterOffset, &sl::Constants::mvecScale, &sl::Constants::cameraPinholeOffset }) {
		for (auto component : { &sl::float2::x, &sl::float2::y }) {
			auto changed = original;
			(changed.*member).*component = 0;
			Require(!SameStreamlineConstants(original, changed));
		}
	}
	for (auto member : { &sl::Constants::cameraPos, &sl::Constants::cameraUp, &sl::Constants::cameraRight, &sl::Constants::cameraFwd }) {
		for (auto component : { &sl::float3::x, &sl::float3::y, &sl::float3::z }) {
			auto changed = original;
			(changed.*member).*component = 0;
			Require(!SameStreamlineConstants(original, changed));
		}
	}
	for (auto member : { &sl::Constants::cameraNear, &sl::Constants::cameraFar, &sl::Constants::cameraFOV,
			 &sl::Constants::cameraAspectRatio, &sl::Constants::motionVectorsInvalidValue, &sl::Constants::minRelativeLinearDepthObjectSeparation }) {
		auto changed = original;
		changed.*member = 0;
		Require(!SameStreamlineConstants(original, changed));
	}
	for (auto member : { &sl::Constants::depthInverted, &sl::Constants::cameraMotionIncluded, &sl::Constants::motionVectors3D,
			 &sl::Constants::reset, &sl::Constants::orthographicProjection, &sl::Constants::motionVectorsDilated, &sl::Constants::motionVectorsJittered }) {
		auto changed = original;
		changed.*member = sl::eTrue;
		Require(!SameStreamlineConstants(original, changed));
	}
	auto changed = original;
	changed.next = &changed;
	Require(!SameStreamlineConstants(changed, changed));
	changed = original;
	changed.structVersion = sl::kStructVersion3;
	Require(!SameStreamlineConstants(changed, changed));
	changed = original;
	auto tinyJitter = original;
	changed.jitterOffset.x = 0.125f;
	tinyJitter.jitterOffset.x = std::nextafter(0.125f, 1.0f);
	Require(!SameStreamlineConstants(changed, tinyJitter));
	std::puts("All Streamline constant components reject mismatched reuse");
}
