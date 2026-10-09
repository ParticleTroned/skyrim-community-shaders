#include "Features/VR/WandTriggerEdges.h"
#include "OCUPointerAPI.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace
{
	using namespace ocu_pointer;
	Snapshot publication{};
	LeaseRequest lastRequest{};
	HMODULE appModule = reinterpret_cast<HMODULE>(1);
	HMODULE systemModule = nullptr;
	bool appQueryExport = true;
	bool appLeaseExport = true;
	bool systemExports = true;
	bool queryThrows = false;
	bool leaseThrows = false;
	bool leaseBusy = false;
	bool badResponse = false;
	bool responseAdvanced = false;
	bool clockAvailable = true;
	std::uint32_t queryResult = 0;
	std::uint32_t allowedHands = 3;
	std::uint32_t ownedHands = 0;
	std::uint64_t focusToken = 0;
	std::int64_t nowQpc = 1000000;
	int queryCalls = 0;
	int leaseCalls = 0;
	int releaseCalls = 0;
	int moduleChecks = 0;
	int lastQueryProvider = 0;

	void ResetProvider()
	{
		publication = {};
		lastRequest = {};
		appModule = reinterpret_cast<HMODULE>(1);
		systemModule = nullptr;
		appQueryExport = appLeaseExport = systemExports = true;
		queryThrows = leaseThrows = leaseBusy = badResponse = responseAdvanced = false;
		clockAvailable = true;
		queryResult = ownedHands = 0;
		allowedHands = 3;
		focusToken = 0;
		nowQpc = 1000000;
		queryCalls = leaseCalls = releaseCalls = moduleChecks = lastQueryProvider = 0;
	}

	void Publish(std::uint64_t a_frame, std::uint64_t a_session = 1)
	{
		const auto leftFlags = publication.hands[0].flags;
		const auto rightFlags = publication.hands[1].flags;
		const auto leftSequence = publication.hands[0].triggerSequence;
		const auto rightSequence = publication.hands[1].triggerSequence;
		publication = {};
		publication.structSize = sizeof(publication);
		publication.version = Version;
		publication.sessionId = a_session;
		publication.frameId = a_frame;
		publication.clientToken = focusToken;
		publication.publicationQpc = nowQpc;
		publication.qpcFrequency = 1000000;
		publication.trackingOrigin = lastRequest.trackingOrigin;
		publication.appliedPitchTrimDegrees = lastRequest.pitchTrimDegrees;
		publication.ownedHands = ownedHands;
		publication.style = {
			{ { 1.0f, 240.0f / 255.0f, 220.0f / 255.0f, 200.0f / 255.0f },
				{ 55.0f / 255.0f, 145.0f / 255.0f, 1.0f, 220.0f / 255.0f } },
			{ { 1.0f, 250.0f / 255.0f, 240.0f / 255.0f, 0.95f },
				{ 225.0f / 255.0f, 245.0f / 255.0f, 1.0f, 0.95f } }
		};
		for (auto& hand : publication.hands) {
			hand.direction[2] = -1.0f;
			hand.flags = RayValid | InputAvailable;
		}
		publication.hands[0].flags |= leftFlags & (TriggerDown | SuppressedUntilRelease);
		publication.hands[1].flags |= rightFlags & (TriggerDown | SuppressedUntilRelease);
		publication.hands[0].triggerSequence = leftSequence;
		publication.hands[1].triggerSequence = rightSequence;
	}

	std::uint32_t __cdecl Query(std::uint32_t a_version, std::uint32_t a_bytes, Snapshot* a_output)
	{
		++queryCalls;
		lastQueryProvider = 1;
		if (queryThrows)
			throw std::runtime_error("injected query exception");
		if (a_version != Version || a_bytes != sizeof(publication) || !a_output)
			throw std::runtime_error("query ABI mismatch");
		if (queryResult == 0) {
			*a_output = publication;
			a_output->ownedHands = ownedHands;
		}
		return queryResult;
	}
	std::uint32_t __cdecl SystemQuery(std::uint32_t a_version, std::uint32_t a_bytes, Snapshot* a_output)
	{
		const auto result = Query(a_version, a_bytes, a_output);
		lastQueryProvider = 2;
		return result;
	}
	std::uint32_t __cdecl UpdateLease(std::uint32_t a_version, std::uint32_t a_bytes,
		const LeaseRequest* a_request, std::uint32_t a_outputBytes, LeaseResponse* a_output)
	{
		++leaseCalls;
		if (leaseThrows)
			throw std::runtime_error("injected lease exception");
		if (a_version != Version || a_bytes != sizeof(LeaseRequest) || !a_request ||
			a_outputBytes != sizeof(LeaseResponse) || !a_output ||
			a_request->structSize != sizeof(LeaseRequest) || a_request->version != Version || a_request->clientToken == 0)
			throw std::runtime_error("lease ABI mismatch");
		if (a_request->flags == 0) {
			++releaseCalls;
			if (a_request->clientToken == focusToken) {
				focusToken = 0;
				ownedHands = 0;
			}
		} else {
			if (leaseBusy || (focusToken != 0 && focusToken != a_request->clientToken))
				return static_cast<std::uint32_t>(Result::Busy);
			if (a_request->handMask != 0 && (a_request->sessionId != publication.sessionId || a_request->frameId != publication.frameId))
				return static_cast<std::uint32_t>(Result::Stale);
			focusToken = a_request->clientToken;
			ownedHands = a_request->handMask & allowedHands;
			lastRequest = *a_request;
		}
		*a_output = {};
		a_output->structSize = sizeof(LeaseResponse);
		a_output->version = badResponse ? 99 : Version;
		a_output->sessionId = publication.sessionId;
		a_output->frameId = publication.frameId + (responseAdvanced ? 1 : 0);
		a_output->ownedHands = ownedHands;
		return 0;
	}

	HMODULE MockGetModuleHandleW(LPCWSTR a_name)
	{
		++moduleChecks;
		if (std::wcscmp(a_name, L"openvr_api.dll") == 0)
			return appModule;
		if (std::wcscmp(a_name, L"vrclient_x64.dll") == 0)
			return systemModule;
		throw std::runtime_error("unexpected runtime module");
	}
	FARPROC MockGetProcAddress(HMODULE a_module, LPCSTR a_name)
	{
		const bool queryName = std::strcmp(a_name, "OCU_GetPointerStateV1") == 0;
		const bool leaseName = std::strcmp(a_name, "OCU_UpdatePointerLeaseV1") == 0;
		if (!queryName && !leaseName)
			throw std::runtime_error("unexpected optional export");
		if (a_module == appModule) {
			if (queryName)
				return appQueryExport ? reinterpret_cast<FARPROC>(&Query) : nullptr;
			return appLeaseExport ? reinterpret_cast<FARPROC>(&UpdateLease) : nullptr;
		}
		if (a_module == systemModule && systemExports)
			return queryName ? reinterpret_cast<FARPROC>(&SystemQuery) : reinterpret_cast<FARPROC>(&UpdateLease);
		return nullptr;
	}
	BOOL MockQueryPerformanceCounter(LARGE_INTEGER* a_value)
	{
		a_value->QuadPart = nowQpc;
		return clockAvailable;
	}
	BOOL MockQueryPerformanceFrequency(LARGE_INTEGER* a_value)
	{
		a_value->QuadPart = 1000000;
		return clockAvailable;
	}
}

