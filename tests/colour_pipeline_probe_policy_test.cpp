#include "Features/Upscaling/ColourPipelineProbePolicy.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			throw std::runtime_error("colour-pipeline staging policy regression");
	}

	void VerifySparseReadback()
	{
		namespace Policy = CSX::Diagnostics::ColourPipelineProbe::Policy;
		for (const auto height : { 1u, 2u, 16u, 17u, 18u, 127u, 4096u, 8192u }) {
			for (const auto width : { 3u, 19u, 127u }) {
				for (const auto bytesPerPixel : { 4u, 8u, 16u }) {
					const auto sourcePitch = width * bytesPerPixel;
					const auto readbackPitch = sourcePitch + 64;
					std::vector<std::uint8_t> source(std::size_t{ sourcePitch } * height);
					for (std::size_t index = 0; index < source.size(); ++index)
						source[index] = static_cast<std::uint8_t>((index * 37) ^ (index >> 8) ^ (index >> 16));
					std::vector<std::uint8_t> readback(std::size_t{ readbackPitch } * Policy::ReadbackHeight(height), 0xcd);
					for (std::uint32_t row = 0; row < Policy::ReadbackHeight(height); ++row) {
						const auto sourceRow = Policy::SourceRow(height, row);
						Require(sourceRow < height);
						std::memcpy(readback.data() + std::size_t{ row } * readbackPitch,
							source.data() + std::size_t{ sourceRow } * sourcePitch, sourcePitch);
					}
					for (std::uint32_t gridY = 0; gridY < Policy::kGridSize; ++gridY) {
						const auto sourceY = (height - 1) * gridY / (Policy::kGridSize - 1);
						const auto readbackY = Policy::SampleRow(height, gridY);
						Require(readbackY < Policy::ReadbackHeight(height));
						Require(Policy::SampleCoordinate(height, gridY) == sourceY);
						for (std::uint32_t gridX = 0; gridX < Policy::kGridSize; ++gridX) {
							const auto sourceX = (width - 1) * gridX / (Policy::kGridSize - 1);
							Require(Policy::SampleCoordinate(width, gridX) == sourceX);
							const auto* expected = source.data() + std::size_t{ sourceY } * sourcePitch + sourceX * bytesPerPixel;
							const auto* actual = readback.data() + std::size_t{ readbackY } * readbackPitch + sourceX * bytesPerPixel;
							Require(std::equal(expected, expected + bytesPerPixel, actual));
						}
					}
				}
			}
		}
	}
}

