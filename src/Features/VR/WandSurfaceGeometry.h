#pragma once

namespace WandSurfaceGeometry
{
	struct Vector
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Surface
	{
		Vector topLeft;
		Vector right;
		Vector down;
	};

	struct Hit
	{
		float u = 0.0f;
		float v = 0.0f;
		float distance = 0.0f;
	};

	constexpr Vector operator+(const Vector& a_left, const Vector& a_right)
	{
		return { a_left.x + a_right.x, a_left.y + a_right.y, a_left.z + a_right.z };
	}

	constexpr Vector operator-(const Vector& a_left, const Vector& a_right)
	{
		return { a_left.x - a_right.x, a_left.y - a_right.y, a_left.z - a_right.z };
	}

	constexpr Vector operator*(const Vector& a_vector, float a_scale)
	{
		return { a_vector.x * a_scale, a_vector.y * a_scale, a_vector.z * a_scale };
	}

	constexpr float Dot(const Vector& a_left, const Vector& a_right)
	{
		return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
	}

	constexpr Vector Cross(const Vector& a_left, const Vector& a_right)
	{
		return {
			a_left.y * a_right.z - a_left.z * a_right.y,
			a_left.z * a_right.x - a_left.x * a_right.z,
			a_left.x * a_right.y - a_left.y * a_right.x
		};
	}

	constexpr float Abs(float a_value)
	{
		return a_value < 0.0f ? -a_value : a_value;
	}

	// Intersect a world-space ray with the same three edges that define the
	// displayed menu. The Gram solve remains correct if the surface axes are
	// non-uniformly scaled or skewed; u/v therefore describe the apparent quad,
	// rather than a separately reconstructed unit plane.
	constexpr bool TryIntersect(
		const Surface& a_surface,
		const Vector& a_rayOrigin,
		const Vector& a_rayDirection,
		Hit& a_hit)
	{
		const Vector normal = Cross(a_surface.right, a_surface.down);
		const float normalLengthSq = Dot(normal, normal);
		const float denominator = Dot(a_rayDirection, normal);
		if (normalLengthSq <= 1e-12f || Abs(denominator) <= 1e-8f)
			return false;

		const float distance = Dot(a_surface.topLeft - a_rayOrigin, normal) / denominator;
		if (distance <= 0.0f)
			return false;

		const Vector fromTopLeft = a_rayOrigin + a_rayDirection * distance - a_surface.topLeft;
		const float rightRight = Dot(a_surface.right, a_surface.right);
		const float downDown = Dot(a_surface.down, a_surface.down);
		const float rightDown = Dot(a_surface.right, a_surface.down);
		const float hitRight = Dot(fromTopLeft, a_surface.right);
		const float hitDown = Dot(fromTopLeft, a_surface.down);
		const float determinant = rightRight * downDown - rightDown * rightDown;
		if (Abs(determinant) <= 1e-12f)
			return false;

		const float u = (hitRight * downDown - hitDown * rightDown) / determinant;
		const float v = (hitDown * rightRight - hitRight * rightDown) / determinant;
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			return false;

		a_hit = { u, v, distance };
		return true;
	}
}
