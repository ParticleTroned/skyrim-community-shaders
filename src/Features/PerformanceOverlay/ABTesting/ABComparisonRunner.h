#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <atomic>
#	include <cstdint>
#	include <memory>
#	include <mutex>
#	include <optional>
#	include <nlohmann/json.hpp>

/** @brief Bounded comparisons using captured reversible feature states and original timing identities. */
class ABComparisonRunner
{
public:
	static ABComparisonRunner& Get();
	~ABComparisonRunner();
	/** @brief Queues a command; only Update mutates feature or profiling state. */
	nlohmann::json Submit(const nlohmann::json& request);
	/** @brief Returns the last publication without touching settings or acquiring samples. */
	nlohmann::json Status();
	/** @brief Render-owner update, including commands while the menu is hidden. */
	void Update();
	/** @brief Also retains ownership after failed restoration until retry or configuration replacement. */
	bool IsRunning() const { return running.load(std::memory_order_acquire); }
	/** @brief Stops acquisition; a replacement configuration owns subsequent restoration. */
	void ConfigurationReplacing() { superseded.store(true, std::memory_order_release); }

private:
	ABComparisonRunner();
	struct Impl;
	std::unique_ptr<Impl> impl;
	std::atomic_bool running{ false }, superseded{ false }, commandPending{ false };
	std::mutex mutex;
	std::optional<nlohmann::json> pending;
	nlohmann::json publication;
	uint64_t nextRequest = 0;
};

#endif
