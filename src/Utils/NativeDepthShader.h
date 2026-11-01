#pragma once

#include <cstring>
#include <d3dcompiler.h>
#include <string>
#include <string_view>
#include <wrl/client.h>

namespace Depth
{
	/** Match the fixed near-plane program, never infer HMD identity from draw size. */
	inline bool IsFixedNearPlaneVertexProgram(const void* bytes, size_t size)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> assembly;
		if (FAILED(D3DDisassemble(bytes, size, 0, nullptr, &assembly)))
			return false;
		std::string program;
		std::string_view source(static_cast<const char*>(assembly->GetBufferPointer()), assembly->GetBufferSize());
		while (!source.empty()) {
			const auto end = source.find('\n');
			auto line = source.substr(0, end);
			if (const auto comment = line.find("//"); comment != line.npos)
				line = line.substr(0, comment);
			std::string instruction;
			for (const auto c : line)
				if (c != ' ' && c != '\t' && c != '\r' && c != '\0')
					instruction += c;
			if (!instruction.empty())
				program += instruction + '\n';
			if (end == source.npos)
				break;
			source.remove_prefix(end + 1);
		}
		for (const auto* literal : { "0.000000", "1.000000" }) {
			for (auto pos = program.find(literal); pos != program.npos; pos = program.find(literal))
				program.replace(pos, std::strlen(literal), literal[0] == '0' ? "0" : "1");
		}
		return program == "vs_5_0\ndcl_globalFlagsrefactoringAllowed\ndcl_inputv0.xy\ndcl_output_sivo0.xyzw,position\nmovo0.xy,v0.xyxx\nmovo0.zw,l(0,0,0,1)\nret\n";
	}
}
