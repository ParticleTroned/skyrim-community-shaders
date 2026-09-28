"""Check the actual DevBench schema and preparation/evidence wiring."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(name):
    return (ROOT / name).read_text()


class CurrentContextContract(unittest.TestCase):
    def test_registered_schema(self):
        bridge = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        raw = re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', bridge, re.S).group(1)

        def unique(pairs):
            result = {}
            for key, value in pairs:
                self.assertNotIn(key, result)
                result[key] = value
            return result

        schema = json.loads(raw, object_pairs_hook=unique)
        inputs = schema["inputSchema"]
        self.assertEqual(inputs["properties"]["experimentalCurrentContext"]["type"], "boolean")
        configure = next(x for x in inputs["allOf"] if x.get("if", {}).get("properties", {}).get("action", {}).get("const") == "nr_configure")
        self.assertIn("experimentalCurrentContext", configure["then"]["propertyNames"]["enum"])
        self.assertIn("default false", schema["description"])
        output = schema["outputSchema"]["properties"]["neuralRendering"]["properties"]["characterRendering"]
        self.assertEqual(output["properties"]["settings"]["properties"]["experimentalCurrentContext"]["type"], "boolean")
        self.assertIn('!parseBoolean("experimentalCurrentContext", a_request.experimentalCurrentContext)', bridge)
        self.assertIn('requestedSettings.neuralCharacterCurrentContextEnabled = *request.experimentalCurrentContext', bridge)

    def test_preparation_and_source_identity(self):
        rendering = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        self.assertIn("add(a_settings.experimentalCurrentContext);", rendering)
        self.assertRegex(rendering, r"ApplyCurrentContextExperiment\(\*slot.roi, a_args.settings, a_args.outputIsJittered,\s*usedEarlyBounds \|\| !plan.fullEyeEligibilityFallback\)")
        self.assertIn("slot.computeSubrect = slot.roi->inferenceContext;", rendering)
        self.assertIn("a_result.roi = slot.roi;", rendering)
        self.assertIn('"contextPolicy", value.currentContextApplied ?', read("src/Features/Upscaling/NeuralRendering/ExecutionEvidenceJson.h"))
        self.assertIn('values["neuralCharacterCurrentContextEnabled"] = settings.neuralCharacterCurrentContextEnabled;', read("src/Features/Upscaling/NeuralCapture.cpp"))

    def test_session_only_configuration(self):
        mapping = read("src/Features/Upscaling/NeuralRendering/CharacterSettingsJson.h")
        self.assertNotIn("neuralCharacterCurrentContextEnabled", mapping)
        serialization = read("src/Features/Upscaling/NeuralRendering/ConfigurationSerialization.h")
        self.assertIn('rendering.erase("neuralCharacterCurrentContextEnabled");', serialization)
        self.assertIn("destination.neuralCharacterCurrentContextEnabled = source.neuralCharacterCurrentContextEnabled;", serialization)
        source = read("src/Features/Upscaling.cpp")
        self.assertIn("policy.experimentalCurrentContext = a_settings.neuralCharacterCurrentContextEnabled;", source)
        self.assertIn("add(a_settings.neuralCharacterCurrentContextEnabled);", source)
        self.assertIn("settings.neuralCharacterCurrentContextEnabled = false;", source)


if __name__ == "__main__":
    unittest.main()
