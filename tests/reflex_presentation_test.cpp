#include "Features/Upscaling/ReflexPresentation.h"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ReflexDiagnostics::Presentation;
void Require(bool value, const char* reason)
{
	if (!value)
		throw std::runtime_error(reason);
}
int main()
{
	try {
		DXGI_SWAP_CHAIN_DESC original{};
		original.BufferCount = 1;
		original.SampleDesc.Count = 1;
		original.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		original.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		original.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		auto desc = original;
		Require(!PrepareFlip(false, true, desc) && desc.SwapEffect == original.SwapEffect && desc.BufferCount == 1, "SE/AE changed");
		Require(!PrepareFlip(true, false, desc) && desc.BufferCount == 1, "default changed");
		Require(PrepareFlip(true, true, desc) && desc.BufferCount == 2 && desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD && desc.Flags == original.Flags, "flip prerequisites incorrect");
		desc = original;
		desc.BufferCount = 3;
		Require(PrepareFlip(true, true, desc) && desc.BufferCount == 3, "existing buffer count changed");
		for (int kind = 0; kind < 5; ++kind) {
			desc = original;
			if (kind == 0)
				desc.SampleDesc.Count = 4;
			if (kind == 1)
				desc.SampleDesc.Quality = 1;
			if (kind == 2)
				desc.BufferCount = 17;
			if (kind == 3)
				desc.Flags |= DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
			if (kind == 4)
				desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			Require(!PrepareFlip(true, true, desc) && desc.SwapEffect == original.SwapEffect, "unsupported contract mutated");
		}
		PresentJournal<2> journal;
		PresentRecord a, b, c;
		Require(!journal.Start(0, 1, 3) && !journal.Start(1, 3, 3), "invalid window admitted");
		Require(journal.Start(1, 100, 200), "start failed");
		Require(!journal.Begin(99, c), "stale sample admitted");
		Require(journal.Begin(100, a) && journal.Begin(110, b), "overlapping producers rejected");
		Require(!journal.Begin(120, c), "capacity reservation failed");
		journal.Stop();
		Require(!journal.Drained() && !journal.Start(2, 200, 300), "inflight replaced");
		journal.Finish(b);
		journal.Finish(a);
		auto snap = journal.Read(150, 0, 256);
		Require(snap.retained == 2 && snap.dropped == 2 && snap.inFlight == 0 && !snap.active, "retention accounting failed");
		Require(snap.records[0].entry == 110 && snap.records[1].sequence == 2, "completion ordering lost");
		Require(journal.Read(150, 1, 1).records.size() == 1 && journal.Read(150, 99, 1).records.empty(), "pagination failed");
		Require(journal.Start(2, 200, 300), "drained restart failed");
		Require(!journal.Begin(300, c) && !journal.Read(300, 0, 1).active, "deadline not enforced");
		Require(journal.Start(3, 300, 400), "restart after expiry failed");
		Require(journal.Begin(310, c), "producer not admitted");
		std::thread stop([&] { journal.Stop(); });
		stop.join();
		journal.Finish(c);
		Require(journal.Drained() && journal.Read(500, 0, 256).retained == 1, "stop lost admitted producer");
		std::cout << "Presentation policy, bounded capture, deadline, pagination and stop/drain passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
