#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/Renderer.h"
#include "Features/Upscaling/NeuralRendering/SourceTransport.h"

#include <iostream>
#include <stdexcept>

using namespace NeuralRendering;

void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}

int main()
{
	RendererApplyArgs first;
	first.frameId = 12;
	first.sourceWorldFrame = 11;
	first.generation = 3;
	first.colorWidth = first.guideWidth = first.outputWidth = 128;
	first.colorHeight = first.guideHeight = first.outputHeight = 96;
	first.executionContext.sourceTransactionId = 7;
	first.executionContext.captureEpoch = 2;
	first.featureSlot = 0;
	auto peer = first;
	peer.featureSlot = 4;
	peer.computeSubrect = { 60, 10, 16, 24 };
	peer.colorOutput = reinterpret_cast<ID3D11Resource*>(1);
	Require(SameSourceTransport(first, peer), "private output and context shape must not prohibit sharing");
	const auto reject = [&](auto change) {
		auto changed = peer;
		change(changed);
		Require(!SameSourceTransport(first, changed), "different source contract must not share");
	};
	reject([](auto& a) { ++a.frameId; });
	reject([](auto& a) { ++a.sourceWorldFrame; });
	reject([](auto& a) { ++a.generation; });
	reject([](auto& a) { ++a.featureSlot; });
	reject([](auto& a) { a.featureSlot = 6; });
	reject([](auto& a) { a.device = reinterpret_cast<ID3D11Device*>(1); });
	reject([](auto& a) { a.context = reinterpret_cast<ID3D11DeviceContext*>(1); });
	reject([](auto& a) { a.colorInput = reinterpret_cast<ID3D11Resource*>(1); });
	reject([](auto& a) { a.depthGuide = reinterpret_cast<ID3D11Resource*>(1); });
	reject([](auto& a) { a.depthGuideSRV = reinterpret_cast<ID3D11ShaderResourceView*>(1); });
	reject([](auto& a) { a.motionVectors = reinterpret_cast<ID3D11Resource*>(1); });
	reject([](auto& a) { ++a.executionContext.sourceTransactionId; });
	reject([](auto& a) { a.executionContext.captureEpoch = 9; });
	reject([](auto& a) { a.executionContext.configurationEpoch = 9; });
	reject([](auto& a) { a.executionContext.sourceColorOrigin = { 1, 0 }; });
	reject([](auto& a) { a.executionContext.jitterPixels = { 0.25f, 0.5f }; });
	reject([](auto& a) { ++a.colorWidth; });
	reject([](auto& a) { ++a.colorHeight; });
	reject([](auto& a) { ++a.guideWidth; });
	reject([](auto& a) { ++a.guideHeight; });
	reject([](auto& a) { ++a.outputWidth; });
	reject([](auto& a) { ++a.outputHeight; });
	reject([](auto& a) { ++a.controlMaskWidth; });
	reject([](auto& a) { ++a.controlMaskHeight; });
	reject([](auto& a) { a.featureUpscaling = true; });
	const std::array<unsigned, 4> retained{ 10, 11, 12, 13 }, replacement{ 20, 21, 22, 23 };
	Require(CanReuseSourceBinding(0, 0, retained, &retained), "unchanged shared binding may be reused");
	Require(!CanReuseSourceBinding(0, 4, retained, static_cast<const std::array<unsigned, 4>*>(nullptr)),
		"a prior borrower becoming private must detach before overwriting its old owner's source");
	Require(!CanReuseSourceBinding(0, 0, retained, &replacement), "owner recreation invalidates borrowed resource identities");
	Require(!CanReuseSourceBinding(0, 4, retained, &retained), "same allocation cannot transfer ownership without retirement");
	Require(CanReuseSourceBinding(4, 4, replacement, static_cast<const std::array<unsigned, 4>*>(nullptr)),
		"a privately owned transport may be reused for a later source");
	struct Transition
	{
		unsigned resource = 0, featureState = 0;
	};
	std::array<Transition, 3> transitions{};
	std::size_t count = 0;
	Require(AddSourceTransition(transitions, count, Transition{ 1, 2 }), "first input");
	Require(AddSourceTransition(transitions, count, Transition{ 1, 2 }) && count == 1, "one barrier for shared input");
	Require(!AddSourceTransition(transitions, count, Transition{ 1, 3 }) && count == 1, "conflicting states fail closed");
	Require(AddSourceTransition(transitions, count, Transition{ 2, 3 }), "private first output");
	Require(AddSourceTransition(transitions, count, Transition{ 3, 3 }), "private second output");
	Require(!AddSourceTransition(transitions, count, Transition{ 4, 3 }) && count == 3, "bounded transition storage");
	Require(!AddSourceTransition(transitions, count, Transition{}), "null resource rejected");
	Color::Experiments experiment;
	Require(!experiment.SharedSourceTransportEnabled(), "sharing defaults off");
#ifdef DEVBENCH_BRIDGE_ENABLED
	experiment.sharedSourceTransport = true;
	Require(experiment.SharedSourceTransportEnabled(), "bridge may enable sharing");
	CapacityRejections<unsigned, 2> ledger;
	ledger.Record({ 10, CapacityRejectionKind::Pressure, -1, 2 });
	Require(ledger.Rejects(10) && !ledger.Rejects(11), "pressure is capacity specific");
	ledger.Record({ 11, CapacityRejectionKind::Unsupported, -2, 3 });
	for (unsigned frame = 0; frame < 1000; ++frame)
		Require(ledger.Rejects(10) && ledger.Rejects(11) && ledger.count == 2, "no time or FPS resurrection");
	ledger.Record({ 12, CapacityRejectionKind::Pressure });
	Require(ledger.saturated && ledger.Rejects(99), "ledger exhaustion cannot evict a rejection");
	ledger = {};
	Require(!ledger.Rejects(10), "explicit safe reset clears the ledger");
	ledger.Record({ 10, CapacityRejectionKind::Pressure });
	ledger.Record({ 10, CapacityRejectionKind::UnsafeProvider });
	Require(ledger.count == 1 && ledger.Rejects(99), "unsafe provider rejection upgrades and blocks all automatic retry");
#endif
	std::cout << "Source transport identity, transition and rejection checks passed\n";
}
