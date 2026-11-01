#include "Features/Upscaling/NeuralRendering/MonoPrewarmPolicy.h"
#include <cassert>
#include <string_view>

int main()
{
	using namespace NeuralRendering;
	MonoSettings settings;
	assert(MonoPrewarmConfigurationBlocker(settings));
	settings.enabled=settings.prewarm=true;
	assert(MonoPrewarmConfigurationBlocker(settings));
	settings.pressureResolution=false;
	assert(!MonoPrewarmConfigurationBlocker(settings));
	settings.charactersOnly=true; assert(MonoPrewarmConfigurationBlocker(settings)); settings.charactersOnly=false;
	settings.sceneCategoryStrengths=true; assert(MonoPrewarmConfigurationBlocker(settings)); settings.sceneCategoryStrengths=false;
	settings.regionPercent=80; assert(MonoPrewarmConfigurationBlocker(settings)); settings.regionPercent=100;
	settings.modelResolutionPercent=60; settings.peripheralCompressionPercent=40;
	assert(!MonoPrewarmConfigurationBlocker(settings));
	MonoPrewarmKey key{};
	key.device=1; key.context=2; key.sourceWidth=1920; key.sourceHeight=1080; key.activeWidth=1280; key.activeHeight=720;
	key.sourceFormat=10; key.mode=2; key.network=5;
	key.layout=BuildNativeEvaluationLayout({768,432},{768,432},{768,432},{},{0,0,768,432},{true,768,432},false);
	MonoPrewarmPolicy policy;
	assert(policy.Begin(1,1000)); assert(!policy.Begin(1,2000)); assert(!policy.Begin(2,1999));
	assert(policy.Begin(2,2000)); policy.Capture(key,2000); policy.Prepared();
	assert(!policy.Expired(31999) && policy.Expired(32000));
	assert(policy.Adopt(key) && !policy.unused && !policy.captured);
	policy.Capture(key,4000); policy.Prepared(); auto different=key; different.activeWidth++;
	assert(!policy.Adopt(different));
	policy.Capture(key,5000); policy.Prepared(); policy.Cancel(true);
	assert(!policy.unused && !policy.Begin(3,40000));
	policy.suppressed=false; assert(policy.Begin(4,40000));
	assert(std::string_view(policy.stage)=="unused_lease_expired");
}
