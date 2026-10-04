#include "FeatureCreationObserver.h"

#include <cstdint>
#include <iostream>
#include <thread>

int main()
{
	try {
		const auto require = [](bool condition) {
			if (!condition)
				throw std::runtime_error("feature creation observer contract failed");
		};
		NrReplay::FeatureCreationObserver::Observe(nullptr);
		unsigned calls = 0;
		const auto* parameters = reinterpret_cast<const NVSDK_NGX_Parameter*>(std::uintptr_t{ 0x1000 });
		{
			NrReplay::FeatureCreationObserver owner([&](const auto* observed) { require(observed == parameters); ++calls; });
			NrReplay::FeatureCreationObserver::Observe(parameters);
			require(calls == 1);
			bool rejected = false;
			try {
				NrReplay::FeatureCreationObserver duplicate([](const auto*) {});
			} catch (const std::runtime_error&) {
				rejected = true;
			}
			require(rejected);
			rejected = false;
			try {
				NrReplay::FeatureCreationObserver::Observe(nullptr);
			} catch (const std::runtime_error&) {
				rejected = true;
			}
			require(rejected && calls == 1);
			rejected = false;
			std::thread foreign([&] {
				try {
					NrReplay::FeatureCreationObserver::Observe(parameters);
				} catch (const std::runtime_error&) {
					rejected = true;
				}
			});
			foreign.join();
			require(rejected && calls == 1);
			NrReplay::FeatureCreationObserver::Observe(parameters);
			require(calls == 2);
		}
		NrReplay::FeatureCreationObserver::Observe(parameters);
		require(calls == 2);
		bool propagated = false;
		try {
			NrReplay::FeatureCreationObserver failing([](const auto*) { throw std::runtime_error("admission failure"); });
			NrReplay::FeatureCreationObserver::Observe(parameters);
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		require(propagated);
		NrReplay::FeatureCreationObserver::Observe(parameters);
		std::cout << "Feature creation observation ownership and failure checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
