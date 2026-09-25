// Frozen comparison implementation from e715653bf620f8442ce48d81e0bc07bd684b8556.
// Test-only timing oracle; never include in production.
void LightLimitFix::RenderVRShadowLightsSnapshotBaseline(RE::ShadowSceneNode* a_node, std::uint32_t& a_index)
{
	if (!a_node)
		return;
	RE::NiPointer<RE::ShadowSceneNode> sceneOwner;
	RE::BSShadowLight* sceneSun = nullptr;
	std::optional<SceneLightSnapshot> snapshot;
	std::vector<RE::BSShadowLight*> renderOrder;
	try {
		auto& runtime = a_node->GetRuntimeData();
		const RE::BSSpinLockGuard lock{ runtime.lightQueueLock };
		if (a_index >= runtime.shadowLightsAccum.size() || !runtime.shadowLightsAccum[a_index])
			return;
		// The scene directly deletes its sun; retaining the sun itself would misapply intrusive ownership.
		sceneOwner.reset(a_node);
		sceneSun = runtime.sunShadowDirLight;
		snapshot.emplace();
		// Native dispatch uses accumulated order and needs no clustered-light enumeration.
		snapshot->RetainScene(runtime, false);
		renderOrder.assign(runtime.shadowLightsAccum.begin(), runtime.shadowLightsAccum.end());
	} catch (const std::bad_alloc&) {
		logger::error("Light Limit Fix: native shadow capture allocation failed; skipping shadow maps");
		return;
	}

	while (a_index < renderOrder.size()) {
		// Only this scene's sun bypasses the queued-light owner lookup; the scene remains retained.
		auto* key = renderOrder[a_index];
		auto* light = key == sceneSun ? static_cast<RE::BSLight*>(sceneSun) : snapshot->Find(key);
		if (!light || !light->IsShadowLight())
			break;
		const auto previousIndex = a_index;
		static_cast<RE::BSShadowLight*>(light)->Render(a_index);
		if (a_index <= previousIndex)
			break;
	}
}
