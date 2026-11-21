#include "DetailMaps.h"
#include "DetailMapsPolicy.h"
#include "Menu/SettingsPage.h"
#include "Utils/DdsResource.h"
#include <DirectXTex.h>
#include <fmt/ranges.h>

#include "Features/Upscaling.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/StringUtils.h"
#include "Utils/UI.h"

namespace
{
	constexpr uint32_t DetailFlag = static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::DetailMaps);
	constexpr uint32_t ReflectionsFlag = static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
	constexpr uint32_t TextureSlot = DetailMapsPolicy::TextureSlot;
	constexpr uint32_t PerDrawSlot = DetailMapsPolicy::ConstantSlot;
	constexpr uint32_t SamplerSlot = DetailMapsPolicy::SamplerSlot;
	constexpr uint32_t FirstPersonRenderMode = 0x16;
	constexpr size_t MaxEntries = 8192;
	constexpr int32_t NoMatch = -1;
	constexpr int32_t Excluded = -2;
	constexpr uint32_t NoSet = std::numeric_limits<uint32_t>::max();
	constexpr std::string_view SetsPath = "Data\\Shaders\\DetailMaps\\Sets";
	constexpr std::string_view RulesPath = "Data\\Shaders\\DetailMaps\\Rules";

	bool IsFirstPersonPass()
	{
		auto* accumulator = *globals::game::currentAccumulator.get();
		return accumulator && static_cast<uint32_t>(accumulator->GetRuntimeData().renderMode) == FirstPersonRenderMode;
	}

	bool IsDetailDescriptor(uint32_t descriptor, bool allowForward)
	{
		static_assert(DetailFlag == DetailMapsPolicy::ShaderFlag);
		static_assert((DetailFlag & SIE::ShaderCache::GetCharacterCategoryFlag(RE::BSShader::Type::Lighting)) == 0);
		return DetailMapsPolicy::IsEligible(descriptor, allowForward);
	}

	std::string ToLower(std::string_view a_text)
	{
		std::string text = Util::ToLowerAscii(a_text);
		std::ranges::replace(text, '/', '\\');
		return text;
	}

	bool MatchesAny(DetailMapsPolicy::PathMatcher& matcher, const std::vector<std::string>& a_patterns)
	{
		return std::ranges::any_of(a_patterns, [&](const std::string& a_pattern) { return matcher.Matches(a_pattern); });
	}

	std::vector<std::string> ReadPatterns(const json& a_object, const char* a_key)
	{
		std::vector<std::string> patterns;
		const auto it = a_object.find(a_key);
		if (it == a_object.end())
			return patterns;
		if (it->is_string()) {
			patterns.push_back(ToLower(it->get<std::string>()));
		} else if (it->is_array()) {
			if (it->size() > 256)
				throw std::invalid_argument("Too many detail map patterns");
			for (const auto& value : *it) {
				if (!value.is_string())
					throw std::invalid_argument("Detail map patterns must be strings");
				patterns.push_back(ToLower(value.get<std::string>()));
			}
		} else
			throw std::invalid_argument("Detail map patterns must be a string or array");
		for (const auto& pattern : patterns)
			if (pattern.empty() || pattern.size() > DetailMapsPolicy::MaxPatternLength)
				throw std::invalid_argument("Detail map patterns must contain 1..512 characters");
		return patterns;
	}

	float ReadNumber(const json& a_object, const char* a_key, float a_default, float a_min, float a_max)
	{
		const auto it = a_object.find(a_key);
		const float value = it != a_object.end() && it->is_number() ? it->get<float>() : a_default;
		return std::isfinite(value) ? std::clamp(value, a_min, a_max) : a_default;
	}

	std::vector<std::filesystem::path> ListFiles(std::string_view a_folder, std::string_view a_extension)
	{
		std::vector<std::filesystem::path> files;
		std::error_code error;
		std::filesystem::directory_iterator it(std::filesystem::path(a_folder), error);
		for (const std::filesystem::directory_iterator end; !error && it != end; it.increment(error)) {
			std::error_code fileError;
			if (it->is_regular_file(fileError) && ToLower(Util::PathToUtf8(it->path().extension())) == a_extension) {
				if (files.size() >= 256)
					throw std::invalid_argument("Too many Detail Maps content files (maximum 256 per folder)");
				files.push_back(it->path());
			}
			if (fileError)
				throw std::filesystem::filesystem_error("Cannot inspect Detail Maps content", it->path(), fileError);
		}
		if (error && error != std::errc::no_such_file_or_directory)
			throw std::filesystem::filesystem_error("Cannot enumerate Detail Maps content", std::filesystem::path(a_folder), error);
		std::ranges::sort(files);
		return files;
	}
}

