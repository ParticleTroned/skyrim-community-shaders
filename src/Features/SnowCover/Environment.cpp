#include "Environment.h"
#include "Globals.h"
#include "Utils/StringUtils.h"
#include <algorithm>
#include <cmath>

namespace SnowEnvironment
{
	bool EqualsInsensitive(std::string_view value, std::string_view other)
	{
		return value.size() == other.size() && Util::IEndsWithAsciiInsensitive(value, other);
	}
	std::string_view GetCurrentWorldspaceOrCellEditorID()
	{
		auto* tes = globals::game::tes;
		if (!tes)
			return {};
		const char* name = nullptr;
		if (tes->interiorCell)
			name = tes->interiorCell->GetFormEditorID();
		else if (auto* worldspace = tes->GetRuntimeData2().worldSpace)
			name = worldspace->GetFormEditorID();
		return name ? name : "";
	}

	bool IsObjectLODTree(const char* name, const RE::BSShaderProperty* property)
	{
		if (!name)
			return false;
		std::string_view geometryName(name);
		constexpr std::string_view treePassThrough = "treepassthru";
		if (geometryName.size() >= treePassThrough.size() && EqualsInsensitive(geometryName.substr(0, treePassThrough.size()), treePassThrough))
			return true;
		if (!property || !property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided))
			return false;
		constexpr std::string_view largeRef = "-LargeRef";
		if (geometryName.size() > largeRef.size() && EqualsInsensitive(geometryName.substr(geometryName.size() - largeRef.size()), largeRef))
			geometryName.remove_suffix(largeRef.size());
		for (const std::string_view category : { "obj", "objHD", "objpassthru" })
			if (EqualsInsensitive(geometryName, category))
				return true;
		return false;
	}

	bool IsHarvestableFlora(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref)
			return false;
		auto base = a_ref->GetObjectReference();
		if (!base)
			return false;
		switch (base->GetFormType()) {
		case RE::FormType::Flora:
			return true;
		case RE::FormType::Tree:
			return static_cast<RE::TESObjectTREE*>(base)->produceItem != nullptr;
		default:
			return false;
		}
	}

	RE::NiBound MergeBounds(const RE::NiBound& a_lhs, const RE::NiBound& a_rhs)
	{
		const RE::NiPoint3 delta = a_rhs.center - a_lhs.center;
		const float distance = delta.Length();
		if (distance + a_rhs.radius <= a_lhs.radius)
			return a_lhs;
		if (distance + a_lhs.radius <= a_rhs.radius)
			return a_rhs;
		RE::NiBound combined;
		combined.radius = 0.5f * (distance + a_lhs.radius + a_rhs.radius);
		combined.center = a_lhs.center + delta * ((combined.radius - a_lhs.radius) / distance);
		return combined;
	}

	float GetFractionalGameMonth(const RE::Calendar& calendar)
	{
		const auto month = std::min(calendar.GetMonth(), static_cast<uint32_t>(std::size(RE::Calendar::DAYS_IN_MONTH) - 1));
		const float result = static_cast<float>(month) + (calendar.GetDay() - 1.0f + calendar.GetHour() / 24.0f) / RE::Calendar::DAYS_IN_MONTH[month];
		return std::isfinite(result) ? result : static_cast<float>(month);
	}

	float GetPrecipitationIntensity(const RE::TESWeather* weather)
	{
		if (auto* precipitation = weather ? weather->precipitationData : nullptr) {
			using DataID = RE::BGSShaderParticleGeometryData::DataID;
			const float intensity = precipitation->GetSettingValue(DataID::kParticleDensity).f * precipitation->GetSettingValue(DataID::kGravityVelocity).f;
			return std::isfinite(intensity) ? std::max(intensity, 0.0f) : 0.0f;
		}
		return 0.0f;
	}

	bool IsArchitecturalModel(const RE::TESModel* model)
	{
		if (!model || !model->GetModel())
			return false;
		auto path = Util::ToLowerAscii(model->GetModel());
		std::replace(path.begin(), path.end(), '/', '\\');
		return path.find("architecture\\") != std::string::npos || path.find("dungeons\\") != std::string::npos;
	}

	bool IsPlacedFireCandidate(RE::TESBoundObject* a_base)
	{
		if (!a_base)
			return false;
		switch (a_base->GetFormType()) {
		case RE::FormType::Static:
		case RE::FormType::MovableStatic:
		case RE::FormType::Light:
		case RE::FormType::Activator:
		case RE::FormType::TalkingActivator:
		case RE::FormType::Furniture:
			return true;
		default:
			return false;
		}
	}

	bool IsFireGeometry(RE::BSGeometry* a_geometry)
	{
		auto& data = a_geometry->GetGeometryRuntimeData();
		auto* property = data.shaderProperty.get();
		auto* alpha = data.alphaProperty.get();
		if (!property || !alpha || property->GetRTTI() != globals::rtti::BSEffectShaderPropertyRTTI.get())
			return false;
		if (alpha->GetDestBlendMode() != RE::NiAlphaProperty::AlphaFunction::kOne)
			return false;
		auto* material = static_cast<RE::BSEffectShaderMaterial*>(property->material);
		if (!material)
			return false;

		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		if (property->flags.any(Flag::kSoftEffect))
			return !material->greyscaleTexturePath.empty() && property->flags.all(Flag::kGrayscaleToPaletteColor, Flag::kGrayscaleToPaletteAlpha);

		auto* particles = a_geometry->AsParticlesGeom();
		if (!particles || netimmerse_cast<RE::BSStripParticleSystem*>(a_geometry) || material->sourceTexturePath.empty())
			return false;
		auto* particleData = particles->GetParticlesRuntimeData().particleData.get();
		return particleData && particleData->GetParticlesRuntimeData().subTextureOffsetsCount != 0;
	}

	void CollectFireGeometry(RE::NiAVObject* a_object, bool a_hidden, bool& a_hasFire, std::vector<RE::NiBound>& a_samples)
	{
		if (!a_object)
			return;
		const bool hidden = a_hidden || a_object->GetAppCulled();
		if (auto* geometry = a_object->AsGeometry()) {
			if (!IsFireGeometry(geometry))
				return;
			a_hasFire = true;
			const auto& bound = geometry->worldBound;
			if (!hidden && bound.radius > 0.0f && std::isfinite(bound.radius) &&
				std::isfinite(bound.center.x) && std::isfinite(bound.center.y) && std::isfinite(bound.center.z))
				a_samples.push_back(bound);
			return;
		}
		if (auto* node = a_object->AsNode()) {
			for (auto& child : node->GetChildren())
				CollectFireGeometry(child.get(), hidden, a_hasFire, a_samples);
		}
	}

}
