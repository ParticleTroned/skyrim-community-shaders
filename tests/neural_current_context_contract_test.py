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
        sample = output["properties"]["profiling"]["properties"]["lastFeature18GpuSample"]["properties"]
        self.assertEqual(sample["physicalPlanAvailable"]["type"], "boolean")
        self.assertEqual(sample["expectedFeatureEvaluationCount"]["type"], ["integer", "null"])
        self.assertIn('!parseBoolean("experimentalCurrentContext", a_request.experimentalCurrentContext)', bridge)
        self.assertIn('requestedSettings.neuralCharacterCurrentContextEnabled = *request.experimentalCurrentContext', bridge)

    def test_single_roi_admission_and_nonpersistent_identity(self):
        bridge = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        schema = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', bridge, re.S).group(1))
        inputs = schema["inputSchema"]["properties"]
        output = schema["outputSchema"]["properties"]["neuralRendering"]["properties"]["characterRendering"]["properties"]["settings"]["properties"]
        configure = next(x for x in schema["inputSchema"]["allOf"] if x.get("if", {}).get("properties", {}).get("action", {}).get("const") == "nr_configure")
        self.assertIn('executionPlan->physicalSlotMask & NeuralRendering::RegionRouteMask(a_logicalMask)', bridge)
        self.assertIn('NeuralRendering::LogicalRegionMask(lastFeatureSlotMask)', bridge)
        self.assertNotIn('attributedPreparation->physicalRegionMasks[featureSlot]', bridge)
        self.assertNotIn('neuralCharacterRegionLimit', read("src/Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"))
        renderer = read("src/Features/Upscaling/NeuralRendering/Renderer.cpp")
        self.assertIn('args.featureSlot >= kLogicalFeatureSlotCount', renderer)
        self.assertNotIn("computeRegions", read("src/Features/Upscaling/NeuralRendering/Renderer.h"))
        self.assertIn('a_args.size() > kEyeCount', renderer)
        self.assertIn('kFeatureSlotCount = kLogicalFeatureSlotCount;', read("src/Features/Upscaling/NeuralRendering/Runtime.h"))
        self.assertIn('capacityFallback_.ApplyBatch(colorConfiguration_.experiments.CompactInputsEnabled()', renderer)
        self.assertIn('return TeardownBackendLocked(false, false, false);', renderer)
        self.assertNotIn('value.reset = true;', renderer)
        self.assertIn('state_->capacityFallback_ = {};', renderer)

    def test_preparation_and_source_identity(self):
        rendering = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        self.assertIn("add(a_settings.experimentalCurrentContext);", rendering)
        self.assertRegex(rendering, r"ApplyCurrentContextExperiment\(\*slot.roi, a_args.settings, a_args.outputIsJittered,\s*!plan.fullEyeEligibilityFallback\)")
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

    def test_gpu_support_is_session_only_and_opt_in(self):
        bridge = read("src/Features/Upscaling/VRRenderScaleDevBenchBridge.cpp")
        schema = json.loads(re.search(r'kNeuralRenderingDescriptor = R"nr\((.*?)\)nr"', bridge, re.S).group(1))
        key = "experimentalGpuMaskSupport"
        self.assertEqual(schema["inputSchema"]["properties"][key]["type"], "boolean")
        settings = schema["outputSchema"]["properties"]["neuralRendering"]["properties"]["characterRendering"]["properties"]["settings"]["properties"]
        self.assertEqual(settings[key]["type"], "boolean")
        self.assertIn(f'!parseBoolean("{key}", a_request.{key})', bridge)
        self.assertIn(f'requestedSettings.neuralCharacterGpuMaskSupportEnabled = *request.{key}', bridge)
        self.assertIn('rendering.erase("neuralCharacterGpuMaskSupportEnabled");', read("src/Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"))
        rendering = read("src/Features/Upscaling/NeuralRendering/CharacterRendering.cpp")
        self.assertIn("bool supportEnabled = false;\n#ifdef DEVBENCH_BRIDGE_ENABLED", rendering)
        self.assertIn("neuralCharacterGpuMaskSupportEnabled = false;", read("src/Features/Upscaling.h"))
        self.assertIn("neuralCharacterGpuMaskSupportEnabled = false;", read("src/Features/Upscaling.cpp"))


if __name__ == "__main__":
    unittest.main()