void DetailMaps::ClampSettings()
{
	DetailMapsConfig::Clamp(settings);
}

void DetailMaps::RestoreDefaultSettings()
{
	settings = {};
}

void DetailMaps::LoadSettings(json& o_json)
{
	settings = o_json;
	ClampSettings();
	SetPerformanceCostMeasurementEnabled(settings.Enabled);
}

void DetailMaps::SaveSettings(json& o_json)
{
	o_json = settings;
}

void DetailMaps::DrawSettings()
{
	MenuUI::SettingsPage page("DetailMaps", { { "appearance", "Surface Detail", "Add fine detail to nearby surfaces.", "Detail Maps", true, true, "Refine surfaces" } });
	if (!page.Is("appearance"))
		return;
	if (!resourceError.empty())
		Util::Text::WrappedWarning("%s", resourceError.c_str());
	if (!HasContent())
		Util::Text::WrappedWarning("%s", "No detail sets or rules found. Install the Detail Maps Pack.");

	for (const auto& field : std::span(DetailMapsConfig::Numbers).first<4>())
		ImGui::SliderFloat(field.label, &(settings.*field.member), static_cast<float>(field.minimum), static_cast<float>(field.maximum), "%.2f", ImGuiSliderFlags_AlwaysClamp);

	DrawDistanceSettings();

	if (ImGui::Button("Reload Rules and Sets"))
		Reload();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Rereads Data\\Shaders\\DetailMaps\\Rules\\*.json and Data\\Shaders\\DetailMaps\\Sets\\*.dds.");

	ImGui::Text("%s: %zu", "Detail sets", sets.size());
	ImGui::Text("%s: %zu", "Rules", rules.size());
	ImGui::Text("%s: %u", "Detail draws last frame", lastPassCount);
	for (const auto& error : ruleErrors)
		ImGui::TextColored(Util::Colors::GetError(), "%s", error.c_str());
}

Feature::SettingsHeaderStatus DetailMaps::GetSettingsHeaderStatus() const
{
	if (!resourceError.empty())
		return { "Unavailable", true };
	if (!HasContent())
		return { "Pack required", true };
	if (!ruleErrors.empty())
		return { "Content errors", true };
	return {};
}

