#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

// Metadata admission depends on rejecting non-finite arithmetic under /fp:fast.
#if defined(_MSC_VER)
#	pragma float_control(precise, on, push)
#endif

namespace ImGuiVRHelperScenePacket
{
	struct PixelExtent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct PixelRect
	{
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	struct NormalizedBounds
	{
		double uMin = 0.0;
		double vMin = 0.0;
		double uMax = 1.0;
		double vMax = 1.0;
	};

	struct OutputViewport
	{
		double x = 0.0;
		double y = 0.0;
		double width = 0.0;
		double height = 0.0;
		bool flipX = false;
		bool flipY = false;
	};

	/// Described view dimensions; identities are comparison tokens, never pointers to dereference.
	struct TextureView
	{
		std::uintptr_t resourceIdentity = 0;
		std::uintptr_t deviceIdentity = 0;
		PixelExtent extent{};
		std::uint32_t sampleCount = 1;
		std::uint32_t arraySize = 1;
		std::uint32_t arraySlice = 0;
		std::uint32_t mipLevel = 0;
	};

	struct DepthView
	{
		TextureView texture{};
		PixelRect activeRect{};
	};

	enum class DepthKind : std::uint8_t
	{
		NativeForwardZ,
		PositiveLinear
	};

	/// Native metres = scale / (offset - sample); linear metres = scale * sample + offset.
	struct DepthEncoding
	{
		DepthKind kind = DepthKind::NativeForwardZ;
		double scale = 0.0;
		double offset = 0.0;
	};

	/// Row-major storage with row-vector multiplication: clip = (x, y, z, 1) * matrix.
	using Matrix4x4 = std::array<double, 16>;
	using Point3 = std::array<double, 3>;

	struct ProjectedDepthTexel
	{
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		double nativeDepth = 0.0;
	};

	/// Require a nonempty half-open rectangle without overflowing unsigned edges.
	[[nodiscard]] constexpr bool Contains(PixelRect a_rect, PixelExtent a_extent) noexcept
	{
		return a_rect.width != 0 && a_rect.height != 0 &&
		       a_rect.x <= a_extent.width && a_rect.y <= a_extent.height &&
		       a_rect.width <= a_extent.width - a_rect.x &&
		       a_rect.height <= a_extent.height - a_rect.y;
	}

	/// Admit only single-sample, single-slice base-mip D3D11 Texture2D views.
	[[nodiscard]] constexpr bool IsValid(const TextureView& a_view) noexcept
	{
		constexpr std::uint32_t maxTexture2DDimension = 16384;
		return a_view.resourceIdentity != 0 && a_view.deviceIdentity != 0 &&
		       a_view.extent.width != 0 && a_view.extent.height != 0 &&
		       a_view.extent.width <= maxTexture2DDimension &&
		       a_view.extent.height <= maxTexture2DDimension &&
		       a_view.sampleCount == 1 && a_view.arraySize == 1 &&
		       a_view.arraySlice == 0 && a_view.mipLevel == 0;
	}

	[[nodiscard]] constexpr bool IsValid(const DepthView& a_view) noexcept
	{
		return IsValid(a_view.texture) && Contains(a_view.activeRect, a_view.texture.extent);
	}

	/// Validate matrix, axial-depth row or point components before arithmetic.
	template <std::size_t Size>
	[[nodiscard]] inline bool IsValid(const std::array<double, Size>& a_values) noexcept
	{
		for (double value : a_values) {
			if (!std::isfinite(value))
				return false;
		}
		return true;
	}

	/// Require a spatial direction; an offset alone cannot measure camera-axis distance.
	[[nodiscard]] inline bool HasAxialDepthDirection(const std::array<double, 4>& a_row) noexcept
	{
		return IsValid(a_row) && (a_row[0] != 0.0 || a_row[1] != 0.0 || a_row[2] != 0.0);
	}