// Substitute the OS boundary; keep production snapshot and ownership behavior.
#define GetModuleHandleW MockGetModuleHandleW
#define GetProcAddress MockGetProcAddress
#define QueryPerformanceCounter MockQueryPerformanceCounter
#define QueryPerformanceFrequency MockQueryPerformanceFrequency
#include "Features/VR/OCUPointerClient.h"
#undef GetModuleHandleW
#undef GetProcAddress
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency

int main()
{
	int assertions = 0;
	auto require = [&](bool a_condition, const char* a_label) {
		++assertions;
		if (!a_condition)
			throw std::runtime_error(a_label);
	};
	using OCUPointer::Client;
	const float distances[2]{ 1.0f, 2.0f };
	try {
		{
			ResetProvider();
			Client client;
			require(!client.ReadForFrame(1, 1, -25.0f, false).providerAvailable && moduleChecks == 0,
				"disabled pointer must not query modules");
			appModule = nullptr;
			require(!client.ReadForFrame(1, 1, -25.0f, true).providerAvailable && queryCalls == 0,
				"SteamVR/no loaded OCU must preserve standalone fallback");
			appModule = reinterpret_cast<HMODULE>(1);
			appLeaseExport = false;
			require(!client.ReadForFrame(1, 1, -25.0f, true).providerAvailable && queryCalls == 0,
				"partial API cannot mix pointer reads with unavailable input ownership");
			systemModule = reinterpret_cast<HMODULE>(2);
			require(client.ReadForFrame(1, 1, -25.0f, true).providerAvailable && !client.ReadForFrame(1, 1, -25.0f, true).valid,
				"system-wide OCU is discovered behind incompatible app loader and starts warming");
			require(focusToken != 0 && lastRequest.flags == MenuVisible && ownedHands == 0 && releaseCalls == 0,
				"initial missing publication keeps visible zero-hand observer alive");
			Publish(1);
			require(client.ReadForFrame(2, 1, -25.0f, true).valid && lastQueryProvider == 2,
				"next runtime publication activates warmed observer");
			require(client.GetFrame().snapshot.style.beamColor[0][0] == 1.0f &&
						client.GetFrame().snapshot.style.dotColor[1][3] == 0.95f,
				"authoritative idle and pressed colors preserve straight-alpha sRGB values");
			const auto calls = queryCalls;
			require(client.ReadForFrame(2, 1, -25.0f, true).valid && queryCalls == calls,
				"repeated input callbacks share one renderer-frame snapshot");
			require(client.UpdateOwnership(3, 1, distances) && client.GetOwnedHands() == 3,
				"both hands may own input while only one displays a hit beam");
			require(lastRequest.pitchTrimDegrees == -25.0f && lastRequest.flags == (MenuVisible | HasLeftHit) &&
						lastRequest.hitDistance[0] == 1.0f && lastRequest.hitDistance[1] == 0.0f,
				"pitch and selected beam endpoint cross the agreed ABI once");
			const auto oldGeneration = client.GetGeneration();
			appLeaseExport = true;
			require(client.ReadForFrame(2, 1, -25.0f, true).providerAvailable && client.GetGeneration() != oldGeneration && client.GetOwnedHands() == 0,
				"app-local provider replaces system provider and cancels cached ownership");
			Publish(1);
			require(client.ReadForFrame(3, 1, -25.0f, true).valid && lastQueryProvider == 1,
				"app-local provider accepts its own first publication after switch");
			appModule = nullptr;
			systemModule = nullptr;
			require(!client.ReadForFrame(3, 1, -25.0f, true).providerAvailable && client.GetOwnedHands() == 0,
				"provider disappearance clears same-frame shared pointer state");
		}
		{
			ResetProvider();
			Client client;
			client.ReadForFrame(1, 1, -25.0f, true);
			Publish(1);
			publication.hands[0].flags |= TriggerDown;
			publication.hands[0].triggerSequence = 10;
			require(client.ReadForFrame(2, 1, -25.0f, true).valid && client.UpdateOwnership(1, 1, distances),
				"held trigger can obtain pointer ownership");
			require(!client.TriggerDown(0) && !client.TriggerEligible(0) && client.TriggerSequence(0) == 10,
				"held trigger during acquisition cannot click through");
			Publish(2);
			publication.hands[0].flags &= ~TriggerDown;
			publication.hands[0].triggerSequence = 11;
			client.ReadForFrame(3, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			require(!client.TriggerDown(0) && client.TriggerEligible(0), "physical release arms without generating a press");
			Publish(3);
			publication.hands[0].flags |= TriggerDown;
			publication.hands[0].triggerSequence = 12;
			client.ReadForFrame(4, 1, -25.0f, true);
			client.UpdateOwnership(1, 0, distances);
			require(client.TriggerDown(0) && !client.TriggerDown(1) && !client.TriggerDown(9),
				"owned trigger holds through dragging off the visible surface");
			allowedHands = 0;
			require(!client.UpdateOwnership(1, 0, distances) && !client.TriggerDown(0) && client.GetOwnedHands() == 0,
				"keyboard preemption immediately releases consumer input");
			allowedHands = 3;
			require(client.UpdateOwnership(1, 1, distances) && !client.TriggerDown(0),
				"reacquiring after preemption cannot replay a still-held trigger");
			Publish(4);
			publication.hands[0].flags &= ~TriggerDown;
			client.ReadForFrame(5, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			Publish(5);
			publication.hands[0].flags |= TriggerDown | SuppressedUntilRelease;
			client.ReadForFrame(6, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			require(!client.TriggerDown(0) && !client.TriggerEligible(0), "provider's release latch is respected independently of client baseline");
			Publish(6);
			publication.hands[0].flags &= ~SuppressedUntilRelease;
			client.ReadForFrame(7, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			require(!client.TriggerDown(0), "clearing provider suppression while still held does not rearm client");
			const auto releases = releaseCalls;
			client.Release();
			require(releaseCalls == releases + 1 && focusToken == 0 && client.GetOwnedHands() == 0 && !client.TriggerDown(0),
				"menu close explicitly releases provider focus and any held input");
			client.Release();
			require(releaseCalls == releases + 1, "repeated close does not call an already-released lease");
		}
		{
			ResetProvider();
			Client client;
			client.ReadForFrame(1, 1, -25.0f, true);
			Publish(10);
			client.ReadForFrame(2, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			publication.hands[0].flags |= TriggerDown;
			Publish(11);
			require(client.ReadForFrame(3, 1, -25.0f, true).valid && client.TriggerDown(0), "owned fresh press reaches consumer");
			nowQpc += 100001;
			require(!client.ReadForFrame(4, 1, -25.0f, true).valid && client.GetOwnedHands() == 0 && !client.TriggerDown(0),
				"stale provider cancels held input");
			require(focusToken != 0 && ownedHands == 0 && releaseCalls == 0,
				"stale provider retains only observation so a fresh publication can recover");
			Publish(12);
			require(client.ReadForFrame(5, 1, -25.0f, true).valid && client.UpdateOwnership(1, 1, distances) && !client.TriggerDown(0),
				"stale recovery requires physical release before accepting another press");
			require(client.ReadForFrame(6, 1, -25.0f, true).valid,
				"repeated runtime sample remains usable within publication age budget");
			Publish(11);
			require(!client.ReadForFrame(7, 1, -25.0f, true).valid, "same-session frame rollback is rejected");
			const auto oldGeneration = client.GetGeneration();
			Publish(1, 2);
			require(client.ReadForFrame(8, 1, -25.0f, true).valid && client.GetGeneration() != oldGeneration,
				"new session epoch resets provider frame history and cancels old generation");
			Publish(2, 2);
			publication.trackingOrigin = 0;
			require(!client.ReadForFrame(9, 1, -25.0f, true).valid, "tracking origin mismatch cannot produce a cursor");
			Publish(3, 2);
			publication.appliedPitchTrimDegrees = 0.0f;
			require(!client.ReadForFrame(10, 1, -25.0f, true).valid, "old pitch publication waits for provider recalibration");
			Publish(4, 2);
			require(client.ReadForFrame(11, 1, -25.0f, true).valid, "matching calibrated publication recovers");
			require(!client.ReadForFrame(11, 1, -20.0f, true).valid && lastRequest.pitchTrimDegrees == -20.0f,
				"same-frame pitch change drops old cache and begins recalibrated observation");
			Publish(5, 2);
			require(client.ReadForFrame(12, 1, -20.0f, true).valid, "new requested trim becomes active only when published");
			client.Release();
		}
		{
			ResetProvider();
			Client client;
			client.ReadForFrame(1, 1, -25.0f, true);
			std::uint64_t frame = 2;
			auto rejects = [&](auto a_corrupt, const char* a_label) {
				Publish(frame);
				a_corrupt();
				require(!client.ReadForFrame(frame++, 1, -25.0f, true).valid && client.GetOwnedHands() == 0, a_label);
			};
			rejects([] { publication.version = 9; }, "wrong version is rejected");
			rejects([] { publication.structSize -= 8; }, "wrong ABI size is rejected");
			rejects([] { publication.clientToken += 1; }, "another client's ray cannot drive this pointer");
			rejects([] { publication.publicationQpc = nowQpc + 1; }, "future publication is rejected");
			rejects([] { publication.qpcFrequency = 42; }, "incompatible QPC frequency is rejected");
			rejects([] { publication.hands[0].origin[0] = std::numeric_limits<float>::infinity(); }, "nonfinite origin is rejected");
			rejects([] { publication.hands[1].direction[2] = 0.0f; }, "zero direction is rejected");
			rejects([] { publication.hands[1].direction[2] = -2.0f; }, "nonnormalized direction is rejected");
			rejects([] { publication.hands[0].flags |= 0x80000000u; }, "unknown hand flags are rejected");
			rejects([] { publication.reserved = 1; }, "nonzero reserved ABI field is rejected");
			rejects([] { publication.style.beamColor[0][0] = std::numeric_limits<float>::quiet_NaN(); }, "nonfinite beam style is rejected");
			rejects([] { publication.style.dotColor[1][3] = -0.1f; }, "negative dot alpha is rejected");
			rejects([] { publication.style.beamColor[1][2] = 1.01f; }, "out-of-range beam color is rejected");
			clockAvailable = false;
			rejects([] {}, "clock failure cannot keep input alive");
			clockAvailable = true;
			queryThrows = true;
			rejects([] {}, "query exception cancels shared input without escaping client");
			queryThrows = false;
			queryResult = static_cast<std::uint32_t>(Result::UnsupportedVersion);
			rejects([] {}, "unsupported query cannot reuse old publication");
			queryResult = 0;
			Publish(frame);
			require(client.ReadForFrame(frame++, 1, -25.0f, true).valid, "fresh valid publication recovers after query failures");
			const float invalidDistances[2]{ std::numeric_limits<float>::quiet_NaN(), 0.0f };
			require(!client.UpdateOwnership(1, 1, invalidDistances) && client.GetOwnedHands() == 0,
				"invalid endpoint cannot reach provider ownership");
			Publish(frame);
			client.ReadForFrame(frame++, 1, -25.0f, true);
			responseAdvanced = true;
			require(!client.UpdateOwnership(1, 1, distances) && client.GetOwnedHands() == 0,
				"endpoint cannot claim a ray from a later provider frame");
			responseAdvanced = false;
			Publish(frame);
			client.ReadForFrame(frame++, 1, -25.0f, true);
			badResponse = true;
			require(!client.UpdateOwnership(1, 1, distances) && !client.TriggerDown(0), "malformed lease result cannot grant input");
			badResponse = false;
			Publish(frame);
			client.ReadForFrame(frame++, 1, -25.0f, true);
			leaseThrows = true;
			require(!client.UpdateOwnership(1, 1, distances) && client.GetOwnedHands() == 0,
				"lease exception cannot retain input ownership");
			leaseThrows = false;
			client.Release();
		}
		{
			ResetProvider();
			Client client;
			leaseBusy = true;
			const auto& frame = client.ReadForFrame(1, 1, -25.0f, true);
			require(frame.providerAvailable && !frame.valid && queryCalls == 0 && client.GetOwnedHands() == 0,
				"another menu's exclusive focus fails closed while API remains available");
			leaseBusy = false;
			client.ReadForFrame(2, 1, -25.0f, true);
			Publish(1);
			require(client.ReadForFrame(3, 1, -25.0f, true).valid, "focus release permits observation and recovery");
			client.UpdateOwnership(1, 1, distances);
			nowQpc += 100001;
			require(!client.ReadForFrame(3, 1, -25.0f, true).valid && client.GetOwnedHands() == 0,
				"repeated callback cannot extend stale input merely by retaining renderer frame ID");
			client.Release();
		}
		{
			ResetProvider();
			Client client;
			client.ReadForFrame(1, 1, -25.0f, true);
			Publish(1);
			client.ReadForFrame(2, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			WandTriggerEdges history;
			history.Update(0, false, true, client.GetHandGeneration(0));
			const auto generation = client.GetHandGeneration(0);
			const auto sourceGeneration = client.GetGeneration();
			ownedHands = 0;
			Publish(2);
			publication.hands[0].triggerSequence = 2;
			client.ReadForFrame(3, 1, -25.0f, true);
			client.UpdateOwnership(1, 1, distances);
			require(client.GetHandGeneration(0) != generation,
				"lost and reacquired ownership invalidates prior trigger history");
			const auto edges = history.Update(client.TriggerSequence(0), client.TriggerDown(0),
				client.TriggerEligible(0), client.GetHandGeneration(0));
			require(edges.cancel && edges.count == 0,
				"a tap sampled during another owner's interval cannot replay after reacquisition");
			const auto reacquiredGeneration = client.GetHandGeneration(0);
			allowedHands = 0;
			client.UpdateOwnership(1, 1, distances);
			require(client.GetHandGeneration(0) != reacquiredGeneration,
				"ownership revoked in a lease response cancels input in the same frame");
			require(client.GetGeneration() == sourceGeneration && client.GetHandGeneration(1) == 0,
				"another hand's ownership loss leaves the provider and that hand's input history unchanged");
			client.Release();
		}
		{
			WandTriggerEdges history;
			auto edges = history.Update(100, false, true, 1);
			require(edges.count == 0 && !edges.cancel, "first released sample establishes edge baseline");
			edges = history.Update(102, false, true, 1);
			require(edges.count == 2 && edges.states[0] && !edges.states[1] && !edges.cancel,
				"skipped down/up sample recovers one fast tap in order");
			edges = history.Update(102, false, true, 1);
			require(edges.count == 0 && !edges.cancel, "repeated sample never duplicates a recovered tap");
			edges = history.Update(103, true, true, 1);
			require(edges.count == 1 && edges.states[0], "single press produces one edge");
			edges = history.Update(105, true, true, 1);
			require(edges.count == 2 && !edges.states[0] && edges.states[1], "release/repress during drag preserves edge order");
			edges = history.Update(106, false, true, 1);
			require(edges.count == 1 && !edges.states[0], "single release completes held click");
			edges = history.Update(107, true, true, 2);
			require(edges.cancel && edges.count == 0, "generation change cannot replay another session's press");
			edges = history.Update(108, false, true, 2);
			require(edges.count == 0 && !edges.cancel, "release after changed generation rearms without replay");
			edges = history.Update(109, true, true, 2);
			require(edges.count == 1 && edges.states[0], "fresh press after generation baseline is accepted");
			edges = history.Update(109, true, false, 2);
			require(edges.cancel && edges.count == 0, "ownership or eligibility loss cancels pending input");
			edges = history.Update(111, true, false, 2);
			require(!edges.cancel && edges.count == 0, "ineligible missed release/press cannot create a tap");
			edges = history.Update(112, false, true, 2);
			require(!edges.cancel && edges.count == 0, "restored eligibility starts from observed release");
			edges = history.Update(116, false, true, 2);
			require(edges.cancel && edges.count == 0, "more than two missed edges are cancelled instead of replayed");
			edges = history.Update(115, true, true, 2);
			require(edges.cancel && edges.count == 0, "sequence rollback cancels rather than underflowing");
			edges = history.Update(115, false, true, 2);
			require(edges.cancel && edges.count == 0, "state change without sequence change violates parity");
			edges = history.Update(117, true, true, 2);
			require(edges.cancel && edges.count == 0, "two transitions cannot change final state parity");
			history.Reset();
			edges = history.Update((std::numeric_limits<std::uint64_t>::max)(), false, true, 3);
			require(edges.count == 0 && !edges.cancel, "maximum sequence is a valid initial baseline");
			edges = history.Update(0, true, true, 3);
			require(edges.cancel && edges.count == 0, "sequence wrap is treated as cancellation");
		}
		std::cout << "PASS: " << assertions << " pointer ABI, discovery, freshness, lease and input assertions\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
