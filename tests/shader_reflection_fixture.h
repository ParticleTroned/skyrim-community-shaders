#pragma once

// Only native relocation and D3D reflection boundaries are substituted.
struct ID3D11Buffer
{};
namespace REX::W32
{
	using ID3D11Buffer = ::ID3D11Buffer;
}
#define RELOCATION_ID(se, ae) se
namespace REL
{
	template <class T>
	struct Relocation
	{
		explicit Relocation(int) {}
		T get() const
		{
			static std::array<void*, 4096> buffers{};
			return reinterpret_cast<T>(buffers.data());
		}
	};
}
struct D3D11_SHADER_DESC
{
	uint32_t InputParameters = 1, ConstantBuffers = 1;
};
struct D3D11_SIGNATURE_PARAMETER_DESC
{
	const char* SemanticName = "POSITION";
	uint32_t SemanticIndex = 0;
};
struct D3D11_SHADER_BUFFER_DESC
{
	const char* Name = "PerGeometry";
	uint32_t Variables = 1, Size = 16;
};
struct D3D11_SHADER_VARIABLE_DESC
{
	const char* Name = "Known";
	uint32_t StartOffset = 4, Size = 16;
};
struct D3D11_SHADER_TYPE_DESC
{
	uint32_t Elements = 0;
};
namespace ReflectionFixture
{
	enum class Failure
	{
		None,
		Reflect,
		NullReflector,
		ShaderDesc,
		Input,
		NullSemantic,
		Buffer,
		NullBuffer,
		Variable,
		NullVariable,
		Type,
		NullType
	};
	inline Failure failure = Failure::None;
	inline uint32_t bufferCount = 1;
	inline const char* bufferName = "PerGeometry";
	inline const char* variableName = "Known";
}
struct ID3D11ShaderReflectionType
{
	HRESULT GetDesc(D3D11_SHADER_TYPE_DESC* desc)
	{
		*desc = {};
		return ReflectionFixture::failure == ReflectionFixture::Failure::Type ? E_FAIL : S_OK;
	}
};
struct ID3D11ShaderReflectionVariable
{
	HRESULT GetDesc(D3D11_SHADER_VARIABLE_DESC* desc)
	{
		*desc = {};
		desc->Name = ReflectionFixture::variableName;
		return ReflectionFixture::failure == ReflectionFixture::Failure::Variable ? E_FAIL : S_OK;
	}
	ID3D11ShaderReflectionType* GetType()
	{
		static ID3D11ShaderReflectionType value;
		return ReflectionFixture::failure == ReflectionFixture::Failure::NullType ? nullptr : &value;
	}
};
struct ID3D11ShaderReflectionConstantBuffer
{
	HRESULT GetDesc(D3D11_SHADER_BUFFER_DESC* desc)
	{
		*desc = {};
		desc->Name = ReflectionFixture::bufferName;
		return ReflectionFixture::failure == ReflectionFixture::Failure::Buffer ? E_FAIL : S_OK;
	}
	ID3D11ShaderReflectionVariable* GetVariableByIndex(uint32_t)
	{
		static ID3D11ShaderReflectionVariable value;
		return ReflectionFixture::failure == ReflectionFixture::Failure::NullVariable ? nullptr : &value;
	}
};
struct __declspec(uuid("8d536ca1-0cca-4956-a837-786963755584")) ID3D11ShaderReflection : IUnknown
{
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
	HRESULT GetDesc(D3D11_SHADER_DESC* desc)
	{
		*desc = {};
		desc->ConstantBuffers = ReflectionFixture::bufferCount;
		return ReflectionFixture::failure == ReflectionFixture::Failure::ShaderDesc ? E_FAIL : S_OK;
	}
	HRESULT GetInputParameterDesc(uint32_t, D3D11_SIGNATURE_PARAMETER_DESC* desc)
	{
		*desc = {};
		if (ReflectionFixture::failure == ReflectionFixture::Failure::NullSemantic)
			desc->SemanticName = nullptr;
		return ReflectionFixture::failure == ReflectionFixture::Failure::Input ? E_FAIL : S_OK;
	}
	ID3D11ShaderReflectionConstantBuffer* GetConstantBufferByIndex(uint32_t)
	{
		static ID3D11ShaderReflectionConstantBuffer value;
		return ReflectionFixture::failure == ReflectionFixture::Failure::NullBuffer ? nullptr : &value;
	}
};
inline HRESULT D3DReflect(const void*, size_t, REFIID, void** result)
{
	static ID3D11ShaderReflection value;
	*result = ReflectionFixture::failure == ReflectionFixture::Failure::NullReflector ? nullptr : &value;
	return ReflectionFixture::failure == ReflectionFixture::Failure::Reflect ? E_FAIL : S_OK;
}
