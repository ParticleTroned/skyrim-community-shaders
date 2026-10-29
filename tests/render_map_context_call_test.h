// Included after the existing Runtime fixture helpers; no D3D device is created.
namespace
{
	void TestContextCallEntryGeneration()
	{
		enum class Mode
		{
			Stable,
			Stop,
			Restart,
			InactiveStart
		};
		for (const auto mode : { Mode::Stable, Mode::Stop, Mode::Restart, Mode::InactiveStart }) {
			Runtime runtime;
			runtime.SetImmediateContext(0x1100);
			if (mode != Mode::InactiveStart)
				Check(runtime.StartCapture(Config()) == StartResult::kStarted, "context call initial capture failed");
			const auto call = runtime.BeginContextCall();
			unsigned nativeCalls = 0;
			const auto result = call.InvokeNative([&](unsigned argument) {
				++nativeCalls;
				Check(argument == 17, "native argument changed at context seam");
				if (mode == Mode::Stop || mode == Mode::Restart)
					runtime.StopCapture();
				if (mode == Mode::Restart || mode == Mode::InactiveStart)
					Check(runtime.StartCapture(Config()) == StartResult::kStarted, "native callback could not start successor");
				return -7;
			},
				17u);
			Check(nativeCalls == 1 && result == -7, "native return/count changed at context seam");
			const auto publication = runtime.ForCapture(call.Generation());
			ResourceViewInput view{};
			view.resource.d3dObject = 0x2200;
			view.view.d3dObject = 0x2300;
			view.view.kind = TargetViewKind::kRenderTarget;
			publication.RegisterDeferredContext(0x3300, 0, false);
			publication.RecordFinishCommandList(0x3300, 0x4400, false, -1);
			publication.RecordFinishCommandList(0x3300, 0x4400, false, 0);
			publication.RecordExecuteCommandList(0x1100, 0x4400, false);
			publication.BindRenderTargetViews(0x1100, 1, &view, nullptr);
			publication.BindResourceViews(0x1100, ResourceBindingKind::kShaderResource,
				ResourceStage::kPixel, 0, 1, &view);
			publication.RecordResourceFlow(0x1100, ResourceFlowOperation::kCopyResource,
				view.resource, view.resource);
			publication.RecordCpuMap(0x1100, view.resource, 0, 1, 0, 0, 1, 2, 4, 4, call.Generation());
			publication.RecordCpuUnmap(0x1100, view.resource, 0, 3, call.Generation());
			publication.RecordDraw(0x3300, DrawOperation::kDraw, 3);
			publication.RecordDispatch(0x3300, DispatchOperation::kDispatch, 1, 1, 1);
			publication.BindStage(0x1100, ShaderStage::kVertex, 0x5500);
			const auto targetSeed = publication.ClaimRenderTargetStateSeed(0x1100);
			const auto resourceSeed = publication.ClaimResourceViewStateSeed(0x1100);
			if (mode == Mode::Stop) {
				Check(!runtime.IsCapturing() && targetSeed == 0 && resourceSeed == 0,
					"stopped callback regained observation authority");
				continue;
			}
			if (mode == Mode::Stable) {
				auto snapshot = runtime.StopCapture();
				Check(snapshot && !snapshot->events.empty() && !snapshot->resourceObservations.empty(),
					"stable context callback lost observation work");
				continue;
			}
			Check(targetSeed == 0 && resourceSeed == 0 && runtime.CommandListCatalogueSizeForTesting() == 0,
				"old context call claimed successor seed/catalogue authority");
			const auto successor = runtime.ActiveCaptureGeneration();
			const auto fresh = runtime.ForCapture(successor);
			Check(fresh.ClaimRenderTargetStateSeed(0x1100) == successor &&
					  fresh.ClaimRenderTargetStateSeed(0x1100) == 0 &&
					  fresh.ClaimResourceViewStateSeed(0x1100) == successor &&
					  fresh.ClaimResourceViewStateSeed(0x1100) == 0,
				"successor initial state was consumed or seeded twice");
			fresh.RecordDraw(0x1100, DrawOperation::kDraw, 3);
			auto snapshot = runtime.StopCapture();
			Check(snapshot && snapshot->resourceObservations.empty() && snapshot->targetViewObservations.empty() &&
					  snapshot->targetBindingObservations.empty(),
				"old context call populated successor resources");
			const auto draw = std::find_if(snapshot->events.begin(), snapshot->events.end(),
				[](const EventRecord& event) { return event.kind == EventKind::kDraw; });
			Check(draw != snapshot->events.end() && draw->commandStreamSequence == 1,
				"old context call advanced successor command sequence");
			Check(std::any_of(snapshot->stageShaderObservations.begin(), snapshot->stageShaderObservations.end(),
					  [](const StageShaderObservationRecord& stage) { return stage.pointerEvidence == 0x5500; }),
				"native stage binding shadow was discarded with stale diagnostics");
			Check(std::none_of(snapshot->events.begin(), snapshot->events.end(), [](const EventRecord& event) {
				return event.kind == EventKind::kResourceFlow || event.kind == EventKind::kFinishCommandList ||
				       event.kind == EventKind::kExecuteCommandList || event.kind == EventKind::kRenderTargetBind;
			}),
				"old context call emitted a successor diagnostic event");
		}

		Runtime runtime;
		const auto call = runtime.BeginContextCall();
		bool nativeThrew = false;
		try {
			call.InvokeNative([] { throw std::runtime_error("native callback failure"); });
		} catch (const std::runtime_error&) {
			nativeThrew = true;
		}
		Check(nativeThrew && call.Generation() == 0, "native exception or inactive entry identity changed");
	}
}
