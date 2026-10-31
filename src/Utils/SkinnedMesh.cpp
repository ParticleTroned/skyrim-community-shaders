#include "SkinnedMesh.h"

#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>

namespace Util
{
	bool ReadSkinnedVertices(RE::BSGeometry* a_geometry, std::vector<SkinnedVertex>& a_vertices)
	{
		a_vertices.clear();
		const auto* skin = a_geometry ? a_geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
		const auto* partitionSet = skin ? skin->skinPartition.get() : nullptr;
		if (!partitionSet || !HasUsableSkin(skin) || partitionSet->numPartitions > 1024 ||
			partitionSet->numPartitions > partitionSet->partitions.size())
			return false;

		for (uint32_t partitionIndex = 0; partitionIndex < partitionSet->numPartitions; ++partitionIndex) {
			const auto& part = partitionSet->partitions[partitionIndex];
			RE::BSGraphics::VertexDesc desc = part.vertexDesc;
			if (!part.bones || !part.buffData || !part.buffData->rawVertexData || !desc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED) || !desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX))
				continue;

			const uint32_t stride = desc.GetSize();
			const uint32_t skinOffset = desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
			const bool fullPosition = desc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC) || desc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0) >= 16;
			const uint32_t positionBytes = fullPosition ? 12u : 6u;
			if (stride < positionBytes || skinOffset < positionBytes || skinOffset + 12 > stride ||
				part.vertices > partitionSet->vertexCount || part.numBones == 0 || part.numBones > 256 || a_vertices.size() + part.vertices > 262144)
				continue;
			if (part.buffData->vertexBuffer) {
				D3D11_BUFFER_DESC buffer{};
				reinterpret_cast<ID3D11Buffer*>(part.buffData->vertexBuffer)->GetDesc(&buffer);
				if (static_cast<uint64_t>(part.vertices) * stride > buffer.ByteWidth)
					continue;
			}
			a_vertices.reserve(a_vertices.size() + part.vertices);
			for (uint16_t vertex = 0; vertex < part.vertices; ++vertex) {
				const uint8_t* data = part.buffData->rawVertexData + static_cast<size_t>(vertex) * stride;
				float position[3];
				if (fullPosition) {
					std::memcpy(position, data, sizeof(position));
				} else {
					uint16_t packed[3];
					std::memcpy(packed, data, sizeof(packed));
					for (size_t axis = 0; axis < 3; ++axis)
						position[axis] = DirectX::PackedVector::XMConvertHalfToFloat(packed[axis]);
				}
				if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
					continue;
				uint16_t weights[4];
				std::memcpy(weights, data + skinOffset, sizeof(weights));
				const uint8_t* indices = data + skinOffset + 4 * sizeof(uint16_t);

				SkinnedVertex out;
				out.position = { position[0], position[1], position[2] };
				for (size_t slot = 0; slot < out.bone.size(); ++slot) {
					const uint8_t partitionBone = indices[slot];
					if (partitionBone >= part.numBones || part.bones[partitionBone] >= skin->numMatrices ||
						part.bones[partitionBone] >= skin->skinData->GetBoneCount())
						continue;
					const float weight = DirectX::PackedVector::XMConvertHalfToFloat(weights[slot]);
					if (!std::isfinite(weight) || weight <= 0.0f || weight > 1.0f)
						continue;
					out.bone[slot] = part.bones[partitionBone];
					out.weight[slot] = weight;
				}
				a_vertices.push_back(out);
			}
		}
		return !a_vertices.empty();
	}
}
