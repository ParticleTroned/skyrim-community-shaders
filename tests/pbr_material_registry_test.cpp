#include "TruePBR/MaterialRegistry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	struct Material
	{};

	struct Extensions
	{
		std::uint64_t revision = 0;
		std::uint64_t complement = ~std::uint64_t{ 0 };
		int owner = 0;
	};

	using Registry = PBRMaterialRegistry<Material, Extensions>;

	void Require(bool condition)
	{
		if (!condition) {
			std::abort();
		}
	}

	void TestMetadataLifecycle()
	{
		Registry registry;
		Material source, destination, missing;
		Require(!registry.Contains(&source));
		Require(!registry.Contains(nullptr));
		registry.Update(&source, [](auto& extensions) {
			extensions.revision = 42;
			extensions.complement = ~std::uint64_t{ 42 };
		});
		registry.Register(&source);
		registry.Copy(&destination, &source);
		registry.Copy(&source, &source);
		int visits = 0;
		registry.ForEach([&](auto*, const auto& extensions) {
			Require(extensions.revision == 42);
			Require(extensions.complement == ~extensions.revision);
			++visits;
		});
		Require(visits == 2);
		registry.Copy(&destination, &missing);
		registry.ForEach([&](auto* material, const auto& extensions) {
			Require(extensions.revision == (material == &source ? 42u : 0u));
		});
		registry.Unregister(&source);
		registry.Unregister(&source);
		Require(!registry.Contains(&source));
		registry.Register(&source);
		registry.ForEach([](auto*, const auto& extensions) { Require(extensions.revision == 0); });
	}

	void TestLandscapeMetadata()
	{
		PBRMaterialRegistry<Material, std::array<int*, 6>> registry;
		Material source, destination;
		int textureData = 7;
		registry.Register(&source);
		registry.Update(&source, [&](auto& textures) { textures[2] = &textureData; });
		registry.Copy(&destination, &source);
		registry.Unregister(&source);
		registry.ForEach([&](auto* material, const auto& textures) {
			Require(material == &destination);
			for (std::size_t i = 0; i < textures.size(); ++i) {
				Require(textures[i] == (i == 2 ? &textureData : nullptr));
			}
		});
	}

	void TestRemovedMaterialIsNotRecreated()
	{
		Registry registry;
		Material material;
		bool called = false;
		const auto update = [&](auto& extensions) {
			called = true;
			extensions.owner = 7;
		};
		Require(!registry.TryUpdate(&material, update));
		Require(!called);
		registry.Register(&material);
		Require(registry.TryUpdate(&material, update));
		Require(called);
		registry.ForEach([](auto*, const auto& extensions) { Require(extensions.owner == 7); });

		Require(registry.Contains(&material));
		registry.Unregister(&material);
		called = false;
		Require(!registry.TryUpdate(&material, update));
		Require(!called);
		Require(!registry.Contains(&material));
	}

	void TestConcurrentOwnershipAndUpdates()
	{
		Registry registry;
		Material material;
		registry.Register(&material);
		constexpr int threadCount = 8;
		constexpr int updateCount = 10000;
		std::latch start(threadCount);
		std::atomic<int> owners = 0;
		std::vector<std::jthread> threads;
		for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
			threads.emplace_back([&, threadIndex] {
				start.arrive_and_wait();
				Require(registry.TryUpdate(&material, [&](auto& extensions) {
					if (extensions.owner == 0) {
						extensions.owner = threadIndex + 1;
						owners.fetch_add(1);
					}
				}));
				for (int update = 0; update < updateCount; ++update) {
					registry.Update(&material, [](auto& extensions) {
						++extensions.revision;
						extensions.complement = ~extensions.revision;
					});
				}
			});
		}
		threads.clear();
		Require(owners.load() == 1);
		registry.ForEach([](auto*, const auto& extensions) {
			Require(extensions.revision == threadCount * updateCount);
			Require(extensions.complement == ~extensions.revision);
		});
	}

	void TestConcurrentGrowthLookupCopyAndErase()
	{
		Registry registry;
		Material stable;
		registry.Register(&stable);
		constexpr int writerCount = 4;
		constexpr int readerCount = 4;
		constexpr int materialCount = 2048;
		std::array<std::array<Material, materialCount>, writerCount> materials;
		std::latch start(writerCount + readerCount);
		std::vector<std::jthread> threads;
		for (int writer = 0; writer < writerCount; ++writer) {
			threads.emplace_back([&, writer] {
				start.arrive_and_wait();
				for (auto& material : materials[writer]) {
					registry.Register(&material);
					registry.Update(&material, [](auto& extensions) {
						extensions.revision = 17;
						std::this_thread::yield();
						extensions.complement = ~extensions.revision;
					});
				}
				for (auto& material : materials[writer]) {
					registry.Copy(&material, &stable);
					registry.Unregister(&material);
				}
			});
		}
		for (int reader = 0; reader < readerCount; ++reader) {
			threads.emplace_back([&, reader] {
				start.arrive_and_wait();
				for (int iteration = 0; iteration < 20000; ++iteration) {
					Require(registry.Contains(&stable));
					registry.Contains(&materials[reader][iteration % materialCount]);
					if (iteration % 128 == 0) {
						registry.ForEach([](auto*, const auto& extensions) {
							Require(extensions.complement == ~extensions.revision);
						});
					}
				}
			});
		}
		threads.clear();
		int remaining = 0;
		registry.ForEach([&](auto* material, const auto&) {
			Require(material == &stable);
			++remaining;
		});
		Require(remaining == 1);
	}

	void TestVisitExcludesDestruction()
	{
		struct TrackedMaterial
		{
			PBRMaterialRegistry<TrackedMaterial, Extensions>& registry;
			std::latch& removing;
			std::atomic<bool>& destroyed;
			int payload = 42;

			~TrackedMaterial()
			{
				removing.count_down();
				registry.Unregister(this);
				payload = 0;
				destroyed.store(true);
			}
		};

		PBRMaterialRegistry<TrackedMaterial, Extensions> registry;
		std::latch visiting(1), releaseVisitor(1), removing(1);
		std::atomic<bool> destroyed = false;
		auto material = std::make_unique<TrackedMaterial>(registry, removing, destroyed);
		registry.Register(material.get());
		auto visitor = std::async(std::launch::async, [&] {
			registry.ForEach([&](auto* entry, const auto&) {
				Require(entry->payload == 42);
				visiting.count_down();
				releaseVisitor.wait();
				Require(entry->payload == 42);
			});
		});
		visiting.wait();
		auto destructor = std::async(std::launch::async, [&] {
			material.reset();
		});
		removing.wait();
		Require(destructor.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
		Require(!destroyed.load());
		releaseVisitor.count_down();
		visitor.get();
		destructor.get();
		Require(destroyed.load());
		registry.ForEach([](auto*, const auto&) { Require(false); });
	}

	void TestLookupWaitsForPublication()
	{
		Registry registry;
		Material material;
		std::latch updating(1), releaseWriter(1), reading(1);
		auto writer = std::async(std::launch::async, [&] {
			registry.Update(&material, [&](auto& extensions) {
				extensions.revision = 42;
				updating.count_down();
				releaseWriter.wait();
				extensions.complement = ~extensions.revision;
			});
		});
		updating.wait();
		auto reader = std::async(std::launch::async, [&] {
			reading.count_down();
			return registry.Contains(&material);
		});
		reading.wait();
		Require(reader.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
		releaseWriter.count_down();
		writer.get();
		Require(reader.get());
		registry.ForEach([](auto*, const auto& extensions) {
			Require(extensions.revision == 42);
			Require(extensions.complement == ~extensions.revision);
		});
	}

	void TestCallbackExceptionReleasesLock()
	{
		Registry registry;
		Material material;
		const auto expectReleasedLock = [&](auto&& action) {
			bool caught = false;
			try {
				action();
			} catch (const std::runtime_error&) {
				caught = true;
			}
			Require(caught);
			Require(registry.Contains(&material));
			registry.Unregister(&material);
			registry.Register(&material);
		};
		expectReleasedLock([&] {
			registry.Update(&material, [](auto&) { throw std::runtime_error("callback"); });
		});
		expectReleasedLock([&] {
			registry.TryUpdate(&material, [](auto&) { throw std::runtime_error("callback"); });
		});
		expectReleasedLock([&] {
			registry.ForEach([](auto*, auto&) { throw std::runtime_error("callback"); });
		});
	}
}

int main()
{
	TestMetadataLifecycle();
	TestLandscapeMetadata();
	TestRemovedMaterialIsNotRecreated();
	TestConcurrentOwnershipAndUpdates();
	TestConcurrentGrowthLookupCopyAndErase();
	TestVisitExcludesDestruction();
	TestLookupWaitsForPublication();
	TestCallbackExceptionReleasesLock();
}
