#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace RE
{
	struct Setting
	{
		struct
		{
			float f = 180.0f;
		} data;
		float GetFloat() const { return data.f; }
	};

	static Setting setting;
	static bool settingAvailable = true;
	static Setting* GetINISetting(std::string_view name)
	{
		REQUIRE(name == "fAutoVanityModeDelay:Camera");
		return settingAvailable ? &setting : nullptr;
	}
}

namespace logger
{
	template <class... Args>
	void info(Args&&...)
	{}
	template <class... Args>
	void warn(Args&&...)
	{}
	template <class... Args>
	void error(Args&&...)
	{}
}

#include "Utils/VanityCamera.cpp"

TEST_CASE("Idle-camera suppression preserves the original delay across owners", "[performance-tuning][camera]")
{
	RE::settingAvailable = true;
	RE::setting.data.f = 37.0f;
	Util::VanityCameraSuppressionLease measurement;
	Util::VanityCameraSuppressionLease editor;
	REQUIRE(measurement.Acquire());
	REQUIRE(measurement.Acquire());
	REQUIRE(RE::setting.data.f == 10000.0f);
	REQUIRE(editor.Acquire());
	measurement.Release();
	measurement.Release();
	REQUIRE(RE::setting.data.f == 10000.0f);
	editor.Release();
	REQUIRE(RE::setting.data.f == 37.0f);
	REQUIRE_FALSE(measurement.IsActive());
	REQUIRE_FALSE(editor.IsActive());
}

TEST_CASE("Idle-camera suppression fails closed and releases on scope exit", "[performance-tuning][camera]")
{
	RE::settingAvailable = false;
	RE::setting.data.f = 64.0f;
	{
		Util::VanityCameraSuppressionLease measurement;
		REQUIRE_FALSE(measurement.Acquire());
		REQUIRE_FALSE(measurement.IsActive());
		REQUIRE(RE::setting.data.f == 64.0f);
		RE::settingAvailable = true;
		REQUIRE(measurement.Acquire());
	}
	REQUIRE(RE::setting.data.f == 64.0f);
}