void DetailMaps::DrawDistanceSettings()
{
	for (const auto& field : std::span(DetailMapsConfig::Numbers).last<2>())
		ImGui::SliderFloat(field.label, &(settings.*field.member), static_cast<float>(field.minimum), static_cast<float>(field.maximum), "%.0f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("%s", "Detail fades out between these distances in game units. A shorter range reduces the number of surfaces using detail.");
	ClampSettings();
}

void DetailMaps::DrawPerformanceSettings(bool)
{
	DrawDistanceSettings();
	if (!HasContent())
		Util::Text::WrappedWarning("%s", "Install the Detail Maps Pack to apply surface detail.");
}

void DetailMaps::SetPerformanceCostMeasurementEnabled(bool value)
{
	settings.Enabled = value;
	if (!value) {
		EndPass();
		passCount = lastPassCount = pendingShaders = lastPendingShaders = 0;
	}
}

void DetailMaps::LoadSets()
{
	size_t totalBytes = 0;
	sets.clear();
	auto* device = globals::d3d::device;
	for (const auto& path : ListFiles(SetsPath, ".dds")) {
		std::error_code sizeError;
		const auto fileBytes = std::filesystem::file_size(path, sizeError);
		DirectX::TexMetadata metadata{};
		if (sizeError || fileBytes > 32u * 1024u * 1024u ||
			FAILED(DirectX::GetMetadataFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, metadata)) ||
			metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.IsCubemap() ||
			!metadata.width || !metadata.height || metadata.width > 4096 || metadata.height > 4096 ||
			!metadata.mipLevels || metadata.mipLevels > 13 ||
			(metadata.format != DXGI_FORMAT_BC7_UNORM && metadata.format != DXGI_FORMAT_BC3_UNORM && metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM)) {
			ruleErrors.push_back(std::format("{}: expected a bounded linear RGBA8, BC3 or BC7 2D DDS", Util::PathToUtf8(path.filename())));
			continue;
		}
		DirectX::ScratchImage image;
		if (FAILED(DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image))) {
			ruleErrors.push_back(std::format("{}: {}", Util::PathToUtf8(path.filename()), "could not be loaded"));
			continue;
		}

		if (image.GetPixelsSize() > 128u * 1024u * 1024u - totalBytes || sets.size() >= 64) {
			ruleErrors.push_back("Detail Maps content exceeds 64 sets or 128 MiB");
			break;
		}
		Set set{ ToLower(Util::PathToUtf8(path.stem())), nullptr };
		try {
			set.view = Util::CreateDDSTextureFromImage(device, image, std::format("DetailMaps::{}", set.name).c_str());
		} catch (const std::exception& error) {
			ruleErrors.push_back(std::format("{}: {}", set.name, error.what()));
			continue;
		}
		totalBytes += image.GetPixelsSize();
		sets.push_back(std::move(set));
	}
	logger::info("[Detail Maps] Loaded {} detail sets", sets.size());
}

void DetailMaps::LoadRules()
{
	rules.clear();
	excludes.clear();

	const auto files = ListFiles(RulesPath, ".json");
	for (size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
		const auto fileName = Util::PathToUtf8(files[fileIndex].filename());
		json data;
		try {
			if (std::filesystem::file_size(files[fileIndex]) > 1024u * 1024u)
				throw std::invalid_argument("Rule file exceeds 1 MiB");
			std::ifstream stream(files[fileIndex]);
			data = json::parse(stream, nullptr, true, true);
		} catch (const std::exception& e) {
			ruleErrors.push_back(std::format("{}: {}", fileName, e.what()));
			continue;
		}
		if (!data.is_object()) {
			ruleErrors.push_back(std::format("{}: {}", fileName, "expected a JSON object"));
			continue;
		}

		try {
			for (auto& pattern : ReadPatterns(data, "exclude")) {
				if (excludes.size() >= 8192)
					throw std::invalid_argument("Detail Maps exclusion limit exceeded");
				excludes.push_back(std::move(pattern));
			}

			const auto list = data.find("rules");
			if (list == data.end())
				continue;
			if (!list->is_array())
				throw std::invalid_argument("Detail Maps rules must be an array");

			for (const auto& item : *list) {
				if (!item.is_object())
					throw std::invalid_argument("Each Detail Maps rule must be an object");

				Rule rule;
				rule.file = fileIndex;
				rule.match = ReadPatterns(item, "match");
				rule.exclude = ReadPatterns(item, "exclude");
				const auto setName = item.contains("set") && item["set"].is_string() ? ToLower(item["set"].get<std::string>()) : std::string();
				const auto set = std::ranges::find(sets, setName, &Set::name);
				if (rule.match.empty() || (set == sets.end() && setName != "none")) {
					ruleErrors.push_back(std::format("{}: {} \"{}\"", fileName, "skipped a rule without patterns or with an unknown set", setName));
					continue;
				}

				rule.set = set == sets.end() ? NoSet : static_cast<uint32_t>(std::distance(sets.begin(), set));
				rule.scale = ReadNumber(item, "scale", 4.0f, 0.01f, 256.0f);
				rule.albedo = ReadNumber(item, "albedo", 1.0f, 0.0f, 4.0f);
				rule.normal = ReadNumber(item, "normal", 1.0f, 0.0f, 4.0f);
				rule.roughness = ReadNumber(item, "roughness", 1.0f, 0.0f, 4.0f);
				rule.transpose = item.contains("transpose") && item["transpose"].is_boolean() && item["transpose"].get<bool>();
				rule.priority = static_cast<int>(ReadNumber(item, "priority", 0.0f, -1000.0f, 1000.0f));
				if (rules.size() >= 8192 || excludes.size() > 8192)
					throw std::invalid_argument("Detail Maps rule/exclusion limit exceeded");
				rules.push_back(std::move(rule));
			}
		} catch (const std::exception& error) {
			ruleErrors.push_back(std::format("{}: {}", fileName, error.what()));
		}
	}

	std::ranges::stable_sort(rules, [](const Rule& a_left, const Rule& a_right) {
		return a_left.priority != a_right.priority ? a_left.priority > a_right.priority : a_left.file > a_right.file;
	});
	logger::info("[Detail Maps] Loaded {} rules and {} exclusions from {} files", rules.size(), excludes.size(), files.size());
}

