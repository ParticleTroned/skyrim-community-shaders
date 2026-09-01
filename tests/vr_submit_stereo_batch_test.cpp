#include "Features/Upscaling/VRSubmitStereoBatch.h"

namespace
{
	enum class Path
	{
		Inactive,
		RuntimeFSR3,
		RuntimeFSR4,
		HostFallback,
	};

	struct Proof
	{
		bool valid = false;
		std::uint32_t frame = 0;
		Path path = Path::Inactive;
		std::uint64_t serial = 0;

		bool operator==(const Proof&) const = default;
	};

	struct Region
	{
		int* color = nullptr;
		int* depth = nullptr;
		int* motionVectors = nullptr;
		int* reactiveMask = nullptr;
		int* transparencyCompositionMask = nullptr;
		int* output = nullptr;
	};

	using Cache = VRSubmitStereoBatch::State<int, int, Region, Proof>;

	struct Fixture
	{
		int source{};
		std::array<int, 12> resources{};
		std::array<Region, 2> regions{
			Region{ &resources[0], &resources[1], &resources[2], &resources[3], &resources[4], &resources[5] },
			Region{ &resources[6], &resources[7], &resources[8], &resources[9], &resources[10], &resources[11] },
		};
		Cache cache;

		void Record(const Proof& a_proof)
		{
			cache.Record(100, 20, 7, 64, 48, 128, 96, &source, regions, a_proof);
		}

		bool Matches(std::uint32_t a_frame, std::uint64_t a_cycle)
		{
			return cache.Matches(a_frame, a_cycle, 7, 64, 48, 128, 96, &source, regions);
		}
	};

	bool ReusesOriginalDispatchAfterPresent()
	{
		Fixture fixture;
		if (fixture.Matches(100, 20))
			return false;

		Proof dispatch{ true, 100, Path::RuntimeFSR4, 81 };
		fixture.Record(dispatch);
		const auto original = dispatch;
		dispatch = { true, 101, Path::HostFallback, 82 };
		if (!fixture.Matches(101, 20) || !fixture.cache.HasValidDispatchProof() || fixture.cache.dispatchProof != original)
			return false;
		if (fixture.Matches(101, 21) || fixture.Matches(101, 0))
			return false;

		fixture.cache.frame = std::numeric_limits<std::uint32_t>::max();
		if (fixture.Matches(101, 20) || fixture.cache.HasValidDispatchProof())
			return false;
		fixture.Record(original);
		fixture.cache = {};
		return !fixture.Matches(101, 20) && !fixture.cache.HasValidDispatchProof();
	}

	bool RejectsChangedBatchContracts()
	{
		Fixture fixture;
		fixture.Record({ true, 100, Path::RuntimeFSR3, 81 });
		int replacement{};
		if (fixture.cache.Matches(101, 20, 8, 64, 48, 128, 96, &fixture.source, fixture.regions) ||
			fixture.cache.Matches(101, 20, 7, 65, 48, 128, 96, &fixture.source, fixture.regions) ||
			fixture.cache.Matches(101, 20, 7, 64, 49, 128, 96, &fixture.source, fixture.regions) ||
			fixture.cache.Matches(101, 20, 7, 64, 48, 129, 96, &fixture.source, fixture.regions) ||
			fixture.cache.Matches(101, 20, 7, 64, 48, 128, 97, &fixture.source, fixture.regions) ||
			fixture.cache.Matches(101, 20, 7, 64, 48, 128, 96, &replacement, fixture.regions))
			return false;

		constexpr std::array members{ &Region::color, &Region::depth, &Region::motionVectors, &Region::reactiveMask, &Region::transparencyCompositionMask, &Region::output };
		for (std::size_t eye = 0; eye < fixture.regions.size(); ++eye) {
			for (const auto member : members) {
				auto changed = fixture.regions;
				changed[eye].*member = &replacement;
				if (fixture.cache.Matches(101, 20, 7, 64, 48, 128, 96, &fixture.source, changed))
					return false;
			}
		}
		fixture.cache.ready = false;
		return !fixture.Matches(101, 20) && !fixture.cache.HasValidDispatchProof();
	}

	bool RequiresSuccessfulEvidenceFromTheRecordedFrame()
	{
		Fixture fixture;
		constexpr std::array invalid{
			Proof{ false, 100, Path::RuntimeFSR4, 81 },
			Proof{ true, 100, Path::RuntimeFSR4, 0 },
			Proof{ true, 0, Path::RuntimeFSR4, 81 },
			Proof{ true, 100, Path::Inactive, 81 },
			Proof{ true, 99, Path::RuntimeFSR4, 81 },
			Proof{ true, 101, Path::RuntimeFSR4, 81 },
		};
		for (const auto& proof : invalid) {
			fixture.Record(proof);
			if (!fixture.Matches(101, 20) || fixture.cache.HasValidDispatchProof())
				return false;
		}
		fixture.Record({ true, 100, Path::HostFallback, 81 });
		return fixture.cache.HasValidDispatchProof() && fixture.cache.dispatchProof.path == Path::HostFallback;
	}

	bool NormalizesOnlyDispatchEvidenceAtFrameZero()
	{
		Fixture fixture;
		fixture.cache.Record(0, 0, 7, 64, 48, 128, 96, &fixture.source, fixture.regions,
			{ true, 1, Path::RuntimeFSR4, 81 });
		if (!fixture.cache.HasValidDispatchProof() || fixture.cache.frame != 0 ||
			!fixture.Matches(0, 0) || fixture.Matches(1, 0))
			return false;
		fixture.cache.dispatchProof.frame = 0;
		if (fixture.cache.HasValidDispatchProof())
			return false;
		fixture.cache.Record(0, 20, 7, 64, 48, 128, 96, &fixture.source, fixture.regions,
			{ true, 1, Path::RuntimeFSR4, 82 });
		return fixture.cache.HasValidDispatchProof() && fixture.Matches(1, 20) &&
		       !fixture.Matches(1, 21) && fixture.cache.frame == 0;
	}

	bool SupportsBuildsWithoutDispatchTelemetry()
	{
		Fixture fixture;
		VRSubmitStereoBatch::State<int, int, Region> cache;
		cache.Record(100, 20, 7, 64, 48, 128, 96, &fixture.source, fixture.regions);
		if (!cache.Matches(101, 20, 7, 64, 48, 128, 96, &fixture.source, fixture.regions))
			return false;
		cache = {};
		if (cache.Matches(101, 20, 7, 64, 48, 128, 96, &fixture.source, fixture.regions))
			return false;
		cache.Record(100, 0, 7, 64, 48, 128, 96, &fixture.source, fixture.regions);
		return cache.Matches(100, 0, 7, 64, 48, 128, 96, &fixture.source, fixture.regions) &&
		       !cache.Matches(101, 0, 7, 64, 48, 128, 96, &fixture.source, fixture.regions);
	}
}

int main()
{
	return ReusesOriginalDispatchAfterPresent() && RejectsChangedBatchContracts() &&
	               RequiresSuccessfulEvidenceFromTheRecordedFrame() && NormalizesOnlyDispatchEvidenceAtFrameZero() && SupportsBuildsWithoutDispatchTelemetry() ?
	           0 :
	           1;
}
