#include "Features/Upscaling/FoveatedMaskCalibrationJson.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>

namespace
{
	using namespace FoveatedMaskCalibration;
	void Require(bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	void CheckInheritedBoundary()
	{
		struct Settings
		{
			float foveatedCenterArea = 0.6f, foveatedOuterBlendFeather = 0.05f;
			float periphery_taa_center_area = 0.3f, periphery_taa_outer_scale = 0.8f, periphery_taa_center_blend_feather = 0.02f;
			bool periphery_taa_enable = true;
			Reference foveatedCalibrationReference;
		} settings;
		InheritFovOnlyBoundary(settings);
		Require(!settings.periphery_taa_enable, "Uncalibrated saved TAA must fall back to FOV only");
		for (const float outer : { 0.25f, 0.6f, 0.85f, 0.95f }) {
			for (const float feather : { 0.0f, 0.05f, 0.1f }) {
				settings.foveatedCenterArea = outer;
				settings.foveatedOuterBlendFeather = feather;
				settings.foveatedCalibrationReference = { .version = 1,
					.outer = { .scale = outer + 2.0f * std::max(feather, FoveatedCommon::kMinimumFeather) },
					.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 },
					.feather = std::max(feather, FoveatedCommon::kMinimumFeather) };
				const auto reference = settings.foveatedCalibrationReference;
				Require(HasFovOnlySetup(settings), "Valid FOV-only calibration must permit TAA");
				for (const float inner : { 0.25f, 0.3f, 0.6f, 1.0f }) {
					settings.periphery_taa_enable = true;
					settings.periphery_taa_center_area = inner;
					InheritFovOnlyBoundary(settings);
					Require(settings.periphery_taa_enable && settings.foveatedCalibrationReference == reference &&
								settings.foveatedCenterArea == outer && settings.foveatedOuterBlendFeather == feather,
						"Mode and inner changes must preserve FOV-only geometry and its reference");
					Require(settings.periphery_taa_center_area <= outer && settings.periphery_taa_center_area >= MinimumInnerScale(settings),
						"The inner area must stay bounded by inherited feather support");
					Require(settings.periphery_taa_center_blend_feather == feather, "TAA must inherit FOV-only feathering");
					for (const float horizontal : { 1.0f, 1.8f }) {
						for (const float offset : { -0.3f, 0.0f, 0.3f }) {
							const auto original = FoveatedMaskVisualization::MeasureCoverage(outer, feather, horizontal, offset, 0.04f, false, 1.0f);
							const auto inherited = FoveatedMaskVisualization::MeasureCoverage(settings.periphery_taa_center_area,
								settings.periphery_taa_center_blend_feather, horizontal, offset, 0.04f, true, settings.periphery_taa_outer_scale);
							Require(std::abs(original.totalPercent - inherited.totalPercent) < 0.0001f,
								"Inherited outer coverage must remain equal across centre sizes, clipping and wide feather support");
						}
					}
				}
			}
		}
		settings.foveatedCalibrationReference.peripheryTaa = true;
		Require(!HasFovOnlySetup(settings), "A legacy TAA reference does not prove FOV-only setup");
		settings.foveatedCalibrationReference.peripheryTaa = false;
		settings.foveatedCalibrationReference.fullImage = true;
		Require(!HasFovOnlySetup(settings), "Full-eye calibration has no bounded TAA periphery");
		settings.foveatedCalibrationReference.fullImage = false;
		settings.foveatedCalibrationReference.leftToRight = {};
		Require(!HasFovOnlySetup(settings), "Corrupt saved projections must never enable TAA");
		settings.periphery_taa_center_area = std::numeric_limits<float>::quiet_NaN();
		InheritFovOnlyBoundary(settings);
		Require(!settings.periphery_taa_enable && std::isfinite(settings.periphery_taa_center_area), "Corrupt saved inner values must sanitize safely");
	}

