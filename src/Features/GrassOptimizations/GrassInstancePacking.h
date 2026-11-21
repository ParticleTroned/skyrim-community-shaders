#pragma once

#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace GrassInstancePacking
{
	inline constexpr uint32_t kStride = 16;
	inline constexpr float kMaximumBasisError = 0.0005f;
	// Each absolute row/column sum can grow by three component errors.
	inline constexpr float kBoundsSlack = 3 * kMaximumBasisError + 0.0001f;
	using Record = std::array<uint32_t, 4>;

	/** @brief Decode native half-floats once for both source bounds and optional compression. */
	inline bool Decode(const void* source, std::array<uint16_t, 16>& halves, std::array<float, 16>& value)
	{
		std::memcpy(halves.data(), source, sizeof(halves));
		for (size_t i = 0; i < value.size(); ++i) {
			value[i] = DirectX::PackedVector::XMConvertHalfToFloat(halves[i]);
			if (!std::isfinite(value[i]))
				return false;
		}
		return true;
	}

	/** @brief Compress a successfully decoded native record, bounding its rotation error. */
	inline bool PackDecoded(const std::array<uint16_t, 16>& halves, const std::array<float, 16>& value, Record& packed)
	{
		if (value[14] != 0 || value[15] != 0)
			return false;
		const DirectX::XMFLOAT3X3 basis{ value[4], value[5], value[6], value[8], value[9], value[10], value[12], value[7], value[11] };
		auto quaternion = DirectX::XMQuaternionRotationMatrix(DirectX::XMMatrixTranspose(DirectX::XMLoadFloat3x3(&basis)));
		DirectX::XMFLOAT4 normalized;
		DirectX::XMStoreFloat4(&normalized, DirectX::XMQuaternionNormalize(quaternion));
		std::array<float, 4> components{ normalized.x, normalized.y, normalized.z, normalized.w };
		uint32_t omitted = 0;
		for (uint32_t i = 0; i < components.size(); ++i) {
			if (!std::isfinite(components[i]))
				return false;
			if (std::abs(components[i]) > std::abs(components[omitted]))
				omitted = i;
		}
		const float sign = components[omitted] < 0 ? -1.0f : 1.0f;
		std::array<uint32_t, 3> encoded{};
		float squared = 0;
		for (uint32_t i = 0, index = 0; i < components.size(); ++i) {
			if (i == omitted)
				continue;
			encoded[index] = uint32_t(std::lround(std::clamp(sign * components[i] * 1.41421356237f, -1.0f, 1.0f) * 16383.0f) + 16384);
			components[i] = (float(encoded[index++]) - 16384.0f) * (0.70710678f / 16383.0f);
			squared += components[i] * components[i];
		}
		components[omitted] = std::sqrt(std::max(0.0f, 1.0f - squared));
		const auto [x, y, z, w] = components;
		const std::array<float, 9> decoded{
			1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
			2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
			2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)
		};
		constexpr std::array<size_t, 9> indices{ 4, 5, 6, 8, 9, 10, 12, 7, 11 };
		for (size_t i = 0; i < indices.size(); ++i)
			if (std::abs(decoded[i] - value[indices[i]]) > kMaximumBasisError)
				return false;
		// Keep native position, brightness and scale bits so density seeds do not change.
		packed = { uint32_t(halves[0]) | (uint32_t(halves[1]) << 16), uint32_t(halves[2]) | (uint32_t(halves[3]) << 16),
			encoded[0] | (encoded[1] << 15) | (omitted << 30), encoded[2] | (uint32_t(halves[13]) << 16) };
		return true;
	}

	/** @brief Preserve native scalar bits and reject records that cannot be compressed safely. */
	inline bool Pack(const void* source, Record& packed)
	{
		std::array<uint16_t, 16> halves;
		std::array<float, 16> value;
		return Decode(source, halves, value) && PackDecoded(halves, value, packed);
	}
}
