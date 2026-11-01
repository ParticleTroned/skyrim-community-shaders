#pragma once

static void CheckNvidiaReference(ID3D11Device* device, ID3D11DeviceContext* context,
	const std::filesystem::path& directory, ID3D11Buffer* cb)
{
	const D3D_SHADER_MACRO defines[]{ { "NR_NVIDIA_REFERENCE", "1" }, { nullptr, nullptr } };
	const auto prepare = Compile(device, directory / "ColorPrepareCS.hlsl", {}, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, defines);
	const auto reconstruct = Compile(device, directory / "ColorReconstructCS.hlsl", {}, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, defines);
	std::vector<Pixel> source(4096, Pixel{ 0.18f, 0.12f, 0.08f, 0.375f });
	Constants constants;
	constants.transform = 3;
	const auto highlightIndex = constants.y * 64 + constants.x;
	source[highlightIndex] = { 32.0f, 16.0f, 8.0f, 0.25f };
	const std::vector<Pixel> sentinel(4096, Pixel{ -7, -7, -7, -7 });
	const auto baseline = MakeTexture(device, source);
	auto prepared = MakeTexture(device, sentinel);
	Dispatch(context, prepare.Get(), cb, constants, { baseline.srv.Get(), nullptr, nullptr }, prepared.uav.Get());
	auto reconstructed = MakeTexture(device, sentinel);
	Dispatch(context, reconstruct.Get(), cb, constants, { baseline.srv.Get(), prepared.srv.Get(), prepared.srv.Get() }, reconstructed.uav.Get());
	const auto unchanged = Read(device, context, reconstructed.texture.Get());
	for (unsigned y = 0; y < constants.height; ++y)
		for (unsigned x = 0; x < constants.width; ++x) {
			const auto index = (y + constants.y) * 64 + x + constants.x;
			Require(unchanged[index].r == source[index].r && unchanged[index].g == source[index].g &&
						unchanged[index].b == source[index].b && unchanged[index].a == source[index].a,
				"reference SDR round trip must preserve original HDR and alpha");
		}
	auto edited = Read(device, context, prepared.texture.Get());
	for (unsigned y = 0; y < constants.height; ++y)
		for (unsigned x = 0; x < constants.width; ++x) {
			auto& value = edited[(y + constants.y) * 64 + x + constants.x];
			Require(value.r >= 0 && value.r <= 1 && value.g >= 0 && value.g <= 1 && value.b >= 0 && value.b <= 1,
				"reference model input must remain SDR");
			value.r *= 0.9f;
			value.g *= 0.9f;
			value.b *= 0.9f;
		}
	const auto neural = MakeTexture(device, edited);
	Dispatch(context, reconstruct.Get(), cb, constants, { baseline.srv.Get(), neural.srv.Get(), prepared.srv.Get() }, reconstructed.uav.Get());
	const auto changed = Read(device, context, reconstructed.texture.Get());
	Require(changed[highlightIndex].r == source[highlightIndex].r && changed[highlightIndex].g == source[highlightIndex].g && changed[highlightIndex].b == source[highlightIndex].b,
		"reference recovery must protect exposed HDR highlights");
	Require(std::isfinite(changed[highlightIndex + 1].r) && changed[highlightIndex + 1].r < source[highlightIndex + 1].r && changed[highlightIndex + 1].a == source[highlightIndex + 1].a,
		"reference processing must retain finite model edits outside highlights");
	Require(changed[63 * 64 + 63].r == -7, "reference processing escaped the owned rectangle");
}