	/// Reject pivots within elimination roundoff after normalizing each row's scale.
	[[nodiscard]] inline bool IsInvertible(const Matrix4x4& a_matrix) noexcept
	{
		if (!IsValid(a_matrix))
			return false;
		std::array<std::array<double, 4>, 4> rows{};
		for (std::size_t row = 0; row < rows.size(); ++row) {
			double scale = 0.0;
			for (std::size_t column = 0; column < rows[row].size(); ++column) {
				const double magnitude = std::abs(a_matrix[row * 4 + column]);
				if (magnitude > scale)
					scale = magnitude;
			}
			if (scale == 0.0)
				return false;
			for (std::size_t column = 0; column < rows[row].size(); ++column)
				rows[row][column] = a_matrix[row * 4 + column] / scale;
		}
		// The tolerance tracks coefficient roundoff rather than a fixed world-unit distance.
		constexpr double relativeTolerance = static_cast<double>(rows.size()) * std::numeric_limits<double>::epsilon();
		double eliminationScale = 1.0;
		for (std::size_t column = 0; column < rows.size(); ++column) {
			std::size_t pivot = column;
			for (std::size_t row = column + 1; row < rows.size(); ++row) {
				if (std::abs(rows[row][column]) > std::abs(rows[pivot][column]))
					pivot = row;
			}
			if (std::abs(rows[pivot][column]) <= relativeTolerance * eliminationScale)
				return false;
			rows[column].swap(rows[pivot]);
			for (std::size_t row = column + 1; row < rows.size(); ++row) {
				const double factor = rows[row][column] / rows[column][column];
				rows[row][column] = 0.0;
				for (std::size_t entry = column + 1; entry < rows[row].size(); ++entry) {
					rows[row][entry] -= factor * rows[column][entry];
					if (!std::isfinite(rows[row][entry]))
						return false;
					const double magnitude = std::abs(rows[row][entry]);
					if (magnitude > eliminationScale)
						eliminationScale = magnitude;
				}
			}
		}
		return true;
	}

	[[nodiscard]] inline bool IsValid(const DepthEncoding& a_encoding) noexcept
	{
		if (!std::isfinite(a_encoding.scale) || a_encoding.scale <= 0.0 ||
			!std::isfinite(a_encoding.offset))
			return false;
		switch (a_encoding.kind) {
		case DepthKind::NativeForwardZ:
			{
				if (a_encoding.offset <= 1.0)
					return false;
				const double nearMetres = a_encoding.scale / a_encoding.offset;
				const double farMetres = a_encoding.scale / (a_encoding.offset - 1.0);
				return std::isfinite(nearMetres) && nearMetres > 0.0 &&
				       std::isfinite(farMetres) && farMetres > nearMetres;
			}
		case DepthKind::PositiveLinear:
			return a_encoding.offset == 0.0;
		default:
			return false;
		}
	}

	/// Preserve reversed OpenVR bounds as orientation flags, never negative viewport sizes.
	[[nodiscard]] inline std::optional<OutputViewport> MakeOutputViewport(
		PixelExtent a_extent, NormalizedBounds a_bounds) noexcept
	{
		if (a_extent.width == 0 || a_extent.height == 0)
			return std::nullopt;
		const std::array bounds{ a_bounds.uMin, a_bounds.vMin, a_bounds.uMax, a_bounds.vMax };
		for (double value : bounds) {
			if (!std::isfinite(value) || value < 0.0 || value > 1.0)
				return std::nullopt;
		}
		OutputViewport viewport;
		viewport.flipX = a_bounds.uMin > a_bounds.uMax;
		viewport.flipY = a_bounds.vMin > a_bounds.vMax;
		const double left = viewport.flipX ? a_bounds.uMax : a_bounds.uMin;
		const double top = viewport.flipY ? a_bounds.vMax : a_bounds.vMin;
		viewport.x = left * a_extent.width;
		viewport.y = top * a_extent.height;
		viewport.width = std::abs(a_bounds.uMax - a_bounds.uMin) * a_extent.width;
		viewport.height = std::abs(a_bounds.vMax - a_bounds.vMin) * a_extent.height;
		if (!std::isfinite(viewport.width) || viewport.width <= 0.0 ||
			!std::isfinite(viewport.height) || viewport.height <= 0.0)
			return std::nullopt;
		return viewport;
	}

	/// Use an explicit rendered-depth rectangle within the described view, without inferred scale.
	[[nodiscard]] inline std::optional<DepthView> MakeDepthView(
		TextureView a_texture, PixelRect a_activeRect) noexcept
	{
		DepthView view{ a_texture, a_activeRect };
		if (!IsValid(view))
			return std::nullopt;
		return view;
	}

	/// Build the native CameraData forward-Z mapping in metres from source-space near/far.
	[[nodiscard]] inline std::optional<DepthEncoding> MakeNativeForwardZ(
		double a_near, double a_far, double a_unitsToMetres) noexcept
	{
		if (!std::isfinite(a_near) || !std::isfinite(a_far) ||
			!std::isfinite(a_unitsToMetres) || a_near <= 0.0 ||
			a_far <= a_near || a_unitsToMetres <= 0.0)
			return std::nullopt;
		const double nearMetres = a_near * a_unitsToMetres;
		const double farMetres = a_far * a_unitsToMetres;
		if (!std::isfinite(nearMetres) || nearMetres <= 0.0 ||
			!std::isfinite(farMetres) || farMetres <= nearMetres)
			return std::nullopt;
		const double offset = a_far / (a_far - a_near);
		DepthEncoding encoding{ DepthKind::NativeForwardZ, nearMetres * offset, offset };
		if (!IsValid(encoding))
			return std::nullopt;
		return encoding;
	}

