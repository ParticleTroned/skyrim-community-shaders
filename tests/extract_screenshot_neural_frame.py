"""Exercise production Present/frame handoff and NR exposure evidence without D3D."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root, output):
    def read(name):
        return (root / name).read_text(encoding="utf-8-sig")

    hooks = read("src/Hooks.cpp")
    feature = read("src/Features/ScreenshotFeature.cpp")
    renderer = read("src/Features/Upscaling/NeuralRendering/MonoRenderer.cpp")
    header = read("src/Features/Upscaling/NeuralRendering/MonoRenderer.h")
    diagnostics = read("src/Features/ScreenshotNeuralDiagnostics.cpp")
    output.mkdir(parents=True, exist_ok=True)
    process = function(feature, "void ScreenshotFeature::ProcessCaptureRequest(")
    acquire = function(feature, "void ScreenshotFeature::AcquirePendingCapture(")
    # Keep declarations in step with production, including the former implicit
    # frame API, so replaying the baseline fails on behavior rather than arity.
    declarations = "\n".join(body[:body.index("{")].replace("ScreenshotFeature::", "") + ";"
                             for body in (process, acquire))
    (output / "screenshot_frame_members.h").write_text(declarations, encoding="utf-8")
    types = "namespace NeuralRendering {\n" + "\n".join(
        function(header, marker) + ";" for marker in ("struct MonoFrame", "struct MonoSnapshot"))
    types += "\n" + function(renderer, "struct alignas(16) ColorConstants") + ";\n}\n"
    (output / "screenshot_neural_types.h").write_text(types, encoding="utf-8")
    color = between(renderer, "\t\tColorConstants color{};", "\t\tif (!Upload(frame.device")
    exposure = between(renderer, "\t\tstate.snapshot.exposureFrame =", "\t\tstate.snapshot.packedCoverageProven =")
    acquisition_frame = next(line for line in acquire.splitlines() if "const uint32_t frame =" in line)
    acquisition_evidence = next(line for line in acquire.splitlines() if "const auto evidence =" in line)
    bodies = [
        "namespace NeuralRendering {\nconst ColorConstants* ConfigureColorForTest(TestState& state, "
        "const MonoFrame& frame, const MonoSettings& settings, DXGI_FORMAT format, ColorConstants& output) {\n"
        + color + exposure + "\noutput = color; return &output;\n}\n}\n",
        "namespace CSX::ScreenshotPolicy {\nusing namespace NeuralRendering::Color;\nusing Json = nlohmann::json;\n"
        + function(diagnostics, "Json Stamp(") + "\n"
        + function(diagnostics, "nlohmann::json BuildMonoNeuralEvidence(") + "\n}\n",
        process,
        acquire[:acquire.index("{")] + "{\n" + acquisition_frame + "\n" + acquisition_evidence
        + "\nacquiredFrames.push_back(frame); lastEvidence = evidence;\n}\n",
        "namespace Hooks {\n" + function(hooks, "HRESULT RunSwapChainPresent(") + "\n}\n",
    ]
    (output / "screenshot_neural_frame_under_test.h").write_text("\n".join(bodies), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
