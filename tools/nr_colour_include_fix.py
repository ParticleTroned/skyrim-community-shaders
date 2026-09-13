"""Temporary workbench fixup: mirror CSX's rooted shader include contract."""
from pathlib import Path
import subprocess

root = Path.cwd()
def replace(path, before, after):
    p = root / path
    text = p.read_text(encoding='utf-8')
    if text.count(before) != 1:
        raise RuntimeError('unexpected include fixup anchor: ' + path)
    p.write_text(text.replace(before, after), encoding='utf-8', newline='\n')
    subprocess.run(['git', 'add', '--', path], check=True)

for shader in ['ColorPrepareCS.hlsl', 'ColorResolveCS.hlsl', 'ColorSamplesCS.hlsl']:
    replace('features/Upscaling/Shaders/Upscaling/NeuralRendering/' + shader,
            '#include "ColorCommon.hlsli"', '#include "Upscaling/NeuralRendering/ColorCommon.hlsli"')
path = 'tests/neural_rendering_color_warp_test.cpp'
replace(path, '#include <filesystem>\n', '#include <filesystem>\n#include <fstream>\n#include <iterator>\n#include <memory>\n')
replace(path, '\tComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::filesystem::path& path)\n', '''	// Util::CompileShader resolves includes against Data/Shaders, not the source
	// file's directory. Mirror that contract rather than accepting relative-only
	// includes which compile in a standalone test but fail in the actual game.
	class RootedShaderInclude final : public ID3DInclude
	{
	public:
		explicit RootedShaderInclude(std::filesystem::path root) : root_(std::move(root)) {}
		HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override
		{
			if (!name || !data || !size) return E_INVALIDARG;
			*data = nullptr; *size = 0;
			try {
				std::ifstream stream(root_ / name, std::ios::binary);
				if (!stream) return E_FAIL;
				const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
				if (stream.bad() || text.size() > std::numeric_limits<UINT>::max()) return E_FAIL;
				auto bytes = std::make_unique<char[]>(text.size());
				std::memcpy(bytes.get(), text.data(), text.size());
				*size = static_cast<UINT>(text.size()); *data = bytes.release();
				return S_OK;
			} catch (...) { return E_FAIL; }
		}
		HRESULT Close(LPCVOID data) override { delete[] static_cast<const char*>(data); return S_OK; }
	private:
		std::filesystem::path root_;
	};
	ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const std::filesystem::path& path)
''')
replace(path, '\t\tComPtr<ID3DBlob> code, errors;\n', '\t\tComPtr<ID3DBlob> code, errors;\n\t\tRootedShaderInclude include(path.parent_path().parent_path().parent_path());\n')
replace(path, 'path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,', 'path.c_str(), nullptr, &include,')
replace('tests/neural_rendering_color_contract_test.cmake',
        'message(STATUS "Shared NR colour source contract passed")', '''foreach(SHADER IN ITEMS ColorPrepareCS.hlsl ColorResolveCS.hlsl ColorSamplesCS.hlsl)
    require_in("features/Upscaling/Shaders/Upscaling/NeuralRendering/${SHADER}"
        "#include \\"Upscaling/NeuralRendering/ColorCommon.hlsli\\"")
endforeach()
require_in(tests/neural_rendering_color_warp_test.cpp "RootedShaderInclude include(")
message(STATUS "Shared NR colour source contract passed")''')
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
