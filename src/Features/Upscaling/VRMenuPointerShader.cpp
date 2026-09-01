#include "VRMenuPointerShader.h"

#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"

#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <winrt/base.h>

namespace
{
	struct PointerVertexShaderValidation
	{
		winrt::com_ptr<ID3D11VertexShader> shader;
		bool valid = false;
	};
	thread_local PointerVertexShaderValidation cachedValidation;

	RE::BSGraphics::VertexShader* FindBoundVertexShaderBytecode(ID3D11VertexShader* a_boundShader)
	{
		auto* nativeShader = globals::game::currentVertexShader ? *globals::game::currentVertexShader : nullptr;
		if (nativeShader && reinterpret_cast<ID3D11VertexShader*>(nativeShader->shader) == a_boundShader) {
			return nativeShader;
		}

		const auto* state = globals::state;
		auto* shaderCache = globals::shaderCache;
		if (!state || !shaderCache || !state->currentShader ||
			state->currentShader->shaderType.get() != RE::BSShader::Type::Effect)
			return nullptr;

		// The engine retains its native wrapper even when CSX binds a replacement.
		auto* replacement = shaderCache->GetVertexShaderIfCached(*state->currentShader, state->modifiedVertexDescriptor);
		if (replacement && reinterpret_cast<ID3D11VertexShader*>(replacement->shader) == a_boundShader) {
			return replacement;
		}
		return nullptr;
	}
}

void ResetVRMenuPointerVertexShaderValidation() noexcept
{
	cachedValidation = {};
}

bool ValidateVRMenuPointerVertexShader(ID3D11DeviceContext* a_context)
{
	if (!a_context)
		return false;
	winrt::com_ptr<ID3D11VertexShader> boundShader;
	a_context->VSGetShader(boundShader.put(), nullptr, nullptr);
	if (!boundShader)
		return false;
	if (boundShader == cachedValidation.shader)
		return cachedValidation.valid;

	// Holding the COM object prevents address reuse and caches unsupported signatures too.
	cachedValidation = { boundShader, false };
	auto* shader = FindBoundVertexShaderBytecode(boundShader.get());
	if (!shader || !shader->byteCodeSize)
		return false;
	winrt::com_ptr<ID3D11ShaderReflection> reflection;
	D3D11_SHADER_DESC shaderDesc{};
	if (FAILED(D3DReflect(shader->rawBytecode, shader->byteCodeSize, IID_PPV_ARGS(reflection.put()))) ||
		FAILED(reflection->GetDesc(&shaderDesc)))
		return false;
	for (UINT index = 0; index < shaderDesc.OutputParameters; ++index) {
		D3D11_SIGNATURE_PARAMETER_DESC output{};
		if (FAILED(reflection->GetOutputParameterDesc(index, &output)))
			return false;
		if (!output.SemanticName || _stricmp(output.SemanticName, "COLOR") != 0 || output.SemanticIndex != 0)
			continue;
		// The capture PS reserves the Effect signature's position and texture slots.
		constexpr UINT colorRegister = 3;
		constexpr BYTE rgbMask = 0x7;
		cachedValidation.valid = output.Register == colorRegister &&
		                         output.SystemValueType == D3D_NAME_UNDEFINED &&
		                         output.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32 &&
		                         (output.Mask & rgbMask) == rgbMask && (output.ReadWriteMask & rgbMask) == 0 &&
		                         output.Stream == 0 && output.MinPrecision == D3D_MIN_PRECISION_DEFAULT;
		break;
	}
	return cachedValidation.valid;
}