	/// Positive linear samples represent axial distance in the supplied source units.
	[[nodiscard]] inline std::optional<DepthEncoding> MakePositiveLinear(double a_scaleToMetres) noexcept
	{
		DepthEncoding encoding{ DepthKind::PositiveLinear, a_scaleToMetres, 0.0 };
		if (!IsValid(encoding))
			return std::nullopt;
		return encoding;
	}

	/// Invalid or nonpositive distances have no comparison value.
	[[nodiscard]] inline std::optional<double> DecodeDepthMetres(
		const DepthEncoding& a_encoding, double a_sample) noexcept
	{
		if (!IsValid(a_encoding) || !std::isfinite(a_sample))
			return std::nullopt;
		double metres;
		if (a_encoding.kind == DepthKind::NativeForwardZ) {
			if (a_sample < 0.0 || a_sample > 1.0)
				return std::nullopt;
			metres = a_encoding.scale / (a_encoding.offset - a_sample);
		} else {
			if (a_sample <= 0.0)
				return std::nullopt;
			metres = a_encoding.scale * a_sample + a_encoding.offset;
		}
		if (!std::isfinite(metres) || metres <= 0.0)
			return std::nullopt;
		return metres;
	}

	/// Project the same tracking-space point into its eye's explicit rendered-depth rectangle.
	/// Texture y increases downward; right/bottom edges are excluded and never clamped inward.
	[[nodiscard]] inline std::optional<ProjectedDepthTexel> ProjectDepthTexel(
		const DepthView& a_depth, const Matrix4x4& a_trackingToClip, const Point3& a_point) noexcept
	{
		if (!IsValid(a_depth) || !IsValid(a_trackingToClip) || !IsValid(a_point))
			return std::nullopt;
		std::array<double, 4> clip{};
		for (std::size_t column = 0; column < clip.size(); ++column) {
			clip[column] = a_point[0] * a_trackingToClip[column] +
			               a_point[1] * a_trackingToClip[4 + column] +
			               a_point[2] * a_trackingToClip[8 + column] +
			               a_trackingToClip[12 + column];
		}
		if (!IsValid(clip) || clip[3] <= 0.0)
			return std::nullopt;
		const double ndcX = clip[0] / clip[3];
		const double ndcY = clip[1] / clip[3];
		const double ndcZ = clip[2] / clip[3];
		if (!std::isfinite(ndcX) || !std::isfinite(ndcY) || !std::isfinite(ndcZ) ||
			ndcX < -1.0 || ndcX >= 1.0 || ndcY <= -1.0 || ndcY > 1.0 ||
			ndcZ < 0.0 || ndcZ > 1.0)
			return std::nullopt;
		const auto& rect = a_depth.activeRect;
		const double x = std::floor(rect.x + (ndcX + 1.0) * 0.5 * rect.width);
		const double y = std::floor(rect.y + (1.0 - ndcY) * 0.5 * rect.height);
		if (!std::isfinite(x) || !std::isfinite(y) || x < rect.x || y < rect.y ||
			x >= static_cast<double>(rect.x) + rect.width ||
			y >= static_cast<double>(rect.y) + rect.height)
			return std::nullopt;
		return ProjectedDepthTexel{ static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), ndcZ };
	}

	/// Return true for occlusion; nullopt means skip comparison, without inventing a depth sample.
	[[nodiscard]] inline std::optional<bool> CompareDepth(
		const DepthEncoding& a_encoding, double a_sceneSample,
		double a_pointDepthMetres, double a_biasMetres = 0.0) noexcept
	{
		if (!std::isfinite(a_pointDepthMetres) || a_pointDepthMetres <= 0.0 ||
			!std::isfinite(a_biasMetres) || a_biasMetres < 0.0)
			return std::nullopt;
		const auto sceneMetres = DecodeDepthMetres(a_encoding, a_sceneSample);
		if (!sceneMetres)
			return std::nullopt;
		const double limit = *sceneMetres + a_biasMetres;
		if (!std::isfinite(limit))
			return std::nullopt;
		return a_pointDepthMetres > limit;
	}
}

#if defined(_MSC_VER)
#	pragma float_control(pop)
#endif
