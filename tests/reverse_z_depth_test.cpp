#include "Utils/DepthConvention.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	struct Matrix
	{
		float m[4][4]{};
	};

	void Require(bool value, const char* reason)
	{
		if (!value)
			throw std::runtime_error(reason);
	}

	Matrix Perspective(float nearPlane, float farPlane, float offset)
	{
		Matrix result{};
		result.m[0][0] = 1.3f;
		result.m[1][1] = 1.7f;
		result.m[2][0] = offset;
		result.m[2][1] = -.15f;
		result.m[2][2] = farPlane / (farPlane - nearPlane);
		result.m[2][3] = 1;
		result.m[3][2] = -nearPlane * farPlane / (farPlane - nearPlane);
		return result;
	}

	Matrix Transpose(const Matrix& value)
	{
		Matrix result{};
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				result.m[row][col] = value.m[col][row];
		return result;
	}

	Matrix Multiply(const Matrix& left, const Matrix& right)
	{
		Matrix result{};
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				for (int inner = 0; inner < 4; ++inner)
					result.m[row][col] += left.m[row][inner] * right.m[inner][col];
		return result;
	}

	bool Equal(const Matrix& left, const Matrix& right, float tolerance = 1e-5f)
	{
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				if (!std::isfinite(left.m[row][col]) || std::abs(left.m[row][col] - right.m[row][col]) > tolerance)
					return false;
		return true;
	}

	void TestRangeAndEndpoints()
	{
		Require(Depth::IsValidRange(1, 100000), "Valid depth range rejected");
		for (auto range : { std::array{ 0.0, 10.0 }, std::array{ -1.0, 10.0 }, std::array{ 10.0, 10.0 },
				 std::array{ 20.0, 10.0 }, std::array{ 1.0, std::numeric_limits<double>::infinity() },
				 std::array{ std::numeric_limits<double>::quiet_NaN(), 10.0 } })
			Require(!Depth::IsValidRange(range[0], range[1]), "Invalid camera range accepted");
		for (auto order : { Depth::Convention::Standard, Depth::Convention::Reversed }) {
			Require(Depth::Linearize(Depth::Near(order), 1, 100000, order) == 1, "Near plane changed");
			Require(Depth::Linearize(Depth::Far(order), 1, 100000, order) == 100000, "Far plane changed");
		}
	}

	void TestCameraConvention()
	{
		auto standard = Transpose(Perspective(1, 100000, 0));
		auto reversed = standard;
		Depth::SetReverseUpload(reversed, 1, 100000);
		Require(Depth::FromUploadedProjection(standard, false) == Depth::Convention::Standard, "Standard renderer selected reversed depth");
		Require(Depth::FromUploadedProjection(standard, true) == Depth::Convention::Standard, "Float storage reversed the initial menu's standard depth");
		Require(Depth::FromUploadedProjection(reversed, true) == Depth::Convention::Reversed, "World-camera activation was missed without a resource generation change");
		Require(Depth::FromUploadedProjection(reversed, false) == Depth::Convention::Standard, "Inactive renderer accepted a stale reversed camera");
		Matrix orthographic{};
		orthographic.m[0][0] = orthographic.m[1][1] = orthographic.m[3][3] = 1;
		orthographic.m[2][2] = .001f;
		Require(Depth::FromUploadedProjection(orthographic, true) == Depth::Convention::Standard, "Orthographic view inherited the world depth convention");
		Require(Depth::UploadedDepthForward(orthographic) == 0, "Orthographic history was treated as reversible perspective depth");
		// Independent previous depth orientation survives large camera rotations/translations.
		for (float angle : { 0.0f, .7f, 1.6f, 3.1f }) {
			Matrix view{};
			view.m[0][0] = view.m[2][2] = std::cos(angle);
			view.m[0][2] = std::sin(angle);
			view.m[2][0] = -std::sin(angle);
			view.m[1][1] = view.m[3][3] = 1;
			view.m[3][0] = 4000;
			view.m[3][2] = -30000;
			for (bool previousReversed : { false, true }) {
				auto previous = Perspective(2, 50000, .02f);
				if (previousReversed)
					Depth::SetReverseProjection(previous, 2, 50000);
				const auto uploaded = Transpose(Multiply(view, previous));
				Require(Depth::FromUploadedViewProjection(uploaded, true) == (previousReversed ? Depth::Convention::Reversed : Depth::Convention::Standard), "Previous depth convention was inferred from the current camera");
				Require(Depth::FromUploadedViewProjection(uploaded, false) == Depth::Convention::Standard, "Inactive renderer accepted reversed history");
			}
		}
	}

	void TestProjectionAndSdkBoundary()
	{
		// Different frusta and temporal jitter must preserve x/y/w while depth reverses.
		for (float nearPlane : { .1f, 1.f, 5.f }) {
			for (float farPlane : { 50000.f, 100000.f, 1000000.f }) {
				for (float jitter : { -.03f, 0.f, .03f }) {
					const auto standard = Perspective(nearPlane, farPlane, jitter);
					auto reversed = standard;
					Depth::SetReverseProjection(reversed, nearPlane, farPlane);
					for (float z : { nearPlane, nearPlane * 2, 100.f, 10000.f, farPlane }) {
						const double depth = (double(z) * reversed.m[2][2] + reversed.m[3][2]) / z;
						const auto reconstructed = Depth::Linearize(depth, nearPlane, farPlane, Depth::Convention::Reversed);
						Require(std::abs(reconstructed - z) < std::max(1e-4, double(z) * 2e-6), "Reversed depth lost distant precision");
					}
					for (int row = 0; row < 4; ++row)
						for (int col : { 0, 1, 3 })
							Require(reversed.m[row][col] == standard.m[row][col], "Depth reversal changed FOV, jitter, or perspective divide");
					const auto once = reversed;
					Depth::SetReverseProjection(reversed, nearPlane, farPlane);
					Require(Equal(once, reversed, 0), "Repeated reversal is not idempotent");

					auto uploaded = Transpose(standard);
					Depth::SetReverseUpload(uploaded, nearPlane, farPlane);
					Require(Equal(uploaded, Transpose(reversed), 0), "GPU and native matrix conventions disagree");
					auto sdkProjection = reversed;
					Depth::ToStandardProjection(sdkProjection, Depth::Convention::Reversed);
					Require(Equal(sdkProjection, standard), "Standard SDK projection disagrees with standard scene depth");
					Depth::ToStandardUpload(uploaded, Depth::Convention::Reversed);
					Require(Equal(uploaded, Transpose(standard)), "Standard SDK upload contains reversed depth");
					const auto sdkOnce = uploaded;
					Depth::ToStandardUpload(uploaded, Depth::Convention::Standard);
					Depth::ToStandardProjection(sdkProjection, Depth::Convention::Standard);
					Require(Equal(uploaded, sdkOnce, 0) && Equal(sdkProjection, standard), "Standard-depth boundary modified its input");

					Matrix view{};
					view.m[0][0] = view.m[1][1] = view.m[2][2] = view.m[3][3] = 1;
					view.m[3][0] = 50;
					view.m[3][1] = -13;
					view.m[3][2] = 200;
					auto combined = Multiply(view, standard);
					Depth::SetReverseProjection(combined, nearPlane, farPlane);
					Require(Equal(combined, Multiply(view, reversed)), "Translated camera depth differs from its projection");
					auto sdkCombined = Transpose(combined);
					Depth::ToStandardUpload(sdkCombined, Depth::Convention::Reversed);
					Require(Equal(sdkCombined, Transpose(Multiply(view, standard)), 3e-5f), "Frame generation combined matrix has incompatible depth");
				}
			}
		}
	}
}

int main()
{
	try {
		TestRangeAndEndpoints();
		TestCameraConvention();
		TestProjectionAndSdkBoundary();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
