#define CSX_DEPTH_ORDER_TEST_REVERSED
#include "/Shaders/Common/DepthOrder.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

/// @tags depth, culling
[numthreads(1, 1, 1)] void TestDepthOrderReversed() {
	ASSERT(AreEqual, DepthOrder::Near(), 1.0);
	ASSERT(AreEqual, DepthOrder::Far(), 0.0);
	ASSERT(AreEqual, DepthOrder::Nearest(0.25, 0.75), 0.75);
	ASSERT(AreEqual, DepthOrder::Farthest(0.25, 0.75), 0.25);
	ASSERT(IsTrue, DepthOrder::IsBehindWithBias(0.25, 0.75, 0.01));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.25, 0.75, 0.5));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.75, 0.75, 0));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(asfloat(0x7fc00000u), 0.75, 0.01));
	ASSERT(IsFalse, DepthOrder::IsBehindWithBias(0.25, asfloat(0x7fc00000u), 0.01));
}
