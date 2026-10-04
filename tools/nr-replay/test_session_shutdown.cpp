#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	std::vector<std::string> events;
	struct ID3D11Device
	{};
	ID3D11Device bootstrapDevice;
	ID3D11Device* shutdownDevice = nullptr;
	bool idleSucceeded = true, chainSucceeded = true, floorSucceeded = true;
	bool runtimeSucceeded = true, sdkSucceeded = true, interopSucceeded = true;
	int receiptCount = 0;
	bool NVSDK_NGX_SUCCEED(int status) { return status == 1; }
	int NVSDK_NGX_D3D11_Shutdown1(ID3D11Device* device)
	{
		events.push_back("sdk_shutdown");
		shutdownDevice = device;
		return sdkSucceeded ? 1 : 0;
	}
	namespace logger
	{
		template <class... Args>
		void info(Args&&...)
		{}
		template <class... Args>
		void error(Args&&...)
		{}
	}
	struct Probe
	{
		std::string name;
		~Probe() { events.push_back(name + "_destroy"); }
		bool Restore(bool idle)
		{
			events.push_back(name + "_restore");
			const bool succeeded = idle && (name == "chain" ? chainSucceeded : floorSucceeded);
			if (succeeded)
				++receiptCount;
			return succeeded;
		}
	};
	struct Runtime
	{
		static Runtime& Instance()
		{
			static Runtime value;
			return value;
		}
		bool Shutdown()
		{
			events.push_back("runtime_shutdown");
			return runtimeSucceeded;
		}
		void AbandonUnsafe() { events.push_back("runtime_abandon"); }
	};
	struct Interop
	{
		bool recording = false;
		bool IsRecording() const { return recording; }
		bool AbortD3D12()
		{
			events.push_back("abort");
			recording = false;
			return true;
		}
		bool WaitForIdle()
		{
			events.push_back("idle");
			return idleSucceeded;
		}
		void AbandonUnsafe() { events.push_back("interop_abandon"); }
		bool Shutdown()
		{
			events.push_back("interop_shutdown");
			return interopSucceeded;
		}
	};
	struct Session
	{
		struct Device
		{
			ID3D11Device* Get() const { return &bootstrapDevice; }
		} device;
		Interop interop;
		std::unique_ptr<Probe> providerFloor = std::make_unique<Probe>("floor");
		std::unique_ptr<Probe> kernelChain = std::make_unique<Probe>("chain");
		bool active = true, ngxInitialized = true, closeSucceeded = true;
#include "session_close_under_test.h"
	};
	void Check(bool condition)
	{
		if (!condition)
			throw std::runtime_error("session shutdown contract failed");
	}
	void Reset()
	{
		events.clear();
		shutdownDevice = nullptr;
		idleSucceeded = chainSucceeded = floorSucceeded = true;
		runtimeSucceeded = sdkSucceeded = interopSucceeded = true;
		receiptCount = 0;
	}
	void TestSuccessfulOwnershipOrder()
	{
		Reset();
		Session session;
		session.interop.recording = true;
		Check(session.Close());
		Check(events == std::vector<std::string>{ "abort", "idle", "chain_restore", "floor_restore", "chain_destroy", "floor_destroy",
							"runtime_shutdown", "sdk_shutdown", "interop_shutdown" });
		Check(receiptCount == 2 && shutdownDevice == &bootstrapDevice && !session.ngxInitialized);
		Check(!session.active && session.closeSucceeded && !session.kernelChain && !session.providerFloor);
		const auto previous = events;
		Check(session.Close() && events == previous);
	}
	void TestUnsafeRestorationRetainsOwners()
	{
		for (unsigned failure = 0; failure < 3; ++failure) {
			Reset();
			Session session;
			idleSucceeded = failure != 0;
			chainSucceeded = failure != 1;
			floorSucceeded = failure != 2;
			auto* chain = session.kernelChain.get();
			auto* floor = session.providerFloor.get();
			Check(!session.Close());
			Check(events == std::vector<std::string>{ "idle", "chain_restore", "floor_restore", "runtime_abandon", "interop_abandon" });
			Check(session.ngxInitialized && !session.closeSucceeded && !shutdownDevice);
			Check(!session.kernelChain && !session.providerFloor);
			const auto previous = events;
			Check(!session.Close() && events == previous);
			delete chain;
			delete floor;
		}
	}
	void TestRuntimeFailureStopsBootstrapRetirement()
	{
		Reset();
		Session session;
		runtimeSucceeded = false;
		Check(!session.Close());
		Check(events == std::vector<std::string>{ "idle", "chain_restore", "floor_restore", "chain_destroy", "floor_destroy",
							"runtime_shutdown", "runtime_abandon", "interop_abandon" });
		Check(receiptCount == 2 && session.ngxInitialized && !shutdownDevice);
		const auto previous = events;
		Check(!session.Close() && events == previous);
	}
	void TestTerminalFailuresRemainVisible()
	{
		for (unsigned failure = 0; failure < 2; ++failure) {
			Reset();
			Session session;
			sdkSucceeded = failure != 0;
			interopSucceeded = failure != 1;
			Check(!session.Close());
			Check(events == std::vector<std::string>{ "idle", "chain_restore", "floor_restore", "chain_destroy", "floor_destroy",
								"runtime_shutdown", "sdk_shutdown", "interop_shutdown" });
			Check(shutdownDevice == &bootstrapDevice && session.ngxInitialized == !sdkSucceeded);
			const auto previous = events;
			Check(!session.Close() && events == previous);
		}
	}
	void TestAbsentOptionalOwnersAndBootstrap()
	{
		Reset();
		Session session;
		session.kernelChain.reset();
		session.providerFloor.reset();
		session.ngxInitialized = false;
		events.clear();
		Check(session.Close());
		Check(events == std::vector<std::string>{ "idle", "runtime_shutdown", "interop_shutdown" } && !shutdownDevice);
		const auto previous = events;
		Check(session.Close() && events == previous);
	}
}

int main()
{
	try {
		TestSuccessfulOwnershipOrder();
		TestUnsafeRestorationRetainsOwners();
		TestRuntimeFailureStopsBootstrapRetirement();
		TestTerminalFailuresRemainVisible();
		TestAbsentOptionalOwnersAndBootstrap();
		std::cout << "session shutdown tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
