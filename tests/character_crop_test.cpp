#include "../src/Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"
#include "../src/Features/Upscaling/NeuralRendering/CharacterCropPolicy.h"
#include "../src/Features/Upscaling/NeuralRendering/RoiDescriptor.h"

#include <limits>
#include <stdexcept>

using namespace NeuralRendering;

static void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("Actor crop policy regression");
}

int main()
{
	CharacterCropCalibration measured;
	measured.cropped.fill({ 8, 2, 7, 0.05, 240 });
	measured.uncropped.fill({ 12, 2, 11, 0.05, 240 });
	measured.qualityQualified = true;
	measured.switchCostMs = 12;
	Require(IsWinningCharacterCropCalibration(measured));
	const auto winner = measured;
	measured.qualityQualified = false;
	Require(!IsWinningCharacterCropCalibration(measured));
	measured = winner;
	measured.cropped[1].frameMs = 12;
	Require(!IsWinningCharacterCropCalibration(measured));
	measured = winner;
	measured.cropped[2].cpuMs = 4;
	Require(!IsWinningCharacterCropCalibration(measured));
	measured = winner;
	measured.cropped[0].frames = 119;
	Require(!IsWinningCharacterCropCalibration(measured));
	measured = winner;
	measured.cropped[0].noiseMs = 2;
	Require(!IsWinningCharacterCropCalibration(measured));
	measured = winner;
	measured.switchCostMs = std::numeric_limits<double>::quiet_NaN();
	Require(!IsWinningCharacterCropCalibration(measured));
	CharacterCropAdmission cache;
	Require(!cache.Qualified(17, 100));
	Require(cache.Qualify(17, 100, winner));
	Require(cache.Qualified(17, 101));
	Require(!cache.Qualified(18, 101));
	Require(!cache.Qualified(17, 100 + CharacterCropAdmission::kLifetimeFrames + 1));
	Require(!cache.Qualify(0, 100, winner));
	const std::array<std::uint64_t, 2> pair{ 17, 18 }, changedPair{ 17, 19 }, missingEye{ 17, 0 };
	Require(CharacterCropPairKey(pair) != CharacterCropPairKey(changedPair));
	Require(!CharacterCropPairKey(missingEye));
	CharacterCropResidence residence;
	Require(!AdmitCharacterCrop(17, 100, true, residence));
	Require(!AdmitCharacterCrop(17, 219, true, residence));
	Require(AdmitCharacterCrop(17, 220, true, residence));
	Require(!AdmitCharacterCrop(18, 221, true, residence));
	Require(!AdmitCharacterCrop(18, 222, false, residence));
	Require(!residence.key);
	for (std::uint64_t key = 100; key < 117; ++key)
		Require(cache.Qualify(key, 300, winner));
	Require(!cache.Qualified(17, 301));
	Require(cache.Qualified(116, 301));
	const ComputeSubrect support{ 80, 96, 64, 80 };
	const auto crop = BuildCharacterProviderComputeSubrect(support, 1024, 1024);
	const ComputeSubrect full{ 0, 0, 1024, 1024 };
	const auto cropped = BuildRoiDescriptor(support, crop, { 1024, 1024 }, true);
	const auto uncropped = BuildRoiDescriptor(support, full, { 1024, 1024 }, true);
	Require(cropped.samplingSupport == uncropped.samplingSupport);
	Require(uncropped.inferenceContext == full && uncropped.ownedOutput == full);
	Require(ContainsComputeSubrect(uncropped.inferenceContext, support));
}