	void CheckPersistence(const Reference& reference)
	{
		const nlohmann::json saved = reference;
		Require(nlohmann::json::parse(saved.dump()).get<Reference>() == reference, "Saved outer reference must round-trip exactly");
		Require(nlohmann::json(Reference{}).get<Reference>() == Reference{}, "Uncalibrated defaults must round-trip");
		std::vector<nlohmann::json> corrupt;
		for (auto version : { nlohmann::json(-1), nlohmann::json(2), nlohmann::json(1.5), nlohmann::json(4294967297ULL) }) {
			auto changed = saved;
			changed["version"] = version;
			corrupt.push_back(changed);
		}
		for (const char* key : { "outer", "leftToRight", "feather", "peripheryTaa" }) {
			auto changed = saved;
			changed.erase(key);
			corrupt.push_back(changed);
		}
		auto changed = saved;
		changed["outer"]["centers"].push_back(0.5);
		corrupt.push_back(changed);
		changed = saved;
		changed["leftToRight"] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		corrupt.push_back(changed);
		changed = saved;
		changed["outer"]["scale"] = 99;
		corrupt.push_back(changed);
		for (const char* key : { "scale", "horizontalScale" }) {
			changed = saved;
			changed["outer"][key] = true;
			corrupt.push_back(changed);
		}
		for (const char* key : { "centerScale", "feather" }) {
			changed = saved;
			changed[key] = key == std::string_view("centerScale");
			corrupt.push_back(changed);
		}
		changed = saved;
		changed["leftToRight"][0] = true;
		corrupt.push_back(changed);
		for (const auto& json : corrupt) {
			auto retained = reference;
			bool rejected = false;
			try {
				json.get_to(retained);
			} catch (const std::exception&) {
				rejected = true;
			}
			Require(rejected && retained == reference, "Invalid persisted calibration must fail without replacing the reference");
		}
	}

	void CheckClipProjection()
	{
		struct ClipMatrix
		{
			double _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44;
		};
		const ClipMatrix matrix{ .93, .04, .03, .1, -.02, 1.03, -.04, .015, 0, 0, 1, 0, .08, .01, .02, 1 };
		const std::array<std::array<double, 4>, 4> rows{ std::array{ .93, .04, .03, .1 }, { -.02, 1.03, -.04, .015 }, { 0, 0, 1, 0 }, { .08, .01, .02, 1 } };
		const auto projection = FromClipProjection(matrix);
		for (Point uv : { Point{ 0, 0 }, Point{ 1, 0 }, Point{ 0, 1 }, Point{ 1, 1 }, Point{ .37, .61 } }) {
			const std::array clip{ uv.x * 2 - 1, 1 - uv.y * 2, .5, 1.0 };
			std::array<double, 4> mapped{};
			for (unsigned row = 0; row < 4; ++row)
				for (unsigned col = 0; col < 4; ++col)
					mapped[row] += rows[row][col] * clip[col];
			const auto actual = Project(projection, uv);
			Require(actual && std::abs(actual->x - (mapped[0] / mapped[3] * .5 + .5)) < 1e-12 &&
						std::abs(actual->y - (.5 - mapped[1] / mapped[3] * .5)) < 1e-12,
				"Calibration UV projection must match the shader clip projection");
			const auto restored = Project(*Inverse(projection), *actual);
			Require(restored && std::abs(restored->x - uv.x) < 1e-12 && std::abs(restored->y - uv.y) < 1e-12,
				"Both eye mappings must agree");
		}
	}

