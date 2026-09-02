#include "Features/Upscaling/DLSSViewportCrop.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
	constexpr float Abs(float a_value)
	{
		return a_value < 0.0f ? -a_value : a_value;
	}

	constexpr bool Near(float a_left, float a_right, float a_epsilon = 1e-5f)
	{
		return Abs(a_left - a_right) <= a_epsilon;
	}

	constexpr bool IsIdentity(const UpscalingDLSS::Matrix4x4& a_matrix)
	{
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				const float expected = row == column ? 1.0f : 0.0f;
				if (!Near(a_matrix[row][column], expected))
					return false;
			}
		}
		return true;
	}

	constexpr std::array<float, 4> TransformRowVector(
		const std::array<float, 4>& a_vector,
		const UpscalingDLSS::Matrix4x4& a_matrix)
	{
		std::array<float, 4> result{};
		for (std::size_t column = 0; column < 4; ++column) {
			for (std::size_t row = 0; row < 4; ++row)
				result[column] += a_vector[row] * a_matrix[row][column];
		}
		return result;
	}
}

int main()
{
	using namespace UpscalingDLSS;

	constexpr auto identityCrop = ViewportCrop::Identity(1511, 1347, 2016, 2240);
	static_assert(identityCrop.IsValid());
	static_assert(identityCrop.IsIdentity());
	static_assert(identityCrop.MatchesEvaluationExtents(1511, 1347, 2016, 2240));
	static_assert(!identityCrop.MatchesEvaluationExtents(1510, 1347, 2016, 2240));
	constexpr auto identityAffine = BuildClipCropAffine(identityCrop);
	static_assert(identityAffine.valid);
	static_assert(IsIdentity(identityAffine.fullClipToCrop));
	static_assert(IsIdentity(identityAffine.cropClipToFull));
	constexpr auto identityMotionScale = BuildMotionVectorScale(identityCrop);
	static_assert(identityMotionScale.valid);
	static_assert(Near(identityMotionScale.x, 1.0f));
	static_assert(Near(identityMotionScale.y, 1.0f));

	constexpr ViewportCrop centeredCrop{
		.fullInput = { 100, 80 },
		.input = { 25, 20, 75, 60 },
		.fullOutput = { 100, 80 },
		.output = { 25, 20, 75, 60 },
	};
	constexpr auto centeredAffine = BuildClipCropAffine(centeredCrop);
	static_assert(centeredAffine.valid);
	static_assert(Near(centeredAffine.scaleX, 0.5f));
	static_assert(Near(centeredAffine.scaleY, 0.5f));
	static_assert(Near(centeredAffine.centerX, 0.0f));
	static_assert(Near(centeredAffine.centerY, 0.0f));
	static_assert(Near(centeredAffine.fullClipToCrop[0][0], 2.0f));
	static_assert(Near(centeredAffine.fullClipToCrop[1][1], 2.0f));
	static_assert(IsIdentity(Multiply(
		centeredAffine.fullClipToCrop, centeredAffine.cropClipToFull)));

	constexpr ViewportCrop offCenterCrop{
		.fullInput = { 100, 100 },
		.input = { 10, 20, 50, 60 },
		.fullOutput = { 100, 100 },
		.output = { 10, 20, 50, 60 },
	};
	constexpr auto offCenterAffine = BuildClipCropAffine(offCenterCrop);
	static_assert(offCenterAffine.valid);
	static_assert(Near(offCenterAffine.scaleX, 0.4f));
	static_assert(Near(offCenterAffine.scaleY, 0.4f));
	static_assert(Near(offCenterAffine.centerX, -0.4f));
	static_assert(Near(offCenterAffine.centerY, 0.2f));
	static_assert(Near(offCenterAffine.fullClipToCrop[3][0], 1.0f));
	static_assert(Near(offCenterAffine.fullClipToCrop[3][1], -0.5f));
	constexpr auto mappedCenter = TransformRowVector(
		{ -0.4f, 0.2f, 0.0f, 1.0f }, offCenterAffine.fullClipToCrop);
	static_assert(Near(mappedCenter[0], 0.0f));
	static_assert(Near(mappedCenter[1], 0.0f));
	static_assert(Near(mappedCenter[3], 1.0f));
	static_assert(IsIdentity(Multiply(
		offCenterAffine.fullClipToCrop, offCenterAffine.cropClipToFull)));
	static_assert(IsIdentity(Multiply(
		offCenterAffine.cropClipToFull, offCenterAffine.fullClipToCrop)));

	constexpr ViewportCrop rightBottomCrop{
		.fullInput = { 100, 100 },
		.input = { 50, 40, 90, 80 },
		.fullOutput = { 100, 100 },
		.output = { 50, 40, 90, 80 },
	};
	constexpr auto rightBottomAffine = BuildClipCropAffine(rightBottomCrop);
	static_assert(Near(rightBottomAffine.centerX, 0.4f));
	static_assert(Near(rightBottomAffine.centerY, -0.2f));
	constexpr auto mappedRightBottomCenter = TransformRowVector(
		{ 0.4f, -0.2f, 0.0f, 1.0f }, rightBottomAffine.fullClipToCrop);
	static_assert(Near(mappedRightBottomCenter[0], 0.0f));
	static_assert(Near(mappedRightBottomCenter[1], 0.0f));

	constexpr ViewportCrop roundedInputCrop{
		.fullInput = { 1511, 1347 },
		.input = { 113, 57, 914, 806 },
		.fullOutput = { 2016, 2240 },
		.output = { 152, 96, 1216, 1344 },
	};
	static_assert(roundedInputCrop.IsValid());
	constexpr auto exactMotionScale = BuildMotionVectorScale(roundedInputCrop);
	static_assert(exactMotionScale.valid);
	static_assert(Near(exactMotionScale.x, 1511.0f / 801.0f));
	static_assert(Near(exactMotionScale.y, 1347.0f / 749.0f));
	constexpr auto neuralMotionScale =
		BuildMotionVectorPixelScale(roundedInputCrop);
	static_assert(neuralMotionScale.valid);
	static_assert(Near(neuralMotionScale.x, 1511.0f));
	static_assert(Near(neuralMotionScale.y, 1347.0f));

	constexpr ViewportCrop invalidCrop{
		.fullInput = { 100, 100 },
		.input = { 20, 20, 101, 80 },
		.fullOutput = { 100, 100 },
		.output = { 20, 20, 80, 80 },
	};
	static_assert(!invalidCrop.IsValid());
	static_assert(!BuildClipCropAffine(invalidCrop).valid);
	static_assert(!BuildMotionVectorScale(invalidCrop).valid);
	static_assert(!BuildMotionVectorPixelScale(invalidCrop).valid);

	constexpr auto history = MakeSuccessfulCropHistory(41, 7, offCenterCrop);
	static_assert(history.valid);
	constexpr auto continuous = EvaluateCropContinuity(history, 42, 7, offCenterCrop);
	static_assert(continuous.currentDescriptorValid);
	static_assert(continuous.continuous);
	static_assert(!continuous.reset);
	static_assert(continuous.reason == CropHistoryResetReason::None);
	static_assert(continuous.previousCrop == offCenterCrop);
	static_assert(std::string_view(GetCropHistoryResetReasonName(continuous.reason)) == "none");
	constexpr auto continuedHistory = MakeSuccessfulCropHistory(
		42, 7, offCenterCrop, continuous);
	constexpr auto sameFrameReplay = EvaluateCropContinuity(
		continuedHistory, 42, 7, offCenterCrop);
	static_assert(sameFrameReplay.sameFrameReplay);
	static_assert(sameFrameReplay.continuous);
	static_assert(!sameFrameReplay.reset);
	static_assert(sameFrameReplay.previousCrop == offCenterCrop);
	static_assert(sameFrameReplay.reason == CropHistoryResetReason::None);
	constexpr auto firstUseHistory = MakeSuccessfulCropHistory(
		41, 8, offCenterCrop);
	constexpr auto firstUseReplay = EvaluateCropContinuity(
		firstUseHistory, 41, 8, offCenterCrop);
	static_assert(firstUseReplay.sameFrameReplay);
	static_assert(!firstUseReplay.continuous);
	static_assert(firstUseReplay.reset);
	static_assert(
		firstUseReplay.reason == CropHistoryResetReason::NoSuccessfulHistory);

	constexpr auto noHistory = EvaluateCropContinuity({}, 42, 7, offCenterCrop);
	static_assert(!noHistory.continuous && noHistory.reset);
	static_assert(noHistory.reason == CropHistoryResetReason::NoSuccessfulHistory);
	static_assert(noHistory.previousCrop == offCenterCrop);

	constexpr auto invalidGeneration = EvaluateCropContinuity(history, 42, 0, offCenterCrop);
	static_assert(invalidGeneration.reason == CropHistoryResetReason::InvalidGeneration);
	constexpr auto changedGeneration = EvaluateCropContinuity(history, 42, 8, offCenterCrop);
	static_assert(changedGeneration.reason == CropHistoryResetReason::GenerationChanged);
	constexpr auto skippedFrame = EvaluateCropContinuity(history, 43, 7, offCenterCrop);
	static_assert(skippedFrame.reason == CropHistoryResetReason::NonSequentialFrame);
	constexpr auto changedCrop = EvaluateCropContinuity(history, 42, 7, rightBottomCrop);
	static_assert(changedCrop.reason == CropHistoryResetReason::DescriptorChanged);
	static_assert(changedCrop.previousCrop == rightBottomCrop);
	constexpr auto invalidDescriptor = EvaluateCropContinuity(history, 42, 7, invalidCrop);
	static_assert(invalidDescriptor.reason == CropHistoryResetReason::InvalidDescriptor);

	constexpr auto wrapHistory = MakeSuccessfulCropHistory(
		std::numeric_limits<std::uint32_t>::max(), 7, offCenterCrop);
	constexpr auto wrappedFrame = EvaluateCropContinuity(
		wrapHistory, 0, 7, offCenterCrop);
	static_assert(wrappedFrame.continuous);
	static_assert(!MakeSuccessfulCropHistory(42, 0, offCenterCrop).valid);
	static_assert(!MakeSuccessfulCropHistory(42, 7, invalidCrop).valid);

	constexpr auto temporalCropBasis = Multiply(
		offCenterAffine.cropClipToFull,
		rightBottomAffine.fullClipToCrop);
	constexpr auto priorCenterInCurrentCrop = TransformRowVector(
		{ 0.0f, 0.0f, 0.0f, 1.0f }, temporalCropBasis);
	static_assert(Near(priorCenterInCurrentCrop[0], -2.0f));
	static_assert(Near(priorCenterInCurrentCrop[1], 1.0f));

	return 0;
}
