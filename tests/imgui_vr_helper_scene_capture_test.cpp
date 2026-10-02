#include "Features/VR/ImGuiVRHelperScenePacket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	namespace Scene = ImGuiVRHelperScenePacket;
	constexpr double kGameUnitToMetres = 0.01428;

	// Only engine storage is replaced; camera construction below is extracted from production.
	struct Matrix
	{
		float m[4][4]{};
	};
	struct Vector
	{
		float x = 0, y = 0, z = 0;
	};
	struct Room
	{
		Vector translate;
		struct Rotation
		{
			float entry[3][3]{};
		} rotate;
		float scale = 1;
	};
	struct FrameBuffer
	{
		Matrix CameraView[2];
		Matrix CameraProj[2];
		Matrix CameraViewProj[2];
		Matrix CameraViewProjUnjittered[2];
		Matrix CameraViewInverse[2];
		Vector CameraPosAdjust[2];
	};
	struct CameraSource
	{
		FrameBuffer buffer;
		Room room;
		std::array<double, 2> activeSize{};
		double nearPlane = 0, farPlane = 0;
		bool temporalResolved = false, valid = false;
	} sourceCamera;
	struct EyeCamera
	{
		Scene::PixelRect depthRect;
		Scene::Matrix4x4 trackingToNativeColorClip;
		Scene::Matrix4x4 trackingToReconstructedColorClip;
		Scene::Matrix4x4 trackingToDepthClip;
		std::array<double, 4> trackingToDepthViewMetres;
		double nearPlane = 0, farPlane = 0, sourceUnitsToMetres = 0;
	};
	struct Snapshot
	{
		std::array<EyeCamera, 2> eyes;
		std::array<double, 3> worldOrigin;
		Scene::Matrix4x4 worldToTracking;
		Scene::Matrix4x4 headToTracking;
		bool nativeTemporalResolved = false;
	};

