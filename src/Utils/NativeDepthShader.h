#pragma once

#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

namespace Depth
{
	enum class NativeVertexProgram
	{
		Other,
		Occluder,
		NearPlane
	};

	/** Retains every registered shader generation and publishes immutable entries for bind-time reads. */
	class NativeVertexShaderRegistry
	{
	public:
		/** Publish classification before returning a newly created native shader to its caller. */
		void Register(ID3D11VertexShader* shader, NativeVertexProgram program)
		{
			if (!shader || program == NativeVertexProgram::Other)
				return;
			std::scoped_lock lock(writerMutex);
			if (Classify(shader) != NativeVertexProgram::Other)
				return;
			auto entry = std::make_unique<Entry>();
			entry->shader = shader;
			entry->program = program;
			entry->next = head.load(std::memory_order_relaxed);
			const auto* published = entry.get();
			entries.push_back(std::move(entry));
			head.store(published, std::memory_order_release);
		}

		/** Read published classifications without locking or acquiring a COM reference. */
		NativeVertexProgram Classify(ID3D11VertexShader* shader) const
		{
			if (shader)
				for (auto* entry = head.load(std::memory_order_acquire); entry; entry = entry->next)
					if (entry->shader.Get() == shader)
						return entry->program;
			return NativeVertexProgram::Other;
		}

	private:
		struct Entry
		{
			Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
			NativeVertexProgram program = NativeVertexProgram::Other;
			const Entry* next = nullptr;
		};
		// Entries never move or retire while render hooks can read their shader identities.
		std::vector<std::unique_ptr<Entry>> entries;
		std::mutex writerMutex;
		std::atomic<const Entry*> head{ nullptr };
	};

	/** Match the fixed near-plane program, never infer HMD identity from draw size. */
	inline bool IsFixedNearPlaneVertexProgram(const void* bytes, size_t size)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		D3D11_SHADER_DESC desc{};
		// Most scene shaders cannot match the two moves and return of the fixed mask.
		if (SUCCEEDED(D3DReflect(bytes, size, IID_PPV_ARGS(&reflection))) && SUCCEEDED(reflection->GetDesc(&desc)) && desc.InstructionCount > 3)
			return false;
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