int main()
{
	namespace Policy = CSX::Diagnostics::ColourPipelineProbe::Policy;
	using CSX::Diagnostics::ColourPipelineProbe::Route;
	using CSX::Diagnostics::ColourPipelineProbe::Stage;
	static_assert(Policy::RequiredSlotCount(Route::MainPass) == 10);
	static_assert(Policy::RequiredSlotCount(Route::CompositorSubmit) == 4);
	static_assert(Policy::MatchesVendorBatch(10, 10));
	static_assert(!Policy::MatchesVendorBatch(10, 11));
	static_assert(!Policy::MatchesVendorBatch(0, 0));
	static_assert(!Policy::MatchesVendorBatch(10, 0));
	constexpr std::uint32_t mainTarget = 1;
	constexpr std::uint32_t vrDestination = 114;
	Require(Policy::ValidImageSpaceTarget(Stage::CombinedMain, mainTarget, true, mainTarget, vrDestination));
	Require(!Policy::ValidImageSpaceTarget(Stage::CombinedMain, mainTarget, false, mainTarget, vrDestination));
	Require(!Policy::ValidImageSpaceTarget(Stage::CombinedMain, vrDestination, true, mainTarget, vrDestination));
	for (const auto stage : { Stage::ImageSpaceInput, Stage::ImageSpaceOutput }) {
		Require(Policy::ValidImageSpaceTarget(stage, vrDestination, false, mainTarget, vrDestination));
		Require(!Policy::ValidImageSpaceTarget(stage, mainTarget, true, mainTarget, vrDestination));
		Require(!Policy::ValidImageSpaceTarget(stage, vrDestination + 1, false, mainTarget, vrDestination));
	}
	for (const auto stage : { Stage::FsrInput, Stage::FsrOutput, static_cast<Stage>(255) })
		Require(!Policy::ValidImageSpaceTarget(stage, vrDestination, true, mainTarget, vrDestination));
	struct CaptureSlot
	{
		bool queued = false;
		bool mapped = false;
	};
	std::array<CaptureSlot, Policy::kSlotCount> slots{};
	for (std::uint32_t index = 0; index < 6; ++index)
		slots[index].queued = true;
	const auto partial = Policy::DescribeStageEyeSlots(slots);
	Require(partial[0].stage == Stage::FsrInput && partial[1].stage == Stage::FsrInput);
	Require(partial[2].stage == Stage::FsrOutput && partial[3].stage == Stage::FsrOutput);
	Require(partial[Policy::RequiredSlotCount(Route::CompositorSubmit)].stage == Stage::CombinedMain);
	for (std::uint32_t index = 0; index < partial.size(); ++index) {
		Require(partial[index].eye == index % 2 && !partial[index].mapped);
		Require(partial[index].queued == (index < 6));
	}
	Require(partial[6].stage == Stage::ImageSpaceInput && partial[6].eye == 0 && !partial[6].queued);
	Require(partial[7].stage == Stage::ImageSpaceInput && partial[7].eye == 1 && !partial[7].queued);
	Require(partial[8].stage == Stage::ImageSpaceOutput && partial[8].eye == 0 && !partial[8].queued);
	Require(partial[9].stage == Stage::ImageSpaceOutput && partial[9].eye == 1 && !partial[9].queued);
	for (auto& slot : slots)
		slot = { true, true };
	const auto complete = Policy::DescribeStageEyeSlots(slots);
	for (const auto& slot : complete)
		Require(slot.queued && slot.mapped);
	slots[3] = {};
	const auto missingRightOutput = Policy::DescribeStageEyeSlots(slots);
	Require(missingRightOutput[3].stage == Stage::FsrOutput && missingRightOutput[3].eye == 1 &&
			!missingRightOutput[3].queued && !missingRightOutput[3].mapped);
	Require(missingRightOutput[6].queued && missingRightOutput[6].mapped);
	struct Dispatch
	{
		std::uint32_t frame = 0;
		std::uint64_t colourContractRevision = 0;
		std::uint32_t contextIndex = 0;
		std::uint64_t dispatchSerial = 0;
		std::uint64_t contextGeneration = 0;
		std::string path;
		float configuredSharpness = 0;
		float effectiveSharpness = 0;
		bool sharpeningEnabled = false;
		bool reset = false;
		float jitterX = 0;
		float jitterY = 0;
		float frameTimeDeltaMilliseconds = 0;
		std::uint64_t vendorBatch = 0;
	};
	for (const auto* path : { "host", "runtime-fsr3", "runtime-fsr4", "runtime-to-host-fallback" }) {
		for (std::uint32_t eye : { 0u, 1u }) {
			Dispatch input{ 8, 3, eye, 0, 99, "previous-runtime-frame", 0.75f, 0, false };
			input.vendorBatch = 10;
			const Dispatch actual{ 8, 3, eye, 123 + eye, 42, path, 0.75f, 0.5f, true,
				eye == 0, -0.25f, 0.375f, 16.5f, 10 };
			Require(Policy::BindInputDispatch(input, actual, 8, 3, eye));
			Require(input.path == actual.path && input.contextGeneration == actual.contextGeneration);
			Require(input.dispatchSerial == actual.dispatchSerial && input.contextIndex == eye);
			Require(input.configuredSharpness == 0.75f && input.effectiveSharpness == 0.5f && input.sharpeningEnabled);
			Require(input.reset == (eye == 0) && input.jitterX == -0.25f &&
					input.jitterY == 0.375f && input.frameTimeDeltaMilliseconds == 16.5f);
			Require(!Policy::BindInputDispatch(input, actual, 9, 3, eye));
			Require(!Policy::BindInputDispatch(input, actual, 8, 4, eye));
			Require(!Policy::BindInputDispatch(input, actual, 8, 3, 1u - eye));
			auto unavailable = actual;
			unavailable.dispatchSerial = 0;
			Require(!Policy::BindInputDispatch(input, unavailable, 8, 3, eye));
			unavailable = actual;
			unavailable.path.clear();
			Require(!Policy::BindInputDispatch(input, unavailable, 8, 3, eye));
		}
	}
	// A deferred attempt cannot lend its inputs to a later same-frame success.
	for (const auto* path : { "host", "runtime-fsr3", "runtime-fsr4", "runtime-to-host-fallback" }) {
		for (std::uint32_t eye : { 0u, 1u }) {
			Dispatch input{ .frame = 8, .colourContractRevision = 3, .contextIndex = eye, .vendorBatch = 10 };
			Dispatch output{ .frame = 8, .colourContractRevision = 3, .contextIndex = eye, .dispatchSerial = 123 + eye, .path = path, .vendorBatch = 11 };
			Require(!Policy::BindInputDispatch(input, output, 8, 3, eye));
			Require(input.vendorBatch == 10 && input.dispatchSerial == 0 && input.path.empty());
			input.vendorBatch = output.vendorBatch;
			Require(Policy::BindInputDispatch(input, output, 8, 3, eye));
			Require(input.vendorBatch == 11 && input.dispatchSerial == 123 + eye);
			input.vendorBatch = output.vendorBatch = 0;
			Require(!Policy::BindInputDispatch(input, output, 8, 3, eye));
		}
	}
	constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
	Require(Policy::ValidRectangle(4096, 2048, 2048, 0, 2048, 2048));
	Require(!Policy::ValidRectangle(4096, 2048, 2049, 0, 2048, 2048));
	Require(!Policy::ValidRectangle(maximum, maximum, maximum - 3, 0, 8, 1));
	Require(!Policy::ValidRectangle(32, 32, maximum, 0, 1, 1));
	Require(!Policy::ValidRectangle(32, 32, 0, 0, 0, 1));
	Require(!Policy::ValidRectangle(maximum, maximum, 0, 0, maximum, maximum));
	Require(Policy::CanAllocate(8192, 8192, 4, 0));
	Require(Policy::CanAllocate(8192, 8192, 8, 0));
	Require(Policy::CanAllocate(8192, 8192, 16, 0));
	Require(!Policy::CanAllocate(8193, 1, 1, 0));
	Require(!Policy::CanAllocate(1, 1, 17, 0));
	Require(!Policy::CanAllocate(1, 1, 0, 0));
	Require(!Policy::CanAllocate(0, 1, 4, 0));
	Require(!Policy::CanAllocate(1, 1, 4, std::numeric_limits<std::uint64_t>::max()));
	for (std::uint32_t bytesPerPixel : { 4u, 8u, 16u }) {
		for (std::uint32_t width : { 1u, 17u, 1920u, 4096u, 8192u }) {
			const auto bytes = std::uint64_t{ width } * 17 * bytesPerPixel;
			Require(Policy::CanAllocate(width, 17, bytesPerPixel, Policy::kMaximumCaptureBytes - bytes));
			Require(!Policy::CanAllocate(width, 17, bytesPerPixel, Policy::kMaximumCaptureBytes - bytes + 1));
		}
	}
	VerifySparseReadback();
}