#include "imgui_vr_helper_scene_capture_under_test.h"

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void Near(double a_actual, double a_expected, const char* a_message, double a_tolerance = 0.000002)
	{
		Require(Scene::IsValid(std::array<double, 1>{ a_actual }) && std::abs(a_actual - a_expected) <= a_tolerance, a_message);
	}

	Matrix StoredMatrix(const std::array<double, 16>& a_rows)
	{
		Matrix result;
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				result.m[column][row] = static_cast<float>(a_rows[row * 4 + column]);
		return result;
	}

	std::array<double, 4> Transform(const Scene::Matrix4x4& a_matrix, const std::array<double, 3>& a_point)
	{
		std::array<double, 4> result{};
		for (std::size_t column = 0; column < 4; ++column)
			result[column] = a_point[0] * a_matrix[column] + a_point[1] * a_matrix[4 + column] +
			                 a_point[2] * a_matrix[8 + column] + a_matrix[12 + column];
		return result;
	}

	void SetCamera(bool a_rotateRoom, double a_scale, double a_origin = 0.0)
	{
		sourceCamera = {};
		sourceCamera.valid = true;
		sourceCamera.activeSize = { 1600, 600 };
		sourceCamera.nearPlane = 5;
		sourceCamera.farPlane = 1000;
		sourceCamera.room.scale = static_cast<float>(a_scale);
		sourceCamera.room.translate = { static_cast<float>(a_origin), static_cast<float>(a_origin), static_cast<float>(a_origin) };
		const float rotation[3][3] = { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
				sourceCamera.room.rotate.entry[row][column] = a_rotateRoom ? rotation[row][column] : (row == column ? 1.0f : 0.0f);
		const double offset = 1000.0 / 995.0;
		for (std::size_t eye = 0; eye < 2; ++eye) {
			const double asymmetricX = eye == 0 ? 0.12 : -0.08;
			const double asymmetricY = 0.04;
			const double jitterX = 0.006, jitterY = -0.004;
			auto& buffer = sourceCamera.buffer;
			buffer.CameraPosAdjust[eye] = {
				static_cast<float>(a_origin + (eye == 0 ? -2.0 : 2.0)),
				static_cast<float>(a_origin), static_cast<float>(a_origin + 100.0)
			};
			buffer.CameraView[eye] = StoredMatrix({ 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1 });
			buffer.CameraViewInverse[eye] = buffer.CameraView[eye];
			buffer.CameraProj[eye] = StoredMatrix({ 2, 0, 0, 0, 0, 3, 0, 0, asymmetricX + jitterX, asymmetricY + jitterY, offset, 1, 0, 0, -5 * offset, 0 });
			buffer.CameraViewProj[eye] = StoredMatrix({ 2, 0, 0, 0, asymmetricX + jitterX, asymmetricY + jitterY, offset, 1, 0, 3, 0, 0, 0, 0, -5 * offset, 0 });
			buffer.CameraViewProjUnjittered[eye] = StoredMatrix({ 2, 0, 0, 0, asymmetricX, asymmetricY, offset, 1, 0, 3, 0, 0, 0, 0, -5 * offset, 0 });
		}
	}

	void CapturesIndependentEyesAndJitter(bool a_rotatedRoom, double a_roomScale)
	{
		SetCamera(a_rotatedRoom, a_roomScale);
		sourceCamera.temporalResolved = true;
		Snapshot snapshot;
		Require(CaptureCamera(snapshot, { 2000, 800 }), "Valid source camera rejected");
		Require(snapshot.nativeTemporalResolved, "Temporal route was lost");
		const double units = kGameUnitToMetres / a_roomScale;
		// The source world point is (10,300,110); room yaw changes its tracking axes, not its projected scene position.
		const std::array<double, 3> tracking = a_rotatedRoom ?
		                                           std::array<double, 3>{ 300 * units, 110 * units, 10 * units } :
		                                           std::array<double, 3>{ 10 * units, 110 * units, -300 * units };
		const auto transformedWorld = Transform(snapshot.worldToTracking, { 10, 300, 110 });
		for (std::size_t axis = 0; axis < 3; ++axis)
			Near(transformedWorld[axis], tracking[axis], "Room origin-relative tracking transform is wrong");
		for (std::size_t eye = 0; eye < 2; ++eye) {
			const auto& captured = snapshot.eyes[eye];
			const auto depth = Transform(captured.trackingToDepthClip, tracking);
			const auto color = Transform(captured.trackingToReconstructedColorClip, tracking);
			const double eyeX = eye == 0 ? -2 : 2;
			const double asymmetricX = eye == 0 ? 0.12 : -0.08;
			Near(depth[0] / depth[3], 2 * (10 - eyeX) / 300 + asymmetricX + 0.006, "Depth projection has wrong eye or jitter");
			Near(depth[1] / depth[3], 0.136, "Depth projection vertical orientation is wrong");
			Near(color[0] / color[3], 2 * (10 - eyeX) / 300 + asymmetricX, "Reconstruction camera retained jitter");
			Near(color[1] / color[3], 0.14, "Reconstruction camera vertical projection is wrong");
			const auto& axial = captured.trackingToDepthViewMetres;
			const double pointMetres = tracking[0] * axial[0] + tracking[1] * axial[1] + tracking[2] * axial[2] + axial[3];
			Near(pointMetres, 300 * units, "Depth axial row lost world scale");
			const auto encoding = Scene::MakeNativeForwardZ(captured.nearPlane, captured.farPlane, captured.sourceUnitsToMetres);
			const auto decoded = Scene::DecodeDepthMetres(*encoding, depth[2] / depth[3]);
			Require(decoded.has_value(), "Captured projection cannot be decoded");
			Near(*decoded, pointMetres, "Projection and native depth encoding disagree", 0.00002);
			Require(captured.depthRect.x == eye * 800 && captured.depthRect.width == 800 && captured.depthRect.height == 600,
				"Active eye rectangle used the allocation extent instead of producer dimensions");
			Require(captured.trackingToNativeColorClip == captured.trackingToDepthClip, "Raw color camera differs from source depth");
		}
		Near(snapshot.headToTracking[12], 0, "Head midpoint is not eye-shared");
		Near(snapshot.headToTracking[13], 100 * units, "Head height has wrong world scale");
		Near(snapshot.headToTracking[14], 0, "Head forward origin drifted");
	}

	void PreservesLargeOrigins()
	{
		constexpr double origin = 16777216.0;
		SetCamera(false, 1.0, origin);
		Snapshot snapshot;
		Require(CaptureCamera(snapshot, { 2000, 800 }), "Large exterior origin rejected");
		Near(snapshot.worldOrigin[0], origin, "World origin was not retained");
		const auto point = Transform(snapshot.worldToTracking, { (origin + 0.25) - snapshot.worldOrigin[0], 0, 0 });
		Near(point[0], 0.25 * kGameUnitToMetres, "Origin subtraction lost sub-unit precision", 1e-12);
		Near(snapshot.worldToTracking[12], 0, "Absolute origin leaked into origin-relative matrix");
		const std::array<double, 3> tracking{ 10 * kGameUnitToMetres, 110 * kGameUnitToMetres, -300 * kGameUnitToMetres };
		const auto clip = Transform(snapshot.eyes[0].trackingToDepthClip, tracking);
		Near(clip[0] / clip[3], 2 * 12.0 / 300 + 0.126, "Large camera and room origins did not cancel");
	}

	void UsesRenderedProjectionInsteadOfRedundantProjection()
	{
		SetCamera(false, 1.0);
		Snapshot expected;
		Require(CaptureCamera(expected, { 2000, 800 }), "Reference camera rejected");
		for (auto& projection : sourceCamera.buffer.CameraProj) {
			projection.m[0][0] *= 0.8f;
			projection.m[1][1] *= 1.1f;
			projection.m[0][2] += 0.03f;
		}
		Snapshot actual;
		Require(CaptureCamera(actual, { 2000, 800 }), "Redundant projection rejected the rendered camera");
		for (std::size_t eye = 0; eye < 2; ++eye) {
			Require(actual.eyes[eye].trackingToDepthClip == expected.eyes[eye].trackingToDepthClip,
				"Redundant projection changed the opaque depth camera");
			Require(actual.eyes[eye].trackingToReconstructedColorClip == expected.eyes[eye].trackingToReconstructedColorClip,
				"Redundant projection changed the resolved color camera");
			Require(actual.eyes[eye].trackingToDepthViewMetres == expected.eyes[eye].trackingToDepthViewMetres,
				"Redundant projection changed axial depth");
		}
	}

	void AcceptsCapturedEngineRotationPrecision()
	{
		SetCamera(false, 1.0);
		sourceCamera.nearPlane = 15;
		sourceCamera.farPlane = 350000;
		// The captured AE matrices are shared with CameraReprojection; they exercise engine float rounding.
		const Scene::Matrix4x4 inverseView{
			-0.1959844083, 0.9806070924, 0.0, 0.0,
			-0.1209190413, -0.0241669156, 0.9923682213, 0.0,
			-0.9731232524, -0.1944886744, -0.1233103946, 0.0,
			0.0, 0.0, 0.0, 1.0
		};
		const Scene::Matrix4x4 viewProjection{
			-0.1795865744, -0.1969811171, -0.9731644392, -0.9731231332,
			0.8985607624, -0.0393687002, -0.1944968998, -0.1944886446,
			0.0, 1.6166006327, -0.1233156174, -0.1233103871,
			0.0, 0.0, -15.0006361008, 0.0
		};
		Scene::Matrix4x4 view{};
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				view[row * 4 + column] = inverseView[column * 4 + row];
		for (std::size_t eye = 0; eye < 2; ++eye) {
			sourceCamera.buffer.CameraView[eye] = StoredMatrix(view);
			sourceCamera.buffer.CameraViewInverse[eye] = StoredMatrix(inverseView);
			sourceCamera.buffer.CameraViewProj[eye] = StoredMatrix(viewProjection);
			sourceCamera.buffer.CameraViewProjUnjittered[eye] = StoredMatrix(viewProjection);
		}
		Snapshot snapshot;
		Require(CaptureCamera(snapshot, { 2000, 800 }), "Captured engine matrix precision rejected");
		const auto relative = Transform(inverseView, { 30, 20, 100 });
		for (std::size_t eye = 0; eye < 2; ++eye) {
			const auto& origin = sourceCamera.buffer.CameraPosAdjust[eye];
			const auto tracking = Transform(snapshot.worldToTracking,
				{ relative[0] + origin.x, relative[1] + origin.y, relative[2] + origin.z });
			const auto clip = Transform(snapshot.eyes[eye].trackingToDepthClip, { tracking[0], tracking[1], tracking[2] });
			Near(clip[0], 27.4899352, "Captured camera horizontal projection changed", 0.00002);
			Near(clip[1], 32.5806642, "Captured camera vertical projection changed", 0.00002);
			Near(clip[2], 85.00360775, "Captured camera native depth changed", 0.00002);
			Near(clip[3], 100, "Captured camera homogeneous depth changed", 0.00002);
		}
	}

	void RejectsMalformedProducerMetadata()
	{
		for (int invalid = 0; invalid < 14; ++invalid) {
			SetCamera(false, 1.0);
			switch (invalid) {
			case 0:
				sourceCamera.valid = false;
				break;
			case 1:
				sourceCamera.room.scale = 0;
				break;
			case 2:
				sourceCamera.room.scale = std::numeric_limits<float>::quiet_NaN();
				break;
			case 3:
				sourceCamera.room.rotate.entry[0][0] = 2;
				break;
			case 4:
				sourceCamera.nearPlane = sourceCamera.farPlane;
				break;
			case 5:
				sourceCamera.activeSize[0] = 2001;
				break;
			case 6:
				sourceCamera.activeSize[1] = std::numeric_limits<double>::infinity();
				break;
			case 7:
				sourceCamera.buffer.CameraViewProj[1] = {};
				break;
			case 8:
				sourceCamera.buffer.CameraViewProjUnjittered[0] = {};
				break;
			case 9:
				sourceCamera.buffer.CameraViewProj[0].m[2][3] *= 1.5f;
				break;
			case 10:
				sourceCamera.buffer.CameraViewInverse[0].m[0][0] = -1;
				break;
			case 11:
				sourceCamera.buffer.CameraPosAdjust[1].z = std::numeric_limits<float>::infinity();
				break;
			case 12:
				sourceCamera.activeSize[0] = 1;
				break;
			case 13:
				sourceCamera.buffer.CameraViewProj[0].m[3][3] = 1;
				break;
			}
			Snapshot snapshot;
			Require(!CaptureCamera(snapshot, { 2000, 800 }), "Malformed producer camera was admitted");
		}
	}
}

int main()
{
	try {
		CapturesIndependentEyesAndJitter(false, 1.0);
		CapturesIndependentEyesAndJitter(true, 2.0);
		CapturesIndependentEyesAndJitter(true, 0.5);
		PreservesLargeOrigins();
		UsesRenderedProjectionInsteadOfRedundantProjection();
		AcceptsCapturedEngineRotationPrecision();
		RejectsMalformedProducerMetadata();
		std::cout << "PASS ImGui VR Helper production scene-camera capture\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL ImGui VR Helper production scene-camera capture: " << error.what() << '\n';
		return 1;
	}
}
