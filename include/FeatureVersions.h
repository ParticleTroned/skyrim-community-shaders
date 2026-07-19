#pragma once
// This file overrides any generated copy in the build tree.
// Make sure your CMake includes ${CMAKE_SOURCE_DIR}/include before ${CMAKE_BINARY_DIR}.

#include <map>
#include <string_view>
#include <unordered_set>

namespace FeatureVersions
{
    using namespace std::literals::string_view_literals;

    inline const std::map<std::string_view, REL::Version> FEATURE_MINIMAL_VERSIONS{
        {"AdaptiveBrightness"sv,   {1,0,0}},
        {"CloudShadows"sv,         {1,4,0}},
        {"CSUtility"sv,            {1,0,0}},
        {"CSEditor"sv,             {2,0,5}},
        {"DynamicCubemaps"sv,      {2,4,0}},
        {"ExponentialHeightFog"sv, {1,3,1}},
        {"ExtendedMaterials"sv,    {1,2,0}},
        {"ExtendedTranslucency"sv, {1,0,0}},
        {"GrassCollision"sv,       {3,1,0}},
        {"GrassLighting"sv,        {2,1,5}},
        {"HairSpecular"sv,         {1,1,2}},
        {"HDRDisplay"sv,           {1,1,0}},
        {"ImageBasedLighting"sv,   {1,2,5}},
        {"InteriorSun"sv,          {1,0,2}},
        {"InverseSquareLighting"sv,{1,3,0}},
        {"LODBlending"sv,          {1,0,0}},
        {"LightLimitFix"sv,        {3,2,5}},
        {"LinearLighting"sv,       {1,1,1}},
        {"PerformanceOverlay"sv,   {1,2,5}},
        {"RenderDoc"sv,            {1,1,5}},
        {"Screenshot"sv,           {1,1,0}},
        {"ScreenSpaceGI"sv,        {4,2,0}},
        {"ScreenSpaceShadows"sv,   {2,2,5}},
        {"Skin"sv,                 {1,0,0}},
        {"SkySync"sv,              {1,3,0}},
        {"Skylighting"sv,          {1,4,0}},
        {"SubsurfaceScattering"sv, {3,1,5}},
        {"TerrainBlending"sv,      {1,2,5}},
        {"TerrainHelper"sv,        {1,0,1}},
        {"TerrainShadows"sv,       {1,2,0}},
        {"TerrainVariation"sv,     {1,0,1}},
        {"TruePBR"sv,              {1,0,0}},
        {"UnifiedWater"sv,         {1,0,2}},
        {"Upscaling"sv,            {1,6,0}},
        {"VolumetricLighting"sv,   {1,1,5}},
        {"VolumetricShadows"sv,    {2,1,0}},
        {"WaterEffects"sv,         {1,2,0}},
        {"WetnessEffects"sv,       {3,1,0}},
    };

    inline const std::unordered_set<std::string_view> FEATURE_CORE_NAMES{
        "AdaptiveBrightness"sv,
        "CSUtility"sv,
        "DynamicCubemaps"sv,
        "ExtendedMaterials"sv,
        "ExtendedTranslucency"sv,
        "GrassCollision"sv,
        "GrassLighting"sv,
        "ImageBasedLighting"sv,
        "InteriorSun"sv,
        "InverseSquareLighting"sv,
        "LODBlending"sv,
        "LightLimitFix"sv,
        "LinearLighting"sv,
        "PerformanceOverlay"sv,
        "RenderDoc"sv,
        "ScreenSpaceGI"sv,
        "ScreenSpaceShadows"sv,
        "Skylighting"sv,
        "SubsurfaceScattering"sv,
        "TerrainBlending"sv,
        "TerrainShadows"sv,
        "TruePBR"sv,
        "Upscaling"sv,
        "VolumetricLighting"sv,
        "WaterEffects"sv,
        "CSEditor"sv
    };
}
