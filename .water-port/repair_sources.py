"""Minimal, idempotent source repairs for the fork-only water port branches."""
from pathlib import Path

WATER = 'package/Shaders/Water.hlsl'
UW = 'src/Features/UnifiedWater.cpp'
FLOW = 'src/Features/UnifiedWater/Flowmap.cpp'
PARALLAX = 'features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli'
FINITE_PATH = 'package/Shaders/Common/WaterFinite.hlsli'
FINITE_TEST_PATH = 'package/Shaders/Tests/TestWaterFinite.hlsl'
FINITE = '''#ifndef WATER_FINITE_HLSLI
#define WATER_FINITE_HLSLI

namespace WaterFinite
{
	// FXC fast-math can discard isfinite checks on computed values; classify the stored exponent instead.
	bool IsFinite(float value)
	{
		return (asuint(value) & 0x7f800000u) != 0x7f800000u;
	}

	bool2 IsFinite(float2 value)
	{
		return (asuint(value) & 0x7f800000u) != 0x7f800000u;
	}

	bool3 IsFinite(float3 value)
	{
		return (asuint(value) & 0x7f800000u) != 0x7f800000u;
	}

	bool4 IsFinite(float4 value)
	{
		return (asuint(value) & 0x7f800000u) != 0x7f800000u;
	}
}

#endif
'''
FINITE_TEST = '''#include "/Shaders/Common/WaterFinite.hlsli"
#include "/Test/STF/ShaderTestFramework.hlsli"

[numthreads(1, 1, 1)]
void TestWaterFiniteClassifiesIEEE754Values()
{
	ASSERT(IsTrue, WaterFinite::IsFinite(asfloat(0x00000000u)));
	ASSERT(IsTrue, WaterFinite::IsFinite(asfloat(0x80000000u)));
	ASSERT(IsTrue, WaterFinite::IsFinite(asfloat(0x00000001u)));
	ASSERT(IsTrue, WaterFinite::IsFinite(asfloat(0x7f7fffffu)));
	ASSERT(IsTrue, WaterFinite::IsFinite(asfloat(0xff7fffffu)));
	ASSERT(IsFalse, WaterFinite::IsFinite(asfloat(0x7f800000u)));
	ASSERT(IsFalse, WaterFinite::IsFinite(asfloat(0xff800000u)));
	ASSERT(IsFalse, WaterFinite::IsFinite(asfloat(0x7fc00000u)));
	ASSERT(IsFalse, WaterFinite::IsFinite(asfloat(0xffc00001u)));
}

[numthreads(1, 1, 1)]
void TestWaterFiniteVectorComponents()
{
	float infinity = asfloat(0x7f800000u);
	float nanValue = asfloat(0x7fc00000u);
	ASSERT(IsTrue, all(WaterFinite::IsFinite(float2(0.0, infinity)) == bool2(true, false)));
	ASSERT(IsTrue, all(WaterFinite::IsFinite(float3(nanValue, -1.0, infinity)) == bool3(false, true, false)));
	ASSERT(IsTrue, all(WaterFinite::IsFinite(float4(1.0, nanValue, infinity, -1.0)) == bool4(true, false, false, true)));
}
'''

class Edit:
    def __init__(self, root): self.root = Path(root)
    def read(self, path): return (self.root/path).read_text(encoding='utf-8-sig')
    def write(self, path, data):
        p=self.root/path
        bom=p.exists() and p.read_bytes().startswith(b'\xef\xbb\xbf')
        p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(data,encoding='utf-8-sig' if bom else 'utf-8',newline='\n')
    def replace(self,path,old,new,count=1):
        text=self.read(path)
        if text.count(old)!=count:
            raise RuntimeError(f'{path}: expected {count} matches for {old!r}, got {text.count(old)}')
        self.write(path,text.replace(old,new))
    def finite(self):
        for path, data in ((FINITE_PATH, FINITE), (FINITE_TEST_PATH, FINITE_TEST)):
            p=self.root/path
            if p.exists() and self.read(path)!=data:
                raise RuntimeError(f'Different shared helper already present: {path}')
            if not p.exists(): self.write(path,data)
    def finite_region(self,path,begin=None,end=None):
        text=self.read(path)
        start=text.index(begin) if begin else 0
        stop=text.index(end,start) if end else len(text)
        part=text[start:stop]
        if 'isfinite(' not in part: raise RuntimeError(f'No finite guards in {path} selected region')
        self.write(path,text[:start]+part.replace('isfinite(', 'WaterFinite::IsFinite(')+text[stop:])


def fix04(e):
    e.replace(FLOW,'sourceTex->rendererTexture->texture->GetType(&resourceDimension);',
        'Util::AsReal(sourceTex->rendererTexture->texture)->GetType(&resourceDimension);')
    e.replace(FLOW,'static_cast<ID3D11Texture2D*>(sourceTex->rendererTexture->texture)->GetDesc(&desc);',
        'static_cast<ID3D11Texture2D*>(Util::AsReal(sourceTex->rendererTexture->texture))->GetDesc(&desc);')
    e.replace(FLOW,'sourceTex->rendererTexture->resourceView->GetDesc(&viewDesc);',
        'Util::AsReal(sourceTex->rendererTexture->resourceView)->GetDesc(&viewDesc);')


def fix03(e):
    e.replace(UW,'CullAllWaterLODParents(*gWaterLOD, tes)', 'CullAllWaterLODParents(gWaterLOD->get(), tes)')
    e.replace(UW,'CullAllWaterLODParents(*uw.gWaterLOD, tes)', 'CullAllWaterLODParents(uw.gWaterLOD->get(), tes)')


