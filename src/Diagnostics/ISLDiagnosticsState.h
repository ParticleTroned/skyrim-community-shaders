#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <cstdint>
#	include <stdexcept>

namespace CSX::Diagnostics::ISL
{
	enum class Luminance : uint8_t
	{
		Normal,
		Native,
		ComputeDiscard
	};
	enum class Renderer : uint8_t
	{
		Normal,
		VanillaOutput,
		VanillaDirect
	};
	enum class Particle : uint8_t
	{
		Normal,
		ScanOff,
		BeforeLock
	};
	enum class Observation : uint8_t
	{
		Off,
		Counters,
		Timed
	};
	struct Policy
	{
		Luminance luminance{};
		Renderer renderer{};
		Particle particle{};
		Observation observation = Observation::Counters;
		bool editorBypass{};
		uint64_t generation{};
		bool operator==(const Policy&) const = default;
		uint64_t Pack() const { return (generation << 16) | uint64_t(luminance) | (uint64_t(renderer) << 2) | (uint64_t(particle) << 4) | (uint64_t(observation) << 6) | (uint64_t(editorBypass) << 8); }
		static Policy Unpack(uint64_t bits) { return { Luminance(bits & 3), Renderer((bits >> 2) & 3), Particle((bits >> 4) & 3), Observation((bits >> 6) & 3), bool(bits & 256), bits >> 16 }; }
		bool Valid(bool suppressed, bool editorClean) const
		{
			return luminance <= Luminance::ComputeDiscard && renderer <= Renderer::VanillaDirect && particle <= Particle::BeforeLock && observation <= Observation::Timed &&
			       (!(editorBypass || renderer == Renderer::VanillaDirect) || editorClean) &&
			       (renderer != Renderer::VanillaDirect || (suppressed && luminance == Luminance::Native));
		}
	};
	/// A single due sweep may move; expiry never advances the owner's last-executed frame.
	struct PruneDelay
	{
		uint32_t frames{}, due{}, target{}, executed{};
		int64_t activated{}, expires{};
		bool armed{}, active{}, consumed{}, delayed{}, cancelled{};
		void Arm(uint32_t count)
		{
			if (!count || count > 240 || armed || active)
				throw std::invalid_argument("prune delay requires 1..240 frames and no existing arm");
			*this = {};
			frames = count;
			armed = true;
		}
		void Activate(int64_t now, int64_t frequency)
		{
			if (armed) {
				armed = false;
				active = true;
				activated = now;
				expires = now + frequency * 5;
			}
		}
		bool Defer(uint32_t frame, int64_t now)
		{
			if (!active)
				return false;
			if (!consumed) {
				consumed = true;
				due = frame;
				target = frame + frames;
			}
			if (now < expires && int32_t(frame - target) < 0) {
				delayed = true;
				return true;
			}
			active = false;
			executed = frame;
			return false;
		}
		void Cancel()
		{
			cancelled = armed || active;
			armed = active = false;
		}
	};
}
#endif
