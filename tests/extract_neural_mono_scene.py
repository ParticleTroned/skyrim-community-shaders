"""Extract the real SE post-processing/source selection and resolution arithmetic."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    def read(name):
        return (root / name).read_text(encoding="utf-8-sig")

    upscaling = read("src/Features/Upscaling.cpp")
    header = read("src/Features/Upscaling.h")
    hdr = read("src/Features/HDRDisplay.cpp")
    d3d = read("src/Utils/D3D.cpp")
    game = read("src/Utils/Game.cpp")
    mono = read("src/Features/Upscaling/NeuralRendering/MonoRenderer.h")
    output.mkdir(parents=True, exist_ok=True)
    redirect = function(hdr, "bool HDRDisplay::RedirectFramebuffer(" if "bool HDRDisplay::RedirectFramebuffer(" in hdr
                        else "void HDRDisplay::RedirectFramebuffer(")
    (output / "neural_hdr_members.h").write_text(
        redirect[:redirect.index("{")].replace("HDRDisplay::", "") + ";\n", encoding="utf-8")
    (output / "neural_scene_types.h").write_text(
        "namespace NeuralRendering {\n" + function(mono, "struct MonoFrame") + ";\n}\n", encoding="utf-8")
    (output / "neural_upscaling_members.h").write_text(
        function(header, "struct RuntimeResolutionPlan") + ";\n"
        + function(header, "static constexpr float GetQualityModeResolutionScale("), encoding="utf-8")
    if "ID3D11Texture2D* ResolveRenderTargetTexture(" in d3d:
        resolve = function(d3d, "ID3D11Texture2D* ResolveRenderTargetTexture(")
    else:
        # The baseline kept this identical source-view resolution in ScreenshotFeature.
        resolve = function(read("src/Features/ScreenshotFeature.cpp"), "ID3D11Texture2D* ResolveSlotTexture(")
        resolve = resolve.replace("ResolveSlotTexture(", "ResolveRenderTargetTexture(", 1)
    bodies = [
        "namespace Util {\n" + resolve + "\n" + function(game, "float2 ConvertToDynamic(") + "\n}\n",
        redirect, function(hdr, "void HDRDisplay::RestoreFramebuffer("),
        function(upscaling, "void Upscaling::Main_PostProcessing::thunk("),
        function(upscaling, "void Upscaling::ApplyFinalNeuralFrame("),
        function(upscaling, "Upscaling::RuntimeResolutionPlan Upscaling::GetRuntimeResolutionPlan("),
    ]
    (output / "neural_mono_scene_under_test.h").write_text("\n".join(bodies), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
