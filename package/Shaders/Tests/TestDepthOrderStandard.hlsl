#include "/Shaders/Common/DepthOrder.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags depth, culling
[numthreads(1, 1, 1)] void TestDepthOrderStandard() {
	ASSERT(AreEqual, DepthOrder::Near(), 0.0);
	ASSERT(AreEqual, DepthOrder::Far(), 1.0);
	ASSERT(AreEqual, DepthOrder::Nearest(0.25, 0.75), 0.25);
	ASSERT(AreEqual, DepthOrder::Farthest(0.25, 0.75), 0.75);
	ASSERT(IsTrue, DepthOrder::IsBehindWithBias(0.75, 0.25, 0.01));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.75, 0.25, 0.5));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.25, 0.25, 0));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(asfloat(0x7fc00000u), 0.25, 0.01));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.75, asfloat(0x7fc00000u), 0.01));
}
