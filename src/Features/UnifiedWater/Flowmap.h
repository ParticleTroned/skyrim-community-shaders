#pragma once

#include <filesystem>
#include <vector>

class Flowmap
{
public:
	/// A usable map owns a texture, shader view, and positive cell dimensions.
	bool IsValid() const;
	bool TryGetFlowmap(RE::NiPointer<RE::NiSourceTexture>& outFlowmapTex) const;
	int32_t GetWidth() const { return width; }
	int32_t GetHeight() const { return height; }
	float GetInverseWidth() const { return invWidth; }
	float GetInverseHeight() const { return invHeight; }
	int32_t GetOffsetX() const { return offsetX; }
	int32_t GetOffsetY() const { return offsetY; }
	/// Failed loads retain the previously validated map and its dimensions.
	bool LoadOrGenerateFlowmap(bool useMips = true);
	/// Load the exact generated file before replacing the active map.
	bool RegenerateAndLoadFlowmap(bool useMips = true);

private:
	RE::NiPointer<RE::NiSourceTexture> flowmapTex = nullptr;
	int32_t width = 0;
	int32_t height = 0;
	float invWidth = 0.0f;
	float invHeight = 0.0f;
	int32_t offsetX = 0;
	int32_t offsetY = 0;

	bool LoadFlowmap();
	bool LoadFlowmap(const std::filesystem::path& path);
	static bool FindFlowmaps(std::vector<std::filesystem::path>& paths);
	static bool GenerateFlowmap(bool useMips, std::filesystem::path& generatedPath);
};
