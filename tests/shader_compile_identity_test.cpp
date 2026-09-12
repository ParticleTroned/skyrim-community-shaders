#include "Utils/ShaderCacheManifest.h"
#include "Utils/ShaderDefines.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace Util;

int main(int argc, char** argv)
{
	const auto global = ContentHash::HashString("ShaderCacheABI=test;");
	std::array<D3D_SHADER_MACRO, 5> defines{ { { "OUTPUT", "0" }, { "PSHADER", nullptr }, { "OUTPUT", "0" }, { nullptr, nullptr }, { "IGNORED", nullptr } } };
	const auto original = defines;
	assert(ShaderDefines::MergeDefinesString(defines, true) == "OUTPUT=0 PSHADER ");
	assert(std::memcmp(defines.data(), original.data(), sizeof(defines)) == 0);
	const auto initial = ShaderDefines::CompileStateDigest(global, defines);
	std::array<D3D_SHADER_MACRO, 3> reordered{ { { "PSHADER", "" }, { "OUTPUT", "0" }, { nullptr, nullptr } } };
	assert(initial == ShaderDefines::CompileStateDigest(global, reordered));
	reordered[1].Definition = "1";
	assert(initial != ShaderDefines::CompileStateDigest(global, reordered));
	reordered[1] = { "RENDER_DEPTH", nullptr };
	assert(initial != ShaderDefines::CompileStateDigest(global, reordered));
	assert(initial != global);
	std::array<D3D_SHADER_MACRO, 2> conflicting{ { { "OUTPUT", "0" }, { "OUTPUT", "1" } } };
	const auto conflictDigest = ShaderDefines::CompileStateDigest(global, conflicting);
	std::swap(conflicting[0], conflicting[1]);
	assert(conflictDigest != ShaderDefines::CompileStateDigest(global, conflicting));
	assert(initial != ShaderDefines::CompileStateDigest(ContentHash::HashString("VR;ShaderCacheABI=test;"), defines));
	std::array<D3D_SHADER_MACRO, 1> embedded{ { { "A", "1 B=2" } } };
	std::array<D3D_SHADER_MACRO, 2> separate{ { { "A", "1" }, { "B", "2" } } };
	assert(ShaderDefines::MergeDefinesString(embedded, true) == ShaderDefines::MergeDefinesString(separate, true));
	assert(ShaderDefines::CompileStateDigest(global, embedded) != ShaderDefines::CompileStateDigest(global, separate));

	constexpr std::string_view source = "float4 main() : SV_Target { return OUTPUT; }";
	Microsoft::WRL::ComPtr<ID3DBlob> first, second, errors;
	std::array<D3D_SHADER_MACRO, 2> output{ { { "OUTPUT", "0" }, { nullptr, nullptr } } };
	assert(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, output.data(), nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &first, &errors)));
	output[0].Definition = "1";
	assert(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, output.data(), nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &second, &errors)));
	assert(first->GetBufferSize() != second->GetBufferSize() || std::memcmp(first->GetBufferPointer(), second->GetBufferPointer(), first->GetBufferSize()) != 0);
	std::vector<std::string> names;
	for (size_t index = 0; index < 128; ++index)
		names.push_back("CUSTOM_" + std::to_string(index));
	std::vector<D3D_SHADER_MACRO> manyDefines;
	for (const auto& name : names)
		manyDefines.push_back({ name.c_str(), "1" });
	manyDefines.push_back({ "OUTPUT", "1" });
	manyDefines.push_back({ nullptr, nullptr });
	Microsoft::WRL::ComPtr<ID3DBlob> manyBlob;
	assert(SUCCEEDED(D3DCompile(source.data(), source.size(), nullptr, manyDefines.data(), nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &manyBlob, &errors)));
	const auto manyDigest = ShaderDefines::CompileStateDigest(global, manyDefines);
	manyDefines[100].Definition = "2";
	assert(manyDigest != ShaderDefines::CompileStateDigest(global, manyDefines));

	if (argc == 2) {
		std::ifstream stream(argv[1]);
		const auto fixture = nlohmann::json::parse(stream);
		ShaderCacheManifest::Manifest manifest;
		manifest.Load(fixture.at("manifest").get<std::string>());
		for (const auto& entry : fixture.at("entries")) {
			std::vector<std::pair<std::string, std::string>> storage;
			for (const auto& text : entry.at("defines").get<std::vector<std::string>>()) {
				const auto split = text.find('=');
				storage.emplace_back(text.substr(0, split), split == std::string::npos ? "" : text.substr(split + 1));
			}
			std::vector<D3D_SHADER_MACRO> macros;
			for (const auto& [name, value] : storage)
				macros.push_back({ name.c_str(), value.empty() ? nullptr : value.c_str() });
			macros.push_back({ nullptr, nullptr });
			const auto sourceDigest = ContentHash::HashFile(entry.at("source").get<std::string>());
			assert(sourceDigest);
			const auto compileDigest = ShaderDefines::CompileStateDigest(ContentHash::HashString(fixture.at("global").get<std::string>()), macros);
			const auto expected = ContentHash::CombineHashes(*sourceDigest, compileDigest).ToHex();
			assert(manifest.Get(entry.at("path").get<std::string>()) == expected);
			if (entry.contains("bytecode")) {
				std::ifstream sourceFile(entry.at("source").get<std::string>(), std::ios::binary);
				const std::string shaderSource{ std::istreambuf_iterator<char>(sourceFile), {} };
				Microsoft::WRL::ComPtr<ID3DBlob> compiled;
				assert(SUCCEEDED(D3DCompile(shaderSource.data(), shaderSource.size(), nullptr, macros.data(), nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &compiled, &errors)));
				std::ifstream bytecodeFile(entry.at("bytecode").get<std::string>(), std::ios::binary);
				const std::string bytecode{ std::istreambuf_iterator<char>(bytecodeFile), {} };
				assert(bytecode.size() == compiled->GetBufferSize());
				assert(std::memcmp(bytecode.data(), compiled->GetBufferPointer(), bytecode.size()) == 0);
			}
		}
		manifest.Load(fixture.at("legacyManifest").get<std::string>());
		for (const auto& entry : fixture.at("entries"))
			assert(!manifest.Get(entry.at("path").get<std::string>()));
	}
	std::cout << "Shader compile identity checks passed\n";
}
