#include "Features/Upscaling/NeuralRendering/ExecutionEvidence.h"

#include <iostream>

#define CHECK(condition)                                         \
	do {                                                         \
		if (!(condition)) {                                      \
			std::cerr << __LINE__ << ": " << #condition << '\n'; \
			return 1;                                            \
		}                                                        \
	} while (false)

int main()
{
	using namespace NeuralRendering;
	ExecutionDescriptor descriptor{};
	descriptor.submissionId = 17;
	descriptor.generation = 3;
	descriptor.frame = 99;
	descriptor.insertion = InsertionPoint::FinalLdrPreUi;
	descriptor.logicalEyeCount = 2;
	const auto summarize = [&] { return SummarizeExecutionPlan(&descriptor, 99, descriptor.insertion); };
	CHECK(!SummarizeExecutionPlan(nullptr, 99, descriptor.insertion));
	for (const auto count : { 1u, 2u, 4u, kEnabledRegionsPerEye }) {
		descriptor.plannedPhysicalSlotMask = 0;
		for (unsigned region = 0; region < count; ++region)
			descriptor.plannedPhysicalSlotMask |= 0b1100u << (region * kLogicalFeatureSlotCount);
		descriptor.regionCount = count * 2;
		descriptor.requestedRegionCount = 16;
		const auto value = summarize();
		CHECK(value && value->submissionId == 17 && value->generation == 3);
		CHECK(value->evaluationCount == count * 2 && value->physicalSlotMask == descriptor.plannedPhysicalSlotMask);
		CHECK(LogicalRegionMask(value->physicalSlotMask) == 0b1100u);
		CHECK(!SummarizeExecutionPlan(&descriptor, 100, descriptor.insertion));
		CHECK(!SummarizeExecutionPlan(&descriptor, 99, InsertionPoint::Count));
	}
	descriptor.regionCount = 0;
	CHECK(!summarize());
	descriptor.regionCount = 2;
	CHECK(!summarize());
	descriptor.plannedPhysicalSlotMask = 0b1100u;
	CHECK(summarize());
	descriptor.logicalEyeCount = 1;
	CHECK(!summarize());
	descriptor.plannedPhysicalSlotMask = FeatureSlotBit(kPhysicalFeatureSlotCount - 1);
	descriptor.regionCount = 1;
	CHECK(summarize());
	descriptor.submissionId = 0;
	CHECK(!summarize());
	descriptor.submissionId = 17;
	descriptor.insertion = InsertionPoint::Count;
	CHECK(!summarize());
	descriptor.insertion = InsertionPoint::FinalLdrPreUi;
	descriptor.frame = UINT32_MAX;
	CHECK(!SummarizeExecutionPlan(&descriptor, UINT32_MAX, descriptor.insertion));
	return 0;
}
