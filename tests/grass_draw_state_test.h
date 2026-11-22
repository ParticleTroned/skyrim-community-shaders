#pragma once

#include "Features/GrassOptimizations/GrassDrawState.h"
#include <array>
#include <stdexcept>

inline void CheckGrassDrawState()
{
	const auto require = [](bool condition) {
		if (!condition)
			throw std::runtime_error("Grass native state commit lost dirty state or feature ordering");
	};
	struct Flags
	{
		uint32_t value = 0;
		uint32_t underlying() const { return value; }
		void reset(uint32_t bits) { value &= ~bits; }
		void set(uint32_t bits) { value |= bits; }
	} flags;
	constexpr uint32_t alpha = 0x100;
	GrassDrawState::Cache cache;
	GrassDrawState::Identity identity{ &cache, &flags, &identity, 1 };
	uint32_t resources = 0, other = 0;
	float reference = .5f;
	unsigned commits = 0, binds = 0, features = 0;
	uint32_t boundSlots = 0;
	unsigned failureStage = 0;
	std::array<int, 16> actual{}, intended{};
	for (size_t index = 0; index < intended.size(); ++index)
		intended[index] = int(index + 1);
	const auto flush = [&] {
		GrassDrawState::Commit(cache, identity, flags, alpha, resources, other, reference, [&] {
				++commits;
				if (failureStage == 1)
					throw std::runtime_error("Injected native flush failure");
				for (size_t index = 0; index < actual.size(); ++index)
					if (resources & (1u << index))
						actual[index] = intended[index];
				resources = other = flags.value = 0;
				if (failureStage == 2) {
					resources = 1u << 10;
					flags.value = 0x400;
					throw std::runtime_error("Injected failure after native state consumption");
				} }, [&](uint32_t first, uint32_t count) {
				++binds;
				boundSlots |= ((1u << count) - 1) << first;
				for (uint32_t index = first; index < first + count; ++index)
					actual[index] = intended[index];
				if (failureStage == 3)
					throw std::runtime_error("Injected material binding failure"); });
		++features;
		actual[2] = 200;
	};
	resources = 0x13;  // Slots 0, 1 and 4; a clean slot contains a feature override.
	actual[2] = 200;
	flush();
	require(commits == 1 && binds == 2 && features == 1 && actual[2] == 200 && boundSlots == 0x13);
	flags.value = alpha;
	flush();
	require(commits == 1 && features == 2 && flags.value == 0);
	resources = 3;
	flush();
	require(commits == 1 && binds == 3 && features == 3);
	reference = .6f;
	flags.value = alpha;
	flush();
	require(commits == 2);
	for (uint32_t pending : { 1u, 0x200u, 0x80000000u }) {
		flags.value = pending;
		const auto before = commits;
		flush();
		require(commits == before + 1);
	}
	for (uint32_t slot : { 7u, 15u }) {
		resources = 1u << slot;
		const auto before = commits;
		flush();
		require(commits == before + 1 && actual[slot] == intended[slot]);
	}
	other = 1;
	const auto before = commits;
	flush();
	require(commits == before + 1);
	++identity.frame;
	flush();
	require(commits == before + 2);
	identity.pixelShader = &actual;
	flush();
	require(commits == before + 3);
	cache.ready = false;
	flush();
	require(commits == before + 4);
	flags.value = alpha;
	resources = 3;
	other = 1;
	failureStage = 1;
	bool caught = false;
	try {
		flush();
	} catch (const std::runtime_error&) {
		caught = true;
	}
	require(caught && !cache.ready && resources == 3 && flags.value == alpha);
	failureStage = 0;
	flush();
	require(cache.ready && resources == 0);
	for (unsigned stage : { 2u, 3u }) {
		failureStage = stage;
		reference += .1f;
		flags.value = alpha | 0x200;
		resources = 3 | (1u << 15);
		caught = false;
		try {
			flush();
		} catch (const std::runtime_error&) {
			caught = true;
		}
		require(caught && !cache.ready && resources == (3u | (1u << 15) | (stage == 2 ? 1u << 10 : 0u)) &&
				flags.value == (alpha | 0x200u | (stage == 2 ? 0x400u : 0u)));
		failureStage = 0;
		flush();
		require(cache.ready && resources == 0 && flags.value == 0);
	}
}
