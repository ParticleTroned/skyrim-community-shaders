#define NOMINMAX
#include "Features/VR/OpenVRDetection.h"
#include "Features/VR/WandSurfaceGeometry.h"
#include <SimpleMath.h>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <openvr.h>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace globals
{
	struct State
	{
		bool isMainMenuOpen = false;
	} stateValue;
	State* state = &stateValue;
	namespace features
	{
		struct RenderDoc
		{
			bool active = false;
			bool ShouldBlockUpscaling() const { return active; }
		} renderDoc;
	}
}

struct VR
{
	struct Config
	{
#include "vr_menu_scale_under_test.h"
	};
	struct Settings
	{
#include "vr_menu_path_under_test.h"
		MenuOverlayPath menuOverlayPath = MenuOverlayPath::Auto;
	} settings;
	VRDetection::OpenVRDetectionResult openVRInfo;
	bool ShouldUseInSceneOverlay() const;
};
#include "vr_menu_route_under_test.h"

namespace
{
#include "vr_menu_matrix_conversion_under_test.h"
#include "vr_menu_native_relative_under_test.h"
#include "vr_menu_native_scale_under_test.h"

	bool CoversRoutes(VRDetection::RuntimeType runtime)
	{
		VR vr;
		vr.openVRInfo = { .isCompatible = true, .hasOverlayInterface = true, .runtimeType = runtime };
		// Auto keeps one runtime path through startup, world loads and return to the main menu.
		for (const bool mainMenu : { false, true, true, false, true, false }) {
			globals::stateValue.isMainMenuOpen = mainMenu;
			if (vr.ShouldUseInSceneOverlay() != (runtime == VRDetection::RuntimeType::OpenComposite))
				return false;
		}
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::InScene;
		if (!vr.ShouldUseInSceneOverlay())
			return false;
		globals::features::renderDoc.active = true;
		if (vr.ShouldUseInSceneOverlay() != (runtime != VRDetection::RuntimeType::SteamVR))
			return false;
		globals::features::renderDoc.active = false;
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::IVROverlay;
		if (vr.ShouldUseInSceneOverlay())
			return false;
		vr.settings.menuOverlayPath = VR::Settings::MenuOverlayPath::Auto;
		vr.openVRInfo.hasOverlayInterface = false;
		if (!vr.ShouldUseInSceneOverlay())
			return false;
		vr.openVRInfo.isCompatible = false;
		return !vr.ShouldUseInSceneOverlay();
	}

	unsigned CheckSurface(float scale, bool hmd, float yaw, bool relative)
	{
		const float aspect = hmd ? VR::Config::kHMDOverlayAspect : VR::Config::kOverlayAspect;
		const Vector3 offset(0.26f, -0.04f, -2.25f);
		const Matrix anchor = Matrix::CreateFromYawPitchRoll(yaw, relative ? 0.2f : 0.0f, 0.0f) *
		                      Matrix::CreateTranslation(0.7f, 1.6f, 0.3f);
		const Matrix placement = Matrix::CreateTranslation(offset) * anchor;
		const Matrix sceneScale = hmd ? VR::Config::CreateHMDOverlayScaleMatrix(scale) :
		                                VR::Config::CreateOverlayScaleMatrix(scale);
		const Matrix scene = sceneScale * placement;
		auto fixedTransform = MatrixToHmdMatrix34(placement);
		ScaleOverlayTransform(fixedTransform, scale, scale * aspect);
		const Matrix native = relative ?
		                          HmdMatrix34ToMatrix(CreateControllerOverlayTransform(offset.x, offset.y, offset.z, scale, scale * aspect)) * anchor :
		                          HmdMatrix34ToMatrix(fixedTransform);
		const auto toGeometry = [](const Vector3& v) { return WandSurfaceGeometry::Vector{ v.x, v.y, v.z }; };
		const Vector3 topLeft = Vector3::Transform(Vector3(-0.5f, 0.5f, 0.0f), scene);
		const WandSurfaceGeometry::Surface surface{ toGeometry(topLeft),
			toGeometry(Vector3::Transform(Vector3(0.5f, 0.5f, 0.0f), scene) - topLeft),
			toGeometry(Vector3::Transform(Vector3(-0.5f, -0.5f, 0.0f), scene) - topLeft) };
		unsigned failures = 0;
		Vector3 normal = placement.Backward();
		normal.Normalize();
		for (const float u : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
			for (const float v : { 0.0f, 0.125f, 0.5f, 0.875f, 1.0f }) {
				// IVROverlay applies width and texture aspect before the supplied transform.
				const Vector3 expected = Vector3::Transform(Vector3((u - 0.5f) * scale, (0.5f - v) * scale * aspect, 0.0f), native);
				const Vector3 actual = Vector3::Transform(Vector3(u - 0.5f, 0.5f - v, 0.0f), scene);
				if ((actual - expected).Length() > 1e-5f)
					++failures;
				WandSurfaceGeometry::Hit hit;
				if (u > 0.0f && u < 1.0f && v > 0.0f && v < 1.0f &&
					(!WandSurfaceGeometry::TryIntersect(surface, toGeometry(expected + normal), toGeometry(-normal), hit) ||
						std::abs(hit.u - u) > 1e-5f || std::abs(hit.v - v) > 1e-5f))
					++failures;
			}
		}
		return failures;
	}
}

int main()
{
	unsigned failures = 0;
	unsigned surfaces = 0;
	for (const auto runtime : { VRDetection::RuntimeType::SteamVR, VRDetection::RuntimeType::OpenComposite, VRDetection::RuntimeType::Unknown }) {
		if (!CoversRoutes(runtime)) {
			std::cout << "route mismatch for runtime=" << static_cast<int>(runtime) << '\n';
			++failures;
		}
	}
	for (const bool hmd : { true, false }) {
		unsigned surfaceFailures = 0;
		for (const float scale : { 0.5f, 1.0f, 1.5f, 2.0f }) {
			for (const float yaw : { -0.7f, 0.0f, 1.2f }) {
				for (const bool relative : { false, true }) {
					surfaceFailures += CheckSurface(scale, hmd, yaw, relative);
					++surfaces;
				}
			}
		}
		failures += surfaceFailures;
		std::cout << "hmd=" << hmd << " mismatches=" << surfaceFailures << '\n';
	}
	std::cout << surfaces << " production surface transforms; " << failures << " failures\n";
	return failures ? 1 : 0;
}
