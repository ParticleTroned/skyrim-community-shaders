#pragma once

#include "test_common.h"

#include <d3dcompiler.h>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace D3D11ShaderTest
{
	inline void Check(HRESULT result)
	{
		if (FAILED(result))
			throw std::runtime_error("D3D11 shader test HRESULT " + std::to_string(result));
	}

	inline void Name(ID3D11DeviceChild* object, const char* name)
	{
		constexpr GUID debugName{ 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };
		Check(object->SetPrivateData(debugName, static_cast<UINT>(std::strlen(name)), name));
	}

	/** Resolve exactly the installed shader package used by the native test target. */
	struct ShaderIncludes : ID3DInclude
	{
		std::filesystem::path root = ShaderTest::GetExecutableDirectory() / "Shaders";
		HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override
		{
			try {
				std::ifstream file(root / name, std::ios::binary | std::ios::ate);
				if (!file)
					return E_FAIL;
				const auto length = file.tellg();
				if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<UINT>::max())
					return E_FAIL;
				auto bytes = std::make_unique<char[]>(static_cast<std::size_t>(length) + 1);
				file.seekg(0);
				file.read(bytes.get(), length);
				if (!file)
					return E_FAIL;
				*size = static_cast<UINT>(length);
				*data = bytes.release();
				return S_OK;
			} catch (...) {
				return E_FAIL;
			}
		}
		HRESULT __stdcall Close(LPCVOID data) override
		{
			delete[] static_cast<const char*>(data);
			return S_OK;
		}
	};

	enum class Stage
	{
		Compute,
		Pixel
	};

	/** Reflected test constants retain the production shader layout and reject oversized writes. */
	struct ConstantBuffer
	{
		ID3D11ShaderReflectionConstantBuffer* reflection;
		std::vector<std::byte> bytes;
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		UINT slot;

		ConstantBuffer(ID3D11Device* device, ID3D11ShaderReflection* shader, const char* name) :
			reflection(shader->GetConstantBufferByName(name))
		{
			D3D11_SHADER_BUFFER_DESC reflected{};
			Check(reflection->GetDesc(&reflected));
			bytes.resize(reflected.Size);
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			Check(shader->GetResourceBindingDescByName(name, &binding));
			slot = binding.BindPoint;
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = reflected.Size;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Check(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf()));
			Name(buffer.Get(), name);
		}

		UINT Offset(const char* name) const
		{
			D3D11_SHADER_VARIABLE_DESC desc{};
			Check(reflection->GetVariableByName(name)->GetDesc(&desc));
			return desc.StartOffset;
		}

		template <class T>
		void SetVariable(const char* name, const T& value)
		{
			D3D11_SHADER_VARIABLE_DESC variable{};
			Check(reflection->GetVariableByName(name)->GetDesc(&variable));
			Write(variable.StartOffset, variable.Size, value);
		}

		template <class T>
		void SetMember(const char* name, const char* member, const T& value)
		{
			auto* variable = reflection->GetVariableByName(name);
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			Check(variable->GetDesc(&variableDesc));
			auto* type = variable->GetType();
			D3D11_SHADER_TYPE_DESC typeDesc{}, memberDesc{};
			Check(type->GetDesc(&typeDesc));
			Check(type->GetMemberTypeByName(member)->GetDesc(&memberDesc));
			size_t end = variableDesc.Size;
			for (UINT i = 0; i < typeDesc.Members; ++i) {
				D3D11_SHADER_TYPE_DESC next{};
				Check(type->GetMemberTypeByIndex(i)->GetDesc(&next));
				if (next.Offset > memberDesc.Offset && next.Offset < end)
					end = next.Offset;
			}
			if (memberDesc.Offset > end)
				throw std::runtime_error("Reflected member exceeds its variable");
			Write(size_t(variableDesc.StartOffset) + memberDesc.Offset, end - memberDesc.Offset, value);
		}

		void Bind(ID3D11DeviceContext* context, Stage stage = Stage::Compute)
		{
			context->UpdateSubresource(buffer.Get(), 0, nullptr, bytes.data(), 0, 0);
			ID3D11Buffer* raw = buffer.Get();
			if (stage == Stage::Compute)
				context->CSSetConstantBuffers(slot, 1, &raw);
			else
				context->PSSetConstantBuffers(slot, 1, &raw);
		}

	private:
		template <class T>
		void Write(size_t offset, size_t size, const T& value)
		{
			if (sizeof(value) > size || offset > bytes.size() || sizeof(value) > bytes.size() - offset)
				throw std::runtime_error("Reflected shader constant exceeds its storage");
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		}
	};
}
