#pragma once

#include "d3d_resource_naming.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

#include <cstddef>
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
			Util::SetResourceName(buffer.Get(), "ShaderTest::%s", name);
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
