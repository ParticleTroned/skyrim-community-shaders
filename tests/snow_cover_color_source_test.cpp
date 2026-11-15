#include "Features/SnowCover/ColorSource.h"

#include <array>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	struct TextureSet
	{
		const char* path = nullptr;
		bool pbr = false;
		TextureSet* seasonal = nullptr;
	};

	SnowCoverColorSource::Source Resolve(TextureSet* textureSet, bool pbrEnabled = true)
	{
		using namespace SnowCoverColorSource;
		return Detail::ResolveTextureSet(textureSet, { std::filesystem::path(Detail::DefaultPaths[0]), false }, [](TextureSet* value) { return value && value->seasonal ? value->seasonal : value; }, [](const TextureSet* value) { return value->path; }, [pbrEnabled](const TextureSet* value) { return pbrEnabled && value->pbr; });
	}
}

int main()
{
	using namespace SnowCoverColorSource;
	using Detail::Candidate;
	const std::array remapped{
		Candidate{ "LandscapeSnow01", "textures/custom/overhaul/blue-snow.dds", true },
		Candidate{ "SNOW02", "textures/custom/overhaul/ice.dds", true },
		Candidate{ "Snow01Rock", "textures/landscape/snow01rock.dds", true }
	};
	auto selected = Detail::SelectRecords(remapped);
	Require(selected[0].index == 0 && selected[1].index == 1, "Canonical identities must survive texture replacement paths");
	const std::array originalPaths{
		Candidate{ "", "Data/TEXTURES/LANDSCAPE/SNOW01.DDS", true },
		Candidate{ "", "landscape\\snow02.dds", true }
	};
	selected = Detail::SelectRecords(originalPaths);
	Require(selected[0].index == 0 && selected[1].index == 1, "Original diffuse paths must identify records without editor IDs");
	const std::array duplicatePaths{
		Candidate{ "AddedSnow", "textures/landscape/snow01.dds", false },
		Candidate{ "", "textures/landscape/snow01.dds", true }
	};
	selected = Detail::SelectRecords(duplicatePaths);
	Require(selected[0].index == 1 && !selected[1].index, "Base-game identity must win same-path mod copies independently per source");
	const std::array ambiguous{
		Candidate{ "LandscapeSnow01", "textures/custom/one.dds", false },
		Candidate{ "Snow01", "textures/custom/two.dds", false },
		Candidate{ "LandscapeSnow02", "textures/custom/other.dds", true }
	};
	selected = Detail::SelectRecords(ambiguous);
	Require(!selected[0].index && selected[0].ambiguous && selected[1].index == 2, "Ambiguous identities must not choose an arbitrary installed texture");
	const std::array unrelated{ Candidate{ "Snow01Rock", "textures/landscape/snow01rock.dds", true } };
	selected = Detail::SelectRecords(unrelated);
	Require(!selected[0].index && !selected[1].index, "Similar snow names must not replace canonical colors");
	Require(!Detail::SelectRecords({})[0].index, "Missing records must retain standard paths");

	TextureSet legacy{ "textures/overhaul/snow.dds" };
	auto source = Resolve(&legacy);
	Require(source.path == "textures\\overhaul\\snow.dds" && !source.pbr, "Installed texture-set path must be retained without mod-name detection");
	TextureSet winter{ "textures/pbr/overhaul/winter.dds", true };
	legacy.seasonal = &winter;
	source = Resolve(&legacy);
	Require(source.path == "textures\\pbr\\overhaul\\winter.dds" && source.pbr, "Seasonal replacement must be resolved before PBR classification");
	Require(!Resolve(&legacy, false).pbr, "Disabled PBR must retain legacy source classification");
	TextureSet empty{ "", true };
	legacy.seasonal = &empty;
	source = Resolve(&legacy);
	Require(source.path == Detail::DefaultPaths[0] && !source.pbr, "Empty seasonal sources must retain the standard legacy fallback");
	Require(Resolve(nullptr).path == Detail::DefaultPaths[0], "Unavailable records must retain the standard fallback");
	legacy.seasonal = nullptr;
	Require(Resolve(&legacy).path == "textures\\overhaul\\snow.dds", "Returning to an authored source must resolve its current color");
	for (const auto* path : { "overhaul/snow.dds", "Data/Textures/Overhaul/Snow.dds" }) {
		legacy.path = path;
		Require(Resolve(&legacy).path == "textures\\overhaul\\snow.dds", "Texture-relative and Data-relative paths must resolve below Textures");
	}
	for (const auto* path : { "../snow.dds", "textures/../snow.dds", "C:/snow.dds", "/snow.dds", "textures/snow.dds:stream" }) {
		legacy.path = path;
		bool rejected = false;
		try {
			Resolve(&legacy);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		Require(rejected, "Rooted, traversing and invalid texture paths must be rejected before normalization");
	}
}
