#include "ColorSource.h"

#include "TruePBR.h"
#include "Utils/Form.h"
#include "Utils/Game.h"

#include <vector>

namespace SnowCoverColorSource
{
	namespace
	{
		std::array<RE::TESLandTexture*, 2> records{};
		bool discovered = false;

		struct SourceKey
		{
			RE::BGSTextureSet* textureSet = nullptr;
			const char* diffusePath = nullptr;
			bool pbrEnabled = false;
			bool operator==(const SourceKey&) const = default;
		};
		std::optional<std::array<SourceKey, 2>> resolvedKey;
		std::array<RE::BSFixedString, 2> resolvedPaths;

		std::array<SourceKey, 2> CurrentKey()
		{
			std::array<SourceKey, 2> result{};
			const auto& pbr = globals::features::truePBR;
			for (size_t i = 0; i < records.size(); ++i) {
				auto* textureSet = Util::GetSeasonalSwap(records[i] ? records[i]->textureSet : nullptr);
				// Texture paths are interned BSFixedStrings; identity changes with authored paths.
				result[i] = { textureSet, textureSet ? textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr,
					pbr.loaded && pbr.settings.Enabled != 0 };
			}
			return result;
		}
	}

	void Discover()
	{
		if (discovered)
			return;
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler)
			return;
		discovered = true;
		Util::FormEditorIDIndex editorIDs;
		std::vector<RE::TESLandTexture*> available;
		std::vector<Detail::Candidate> candidates;
		for (auto* record : handler->GetFormArray<RE::TESLandTexture>()) {
			if (!record)
				continue;
			auto* file = record->GetFile(0);
			const auto* path = record->textureSet ? record->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
			available.push_back(record);
			candidates.push_back({ Util::GetFormEditorID(record, &editorIDs), path ? path : "",
				file && Util::ToLowerAscii(file->GetFilename()) == "skyrim.esm" });
		}
		const auto selected = Detail::SelectRecords(candidates);
		for (size_t i = 0; i < records.size(); ++i) {
			if (selected[i].index)
				records[i] = available[*selected[i].index];
			else if (selected[i].ambiguous)
				logger::warn("Snow Cover: ambiguous landscape snow source {}; using its standard texture path", Detail::DefaultPaths[i]);
		}
	}

	std::array<Source, 2> Resolve()
	{
		std::array<Source, 2> result;
		for (size_t i = 0; i < records.size(); ++i) {
			try {
				result[i] = Detail::ResolveTextureSet(records[i] ? records[i]->textureSet : nullptr, { std::filesystem::path(Detail::DefaultPaths[i]), false }, [](RE::BGSTextureSet* textureSet) { return Util::GetSeasonalSwap(textureSet); }, [](RE::BGSTextureSet* textureSet) { return textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse); }, [](const RE::BGSTextureSet* textureSet) {
						auto& pbr = globals::features::truePBR;
						return pbr.loaded && pbr.settings.Enabled && pbr.IsPBRTextureSet(textureSet); });
			} catch (const std::invalid_argument& error) {
				logger::warn("Snow Cover: invalid landscape color source {}: {}; retaining configured color", Detail::DefaultPaths[i], error.what());
			}
		}
		resolvedKey = CurrentKey();
		// Retain prior interned names so address reuse cannot hide a path change.
		for (size_t i = 0; i < resolvedPaths.size(); ++i)
			resolvedPaths[i] = (*resolvedKey)[i].diffusePath ? (*resolvedKey)[i].diffusePath : "";
		return result;
	}

	bool Changed()
	{
		return resolvedKey && *resolvedKey != CurrentKey();
	}
}
