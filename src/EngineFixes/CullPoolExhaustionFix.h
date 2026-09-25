#pragma once

#include "EngineFix.h"

#include <atomic>
#include <cstdint>

namespace RE
{
	class BSCullingProcess;
	class BSGeometry;
}

/** @brief Preserves non-pool appends while refusing pool allocations near exhaustion. */
struct CullPoolExhaustionFix : EngineFix
{
	std::string GetName() override { return "Cull Pool Exhaustion Fix"; }
	const char* GetEngineFixesName() const override { return "CullingProcessAppendVirtualPoolGuard"; }
	bool TryInstall() override;

private:
	using AppendVirtual = void (*)(RE::BSCullingProcess*, RE::BSGeometry*, std::int32_t);
	static inline AppendVirtual original = nullptr;
	static inline std::atomic<std::uint64_t> droppedAppends{ 0 };
	static void AppendGuarded(RE::BSCullingProcess* a_process, RE::BSGeometry* a_geometry, std::int32_t a_alphaGroupIndex);
};
