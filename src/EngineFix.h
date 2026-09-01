#pragma once

#include <string>
#include <vector>

struct EngineFix
{
	virtual ~EngineFix() = default;

	virtual std::string GetName() = 0;

	virtual void Install() {}
	/** @brief Returns whether installation succeeded; legacy fixes retain their Install contract. */
	virtual bool TryInstall()
	{
		Install();
		return true;
	}
	/** @brief Returns the corresponding Engine Fixes installed-fix name, when one exists. */
	virtual const char* GetEngineFixesName() const { return nullptr; }

	/** @brief Queries actual patch ownership in either loaded Engine Fixes plugin. */
	static bool IsInstalledByEngineFixes(const char* a_name);

	static void InstallOnPostPostLoadFixes();
	static void InstallOnDataLoadedFixes();

private:
	static const std::vector<EngineFix*>& GetOnPostPostLoadFixesList();
	static const std::vector<EngineFix*>& GetOnDataLoadedFixesList();
	static void InstallFixes(const std::vector<EngineFix*>& fixes);
};
