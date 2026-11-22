// Included inside the colour-test namespace after its shared D3D fixture.
unsigned VerifyZeroAttenuation(ID3D11Device* device, ID3D11DeviceContext* context)
{
	unsigned checked = 0, exceptionalDifferences = 0;
	for (bool vr : { false, true }) {
		for (const char* consumer : { "TEST_EFFECT", "TEST_PARTICLE", "TEST_WATER" }) {
			for (bool isl : { false, true }) {
				std::vector<const char*> defines{ consumer };
				if (isl)
					defines.push_back("ISL");
				Fixture candidate(device, vr, ShaderMode::ZeroAttenuation, defines);
				defines.push_back("ZERO_ATTENUATION_REFERENCE");
				Fixture reference(device, vr, ShaderMode::ZeroAttenuation, defines);
				auto compare = [&](float distance, unsigned flags, bool ll, bool grading, unsigned eye, bool exceptional) {
					Pixels results[2];
					unsigned result = 0;
					for (auto* fixture : { &candidate, &reference }) {
						auto colors = pointLightColors;
						colors[5] = { 1e8f, 5e7f, 2e7f, 1 };
						if (exceptional) {
							colors[6][0] = std::numeric_limits<float>::infinity();
							colors[7][0] = std::numeric_limits<float>::quiet_NaN();
						}
						fixture->inputs->SetVariable("colors", colors);
						fixture->inputs->SetVariable("lightParameters", std::array<unsigned, 4>{ eye, flags, vr ? eye : 0, 3 });
						fixture->inputs->SetVariable("geometryParameters", Pixel{ 1.0f, distance, grading ? 0.7f : 1.0f, exceptional ? 1.0f : -1.0f });
						fixture->permutation->SetVariable("Permutation::ExtraShaderDescriptor", 1u);
						fixture->feature->SetMember("SharedData::linearLightingSettings", "enableLinearLighting", unsigned(ll));
						fixture->feature->SetMember("SharedData::linearLightingSettings", "effectLightingMult", 1.2f);
						fixture->feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightCurve", grading ? 1.2f : 1.0f);
						fixture->feature->SetMember("SharedData::adaptiveBalanceSettings", "pointLightSaturation", grading ? 0.7f : 1.0f);
						auto appearance = AdaptiveBalanceTest::NeutralAppearance();
						appearance[40] = grading ? 1.7f : 1.0f;
						if (grading) {
							appearance[86] = std::bit_cast<float>(1u);
							appearance[57] = 0.15f;
							appearance[59] = 0.2f;
						}
						fixture->feature->SetMember("SharedData::adaptiveBalanceSettings", "appearance", appearance);
						results[result++] = fixture->Dispatch(context);
					}
					for (unsigned sample = 0; sample < 8; ++sample) {
						for (unsigned channel = 0; channel < 3; ++channel) {
							const float actual = results[0][sample][channel], baseline = results[1][sample][channel];
							if (exceptional || (distance == 0.0f && std::string_view(consumer) == "TEST_WATER")) {
								exceptionalDifferences += std::bit_cast<unsigned>(actual) != std::bit_cast<unsigned>(baseline);
							} else {
								Require(std::isfinite(actual) && std::isfinite(baseline) && Close(actual, baseline),
									"Exact-zero exit changed a finite light contribution");
								++checked;
							}
						}
					}
				};
				for (float distance : { 0.0f, 0.5f, 0.99999994f, 1.0f, 1.00000012f, 2.0f }) {
					for (unsigned flags : { 0u, 1u << 10, 1u << 9 }) {
						for (bool ll : { false, true })
							for (bool grading : { false, true })
								for (unsigned eye : { 0u, 1u })
									compare(distance, flags, ll, grading, eye, false);
					}
				}
				for (float distance : { 0.0f, 1.0f, 2.0f })
					compare(distance, 0, false, false, 0, true);
			}
		}
	}
	std::cout << checked << " finite attenuation channels passed; " << exceptionalDifferences
			  << " exceptional-value bit differences (not an equivalence claim).\n";
	return checked;
}
