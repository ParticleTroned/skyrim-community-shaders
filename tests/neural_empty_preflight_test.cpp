#include "Features/Upscaling/NeuralRendering/CharacterRendering.h"

#include <mutex>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}
using namespace NeuralRendering;

struct Fixture
{
	std::mutex mutex_;
	unsigned earlyMaskCaptureSerial_ = 7, capturedFrame_ = 10;
	struct
	{
		unsigned frame = 10;
	} capturedGeometry_;
	unsigned capturedEyeWidth_ = 100, capturedHeight_ = 80, capturedEyeCount_ = 2;
	unsigned capturedEnabledCategoryMask_ = 14;
	bool capturedCategoriesEmpty_ = false;
	struct Plan
	{
		bool projectionUncertain = false;
		std::vector<unsigned> regions;
	} plan;
	struct EarlySelectionPlan
	{
		unsigned key;
		Plan plan;
	};
	std::array<std::optional<EarlySelectionPlan>, 2> earlySelectionPlans_;
	unsigned plans = 0;
	bool throws = false;
	Plan BuildPlan(const CharacterMaskPrepareArgs&)
	{
		++plans;
		if (throws)
			throw std::runtime_error("projection unavailable");
		return plan;
	}
	unsigned SelectionPlanKey(const CharacterMaskPrepareArgs&) const { return 3; }
};
static Fixture* producer;
namespace NeuralRendering
{
#include "neural_authored_mode_under_test.h"
	class CharacterRendering::State : public Fixture
	{};
	CharacterRendering::CharacterRendering() : state_(std::make_unique<State>()) { producer = state_.get(); }
	CharacterRendering::~CharacterRendering() = default;
	CharacterRendering& CharacterRendering::Instance()
	{
		static CharacterRendering value;
		return value;
	}
#include "neural_empty_preflight_under_test.h"
}
static void Require(bool value)
{
	if (!value)
		throw std::runtime_error("Empty preflight admitted stale, uncertain or invalid source");
}
int main()
{
	auto& rendering = CharacterRendering::Instance();
	CharacterMaskPrepareArgs args{};
	args.settings.enabled = true;
	args.frameId = args.sourceWorldFrame = 10;
	args.generation = 2;
	args.outputWidth = 50;
	args.outputHeight = 40;
	args.viewportCrop = { { 100, 80 }, { 3, 5, 53, 45 }, { 100, 80 }, { 3, 5, 53, 45 } };
	producer->capturedEnabledCategoryMask_ = GetEnabledCharacterCategoryMask(args.settings);
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		producer->capturedEyeCount_ = vr ? 2 : 1;
		for (unsigned eye = 0; eye < producer->capturedEyeCount_; ++eye) {
			args.eyeIndex = eye;
			producer->plan = {};
			Require(rendering.IsCurrentSelectionEmpty(args));
			Require(producer->earlySelectionPlans_[eye].has_value());
			producer->plan.projectionUncertain = true;
			Require(!rendering.IsCurrentSelectionEmpty(args));
			producer->plan = { false, { 1 } };
			Require(!rendering.IsCurrentSelectionEmpty(args));
			for (auto mode : { CharacterMaskTestMode::ForceOne, CharacterMaskTestMode::ForceHalf, CharacterMaskTestMode::InvertAuthored }) {
				args.settings.maskTestMode = mode;
				Require(!rendering.IsCurrentSelectionEmpty(args));
			}
			args.settings.maskTestMode = CharacterMaskTestMode::ForceZero;
			Require(rendering.IsCurrentSelectionEmpty(args));
			args.settings.maskTestMode = CharacterMaskTestMode::Authored;
			producer->plan = {};
			producer->throws = true;
			Require(!rendering.IsCurrentSelectionEmpty(args));
			producer->throws = false;
		}
	}
	args.eyeIndex = 0;
	for (unsigned invalid = 0; invalid < 10; ++invalid) {
		auto value = args;
		if (invalid == 0)
			value.frameId++;
		if (invalid == 1)
			value.sourceWorldFrame--;
		if (invalid == 2)
			value.generation = 0;
		if (invalid == 3)
			value.eyeIndex = 2;
		if (invalid == 4)
			value.settings.enabled = false;
		if (invalid == 5)
			value.outputWidth++;
		if (invalid == 6)
			value.viewportCrop.fullInput.width++;
		if (invalid == 7)
			producer->earlyMaskCaptureSerial_ = 0;
		if (invalid == 8)
			producer->capturedEnabledCategoryMask_ = 0;
		if (invalid == 9)
			producer->capturedGeometry_.frame = 9;
		Require(!rendering.IsCurrentSelectionEmpty(value));
		producer->capturedGeometry_.frame = 10;
		producer->earlyMaskCaptureSerial_ = 7;
		producer->capturedEnabledCategoryMask_ = GetEnabledCharacterCategoryMask(args.settings);
	}
	producer->capturedCategoriesEmpty_ = true;
	producer->plan = { true, { 1 } };
	const auto plans = producer->plans;
	Require(rendering.IsCurrentSelectionEmpty(args));
	Require(producer->plans == plans);
}
