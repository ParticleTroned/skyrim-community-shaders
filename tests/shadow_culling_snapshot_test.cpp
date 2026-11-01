#include "Features/LightLimitFix/ShadowCullingSnapshot.h"

#include <array>
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>
#include <thread>

namespace
{
	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Shadow snapshot assertion failed at " << a_location.line() << '\n';
			std::abort();
		}
	}

	struct Demand
	{
		Demand(unsigned a_sequence, std::atomic<unsigned>& a_destroyed) : sequence(a_sequence), destroyed(a_destroyed)
		{
			values.fill(sequence);
		}
		~Demand() { ++destroyed; }
		const unsigned sequence;
		std::array<unsigned, 72> values;
		std::atomic<unsigned>& destroyed;
	};
	using Publication = LightLimitFixDetail::ShadowCulling::SnapshotPublication<Demand>;

	void ReplacementAndClear()
	{
		std::atomic<unsigned> destroyed{ 0 };
		Publication publication;
		Publication::Reader reader{ publication };
		Require(!reader.Acquire());
		publication.Publish(std::make_shared<Demand>(1, destroyed));
		auto first = reader.Acquire();
		Require(first && first->sequence == 1);
		for (unsigned i = 0; i < 1000; ++i)
			Require(reader.Acquire() == first);

		publication.Publish(std::make_shared<Demand>(2, destroyed));
		auto second = reader.Acquire();
		Require(second && second->sequence == 2 && second != first);
		Require(first->sequence == 1 && destroyed == 0);
		publication.Publish({});
		Require(!publication.Read() && !reader.Acquire());
		Require(destroyed == 0);
		first.reset();
		second.reset();
		Require(destroyed == 2);

		publication.Publish(std::make_shared<Demand>(3, destroyed));
		Require(reader.Acquire()->sequence == 3);
		publication.Publish({});
		Require(destroyed == 3);
	}

	void IdleReaderAndInFlightOwner()
	{
		std::atomic<unsigned> destroyed{ 0 };
		Publication publication;
		publication.Publish(std::make_shared<Demand>(1, destroyed));
		std::barrier rendezvous{ 2 };
		std::jthread worker([&] {
			Publication::Reader reader{ publication };
			auto inFlight = reader.Acquire();
			rendezvous.arrive_and_wait();
			rendezvous.arrive_and_wait();
			Require(inFlight->sequence == 1 && destroyed == 0);
			Require(!reader.Acquire());
			inFlight.reset();
			rendezvous.arrive_and_wait();
			rendezvous.arrive_and_wait();
			Require(reader.Acquire()->sequence == 2);
			rendezvous.arrive_and_wait();
			rendezvous.arrive_and_wait();
			Require(!reader.Acquire());
		});
		rendezvous.arrive_and_wait();
		publication.Publish({});
		rendezvous.arrive_and_wait();
		rendezvous.arrive_and_wait();
		Require(destroyed == 1);
		publication.Publish(std::make_shared<Demand>(2, destroyed));
		rendezvous.arrive_and_wait();
		rendezvous.arrive_and_wait();
		publication.Publish({});
		Require(destroyed == 2);
		rendezvous.arrive_and_wait();
	}

	void AcquireDuringRetirement()
	{
		std::atomic<unsigned> destroyed{ 0 };
		std::barrier retirement{ 2 };
		Publication publication;
		Publication::Reader reader{ publication };
		publication.Publish(std::shared_ptr<const Demand>(new Demand(1, destroyed), [&](const Demand* demand) {
			delete demand;
			retirement.arrive_and_wait();
			retirement.arrive_and_wait();
		}));
		Require(reader.Acquire()->sequence == 1);
		std::jthread publisher([&] {
			publication.Publish(std::make_shared<Demand>(2, destroyed));
		});
		retirement.arrive_and_wait();
		Require(destroyed == 1);
		Require(!reader.Acquire());
		retirement.arrive_and_wait();
		publisher.join();
		auto replacement = reader.Acquire();
		Require(replacement && replacement->sequence == 2);
		publication.Publish({});
		Require(!reader.Acquire() && destroyed == 1);
		replacement.reset();
		Require(destroyed == 2);
	}

	void ConcurrentPublication()
	{
		std::atomic<unsigned> destroyed{ 0 };
		Publication publication;
		constexpr unsigned rounds = 100;
		constexpr unsigned publicationsPerRound = 100;
		std::barrier rendezvous{ 5 };
		std::array<std::jthread, 4> workers;
		publication.Publish(std::make_shared<Demand>(0, destroyed));
		for (auto& worker : workers) {
			worker = std::jthread([&] {
				Publication::Reader reader{ publication };
				Require(reader.Acquire()->sequence == 0);
				unsigned last = 0;
				rendezvous.arrive_and_wait();
				for (unsigned round = 1; round <= rounds; ++round) {
					for (unsigned attempt = 0; attempt < publicationsPerRound; ++attempt) {
						if (const auto captured = reader.Acquire()) {
							Require(captured->sequence >= last);
							last = captured->sequence;
							for (const auto value : captured->values)
								Require(value == last);
						}
					}
					rendezvous.arrive_and_wait();
					// Every reader must observe replacement, even if scheduling serializes the burst.
					const auto captured = reader.Acquire();
					Require(captured && captured->sequence == round * publicationsPerRound);
					last = captured->sequence;
					rendezvous.arrive_and_wait();
				}
				rendezvous.arrive_and_wait();
				Require(!reader.Acquire());
			});
		}
		rendezvous.arrive_and_wait();
		for (unsigned round = 1; round <= rounds; ++round) {
			for (unsigned i = 1; i <= publicationsPerRound; ++i) {
				const auto sequence = (round - 1) * publicationsPerRound + i;
				publication.Publish(std::make_shared<Demand>(sequence, destroyed));
				if (i % 4 == 0 && i != publicationsPerRound)
					publication.Publish({});
			}
			rendezvous.arrive_and_wait();
			rendezvous.arrive_and_wait();
		}
		publication.Publish({});
		rendezvous.arrive_and_wait();
		for (auto& worker : workers)
			worker.join();
		Require(destroyed == rounds * publicationsPerRound + 1);
	}
}

int main()
{
	ReplacementAndClear();
	IdleReaderAndInFlightOwner();
	AcquireDuringRetirement();
	ConcurrentPublication();
	std::cout << "Shadow culling snapshot ownership and concurrency checks passed\n";
}
