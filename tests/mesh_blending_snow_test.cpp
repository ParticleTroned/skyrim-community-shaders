#include "Features/MeshBlendingSnowPolicy.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "mesh_blending_snow_flags_under_test.h"

namespace
{
	using std::abs;
	using std::max;

	bool descriptorEnabled = false;
	unsigned fadeCalls = 0u;
	unsigned derivativeCalls = 0u;
	constexpr float depthFade = 0.4f;

	float smoothstep(float a_minimum, float a_maximum, float a_value)
	{
		const auto t = std::clamp((a_value - a_minimum) / (a_maximum - a_minimum), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float fwidth(float)
	{
		++derivativeCalls;
		return 0.1f;
	}

	namespace SharedData
	{
		struct
		{
			bool Enabled = false;
		} terrainBlendingSettings;
		struct
		{
			float ProjectedSnowEdgeWidth = 0.0f;
		} meshBlendingSettings;
	}

	namespace MeshBlending
	{
		bool ShouldApply() { return descriptorEnabled; }
		float ComputeFade(int, float, unsigned)
		{
			++fadeCalls;
			return depthFade;
		}
	}

#include "mesh_blending_snow_coverage_under_test.h"

	struct ShaderOutput
	{
		struct
		{
			float w = 0.0f;
		} Diffuse;
	};
	constexpr int screenUV = 0;
	constexpr unsigned eyeIndex = 0u;
	struct
	{
		struct
		{
			float z = 0.0f;
		} Position;
	} input;
	enum class Family
	{
		None,
		Snow,
		Nif
	};
	struct FinalAlphaCase
	{
		float (*finalize)(float, float, float);
		bool usesTerrain;
		Family family;
	};

#include "mesh_blending_final_alpha_under_test.h"

	void CheckEligibility()
	{
		using namespace EngineFlags;
		using namespace CSX::MeshBlendingSnowPolicy;
		constexpr auto required = RequiredFlags<ShaderFlag>();
		constexpr auto snow = ToMask(ShaderFlag::kSnow);
		const auto classify = [](std::uint64_t a_flags, MaterialFeature a_feature = MaterialFeature::kMultiIndexTriShapeSnow) {
			return Classify<ShaderFlag>(a_flags, a_feature, kExcludedShaderFlags);
		};
		assert(classify(required) == Eligibility::kAccepted);
		assert(classify(required | snow) == Eligibility::kAccepted);
		assert(classify(required | snow | ToMask(ShaderFlag::kZBufferTest) | ToMask(ShaderFlag::kVertexAlpha)) == Eligibility::kAccepted);
		assert(classify(ToMask(ShaderFlag::kProjectedUV) | snow) == Eligibility::kMissingFlags);
		assert(classify(ToMask(ShaderFlag::kMultiIndexSnow) | snow) == Eligibility::kMissingFlags);
		assert(classify(required | snow, MaterialFeature::kDefault) == Eligibility::kMaterialFeature);
		assert(classify(required | snow, MaterialFeature::kMultilayerParallax) == Eligibility::kMaterialFeature);
		for (unsigned bit = 0u; bit < 64u; ++bit) {
			const auto flag = std::uint64_t{ 1u } << bit;
			if ((flag & kExcludedShaderFlags) != 0u && (flag & (required | snow)) == 0u)
				assert(classify(required | snow | flag) == Eligibility::kExcludedFlags);
		}
	}

	void CheckProjectionControls()
	{
		for (const bool enabled : { false, true }) {
			for (const float width : { 0.0f, 2.0f }) {
				descriptorEnabled = enabled;
				SharedData::meshBlendingSettings.ProjectedSnowEdgeWidth = width;
				derivativeCalls = 0u;
				const auto negative = ProjectedCoverage(-0.05f);
				const auto zero = ProjectedCoverage(0.0f);
				const auto positive = ProjectedCoverage(0.05f);
				if (enabled && width > 0.0f) {
					assert(negative && std::abs(*negative - 0.15625f) < 1.0e-6f);
					assert(zero && *zero == 0.5f);
					assert(positive && std::abs(*positive - 0.84375f) < 1.0e-6f);
					assert(derivativeCalls == 3u);
					assert(!ProjectedCoverage(-0.2f));
					assert(ProjectedCoverage(0.2f) == 1.0f);
				} else {
					assert(!negative && zero == 1.0f && positive == 1.0f);
					assert(derivativeCalls == 0u);
				}
			}
		}
	}

	void CheckFinalAlpha()
	{
		for (const auto& test : FinalAlphaCases) {
			for (const bool terrain : { false, true }) {
				for (const bool enabled : { false, true }) {
					for (const float coverage : { 0.0f, 0.25f, 1.0f }) {
						SharedData::terrainBlendingSettings.Enabled = terrain;
						descriptorEnabled = enabled;
						fadeCalls = 0u;
						float expected = test.usesTerrain && terrain ? 0.8f : 0.7f;
						if (test.family == Family::Snow)
							expected *= coverage;
						if (test.family == Family::Nif && enabled)
							expected *= depthFade;
						assert(std::abs(test.finalize(0.7f, 0.8f, coverage) - expected) < 1.0e-6f);
						assert(fadeCalls == (test.family == Family::Nif && enabled ? 1u : 0u));
					}
				}
			}
		}
	}
}

int main()
{
	CheckEligibility();
	CheckProjectionControls();
	CheckFinalAlpha();
}
