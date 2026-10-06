#pragma once

static void CheckExposureRange(ID3D11Device* device, ID3D11DeviceContext* context,
	ID3D11ComputeShader* prepare, ID3D11ComputeShader* reconstruct, ID3D11Buffer* cb)
{
	std::vector<Pixel> ramp(4096);
	for (unsigned i = 0; i < ramp.size(); ++i) {
		const float value = static_cast<float>(i % 256) / 255.0f;
		ramp[i] = { value, value * 0.5f, value * 0.25f, 1.0f };
	}
	for (const auto format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R11G11B10_FLOAT }) {
		auto baseline = StorePixels(device, context, prepare, cb, ramp, format);
		const auto originals = ReadStored(device, context, prepare, cb, baseline);
		for (const auto transform : { Transform::LinearToSRGB, Transform::ReversibleProxy }) {
			for (const float exposure : { 1.0f / 256.0f, 1.0f, 256.0f }) {
				auto constants = FullTextureConstants();
				constants.mode = 1;
				constants.domain = 1;
				constants.transform = static_cast<unsigned>(transform);
				constants.exposure = exposure;
				constants.bypass = static_cast<unsigned>(format == DXGI_FORMAT_R8G8B8A8_UNORM ? Storage::UNorm : Storage::R11G11B10);
				const Profile profile{ Domain::Linear, transform, exposure };
				auto prepared = MakeTexture(device, {}, DXGI_FORMAT_R16G16B16A16_FLOAT);
				Dispatch(context, prepare, cb, constants, { baseline.srv.Get() }, prepared.uav.Get());
				const auto inputs = ReadStored(device, context, prepare, cb, prepared);
				std::vector<Pixel> edited(ramp.size()), targets(ramp.size());
				for (unsigned i = 0; i < inputs.size(); ++i) {
					const auto& source = originals[i];
					RGB decoded{};
					Require(Inverse({ inputs[i].r, inputs[i].g, inputs[i].b }, profile, decoded),
						"FP16 prepared colours must remain invertible at every exposure endpoint");
					Require(std::abs(decoded[0] - source.r) < 0.015f,
						"exposure preparation must not black out bright pixels");
					const RGB target{ source.r * 0.8f + 0.02f, source.g * 0.8f + 0.02f, source.b * 0.8f + 0.02f };
					RGB encoded{};
					Require(Forward(target, profile, encoded), "valid neural fixture encoding");
					edited[i] = { encoded[0], encoded[1], encoded[2], 0.0f };
					targets[i] = { target[0], target[1], target[2], source.a };
				}
				auto neural = StorePixels(device, context, prepare, cb, edited, DXGI_FORMAT_R16G16B16A16_FLOAT);
				auto result = MakeTexture(device, {}, format);
				for (const bool applyEdit : { false, true }) {
					Dispatch(context, reconstruct, cb, constants,
						{ baseline.srv.Get(), applyEdit ? neural.srv.Get() : prepared.srv.Get(), prepared.srv.Get() }, result.uav.Get());
					const auto pixels = ReadStored(device, context, prepare, cb, result);
					for (unsigned i = 0; i < pixels.size(); ++i) {
						const auto& expected = applyEdit ? targets[i] : originals[i];
						const auto& actual = pixels[i];
						Require(Finite(RGB{ actual.r, actual.g, actual.b }), "calibration output must stay finite");
						if (!applyEdit) {
							Require(actual.r == expected.r && actual.g == expected.g && actual.b == expected.b,
								"mixed-format colour bypass must exactly preserve stored source");
						} else {
							Require(std::abs(actual.r - expected.r) < 0.015f && std::abs(actual.g - expected.g) < 0.015f &&
										std::abs(actual.b - expected.b) < 0.015f,
								"valid edits must survive reconstruction at both exposure extremes");
						}
						Require(actual.a == expected.a, "mixed-format colour transport must preserve source alpha");
					}
				}
			}
		}
	}
}
