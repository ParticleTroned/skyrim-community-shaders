#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/** Recover a bounded trigger tap between coherent owned pointer samples. */
class WandTriggerEdges
{
public:
	struct Edges
	{
		std::array<bool, 2> states{};
		std::size_t count = 0;
		bool cancel = false;
	};

	/** Input is eligible only after release, with unchanged physical-hand ownership. */
	Edges Update(std::uint64_t a_sequence, bool a_down, bool a_eligible, std::uint64_t a_generation) noexcept
	{
		Edges edges;
		if (!a_eligible) {
			edges.cancel = initialized;
			*this = {};
			return edges;
		}
		if (!initialized || generation != a_generation) {
			edges.cancel = initialized;
			Baseline(a_sequence, a_down, a_generation);
			return edges;
		}

		const bool reversed = a_sequence < sequence;
		const auto difference = reversed ? 0 : a_sequence - sequence;
		const bool parityMatches = (difference % 2 != 0) == (down != a_down);
		if (reversed || difference > edges.states.size() || !parityMatches) {
			edges.cancel = true;
			Baseline(a_sequence, a_down, a_generation);
			return edges;
		}
		if (armed) {
			for (std::uint64_t edge = 0; edge < difference; ++edge)
				edges.states[edges.count++] = edge % 2 == 0 ? !down : down;
		} else if (!a_down) {
			armed = true;
		}
		sequence = a_sequence;
		down = a_down;
		return edges;
	}

	/** Discard history when changing input source or closing the menu. */
	void Reset() noexcept { *this = {}; }

private:
	void Baseline(std::uint64_t a_sequence, bool a_down, std::uint64_t a_generation) noexcept
	{
		sequence = a_sequence;
		generation = a_generation;
		down = a_down;
		armed = !a_down;
		initialized = true;
	}

	std::uint64_t sequence = 0;
	std::uint64_t generation = 0;
	bool down = false;
	bool initialized = false;
	bool armed = false;
};
