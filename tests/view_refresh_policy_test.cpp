#define NOMINMAX
#include "Features/VR/ViewRefreshPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace CSX::ViewRefresh;
	using DirectX::SimpleMath::Vector4;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Near(const Matrix& actual, const Matrix& expected)
	{
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				Require(std::abs(actual.m[row][col] - expected.m[row][col]) < 0.0002f, "Incorrect transform");
	}
	void Admission()
	{
		const SceneIdentity source{ 40, 100, 8, 2, 99, true, 77 };
		const SceneIdentity next{ 41, 101, 8, 2, 99, false, 77 };
		Require(CanReuse(source, next, 0.011), "Next complete scene rejected");
		for (int field = 0; field < 7; ++field) {
			auto changed = next;
			switch (field) {
			case 0:
				++changed.frame;
				break;
			case 1:
				++changed.cycle;
				break;
			case 2:
				++changed.generation;
				break;
			case 3:
				++changed.settingsRevision;
				break;
			case 4:
				++changed.thread;
				break;
			case 5:
				--changed.frame;
				break;
			case 6:
				++changed.device;
				break;
			}
			Require(!CanReuse(source, changed, 0.011), "Stale or mismatched source admitted");
		}
		auto incomplete = source;
		incomplete.complete = false;
		Require(!CanReuse(incomplete, next, 0.011), "Partial stereo source admitted");
		for (double age : { -0.01, 0.036, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
			Require(!CanReuse(source, next, age), "Invalid scene age admitted");
		Require(!CanReuse(next, next, 0), "Synthetic source admitted");
		auto presentation = source;
		++presentation.settingsRevision;
		Require(SameFrame(source, presentation), "A toggle interrupted a prepared pair");
		++presentation.generation;
		Require(!SameFrame(source, presentation), "Prepared output crossed resource generations");
		Require(!SameFrame({}, {}), "Empty frame identities matched");
		const Vector3 position(1, 2, 3), angles(0, 0, 0);
		Require(StationaryPlayer(position, angles, position, angles), "Stationary player rejected");
		Require(!StationaryPlayer(position, angles, position + Vector3(1, 0, 0), angles), "Locomotion admitted");
		for (float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			Require(!StationaryPlayer(position, angles, Vector3(invalid, 0, 0), angles), "Invalid player position admitted");
			Require(!StationaryPlayer(position, angles, position, Vector3(0, invalid, 0)), "Invalid player angle admitted");
		}
	}
	void PresentationRegions()
	{
		ImageRegion eye;
		Require(ResolveImageRegion(4000, 2000, 0.5f, 0, 1, 1, eye) && eye.left == 2000 && eye.width == 2000 && eye.height == 2000,
			"Native stereo bounds were not isolated");
		Require(ResolveImageRegion(2000, 2000, 0, 0, 1, 1, eye) && eye.left == 0 && eye.width == 2000,
			"Completed per-eye DLSS/NR image was rejected");
		for (float value : { -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
			Require(!ResolveImageRegion(2000, 2000, value, 0, 1, 1, eye), "Invalid image bounds reached a copy");
		Require(MatchesEyeLayout(4000, 2000, 1, { 2000, 0, 2000, 2000 }), "Right stereo layout rejected");
		Require(!MatchesEyeLayout(4000, 2000, 0, { 2000, 0, 2000, 2000 }), "Wrong stereo half accepted");
		Require(!MatchesEyeLayout(4000, 2000, 1, { 1000, 0, 2000, 2000 }), "Partial eye crop accepted as a full projection");
		Require(!ResolveImageRegion(0, 2000, 0, 0, 1, 1, eye), "Empty texture accepted");
		Require(!ResolveImageRegion(20000, 2000, 0, 0, 1, 1, eye), "Oversize allocation accepted");
		Require(!ResolveImageRegion(2000, 2000, 0, 1, 1, 0, eye), "Inverted eye accepted");
		Require(!ResolveImageRegion(2000, 2000, 0.5f, 0, 0.5f, 1, eye), "Empty region accepted");
	}
	void Cameras()
	{
		const auto source = Matrix::CreateRotationY(0.3f) * Matrix::CreateTranslation(4, 1.7f, -3);
		const auto target = Matrix::CreateRotationY(0.32f) * Matrix::CreateTranslation(4.01f, 1.7f, -3);
		Require(BoundedMotion(source, target), "Small valid motion rejected");
		Require(!BoundedMotion(source, Matrix::CreateTranslation(0, 0, 0)), "Recenter admitted");
		Require(!RigidPose(Matrix::CreateScale(2)), "Scaled tracking matrix admitted");
		Require(!RigidPose(Matrix::CreateScale(1, 1, -1)), "Reflected tracking matrix admitted");
		auto bad = source;
		bad._11 = std::numeric_limits<float>::quiet_NaN();
		Require(!BoundedMotion(bad, target), "NaN tracking admitted");
		for (bool leftHanded : { false, true }) {
			const Matrix projection = leftHanded ?
			                              Matrix(DirectX::XMMatrixPerspectiveOffCenterLH(-0.08f, 0.12f, -0.09f, 0.11f, 0.1f, 1000)) :
			                              Matrix(DirectX::XMMatrixPerspectiveOffCenterRH(-0.08f, 0.12f, -0.09f, 0.11f, 0.1f, 1000));
			for (float ipd : { -0.032f, 0.032f }) {
				const auto eye = Matrix::CreateTranslation(ipd, 0, 0);
				EyeTransform identity;
				Require(BuildEyeTransform(projection, eye, source, source, 70, identity), "Identity stereo camera rejected");
				Near(identity.sourceViewToTargetView, Matrix::Identity);
				Near(identity.targetClipToSourceSkyClip, Matrix::Identity);
				EyeTransform motion;
				Require(BuildEyeTransform(projection, eye, source, target, 70, motion), "Stereo camera rejected");
				auto expected = eye * source * (eye * target).Invert();
				expected._41 *= 70;
				expected._42 *= 70;
				expected._43 *= 70;
				const auto handedness = Matrix::CreateScale(1, 1, leftHanded ? -1.0f : 1.0f);
				Near(motion.sourceViewToTargetView, handedness * expected * handedness);
				const Vector4 point(1, 2, leftHanded ? 20.0f : -20.0f, 1);
				const auto clip = Vector4::Transform(point, projection);
				const auto recovered = Vector4::Transform(clip, motion.sourceClipToView);
				Require((recovered - point).Length() < 0.001f, "Projection reconstruction failed");
			}
		}
		EyeTransform invalid;
		Require(!BuildEyeTransform(Matrix::CreateScale(0), Matrix::Identity, source, target, 70, invalid), "Singular projection admitted");
		Require(!BuildEyeTransform(Matrix::Identity, Matrix::Identity, source, target, 0, invalid), "Invalid world scale admitted");
	}
	void SubmissionReceipts()
	{
		for (uint32_t first : { 0u, 1u }) {
			StereoReceipt pair{ 123 };
			pair.Record(first, 123, true);
			Require(!pair.Complete(123), "One eye accepted as stereo");
			pair.Record(1 - first, 123, true);
			Require(pair.Complete(123), "Successful stereo submission rejected");
			Require(!pair.Complete(124), "Submission crossed compositor cycles");
			pair.Record(first, 123, true);
			Require(!pair.Complete(123), "Duplicate submission accepted");
		}
		for (bool left : { false, true })
			for (bool right : { false, true }) {
				StereoReceipt pair{ 123 };
				pair.Record(0, 123, left);
				pair.Record(1, 123, right);
				Require(pair.Complete(123) == (left && right), "Failed eye accepted");
			}
		StereoReceipt wrongCycle{ 123 };
		wrongCycle.Record(0, 124, true);
		wrongCycle.Record(1, 123, true);
		Require(!wrongCycle.Complete(123), "Mixed-cycle stereo accepted");
		StereoReceipt invalidEye{ 123 };
		invalidEye.Record(2, 123, true);
		invalidEye.Record(0, 123, true);
		invalidEye.Record(1, 123, true);
		Require(!invalidEye.Complete(123), "Invalid eye accepted");
	}
}

int main()
{
	try {
		Admission();
		PresentationRegions();
		Cameras();
		SubmissionReceipts();
		std::cout << "View Refresh cadence, stale inputs, recenter, pose validation, stereo pivots and projection conventions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
