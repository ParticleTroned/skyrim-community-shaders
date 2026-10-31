#include <cassert>
#include <cstdint>

#include "Features/MeshBlendingPolicy.h"

namespace RE
{
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Other
		};
	};
}

namespace SIE::ShaderCache
{
	enum class LightingShaderFlags : std::uint32_t
	{
		ProjectedUV = 1u << 5u
	};
}

namespace Util
{
	inline int* depth = nullptr;
	inline unsigned queries = 0u;
	int* GetCurrentSceneDepthSRV(bool a_prefer16bit)
	{
		assert(!a_prefer16bit);
		++queries;
		return depth;
	}
}

struct Context
{
	unsigned calls = 0u;
	int* bound = nullptr;
	void PSSetShaderResources(unsigned a_slot, unsigned a_count, int** a_resource)
	{
		assert(a_slot == 17u && a_count == 1u);
		++calls;
		bound = *a_resource;
	}
};

struct Shader
{
	struct
	{
		RE::BSShader::Type value = RE::BSShader::Type::Lighting;
		auto get() const { return value; }
	} shaderType;
};

struct DrawState
{
#include "mesh_blending_descriptors_under_test.h"
	struct
	{
		std::uint32_t ExtraShaderDescriptor = 0u;
		std::uint32_t ExtraFeatureDescriptor = 0u;
	} permutationData;
	Shader* currentShader = nullptr;
	std::uint32_t currentPixelDescriptor = 0u;
	Context* context = nullptr;

	void Bind(bool a_isCompute)
	{
#include "mesh_blending_depth_under_test.h"
	}

	void ClearWhenUnloaded()
	{
		auto* state = this;
#include "mesh_blending_cleanup_under_test.h"
	}
};

int main()
{
	constexpr auto enabled = static_cast<std::uint32_t>(DrawState::ExtraShaderDescriptors::MeshBlending);
	static_assert(enabled == CSX::MeshBlendingPolicy::EnableMask);
	constexpr auto otherFlag = static_cast<std::uint32_t>(DrawState::ExtraShaderDescriptors::IsAurora);
	static_assert((enabled & otherFlag) == 0u);
	Context context;
	Shader shader;
	DrawState state;
	state.context = &context;
	state.currentShader = &shader;
	state.permutationData.ExtraShaderDescriptor = enabled | otherFlag;
	state.permutationData.ExtraFeatureDescriptor = 1u << 9u;
	state.ClearWhenUnloaded();
	assert(state.permutationData.ExtraShaderDescriptor == otherFlag);
	assert(state.permutationData.ExtraFeatureDescriptor == (1u << 9u));
	state.permutationData.ExtraShaderDescriptor = enabled | otherFlag;
	int firstDepth = 1;
	int secondDepth = 2;
	Util::depth = &firstDepth;
	state.Bind(true);
	assert(context.calls == 0u && Util::queries == 0u);
	assert(state.permutationData.ExtraShaderDescriptor == (enabled | otherFlag));
	state.Bind(false);
	assert(context.calls == 1u && context.bound == &firstDepth);
	Util::depth = &secondDepth;
	state.Bind(false);
	assert(context.calls == 2u && context.bound == &secondDepth);
	Util::depth = nullptr;
	state.Bind(false);
	assert(context.calls == 2u && state.permutationData.ExtraShaderDescriptor == otherFlag);
	const auto queried = Util::queries;
	state.Bind(false);
	assert(Util::queries == queried);
	state.permutationData.ExtraShaderDescriptor |= enabled;
	state.currentPixelDescriptor = static_cast<std::uint32_t>(SIE::ShaderCache::LightingShaderFlags::ProjectedUV);
	state.Bind(false);
	assert(Util::queries == queried && context.calls == 2u);
	state.currentPixelDescriptor = 0u;
	shader.shaderType.value = RE::BSShader::Type::Other;
	state.Bind(false);
	assert(Util::queries == queried && context.calls == 2u);
	state.currentShader = nullptr;
	state.Bind(false);
	assert(Util::queries == queried && context.calls == 2u);
}