def fix09(e):
    e.replace('src/Hooks.cpp','static_cast<uint32_t>(target) < Util::GetRenderTargetCount()',
        'static_cast<uint32_t>(target) < static_cast<uint32_t>(Util::GetRenderTargetCount())')


def fix02(e):
    e.finite()
    path='package/Shaders/Common/WaterRefraction.hlsli'
    e.replace(path,'#include "Common/VR.hlsli"', '#include "Common/VR.hlsli"\n#include "Common/WaterFinite.hlsli"')
    e.finite_region(path)
    # The whole diffuse function region excludes independent reflection/shallow changes.
    e.finite_region(WATER,'DiffuseOutput GetWaterDiffuseColor(', '\nPS_OUTPUT main(')
    e.replace(WATER,'\tfloat2 refractionMinUV;\n\tfloat2 refractionMaxUV;',
        '\tfloat2 refractionMinUV = 0.0;\n\tfloat2 refractionMaxUV = 0.0;')
    e.replace(WATER,'\tif (!WaterRefraction::TryGetUVBounds(', '\t[flatten] if (!WaterRefraction::TryGetUVBounds(')
    # Deterministic return values in every permutation, including extension fields.
    e.replace(WATER,'\tDiffuseOutput output;', '\tDiffuseOutput output = (DiffuseOutput)0;',e.read(WATER).count('\tDiffuseOutput output;'))
    path='package/Shaders/Tests/TestWaterRefraction.hlsl'
    e.write(path,e.read(path)+'''
[numthreads(1, 1, 1)]
void TestWaterRefractionRejectsNonFiniteFootprints()
{
	float2 lo = 1.0, hi = 1.0;
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(asfloat(0x7f800000u), 100), float2(100, 100), 0, lo, hi));
	ASSERT(IsTrue, all(lo == 0.0) && all(hi == 0.0));
	ASSERT(IsFalse, WaterRefraction::TryGetUVBounds(float2(100, 100), float2(100, asfloat(0x7fc00000u)), 0, lo, hi));
	ASSERT(IsTrue, all(lo == 0.0) && all(hi == 0.0));
}

[numthreads(1, 1, 1)]
void TestWaterRefractionFallsBackFromInfinity()
{
	float2 value = WaterRefraction::ClampUV(float2(asfloat(0xff800000u), 0.5), float2(asfloat(0x7fc00000u), 0.5), 0.1.xx, 0.9.xx);
	ASSERT(IsTrue, all(abs(value - 0.5.xx) < 1e-6));
}
''')


def fix08(e):
    e.finite()
    path='package/Shaders/Common/WaterParallaxMath.hlsli'
    e.replace(path,'#define WATER_PARALLAX_MATH_HLSLI', '#define WATER_PARALLAX_MATH_HLSLI\n\n#include "Common/WaterFinite.hlsli"')
    e.finite_region(path)
    e.finite_region(PARALLAX)
    path='package/Shaders/Tests/TestWaterParallaxMath.hlsl'
    e.write(path,e.read(path)+'''
[numthreads(1, 1, 1)]
void TestWaterParallaxNonFiniteBudgetUsesFullCap()
{
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(0x7f800000u)), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(0xff800000u)), 64u);
	ASSERT(AreEqual, WaterParallaxMath::Budget(asfloat(0xffc00001u)), 64u);
}
''')


def fix10a(e):
    e.finite()
    e.replace(WATER,'#\tinclude "Common/MotionBlur.hlsli"', '#\tinclude "Common/MotionBlur.hlsli"\n#\tinclude "Common/WaterFinite.hlsli"')
    e.finite_region(WATER,'float3 GetWaterSpecularColor(', '\nstatic const float ShoreFadeDepth')


def fix01(e):
    # The repaired refraction prerequisite already provides the shared helper.
    e.finite()
    path='features/Unified Water/Shaders/UnifiedWater/ShallowSurface.hlsli'
    e.write(path,'#include "Common/WaterFinite.hlsli"\n\n'+e.read(path))
    e.finite_region(path)
    e.replace(WATER,'isfinite(planeLength)', 'WaterFinite::IsFinite(planeLength)')


def fix11b(e):
    e.finite()
    path='package/Shaders/Common/WaterContactCandidates.hlsli'
    e.replace(path,'uint2(0xffffffff, 0xffffffff)', 'uint2(0xffffffffu, 0xffffffffu)')
    path='features/Light Limit Fix/Shaders/LightLimitFix/WaterContactCandidatesCS.hlsl'
    e.replace(path,'#include "Common/WaterContactCandidates.hlsli"',
        '#include "Common/WaterContactCandidates.hlsli"\n#include "Common/WaterFinite.hlsli"')
    e.finite_region(path)
    path='package/Shaders/Tests/TestWaterContactCandidates.hlsl'
    e.write(path,e.read(path)+'''
[numthreads(1, 1, 1)]
void TestWaterCandidateEmptySentinelIsUnsigned()
{
	uint2 packed = WaterContactCandidates::Empty();
	ASSERT(IsTrue, all(packed == uint2(0xffffffffu, 0xffffffffu)));
	[unroll] for (uint slot = 0; slot < WaterContactCandidates::MaxCandidates; ++slot)
		ASSERT(AreEqual, WaterContactCandidates::Load(packed, slot), WaterContactCandidates::EmptyIndex);
}
''')

FIXES = {'04-flowmap': fix04,'03-child-worldspace':fix03,'09-history':fix09,
         '02-refraction':fix02,'08-parallax':fix08,'10a-reflection-source':fix10a,
         '01-shallow':fix01,'11b-contact-candidates':fix11b}

if __name__ == '__main__':
    import sys
    editor=Edit(sys.argv[1])
    for name in sys.argv[2:]: FIXES[name](editor)
