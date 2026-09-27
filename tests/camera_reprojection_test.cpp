#define NOMINMAX
#include "Features/Upscaling/CameraReprojection.h"
#include "Features/Upscaling/VRSubmitTemporalSnapshot.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace
{
	using DirectX::SimpleMath::Matrix;
	using DirectX::SimpleMath::Vector3;
	using DirectX::SimpleMath::Vector4;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void RequireVectorNear(const Vector4& actual, const Vector4& expected)
	{
		Check(std::abs(actual.x - expected.x) <= 0.001f && std::abs(actual.y - expected.y) <= 0.001f &&
				  std::abs(actual.z - expected.z) <= 0.001f && std::abs(actual.w - expected.w) <= 0.001f,
			"projected point differs from the expected camera space");
	}

	void RequireMatrixNear(const Matrix& actual, const Matrix& expected)
	{
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				Check(std::abs(actual.m[row][column] - expected.m[row][column]) <= 0.00001f,
					"camera transform differs from the expected matrix");
	}

	void CapturedAEProjection()
	{
		const Matrix capturedViewInverse(
			-0.1959844083f, -0.1209190413f, -0.9731232524f, 0.0f,
			0.9806070924f, -0.0241669156f, -0.1944886744f, 0.0f,
			0.0f, 0.9923682213f, -0.1233103946f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		const Matrix capturedViewProjection(
			-0.1795865744f, 0.8985607624f, 0.0f, 0.0f,
			-0.1969811171f, -0.0393687002f, 1.6166006327f, 0.0f,
			-0.9731644392f, -0.1944968998f, -0.1233156174f, -15.0006361008f,
			-0.9731231332f, -0.1944886446f, -0.1233103871f, 0.0f);
		const auto matrices = UpscalingCamera::BuildReprojection(
			capturedViewInverse.Transpose(), capturedViewProjection.Transpose(), capturedViewProjection.Transpose(), Vector3::Zero);
		const Vector4 viewPoint(30.0f, 20.0f, 100.0f, 1.0f);
		const auto clip = Vector4::Transform(viewPoint, matrices.cameraViewToClip);
		RequireVectorNear(clip, Vector4(27.4899352f, 32.5806642f, 85.00360775f, 100.0f));
		RequireVectorNear(Vector4::Transform(clip, matrices.clipToCameraView), viewPoint);
		RequireMatrixNear(matrices.clipToPrevClip, Matrix::Identity);
	}

	void MovementAndProjectionChanges()
	{
		const Matrix projection(DirectX::XMMatrixPerspectiveOffCenterLH(-10.0f, 13.0f, -8.0f, 9.0f, 15.0f, 370000.0f));
		const Matrix previousProjection(DirectX::XMMatrixPerspectiveOffCenterLH(-11.0f, 12.0f, -9.0f, 8.0f, 15.0f, 370000.0f));
		const auto view = Matrix::CreateRotationY(0.1f) * Matrix::CreateRotationX(-0.2f);
		const auto previousView = Matrix::CreateRotationY(0.08f) * Matrix::CreateRotationX(-0.18f);
		const Vector3 originDelta(4.0f, -3.0f, 2.0f);
		const auto matrices = UpscalingCamera::BuildReprojection(view.Invert(), view * projection, previousView * previousProjection, originDelta);
		RequireMatrixNear(matrices.cameraViewToClip, projection);
		for (const Vector4 point : { Vector4(10.0f, 5.0f, 100.0f, 1.0f), Vector4(-30.0f, 20.0f, 500.0f, 1.0f) }) {
			const auto currentClip = Vector4::Transform(Vector4::Transform(point, view), projection);
			const auto previousPoint = point + Vector4(originDelta.x, originDelta.y, originDelta.z, 0.0f);
			const auto previousClip = Vector4::Transform(Vector4::Transform(previousPoint, previousView), previousProjection);
			RequireVectorNear(Vector4::Transform(currentClip, matrices.clipToPrevClip), previousClip);
			RequireVectorNear(Vector4::Transform(previousClip, matrices.prevClipToClip), currentClip);
		}
	}

	struct EyeCamera
	{
		Matrix viewInverse;
		Matrix viewProjectionUnjittered;
		Matrix previousViewProjectionUnjittered;
		Vector3 position;
		Vector3 previousPosition;
	};

	UpscalingCamera::Reprojection Reproject(const EyeCamera& eye)
	{
		return UpscalingCamera::BuildReprojection(
			eye.viewInverse, eye.viewProjectionUnjittered, eye.previousViewProjectionUnjittered, eye.position - eye.previousPosition);
	}

	void FrozenStereoHistory()
	{
		const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(1.2f, 16.0f / 9.0f, 15.0f, 370000.0f));
		const auto view = Matrix::CreateRotationY(0.2f);
		const auto previousView = Matrix::CreateRotationY(0.15f);
		const auto otherEyeView = Matrix::CreateTranslation(-3.2f, 0.0f, 0.0f) * view;
		std::array<EyeCamera, 2> cameras{
			EyeCamera{ view.Invert(), view * projection, previousView * projection,
				Vector3(25005.0f, 0.0f, 0.0f), Vector3(25000.0f, 0.0f, 0.0f) },
			EyeCamera{ otherEyeView.Invert(), otherEyeView * projection, previousView * projection,
				Vector3(25011.0f, 2.0f, 0.0f), Vector3(25006.0f, 1.0f, 0.0f) }
		};
		VRSubmitTemporalSnapshot::Snapshot<EyeCamera> snapshot;
		const VRSubmitTemporalSnapshot::Key key{ 42, 7, 3, 1200, 1300, 1800, 1950 };
		const VRSubmitTemporalSnapshot::Scalars scalars{ 0.25f, -0.375f, 15.0f, 370000.0f, 1.2f, 11.1f, false };
		Check(snapshot.Publish(key, scalars, cameras), "valid stereo snapshot was rejected");
		const auto left = Reproject(snapshot.eyes[0]);
		const auto right = Reproject(snapshot.eyes[1]);
		RequireMatrixNear(left.cameraViewToClip, projection);
		RequireMatrixNear(right.cameraViewToClip, projection);
		const Vector4 point(10.0f, 5.0f, 100.0f, 1.0f);
		const auto rightClip = Vector4::Transform(point, otherEyeView * projection);
		const auto previousRightPoint = point + Vector4(5.0f, 1.0f, 0.0f, 0.0f);
		RequireVectorNear(Vector4::Transform(rightClip, right.clipToPrevClip),
			Vector4::Transform(previousRightPoint, previousView * projection));

		cameras[0].position = Vector3::Zero;
		cameras[0].previousViewProjectionUnjittered = Matrix::Identity;
		Check(snapshot.Publish(key, scalars, cameras), "repeat producer invalidated matching history");
		const auto repeatedLeft = Reproject(snapshot.eyes[0]);
		RequireMatrixNear(repeatedLeft.clipToPrevClip, left.clipToPrevClip);
		RequireMatrixNear(repeatedLeft.prevClipToClip, left.prevClipToClip);
	}

	void RepairedHistory()
	{
		const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(1.2f, 16.0f / 9.0f, 15.0f, 370000.0f));
		const auto view = Matrix::CreateRotationY(0.2f);
		EyeCamera eye{ view.Invert(), view * projection, Matrix::Identity,
			Vector3(1000.0f, 500.0f, 2000.0f), Vector3::Zero };
		Check(VRSubmitTemporalSnapshot::PrepareCameraHistoryForPublication(eye, true, true, false),
			"reset frame could not seed camera history");
		const auto reset = Reproject(eye);
		RequireMatrixNear(reset.clipToPrevClip, Matrix::Identity);
		RequireMatrixNear(reset.prevClipToClip, Matrix::Identity);

		const auto retained = eye;
		eye.position.x += 4.0f;
		eye.previousPosition = Vector3::Zero;
		eye.previousViewProjectionUnjittered = Matrix::Identity;
		Check(VRSubmitTemporalSnapshot::PrepareCameraHistoryForPublication(eye, false, true, false, &retained),
			"adjacent retained history was rejected");
		const auto repaired = Reproject(eye);
		const Vector4 point(10.0f, 5.0f, 100.0f, 1.0f);
		const auto clip = Vector4::Transform(point, eye.viewProjectionUnjittered);
		RequireVectorNear(Vector4::Transform(clip, repaired.clipToPrevClip),
			Vector4::Transform(point + Vector4(4.0f, 0.0f, 0.0f, 0.0f), retained.viewProjectionUnjittered));
	}
}

int main()
{
	try {
		CapturedAEProjection();
		MovementAndProjectionChanges();
		FrozenStereoHistory();
		RepairedHistory();
		std::cout << "Camera reprojection checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