	void CheckReferenceCompatibility()
	{
		const Projection saved{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		auto moved = saved;
		moved[2] = -0.001;
		Require(!MatchesProjection(saved, moved), "Projection drift larger than the fit margin must require recalibration");
		moved[2] = 1e-7;
		Require(MatchesProjection(saved, moved), "Numerical camera noise inside the coverage margin must be tolerated");
		for (double& value : moved)
			value *= 2;
		Require(MatchesProjection(saved, moved), "Equivalent homogeneous matrix scales must not invalidate calibration");
		moved = saved;
		moved[8] = 0;
		Require(!MatchesProjection(saved, moved), "An invalid live projection must fail closed");
		const Projection compressed{ .01, 0, .495, 0, 1, 0, 0, 0, 1 };
		auto compressedMoved = compressed;
		compressedMoved[2] += 1e-7;
		Require(!MatchesProjection(compressed, compressedMoved),
			"Projection drift must stay within the fit margin in both eye mappings");
		const Reference reference{ .version = 1, .outer = { .scale = .6f }, .leftToRight = saved, .peripheryTaa = true };
		Require(MatchesProfile(reference, true, .3f, .05f), "Unchanged TAA controls must retain calibration");
		Require(!MatchesProfile(reference, false, .3f, .05f) && !MatchesProfile(reference, true, .4f, .05f) &&
					!MatchesProfile(reference, true, .3f, .06f),
			"Mode, centre and transition edits must invalidate TAA calibration");
	}

	bool Covered(const Reference& reference, const Solution& solution, unsigned eye, Point point)
	{
		if (solution.fullImage)
			return true;
		const auto& g = solution.geometry;
		const float center = reference.peripheryTaa ? reference.centerScale : g.scale;
		const double support = reference.peripheryTaa ? std::max(g.scale, center + 2.0f * std::max(reference.feather, 1e-4f)) : center + 2.0f * std::max(reference.feather, 1e-4f);
		const double x = (point.x - g.centers[eye * 2]) / (support * g.horizontalScale * 0.5);
		const double y = (point.y - g.centers[eye * 2 + 1]) / (support * 0.5);
		return x * x * x * x + y * y * y * y <= 1.00001;
	}

	Solution CheckFit(const Reference& reference, float percent)
	{
		const auto solution = Solve(reference, percent);
		Require(solution.has_value(), "A valid FOV reference must have a safe result");
		const auto targets = BuildTargets(reference, percent);
		Require(targets.has_value(), "Targets must exist for a valid solution");
		for (unsigned eye = 0; eye < 2; ++eye) {
			const auto& polygon = (*targets)[eye];
			for (size_t i = 0; i < polygon.size(); ++i) {
				const Point start = polygon[i], end = polygon[(i + 1) % polygon.size()];
				for (unsigned sample = 0; sample <= 64; ++sample) {
					const double t = sample / 64.0;
					Require(Covered(reference, *solution, eye, { start.x + t * (end.x - start.x), start.y + t * (end.y - start.y) }),
						"All outer contour and central envelope segments must be covered in both eyes");
				}
			}
			const double centerScale = reference.peripheryTaa ? reference.centerScale : solution->geometry.scale;
			const double expansion = centerScale * 0.5 * (solution->geometry.horizontalScale - 1.0) * (eye == 0 ? -1.0 : 1.0);
			const double manualX = solution->geometry.centers[eye * 2] - 0.5 - expansion;
			const float appliedX = 0.5f + FoveatedCommon::ResolveMaskOffsetX(static_cast<float>(manualX),
											  static_cast<float>(centerScale), solution->geometry.horizontalScale, eye != 0, true);
			Require(std::abs(appliedX - solution->geometry.centers[eye * 2]) < 1e-6,
				"A fitted centre must survive the live slider offset and saturation rules");
			Require(std::abs(manualX) <= 0.300001 && std::abs(solution->geometry.centers[eye * 2 + 1] - 0.5) <= 0.300001,
				"Fit must round-trip through existing manual offset limits");
		}
		if (percent == 100.0f && !reference.fullImage) {
			const auto inverse = *Inverse(reference.leftToRight);
			for (unsigned eye = 0; eye < 2; ++eye) {
				for (unsigned i = 0; i <= 512; ++i) {
					const double angle = -1.5707963267948966 + i * 3.141592653589793 / 512.0;
					const double cx = reference.outer.centers[eye * 2], cy = reference.outer.centers[eye * 2 + 1];
					const Point point{ cx + (eye == 0 ? -1 : 1) * reference.outer.scale * reference.outer.horizontalScale * 0.5 * std::sqrt(std::abs(std::cos(angle))),
						cy + std::copysign(reference.outer.scale * 0.5 * std::sqrt(std::abs(std::sin(angle))), std::sin(angle)) };
					if (point.x < 0 || point.x > 1 || point.y < 0 || point.y > 1)
						continue;
					Require(Covered(reference, *solution, eye, point), "The analytic outward curve must remain covered in its own eye");
					const auto peer = Project(eye == 0 ? reference.leftToRight : inverse, point);
					if (peer && peer->x >= 0 && peer->x <= 1 && peer->y >= 0 && peer->y <= 1)
						Require(Covered(reference, *solution, 1 - eye, *peer), "Every shared outward direction must also be covered in the other eye");
				}
			}
		}
		return *solution;
	}
}

int main()
{
	CheckInheritedBoundary();
	const auto started = std::chrono::steady_clock::now();
	Reference reference{ .version = 1, .outer = { .scale = 0.6f, .horizontalScale = 1.0f }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
	CheckPersistence(reference);
	CheckClipProjection();
	CheckReferenceCompatibility();
	Require(!Solve(Reference{}, 100), "An absent reference must not be interpreted as calibration");
	for (float percent : { 0.0f, 69.0f, 131.0f, std::numeric_limits<float>::quiet_NaN() })
		Require(!Solve(reference, percent), "Invalid scaling must fail closed");
	auto invalid = reference;
	invalid.leftToRight = {};
	Require(!Solve(invalid, 100), "Unknown stereo projection must fail closed");
	invalid = reference;
	invalid.outer.centers[0] = std::numeric_limits<float>::infinity();
	Require(!Solve(invalid, 100), "Nonfinite calibration values must fail closed");

	const auto baseline = CheckFit(reference, 100);
	const auto smaller = CheckFit(reference, 85);
	const auto larger = CheckFit(reference, 115);
	Require(smaller.areaPercent < baseline.areaPercent && baseline.areaPercent < larger.areaPercent,
		"Scaling must change processed area in the requested direction");
	Require(CheckFit(reference, 100).geometry == baseline.geometry, "Returning to 100 percent must restore the exact deterministic fit");

	// Dense analytic samples independently check preservation between circumscribed polygon vertices.
	for (unsigned eye = 0; eye < 2; ++eye) {
		for (unsigned i = 0; i <= 10000; ++i) {
			const double angle = -1.5707963267948966 + i * 3.141592653589793 / 10000.0;
			const double c = std::cos(angle), s = std::sin(angle);
			const Point point{ 0.5 + (eye == 0 ? -1 : 1) * 0.3 * std::sqrt(std::abs(c)),
				0.5 + std::copysign(0.3 * std::sqrt(std::abs(s)), s) };
			Require(Covered(reference, baseline, eye, point), "The complete curved reference, not just four extremes, must remain covered");
		}
	}

	reference.outer = { .scale = 0.4f, .horizontalScale = 1.0f, .centers = { .25f, .48f, .75f, .52f } };
	const auto gap = CheckFit(reference, 100);
	Require(Covered(reference, gap, 0, { .5, .5 }) && Covered(reference, gap, 1, { .5, .5 }),
		"Disjoint starting masks must gain central coverage");

	reference.outer = { .scale = 0.6f, .horizontalScale = 1.8f, .centers = { .65f, .5f, .35f, .5f } };
	const auto excess = CheckFit(reference, 100);
	const float oldArea = FoveatedMaskVisualization::MeasureCoverage(.5f, .05f, 1.8f, .15f, 0, false, 1).totalPercent;
	Require(excess.areaPercent < oldArea, "Unnecessary inward overlap must be removable without retaining whole starting masks");

	for (const Projection projection : { Projection{ 1, 0, .15, 0, 1, 0, 0, 0, 1 },
			 Projection{ .93, .04, .12, -.02, 1.03, .015, .08, .01, 1 } }) {
		reference.leftToRight = projection;
		reference.outer = { .scale = .58f, .horizontalScale = 1.3f, .centers = { .43f, .47f, .57f, .53f } };
		for (float percent : { 70.0f, 100.0f, 130.0f })
			CheckFit(reference, percent);
		reference.peripheryTaa = true;
		reference.centerScale = .3f;
		CheckFit(reference, 100);
		reference.peripheryTaa = false;
	}
	// A canted headset can put the other eye's image corners behind its projection plane.
	const double angle = 40.0 * 3.141592653589793 / 180.0;
	const double c = std::cos(angle), t = std::sin(angle) * std::tan(55.0 * 3.141592653589793 / 180.0);
	const Projection canted{ c + t, 0, -t, t, 1, (c - t - 1) * 0.5, 2 * t, 0, c - t };
	Require(!Project(canted, { 0, 0 }), "Canted fixture must include a monocular corner behind the peer view");
	Require(MatchesProjection(canted, canted), "Unchanged canted projections must remain usable");
	reference = { .version = 1, .outer = { .scale = .6f }, .leftToRight = canted };
	for (float percent : { 70.0f, 100.0f, 130.0f })
		CheckFit(reference, percent);
	reference.fullImage = true;
	Require(CheckFit(reference, 100).fullImage, "A canted full-view reference must preserve both full eye images");

	std::mt19937 random(20261009);
	const auto range = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); };
	for (unsigned sample = 0; sample < 24; ++sample) {
		reference = { .version = 1, .outer = { .scale = range(.35f, 1.1f), .horizontalScale = range(1, 2), .centers = { range(.2f, .8f), range(.2f, .8f), range(.2f, .8f), range(.2f, .8f) } }, .leftToRight = { 1, range(-.04f, .04f), range(-.2f, .2f), range(-.04f, .04f), 1, range(-.05f, .05f), 0, 0, 1 }, .feather = range(0, .1f) };
		const auto retained = reference;
		CheckFit(reference, 100);
		Require(reference == retained, "Optimization must leave the outer reference immutable");
	}

	reference = { .version = 1, .outer = { .scale = .4002f, .horizontalScale = 1.0f }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, .peripheryTaa = true, .centerScale = .4f, .feather = 0 };
	CheckFit(reference, 100);
	reference = { .version = 1, .outer = { .scale = 1.1f, .horizontalScale = 2.0f, .centers = { .35f, .5f, .65f, .5f } }, .leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, .peripheryTaa = true, .centerScale = .9f, .feather = .1f };
	const auto fullTaa = CheckFit(reference, 100);
	Require(!fullTaa.fullImage && fullTaa.areaPercent > 99.99f,
		"A feasible TAA fit covering the entire image must be accepted without changing its centre scale");

	reference.peripheryTaa = false;
	reference.fullImage = true;
	reference.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	Require(CheckFit(reference, 100).fullImage, "A rectangular full-view reference must retain safe full coverage");
	std::cout << "PASS: outer contours, central gaps, excess overlap, stereo projection, scaling, restoration, bounds and TAA ("
			  << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " seconds)\n";
}
