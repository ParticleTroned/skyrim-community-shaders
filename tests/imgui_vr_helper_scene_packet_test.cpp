#include "Features/VR/ImGuiVRHelperScenePacket.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace ImGuiVRHelperScenePacket;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	bool Near(double a_actual, double a_expected)
	{
		return std::isfinite(a_actual) && std::abs(a_actual - a_expected) <= 1e-9;
	}

	TextureView Texture(PixelExtent a_extent)
	{
		return { 1, 2, a_extent, 1, 1, 0, 0 };
	}

	DepthView Depth(PixelExtent a_extent, PixelRect a_rectangle)
	{
		const auto view = MakeDepthView(Texture(a_extent), a_rectangle);
		Require(view.has_value(), "Valid depth fixture was rejected");
		return *view;
	}

	Matrix4x4 Perspective()
	{
		// Positive axial Z supplies homogeneous W; each eye shares this camera.
		return {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 0.5, 1,
			0, 0, 0, 0
		};
	}

	void ChecksRectanglesAndViewDescriptions()
	{
		Require(Contains({ 0, 0, 2000, 800 }, { 2000, 800 }), "Full texture rectangle rejected");
		Require(Contains({ 1000, 0, 1000, 800 }, { 2000, 800 }), "Right-eye rectangle rejected");
		Require(!Contains({ 1000, 0, 1001, 800 }, { 2000, 800 }), "Rectangle crossed right boundary");
		Require(!Contains({ 0, 1, 2000, 800 }, { 2000, 800 }), "Rectangle crossed bottom boundary");
		Require(!Contains({ 0, 0, 0, 1 }, { 2000, 800 }), "Empty rectangle accepted");
		Require(!Contains({ 0, 0, 1, 1 }, { 0, 800 }), "Empty resource accepted");
		constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
		Require(!Contains({ maximum - 1, 0, 4, 1 }, { maximum, 800 }), "Horizontal addition overflow accepted");
		Require(!Contains({ 0, maximum - 1, 1, 4 }, { 2000, maximum }), "Vertical addition overflow accepted");

		const auto valid = Texture({ 2000, 800 });
		Require(IsValid(valid), "Single-sample base-mip texture rejected");
		for (int invalidCase = 0; invalidCase < 7; ++invalidCase) {
			auto texture = valid;
			switch (invalidCase) {
			case 0:
				texture.resourceIdentity = 0;
				break;
			case 1:
				texture.deviceIdentity = 0;
				break;
			case 2:
				texture.extent.width = 0;
				break;
			case 3:
				texture.sampleCount = 4;
				break;
			case 4:
				texture.arraySize = 2;
				break;
			case 5:
				texture.arraySlice = 1;
				break;
			case 6:
				texture.mipLevel = 1;
				break;
			}
			Require(!IsValid(texture), "Unsupported texture description accepted");
			Require(!MakeDepthView(texture, { 0, 0, 1000, 800 }), "Invalid texture admitted as depth view");
		}
		Require(!MakeDepthView(valid, { 1500, 0, 1000, 800 }), "Depth view crossed its actual extent");
		Require(!IsValid(Texture({ 16385, 800 })), "Texture exceeded the D3D11 dimension limit");
	}

	void ReversedBoundsRetainAPositiveViewport()
	{
		const auto forward = MakeOutputViewport({ 2048, 1024 }, { 0.25, 0.125, 0.75, 0.875 });
		const auto reversed = MakeOutputViewport({ 2048, 1024 }, { 0.75, 0.875, 0.25, 0.125 });
		Require(forward && reversed, "Valid cropped bounds rejected");
		for (const auto& viewport : { *forward, *reversed }) {
			Require(Near(viewport.x, 512) && Near(viewport.y, 128), "Viewport origin ignored crop");
			Require(Near(viewport.width, 1024) && Near(viewport.height, 768), "Reversed bounds produced wrong extent");
		}
		Require(!forward->flipX && !forward->flipY, "Forward bounds gained an orientation flip");
		Require(reversed->flipX && reversed->flipY, "Reversed bounds lost orientation");
		const auto horizontal = MakeOutputViewport({ 2048, 1024 }, { 0.75, 0.125, 0.25, 0.875 });
		Require(horizontal && horizontal->flipX && !horizontal->flipY, "Independent output flips were coupled");
		Require(!MakeOutputViewport({ 0, 1024 }, { 0, 0, 1, 1 }), "Zero-width output accepted");
		Require(!MakeOutputViewport({ 2048, 1024 }, { 0.5, 0, 0.5, 1 }), "Zero-width viewport accepted");
		Require(!MakeOutputViewport({ 2048, 1024 }, { -0.1, 0, 1, 1 }), "Negative normalized bound accepted");
		Require(!MakeOutputViewport({ 2048, 1024 }, { 0, 0, 1, 1.1 }), "Normalized bound beyond texture accepted");
		Require(!MakeOutputViewport({ 2048, 1024 }, { 0, 0, std::numeric_limits<double>::infinity(), 1 }), "Infinite bound accepted");
		Require(!MakeOutputViewport({ 2048, 1024 }, { 0, std::numeric_limits<double>::quiet_NaN(), 1, 1 }), "NaN bound accepted");
	}

	void ProjectsTheSamePointIntoIndependentEyeRectangles()
	{
		const auto left = Depth({ 2000, 800 }, { 0, 0, 1000, 800 });
		const auto right = Depth({ 2000, 800 }, { 1000, 0, 1000, 800 });
		const Point3 point{ -0.4, 0.4, 2 };
		const auto leftPixel = ProjectDepthTexel(left, Perspective(), point);
		const auto rightPixel = ProjectDepthTexel(right, Perspective(), point);
		Require(leftPixel && rightPixel, "Visible stereo point rejected");
		Require(leftPixel->x == 400 && leftPixel->y == 320, "Left eye did not address its own depth rectangle");
		Require(rightPixel->x == 1400 && rightPixel->y == 320, "Right eye sampled the left-eye depth region");
		Require(Near(leftPixel->nativeDepth, 0.5) && Near(rightPixel->nativeDepth, 0.5), "Native depth did not divide by homogeneous W");

		// A separate 2000-pixel color eye places this point at x=800. A global
		// atlas/color size ratio incorrectly selects the unrelated near object there.
		const auto sceneDepth = [](const ProjectedDepthTexel& pixel) {
			return pixel.x == 800 && pixel.y == 320 ? 1.0 : 8.0;
		};
		const auto encoding = MakePositiveLinear(1);
		Require(encoding.has_value(), "Metre depth encoding rejected");
		for (const auto& pixel : { *leftPixel, *rightPixel }) {
			const auto occluded = CompareDepth(*encoding, sceneDepth(pixel), 2, 0.01);
			Require(occluded && !*occluded, "Off-ray near object erased the projected subtitle");
			const auto intervening = CompareDepth(*encoding, 1, 2, 0.01);
			Require(intervening && *intervening, "A true intervening object failed to occlude");
		}
		const auto wrongMapping = CompareDepth(*encoding, sceneDepth({ 800, 320, 0.5 }), 2, 0.01);
		Require(wrongMapping && *wrongMapping, "Occlusion oracle cannot detect the global-coordinate failure");

		const auto separateDepth = Depth({ 1000, 800 }, { 0, 0, 1000, 800 });
		const auto separatePixel = ProjectDepthTexel(separateDepth, Perspective(), point);
		Require(separatePixel && separatePixel->x == 400 && separatePixel->y == 320, "Separate depth eye depended on a color atlas layout");
	}

	void ProjectsAsymmetricTranslatedAndRotatedCameras()
	{
		const Matrix4x4 asymmetric{
			2, 0, 0, 0,
			0, 3, 0, 0,
			0.5, -0.25, 0.5, 1,
			0.25, -0.5, 0, 0
		};
		const auto asymmetricPixel = ProjectDepthTexel(Depth({ 1024, 768 }, { 100, 200, 640, 320 }), asymmetric, { 0.5, -0.25, 4 });
		Require(asymmetricPixel && asymmetricPixel->x == 680 && asymmetricPixel->y == 450, "Off-axis camera/crop projection is incorrect");

		const Matrix4x4 rotated{
			0, 1, 0, 0,
			-1, 0, 0, 0,
			0, 0, 0.5, 1,
			0.5, -0.25, 0, 0
		};
		const auto rotatedPixel = ProjectDepthTexel(Depth({ 200, 160 }, { 20, 40, 160, 80 }), rotated, { 0.5, 0, 2 });
		Require(rotatedPixel && rotatedPixel->x == 120 && rotatedPixel->y == 75, "Row-vector rotation/translation convention changed");
	}

	void RejectsSingularCamerasIndependentlyOfWorldScale()
	{
		// The third row is exactly the sum of the first two; rounded elimination
		// residuals must not turn this rank-deficient transform into a camera.
		const Matrix4x4 dependentRows{
			1, 2, 3, 0,
			4, 5, 6, 0,
			5, 7, 9, 0,
			0, 0, 0, 1
		};
		Require(!IsInvertible(dependentRows), "Exactly dependent camera rows were accepted after floating-point elimination");
		Require(!IsInvertible(Matrix4x4{}), "Zero camera matrix was accepted");

		for (const double scale : { 1e-200, 1.0, 1e200 }) {
			const Matrix4x4 scaledIdentity{
				scale, 0, 0, 0,
				0, scale, 0, 0,
				0, 0, scale, 0,
				0, 0, 0, scale
			};
			Require(IsInvertible(scaledIdentity), "Camera admission imposed an absolute world-scale threshold");
		}
		const Matrix4x4 independentRowScales{
			1e-200, 0, 0, 0,
			0, 1e200, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
		Require(IsInvertible(independentRowScales), "Independent finite row scales made a nonsingular camera invalid");

		// Finite forward-Z perspective with near=1, far=10 and an off-axis frustum.
		const Matrix4x4 perspective{
			1.25, 0, 0, 0,
			0, 1.5, 0, 0,
			0.2, -0.1, 10.0 / 9.0, 1,
			0, 0, -10.0 / 9.0, 0
		};
		Require(IsInvertible(perspective), "Valid finite perspective camera was rejected");
	}

	void RejectsProjectionOutsideTheEye()
	{
		const auto view = Depth({ 2000, 800 }, { 1000, 0, 1000, 800 });
		const auto matrix = Perspective();
		const auto topLeft = ProjectDepthTexel(view, matrix, { -1, 1, 1 });
		Require(topLeft && topLeft->x == 1000 && topLeft->y == 0, "Inclusive top-left pixel was rejected");
		const auto bottomRight = ProjectDepthTexel(view, matrix, { 0.99999, -0.99999, 1 });
		Require(bottomRight && bottomRight->x == 1999 && bottomRight->y == 799, "Last valid eye pixel was rejected");
		for (const Point3 point : { Point3{ 1, 0, 1 }, Point3{ 0, -1, 1 }, Point3{ -1.00001, 0, 1 }, Point3{ 0, 1.00001, 1 }, Point3{ 0, 0, 0 }, Point3{ 0, 0, -1 } })
			Require(!ProjectDepthTexel(view, matrix, point), "Invalid point was clamped or wrapped into an eye");

		auto invalidMatrix = matrix;
		invalidMatrix[10] = 2;
		Require(!ProjectDepthTexel(view, invalidMatrix, { 0, 0, 1 }), "Point beyond far clip plane accepted");
		invalidMatrix[10] = -0.5;
		Require(!ProjectDepthTexel(view, invalidMatrix, { 0, 0, 1 }), "Point before near clip plane accepted");
		for (const double invalid : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
			invalidMatrix = matrix;
			invalidMatrix[12] = invalid;
			Require(!IsValid(invalidMatrix), "Nonfinite camera matrix accepted");
			Require(!ProjectDepthTexel(view, invalidMatrix, { 0, 0, 1 }), "Nonfinite camera projected a texel");
			Require(!ProjectDepthTexel(view, matrix, { invalid, 0, 1 }), "Nonfinite world point projected a texel");
		}
		invalidMatrix = matrix;
		invalidMatrix[0] = std::numeric_limits<double>::max();
		Require(!ProjectDepthTexel(view, invalidMatrix, { 2, 0, 1 }), "Overflowed clip coordinate projected a texel");
		auto invalidView = view;
		invalidView.activeRect.width = 1001;
		Require(!ProjectDepthTexel(invalidView, matrix, { 0, 0, 1 }), "Unvalidated depth rectangle reached projection");
	}

	void DecodesKnownDepthsAndContactBias()
	{
		const auto native = MakeNativeForwardZ(100, 1000, 0.01);
		const auto linear = MakePositiveLinear(0.01);
		Require(native && linear, "Valid depth encodings rejected");
		const double samples[]{ 0, 5.0 / 9.0, 8.0 / 9.0, 1 };
		const double metres[]{ 1, 2, 5, 10 };
		for (std::size_t index = 0; index < 4; ++index) {
			const auto decoded = DecodeDepthMetres(*native, samples[index]);
			const auto decodedLinear = DecodeDepthMetres(*linear, metres[index] * 100);
			Require(decoded && Near(*decoded, metres[index]), "Forward-Z reconstruction or source units are incorrect");
			Require(decodedLinear && Near(*decodedLinear, metres[index]), "Positive linear depth units are incorrect");
		}
		const auto touching = CompareDepth(*linear, 300, 3.005, 0.01);
		const auto behind = CompareDepth(*linear, 300, 3.02, 0.01);
		const auto inFront = CompareDepth(*linear, 300, 2, 0);
		Require(touching && !*touching, "Metre contact bias incorrectly occluded the surface");
		Require(behind && *behind, "Depth beyond metre contact bias remained visible");
		Require(inFront && !*inFront, "Foreground surface was occluded");
		Require(!CompareDepth(*linear, 300, 2, -0.01), "Negative contact bias accepted");
		Require(!CompareDepth(*linear, 300, 0, 0), "Nonpositive point distance accepted");
		Require(!DecodeDepthMetres(*native, -0.1) && !DecodeDepthMetres(*native, 1.1), "Native depth outside [0,1] accepted");
		Require(!DecodeDepthMetres(*linear, 0) && !DecodeDepthMetres(*linear, -1), "Nonpositive scene depth accepted");
		Require(!MakeNativeForwardZ(0, 1000, 0.01) && !MakeNativeForwardZ(100, 100, 0.01), "Invalid near/far planes accepted");
		Require(!MakeNativeForwardZ(1000, 100, 0.01) && !MakeNativeForwardZ(100, 1000, 0), "Reversed planes or zero unit conversion accepted");
		Require(!MakePositiveLinear(0) && !MakePositiveLinear(-1), "Nonpositive linear unit conversion accepted");
		const auto largeLinear = MakePositiveLinear(std::numeric_limits<double>::max());
		Require(largeLinear && !DecodeDepthMetres(*largeLinear, 2), "Overflowed linear distance accepted");
		Require(!CompareDepth(*linear, std::numeric_limits<double>::max(), 2, std::numeric_limits<double>::max()), "Overflowed scene/bias distance accepted");
		for (const double invalid : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
			Require(!MakeNativeForwardZ(invalid, 1000, 0.01), "Nonfinite near plane accepted");
			Require(!MakeNativeForwardZ(100, invalid, 0.01), "Nonfinite far plane accepted");
			Require(!MakeNativeForwardZ(100, 1000, invalid), "Nonfinite native unit conversion accepted");
			Require(!MakePositiveLinear(invalid), "Nonfinite linear scale accepted");
			Require(!DecodeDepthMetres(*native, invalid), "Nonfinite native sample accepted");
			Require(!CompareDepth(*linear, 300, invalid, 0), "Nonfinite point depth accepted");
			Require(!CompareDepth(*linear, 300, 2, invalid), "Nonfinite contact bias accepted");
		}
		auto invalidEncoding = *native;
		invalidEncoding.kind = static_cast<DepthKind>(255);
		Require(!IsValid(invalidEncoding) && !DecodeDepthMetres(invalidEncoding, 0.5), "Unknown depth encoding accepted");
	}
}

int main()
{
	try {
		ChecksRectanglesAndViewDescriptions();
		ReversedBoundsRetainAPositiveViewport();
		ProjectsTheSamePointIntoIndependentEyeRectangles();
		ProjectsAsymmetricTranslatedAndRotatedCameras();
		RejectsSingularCamerasIndependentlyOfWorldScale();
		RejectsProjectionOutsideTheEye();
		DecodesKnownDepthsAndContactBias();
		std::cout << "PASS ImGui VR Helper scene packet geometry and depth\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL ImGui VR Helper scene packet: " << error.what() << '\n';
		return 1;
	}
}
