"""Extract frame-generation input and presentation methods for controller tests."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    feature = root / "src/Features"
    source = (feature / "Upscaling.cpp").read_text(encoding="utf-8-sig")
    header = (feature / "Upscaling.h").read_text(encoding="utf-8-sig")
    swapchain = (feature / "Upscaling/DX12SwapChain.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    members = re.search(r"\tstd::atomic_bool frameGenerationPrepared\{[^;]+;", header)
    if members is None:
        raise ValueError("Missing frame-generation input readiness state")
    (output / "frame_generation_members.h").write_text(members[0] + "\n", encoding="utf-8")
    methods = [function(source, signature) for signature in [
        "void Upscaling::PrepareFrameGenerationInputs()",
        "bool Upscaling::CopySharedD3D12Resources()",
        "bool Upscaling::IsFrameGenerationDx12PathActive()",
        "bool Upscaling::ShouldPrepareFrameGeneration()",
        "bool Upscaling::ShouldUseFrameGenerationThisFrame()",
        "void Upscaling::InvalidateFrameGenerationInputs()",
    ]]
    methods.extend(function(swapchain, signature) for signature in [
        "HRESULT DX12SwapChain::Present(",
        "HRESULT DX12SwapChain::Present1(",
        "HRESULT DX12SwapChain::PresentInternal(",
    ])
    (output / "frame_generation_under_test.h").write_text("\n".join(methods), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
