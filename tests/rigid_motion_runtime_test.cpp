#include "EngineFixes/RigidMotionHistory.h"
#include "EngineFixes/VRRigidMotionHistory.h"
#include "Utils/Finite.h"

#include <ankerl/unordered_dense.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	std::size_t allocationCount = 0;
	std::ptrdiff_t failAfter = -1;
}

void* operator new(std::size_t a_size)
{
	if (failAfter >= 0 && failAfter-- == 0)
		throw std::bad_alloc{};
	++allocationCount;
	if (auto* memory = std::malloc(a_size ? a_size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }

namespace logger
{
	int warnings = 0;
	void warn(const char*) { ++warnings; }
}

namespace RE
{
	using NiTransform = std::array<float, 3>;
	using FormID = std::uint32_t;
	enum class FormType
	{
		ActorCharacter
	};
	struct BSShaderAccumulator
	{
		enum class RENDER_FLAG : std::uint32_t
		{
			kCubeMap = 1 << 3
		};
	};
	class TESObjectREFR
	{
	public:
		bool actor = true;
		FormID id = 1;
		bool Is(FormType) const { return actor; }
		FormID GetFormID() const { return id; }
	};
	struct NiNode
	{};
	class BSGeometry
	{
	public:
		NiTransform world{ 42, 75, -42 };
		NiTransform previousWorld{ 195, -2612, -100 };
		NiNode* parent = nullptr;
		int retained = 0;
	};
	template <class T>
	class NiPointer
	{
	public:
		explicit NiPointer(T* a_value) : value(a_value) { ++value->retained; }
		NiPointer(const NiPointer& a_other) : value(a_other.value)
		{
			if (value)
				++value->retained;
		}
		NiPointer(NiPointer&& a_other) noexcept : value(std::exchange(a_other.value, nullptr)) {}
		~NiPointer()
		{
			if (value)
				--value->retained;
		}
		NiPointer& operator=(NiPointer a_other) noexcept
		{
			std::swap(value, a_other.value);
			return *this;
		}

	private:
		T* value;
	};
	struct BSShaderProperty
	{
		enum class EShaderPropertyFlag
		{
			kSkinned
		};
		struct Flags
		{
			bool skinned = false;
			bool any(EShaderPropertyFlag) const { return skinned; }
		} flags;
	};
	class BSShader
	{};
	class BSRenderPass
	{
	public:
		BSGeometry* geometry;
		BSShaderProperty* shaderProperty;
	};
}

namespace globals
{
	struct State
	{
		bool inWorld = true, loading = false, transition = false;
		std::uint32_t frameCount = 100;
		bool IsMainOrLoadingMenuOpen() const { return loading; }
		bool IsWorldLoadTransitionActive() const { return transition; }
	} stateValue;
	struct ShaderCache
	{
		bool enabled = true;
		bool IsEnabled() const { return enabled; }
	} cache;
	auto* state = &stateValue;
	auto* shaderCache = &cache;
}

#include "rigid_motion_runtime_under_test.h"

namespace
{
	int nativeCalls = 0;
	bool throwNative = false;
	std::uint32_t expectedFlags = 0x45;
	RE::NiTransform seen;
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}
	void Native(RE::BSShader*, RE::BSRenderPass* a_pass, std::uint32_t a_flags)
	{
		++nativeCalls;
		Require(a_flags == expectedFlags, "Native render flags must be preserved");
		if (a_pass && a_pass->geometry)
			seen = a_pass->geometry->previousWorld;
		if (throwNative)
			throw 1;
	}
}

int main()
{
	RE::BSGeometry geometry;
	RE::BSShaderProperty property;
	RE::BSRenderPass pass{ &geometry, &property };
	RE::TESObjectREFR owner;
	std::vector<std::unique_ptr<RE::BSGeometry>> crowded;
	const auto stale = geometry.previousWorld;
	const auto first = geometry.world;
	try {
		const auto setup = [&] { VRRigidMotionHistory::SetupGeometry(nullptr, &pass, expectedFlags, &owner, Native); };
		const auto beforeReset = allocationCount;
		VRRigidMotionHistory::Reset(100, false);
		Require(allocationCount == beforeReset && !entries, "Empty frames must not allocate a history cache");
		for (std::ptrdiff_t failure = 0; failure != 2; ++failure) {
			const int before = nativeCalls;
			failAfter = failure;
			setup();
			failAfter = -1;
			Require(nativeCalls == before + 1 && seen == first && geometry.previousWorld == stale,
				"Allocation failure must preserve native setup with scoped camera-only motion");
			Require(!entries && allocationFailed && geometry.retained == 0, "Failed reservation must retain no partial cache or geometry");
			failAfter = 0;
			setup();
			VRRigidMotionHistory::Reset(100, false);
			Require(failAfter == 0 && allocationFailed, "Allocation failure must not be retried on every draw or frame");
			failAfter = -1;
			VRRigidMotionHistory::Reset(100, true);
			Require(!allocationFailed, "A world transition must allow allocation recovery");
		}
		Require(logger::warnings == 1, "Allocation failure warnings must be bounded");
		setup();
		Require(seen == first && geometry.previousWorld == stale, "First-frame stale history must be scoped and restored");
		Require(geometry.retained == 1, "Tracked geometry must be retained against address reuse");
		geometry.world = { 43, 76, -41 };
		setup();
		Require(seen == first, "Another eye must reuse the same previous transform");
		const auto last = geometry.world;
		++globals::stateValue.frameCount;
		VRRigidMotionHistory::Reset(globals::stateValue.frameCount, false);
		geometry.world = { 44, 77, -40 };
		setup();
		Require(seen == last, "Native setup must receive the prior rendered frame");

		throwNative = true;
		bool propagated = false;
		try {
			setup();
		} catch (int) {
			propagated = true;
		}
		throwNative = false;
		Require(propagated, "Native setup exceptions must propagate to the caller");
		Require(geometry.previousWorld == stale, "A failed native setup must not leave an override installed");
		Require(entries->empty() && geometry.retained == 0, "A failed setup must retire history for the undrawn geometry");
		setup();
		geometry.world = { 46, 79, -38 };
		RE::NiNode newParent;
		geometry.parent = &newParent;
		setup();
		Require(seen == geometry.world, "Reparenting must reset attachment history");
		geometry.world = { 45, 78, -39 };
		++owner.id;
		setup();
		Require(seen == geometry.world, "Changing actor ownership must reset history");
		VRRigidMotionHistory::Reset(103, false);
		Require(entries->empty() && geometry.retained == 0, "Unseen geometry must be released after one frame");

		const auto skipped = [&] {
			const int before = nativeCalls;
			setup();
			Require(nativeCalls == before + 1 && seen == stale && entries->empty(), "Excluded geometry must retain native behavior");
		};
		owner.actor = false;
		skipped();
		owner.actor = true;
		property.flags.skinned = true;
		skipped();
		property.flags.skinned = false;
		globals::stateValue.inWorld = false;
		skipped();
		globals::stateValue.inWorld = true;
		globals::stateValue.loading = true;
		skipped();
		globals::stateValue.loading = false;
		globals::stateValue.transition = true;
		skipped();
		globals::stateValue.transition = false;
		globals::cache.enabled = false;
		skipped();
		globals::cache.enabled = true;
		globals::state = nullptr;
		skipped();
		globals::state = &globals::stateValue;
		globals::shaderCache = nullptr;
		skipped();
		globals::shaderCache = &globals::cache;
		expectedFlags |= 8;
		skipped();
		expectedFlags = 0x45;
		VRRigidMotionHistory::SetupGeometry(nullptr, nullptr, 0x45, &owner, Native);
		pass.geometry = nullptr;
		setup();
		pass.geometry = &geometry;
		pass.shaderProperty = nullptr;
		setup();
		pass.shaderProperty = &property;
		VRRigidMotionHistory::SetupGeometry(nullptr, &pass, 0x45, nullptr, Native);
		Require(entries->empty(), "Missing draw metadata must not admit history");
		geometry.world[0] = std::numeric_limits<float>::quiet_NaN();
		skipped();
		geometry.world = first;
		setup();
		geometry.world[1] = std::numeric_limits<float>::infinity();
		skipped();
		Require(geometry.retained == 0, "Non-finite transforms must retire poisoned history");
		geometry.world = first;

		crowded.reserve(kMaximumHistoryEntries + 1);
		for (std::size_t i = 0; i <= kMaximumHistoryEntries; ++i) {
			crowded.push_back(std::make_unique<RE::BSGeometry>());
			pass.geometry = crowded.back().get();
			const auto before = allocationCount;
			setup();
			Require(allocationCount == before, "Bounded history admission must not allocate during draws");
			Require(seen == pass.geometry->world && pass.geometry->previousWorld == stale, "Capacity fallback must reject stale motion and restore native state");
		}
		Require(entries->size() == kMaximumHistoryEntries, "History admission must remain bounded");
		VRRigidMotionHistory::Reset(101, true);
		Require(entries->empty(), "World transitions must clear history");
		for (const auto& item : crowded)
			Require(item->retained == 0, "Clearing history must release all retained geometry");
		std::cout << "Rigid motion runtime checks passed\n";
		return 0;
	} catch (const std::exception& e) {
		failAfter = -1;
		VRRigidMotionHistory::Reset(0, true);
		std::cerr << e.what() << '\n';
		return 1;
	}
}
