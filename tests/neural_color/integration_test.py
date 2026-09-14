"""Supplementary wiring checks; these do not replace C++/GPU execution."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
renderer = (root / "src/Features/Upscaling/NeuralRendering/Renderer.cpp").read_text()
pipeline = (root / "src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp").read_text()
shader = (root / "features/Upscaling/Shaders/Upscaling/NeuralRendering/ColorPipelineCS.hlsl").read_text()
batch = renderer[renderer.index("bool Renderer::State::ApplyBatchLocked("):]
checks = [
    ('shared batch colour preparation', 'colorPipeline_.Prepare' in batch),
    ('shared batch reconstruction', 'colorPipeline_.Resolve' in batch),
    ('all resolves precede caller copies', batch.index('colorPipeline_.Resolve') < batch.index('resources[index].output.texture.Get()')),
    ('interop ends before resolve', batch.index('interop_.EndD3D12()') < batch.index('colorPipeline_.Resolve')),
    ('round trip outside feature timer', batch.index('interop_.EndFeatureTiming') < batch.index('colorPipeline_.RecordRoundTrip')),
    ('actual runtime still evaluated', 'const bool evaluated = Runtime::Instance().Execute(' in batch),
    ('region orchestration retained', 'physical.computeRegions = {};' in batch),
    ('source alpha retained', 'float4(encoded, original.a)' in shader),
    ('actual input used as reference', 'encodedReference = Prepared.Load' in shader),
    ('no character strength in new shader', 'CharacterMask' not in shader),
    ('predication restored', 'context_->SetPredication(predicate_, predicateValue_)' in pipeline),
    ('bounds guard', 'if (any(id.xy >= Size)) return;' in shader),
    ('ROI-clamped filter', 'int2(Base + Size - 1)' in shader),
    ('precision', 'half' not in shader and 'min16float' not in shader),
    ('new NR ABI keys not invented', 'DLSSNR.' not in pipeline),
    ('configuration size bounded', 'kMaximumConfigurationBytes + 1' in pipeline),
    ('no blocking readback', '->Map(' not in pipeline and '->Flush(' not in pipeline),
    ('raw output stays available', 'colorPipeline_.Output(a_args[index].featureSlot) : slots[index]->output.resource11.Get()' in batch),
    ('config reload only after successful reset', 'if (reset) colorPipeline_.ReloadSettings();' in renderer),
]
for label, passed in checks:
    if not passed:
        raise SystemExit(f'FAIL: {label}')
print(f'{len(checks)} integration contracts passed; GPU/Windows build not implied')