void DetailMaps::Reload()
{
	ruleErrors.clear();
	EndPass();
	auto previousSets = std::move(sets);
	auto previousRules = std::move(rules);
	auto previousExcludes = std::move(excludes);
	try {
		LoadSets();
		LoadRules();
	} catch (const std::exception& error) {
		ruleErrors.push_back(error.what());
	}
	if (!ruleErrors.empty()) {
		sets = std::move(previousSets);
		rules = std::move(previousRules);
		excludes = std::move(previousExcludes);
		logger::warn("[Detail Maps] Content reload rejected; previous content retained: {}", fmt::join(ruleErrors, "; "));
	}
	entries.clear();
}

int32_t DetailMaps::MatchRule(const char* a_path) const
{
	if (!a_path || !*a_path)
		return NoMatch;

	const auto path = DetailMapsPolicy::NormalizePath(a_path);
	DetailMapsPolicy::PathMatcher matcher(path);
	if (MatchesAny(matcher, excludes))
		return Excluded;

	for (size_t index = 0; index < rules.size(); ++index) {
		const auto& rule = rules[index];
		if (MatchesAny(matcher, rule.match) && !MatchesAny(matcher, rule.exclude))
			return rule.set == NoSet ? Excluded : static_cast<int32_t>(index);
	}
	return NoMatch;
}

int32_t DetailMaps::FindRule(RE::BSRenderPass* a_pass)
{
	auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_pass->shaderProperty->material);
	auto* diffuse = material ? material->diffuseTexture.get() : nullptr;
	if (!diffuse)
		return NoMatch;

	const char* fallback = material->textureSet ? material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : "";
	if (!fallback)
		fallback = "";
	auto it = entries.find(diffuse);
	if (it == entries.end() || it->second.name != diffuse->name || std::strcmp(it->second.fallback.c_str(), fallback) != 0) {
		int32_t rule = MatchRule(diffuse->name.c_str());
		if (rule == NoMatch && material->textureSet)
			rule = MatchRule(fallback);
		if (entries.size() >= MaxEntries)
			entries.clear();
		it = entries.insert_or_assign(diffuse, Entry{ diffuse->name, fallback, std::max(rule, NoMatch) }).first;
	}

	return it->second.rule;
}

void DetailMaps::GenerateShaderPermutations(RE::BSShader* shader)
{
	if (!settings.Enabled || shader->shaderType != RE::BSShader::Type::Lighting)
		return;
	std::error_code error;
	if (!std::filesystem::is_directory(std::filesystem::path(SetsPath), error) ||
		!std::filesystem::is_directory(std::filesystem::path(RulesPath), error))
		return;
	const bool categories = globals::features::upscaling.NeedsCharacterCategoryShaders();
	for (const auto& entry : shader->pixelShaders) {
		for (bool deferred : { false, true }) {
			auto vertex = entry->id;
			auto pixel = entry->id;
			globals::state->ModifyShaderLookup(*shader, vertex, pixel, deferred);
			if (IsDetailDescriptor(pixel, true))
				globals::shaderCache->PrewarmDeferredPixelShaders(*shader, pixel | DetailFlag, deferred && categories, true);
		}
	}
}

DetailMaps::PassState::PassState(ID3D11DeviceContext* value) : context(value)
{
	context->PSGetShader(shader.put(), nullptr, nullptr);
	context->PSGetConstantBuffers(PerDrawSlot, 1, buffer.put());
	context->PSGetShaderResources(TextureSlot, 1, view.put());
	context->PSGetSamplers(SamplerSlot, 1, sampler.put());
}

