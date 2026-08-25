#include "Utils/CacheInvalidation.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
	class TempDirectory
	{
	public:
		TempDirectory()
		{
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
			path = std::filesystem::temp_directory_path() /
			       ("csx-cache-invalidation-" + std::to_string(nonce));
			[[maybe_unused]] const bool created = std::filesystem::create_directories(path);
			assert(created);
		}

		~TempDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};

	void Write(const std::filesystem::path& a_path, std::string_view a_contents)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream(a_path);
		assert(stream.is_open());
		stream << a_contents;
		assert(stream.good());
	}

	void TestResolvedImageSpaceTechnique()
	{
		TempDirectory temp;
		const auto shaders = temp.path / "Shaders";
		const auto cache = temp.path / "ShaderCache";
		Write(shaders / "ISHDR.hlsl", "#if defined(POSTPROCESS)\n#endif\n");
		Write(shaders / "ISBasicCopy.hlsl", "float4 main() { return 0; }\n");
		Write(shaders / "Utility.hlsl", "float4 main() { return 0; }\n");
		Write(cache / "ISHDRTonemapBlendCinematic/37.pso", "blob");
		Write(cache / "ISBasicCopy/68.pso", "blob");

		size_t deleted = 0;
		size_t kept = 0;
		[[maybe_unused]] const bool success = Util::CacheInvalidation::TryPartialInvalidation(
			cache, shaders, { "POSTPROCESS" }, &deleted, &kept);
		assert(success);
		assert(deleted == 1);
		assert(kept == 1);
		assert(!std::filesystem::exists(cache / "ISHDRTonemapBlendCinematic"));
		assert(std::filesystem::exists(cache / "ISBasicCopy/68.pso"));
	}

	void TestUnresolvedImageSpaceTechnique()
	{
		TempDirectory temp;
		const auto shaders = temp.path / "Shaders";
		const auto cache = temp.path / "ShaderCache";
		Write(shaders / "ISCompositeLensFlareVolumetricLighting.hlsl", "float4 main() { return 0; }\n");
		Write(shaders / "Utility.hlsl", "float4 main() { return 0; }\n");
		Write(cache / "ISCompositeLensFlare/72.pso", "blob");

		[[maybe_unused]] const bool success =
			Util::CacheInvalidation::TryPartialInvalidation(cache, shaders, { "POSTPROCESS" });
		assert(success);
		assert(!std::filesystem::exists(cache / "ISCompositeLensFlare"));
	}
}

int main()
{
	TestResolvedImageSpaceTechnique();
	TestUnresolvedImageSpaceTechnique();
	return 0;
}
