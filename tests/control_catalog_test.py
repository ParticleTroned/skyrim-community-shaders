"""Exercise search metadata extraction without compiling or rendering game UI."""

import importlib.util
from pathlib import Path
import sys
import unittest

path = Path(__file__).resolve().parents[1] / "tools" / "generate-control-catalog.py"
spec = importlib.util.spec_from_file_location("control_catalog", path)
catalog = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = catalog
spec.loader.exec_module(catalog)


def extract(body, helpers="", header=""):
    sources = [catalog.Source("src/Features/Sample.h", '''
struct Sample { std::string GetShortName() const override { return "Sample"; } };
''' + header), catalog.Source("src/Features/Sample.cpp", helpers + '''
void Sample::DrawSettings() {
 MenuUI::SettingsPage page("LegacyId", {});
''' + body + "\n}")]
    return catalog.collect(sources)


class CatalogTests(unittest.TestCase):
    def test_unvisited_tabs_and_helper_labels(self):
        entries = extract('''
if (page.Is("first")) { Helper("Strength##one"); }
if (page.Is("second")) Helper("Strength##two");
''', 'void Sample::Helper(const char* label) { Util::Widgets::SliderFloat(label, &v, 0, 1); }')
        self.assertEqual({(e["page"], e["section"], e["raw"]) for e in entries}, {
            ("Sample", "first", "Strength##one"), ("Sample", "second", "Strength##two")})
        self.assertEqual(len({e["id"] for e in entries}), 2)

    def test_comments_strings_and_unknown_labels_are_not_controls(self):
        entries = extract(r'''
// ImGui::Button("comment");
/* ImGui::Checkbox("block", &v); */
const auto text = R"raw(ImGui::Button("string"); } {)raw";
ImGui::Button(computeLabel());
ImGui::Button("##Invisible");
ImGui::Button("Known " "label");
''')
        self.assertEqual([e["label"] for e in entries], ["Known label"])

    def test_lambda_and_table_labels(self):
        entries = extract('''
if (page.Is("detail")) {
 const auto draw = [&](const char* label) { ImGui::Checkbox(label, &v); };
 draw("Lambda control");
 for (const auto& field : Fields::kRows) Util::Widgets::SliderFloat(field.label, &v, 0, 1);
}
''', header='''
namespace Fields {
struct Row { const char* name; const char* label; const char* group; float minimum; };
inline constexpr std::array kRows { Row{"x", "Table X", "Colour", 0}, Row{"y", "Table Y", "Lighting", 0} };
}
''')
        self.assertEqual({e["label"] for e in entries}, {"Lambda control", "Table X", "Table Y"})
        self.assertEqual({e["label"]: e["group"] for e in entries if e["group"]}, {"Table X": "Colour", "Table Y": "Lighting"})
        self.assertTrue(all(e["section"] == "detail" for e in entries))

    def test_navigation_choices_are_not_feature_setting_choices(self):
        entries = extract('''
if (page.Is("global")) DrawControls(true);
if (page.Is("profile")) DrawControls(false);
''', '''
void Sample::DrawControls(bool global) {
 const char* groups[] = {"Lighting", "Water"};
 int selected = 0;
 MenuUI::NavigationChoice("Adjust", &selected, groups, global ? 2 : 1);
 switch(selected) { case 0: DrawLight(); break; case 1: ImGui::SliderFloat("Waves", &v, 0, 1); break; }
 MenuUI::ChoiceSetting("Rendering mode", &mode, groups, 2);
 switch(mode) { case 0: ImGui::Checkbox("Conditional", &v); break; }
}
void Sample::DrawLight() { ImGui::SliderFloat("Brightness", &v, 0, 1); }
''')
        waves = [e for e in entries if e["label"] == "Waves"]
        self.assertEqual([(e["section"], e["selector"], e["choice"]) for e in waves], [("global", "Adjust", "Water")])
        self.assertTrue(all(e["choice"] == "Lighting" for e in entries if e["label"] == "Brightness"))
        self.assertTrue(all(not e["selector"] for e in entries if e["label"] == "Conditional"))

    def test_table_controls_reveal_their_declared_section(self):
        entries = extract('''
for (const auto& field : Numbers)
 if (page.Is(field.section)) Util::Widgets::SliderFloat(field.label, &v, 0, 1);
''', helpers='''
struct Number { const char* label; const char* section; float minimum; };
constexpr std::array Numbers { Number{"Roughness", "lighting", 0}, Number{"Tiling", "detail", 1} };
''')
        self.assertEqual({e["label"]: e["section"] for e in entries}, {"Roughness": "lighting", "Tiling": "detail"})

    def test_identity_is_stable_across_unrelated_source_order_changes(self):
        first = extract('ImGui::Button("Alpha"); ImGui::Button("Beta");')
        second = extract('ImGui::Button("New"); ImGui::Button("Beta"); ImGui::Button("Alpha");')
        ids = {e["label"]: e["id"] for e in second}
        self.assertTrue(all(e["id"] == ids[e["label"]] for e in first))

    def test_skin_controls_have_visible_destinations(self):
        root = Path(__file__).resolve().parents[1]
        sources = [catalog.Source(name, (root / name).read_text(encoding="utf-8"))
                   for name in ("src/Features/Skin.h", "src/Features/Skin.cpp")]
        entries = catalog.collect(sources)
        by_label = {e["label"]: e["section"] for e in entries}
        for label, section in {"Fresnel Reflectance": "lighting", "Body Tiling Multiplier": "detail",
                               "Transmission Depth": "transmission", "Wetness Fade Time": "wetness"}.items():
            self.assertEqual(by_label[label], section)
        self.assertEqual(sum(e["label"] == "Enable Skin Detail" for e in entries), 1)
        self.assertEqual(sum(e["label"] == "Enable Skin Transmission" for e in entries), 1)

    def test_recursive_helpers_terminate_and_deduplicate(self):
        entries = extract('Helper(); Helper();', 'void Sample::Helper() { ImGui::Button("Once"); Helper(); }')
        self.assertEqual([e["label"] for e in entries], ["Once"])

    def test_unicode_and_quotes_have_portable_cpp_encoding(self):
        self.assertEqual(catalog.cpp_string('A"\\\n🕹'), '"A\\042\\134\\012\\360\\237\\225\\271"')
        entries = extract('ImGui::Button("日本語");')
        self.assertEqual(entries[0]["label"], "日本語")

    def test_local_variable_from_other_function_is_not_a_constant(self):
        entries = extract('ImGui::Button(label);', 'void Other() { const char* label = "Unrelated"; }')
        self.assertEqual(entries, [])


if __name__ == "__main__":
    unittest.main()