DetailMaps::PassState::~PassState()
{
	context->PSSetShader(shader.get(), nullptr, 0);
	ID3D11Buffer* oldBuffer = buffer.get();
	ID3D11ShaderResourceView* oldView = view.get();
	ID3D11SamplerState* oldSampler = sampler.get();
	context->PSSetConstantBuffers(PerDrawSlot, 1, &oldBuffer);
	context->PSSetShaderResources(TextureSlot, 1, &oldView);
	context->PSSetSamplers(SamplerSlot, 1, &oldSampler);
}

void DetailMaps::EndPass()
{
	passState.reset();
}

void DetailMaps::BeginPass(RE::BSShader* shader, RE::BSRenderPass* pass)
{
	EndPass();
	if (!loaded || !settings.Enabled || !resourceError.empty() || !perDrawCB || !detailSampler || !HasContent() ||
		!globals::shaderCache->IsEnabled() || globals::state->selectedCustomPixelShaderType != RE::BSShader::Type::Lighting)
		return;
	try {
		BindPass(shader, pass);
	} catch (const std::exception& error) {
		EndPass();
		resourceError = std::format("Detail Maps disabled for this session: {}", error.what());
		logger::error("{}", resourceError);
	}
}

bool DetailMaps::BindPass(RE::BSShader* shader, RE::BSRenderPass* pass)
{
	const bool firstPerson = IsFirstPersonPass();
	if (!shader || !pass || !pass->geometry || !pass->shaderProperty ||
		(globals::state->permutationData.ExtraShaderDescriptor & ReflectionsFlag) != 0 ||
		!IsDetailDescriptor(globals::state->modifiedPixelDescriptor, firstPerson))
		return false;
	const auto* material = pass->shaderProperty->GetBaseMaterial();
	if (!material || material->GetType() != RE::BSShaderMaterial::Type::kLighting)
		return false;
	using enum RE::BSShaderProperty::EShaderPropertyFlag;
	if (pass->shaderProperty->flags.any(kDecal, kDynamicDecal))
		return false;
	const auto& geometryData = pass->geometry->GetGeometryRuntimeData();
	if (!firstPerson && geometryData.alphaProperty && geometryData.alphaProperty->GetAlphaBlending())
		return false;

	auto eye = Util::GetEyePosition(0);
	if (globals::game::isVR)
		eye = (eye + Util::GetEyePosition(1)) * 0.5f;
	const auto& bound = pass->geometry->worldBound;
	const float distance = firstPerson ? 0.0f : bound.center.GetDistance(eye) - bound.radius;
	if (!std::isfinite(distance) || distance >= settings.FadeEnd)
		return false;
	const int32_t index = FindRule(pass);
	if (index < 0 || static_cast<size_t>(index) >= rules.size())
		return false;
	const auto& rule = rules[static_cast<size_t>(index)];
	if (rule.set >= sets.size())
		return false;

	auto* pixel = globals::shaderCache->GetPixelShader(*shader,
		globals::state->modifiedPixelDescriptor | DetailFlag, SIE::ShaderCache::PixelShaderLookup::Compile);
	if (!pixel) {
		++pendingShaders;
		return false;
	}
	const PerDraw data{ rule.scale * settings.Tiling, rule.albedo * settings.AlbedoStrength,
		rule.normal * settings.NormalStrength, rule.roughness * settings.RoughnessStrength,
		settings.FadeEnd, 1.0f / std::max(settings.FadeEnd - settings.FadeStart, 1.0f), rule.transpose ? 1.0f : 0.0f, 0.0f };
	if (!hasPerDraw || std::memcmp(&data, &lastPerDraw, sizeof(data))) {
		perDrawCB->Update(data);
		lastPerDraw = data;
		hasPerDraw = true;
	}
	auto* context = globals::d3d::context;
	passState.emplace(context);
	ID3D11Buffer* buffer = perDrawCB->CB();
	auto* view = sets[rule.set].view.get();
	auto* sampler = detailSampler.get();
	context->PSSetConstantBuffers(PerDrawSlot, 1, &buffer);
	context->PSSetShaderResources(TextureSlot, 1, &view);
	context->PSSetSamplers(SamplerSlot, 1, &sampler);
	context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(pixel->shader), nullptr, 0);
	++passCount;
	return true;
}

void DetailMaps::Reset()
{
	if (!IsEnabled() && !HasActivePass() && !lastPassCount && !lastPendingShaders && !passCount && !pendingShaders)
		return;
	EndPass();
	lastPassCount = std::exchange(passCount, 0);
	lastPendingShaders = std::exchange(pendingShaders, 0);
	if (globals::state->IsMainOrLoadingMenuOpen())
		entries.clear();
}

void DetailMaps::SetupResources()
{
	EndPass();
	// A resource rebuild cannot retain views from a previous D3D device.
	sets.clear();
	rules.clear();
	excludes.clear();
	entries.clear();
	perDrawCB.reset();
	detailSampler = nullptr;
	resourceError.clear();
	hasPerDraw = false;
	try {
		perDrawCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PerDraw>(), "DetailMaps::PerDraw");
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_ANISOTROPIC;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.MaxAnisotropy = 16;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&sampler, detailSampler.put()));
		Util::SetResourceName(detailSampler.get(), "DetailMaps::WrapSampler");
		Reload();
	} catch (const std::exception& error) {
		resourceError = error.what();
		logger::error("Detail Maps resources unavailable: {}", resourceError);
	}
}

json DetailMaps::CapturePerformanceSettingsState() const
{
	return settings;
}

void DetailMaps::RestorePerformanceToggleState(const json& state)
{
	if (state.is_boolean())
		settings.Enabled = state.get<bool>();
	else
		settings = state.get<Settings>();
	ClampSettings();
	SetPerformanceCostMeasurementEnabled(settings.Enabled);
}

#ifdef DEVBENCH_BRIDGE_ENABLED
json DetailMaps::GetControlSchemas()
{
	const auto properties = DetailMapsConfig::Properties();
	return { { "settings", { { "type", "object" }, { "additionalProperties", false }, { "properties", properties } } },
		{ "status", { { "type", "object" }, { "properties", { { "loaded", { { "type", "boolean" } } }, { "ready", { { "type", "boolean" } } },
																{ "settings", { { "type", "object" }, { "properties", properties } } },
																{ "setCount", { { "type", "integer" }, { "minimum", 0 } } },
																{ "ruleCount", { { "type", "integer" }, { "minimum", 0 } } },
																{ "drawsLastFrame", { { "type", "integer" }, { "minimum", 0 } } },
																{ "pendingShaderDrawsLastFrame", { { "type", "integer" }, { "minimum", 0 } } },
																{ "errors", { { "type", "array" }, { "items", { { "type", "string" } } } } },
																{ "resourceError", { { "type", "string" } } }, { "sessionOnly", { { "type", "boolean" } } } } } } } };
}

json DetailMaps::Control(const json& args)
{
	const auto operation = args.value("detailMapsOperation", std::string("inspect"));
	if (operation != "inspect" && operation != "reload")
		throw std::invalid_argument("detailMapsOperation must be inspect or reload");
	if (operation == "reload" && args.contains("detailMaps"))
		throw std::invalid_argument("Reload and settings edits must be separate requests");
	if ((operation == "reload" || args.contains("detailMaps")) && !loaded)
		throw std::invalid_argument("Detail Maps is not loaded");
	if (args.contains("detailMaps")) {
		settings = DetailMapsConfig::Patch(settings, args.at("detailMaps"));
		SetPerformanceCostMeasurementEnabled(settings.Enabled);
	}
	if (operation == "reload")
		Reload();
	return { { "loaded", loaded }, { "ready", loaded && perDrawCB && detailSampler && HasContent() && resourceError.empty() },
		{ "settings", settings }, { "setCount", sets.size() }, { "ruleCount", rules.size() },
		{ "drawsLastFrame", lastPassCount }, { "pendingShaderDrawsLastFrame", lastPendingShaders },
		{ "errors", ruleErrors }, { "resourceError", resourceError }, { "sessionOnly", true } };
}
#endif
