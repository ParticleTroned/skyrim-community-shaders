// Included after the Runtime fixture helpers; observations use controlled CPU inputs.
namespace
{
	void TestPostProcessingContextCallGeneration()
	{
		for (const bool inactiveEntry : { false, true }) {
			Runtime runtime;
			runtime.SetImmediateContext(0xA000);
			auto config = Config();
			config.maxEvents = 256;
			config.maxBytes = Collector::RequiredStorageBytes(config);
			const ResourceObservationInput resource{ .d3dObject = 0xA100,
				.dimension = ResourceDimension::kTexture2D,
				.widthOrBytes = 64,
				.height = 32,
				.depthOrArraySize = 1,
				.mipLevels = 1 };
			const ResourceViewInput view{ .resource = resource,
				.view = { .kind = TargetViewKind::kRenderTarget, .d3dObject = 0xA200 } };
			if (!inactiveEntry)
				Check(runtime.StartCapture(config) == StartResult::kStarted, "post-processing source capture failed");
			const auto call = runtime.BeginContextCall();
			unsigned nativeCalls = 0;
			call.InvokeNative([&] {
				++nativeCalls;
				if (!inactiveEntry)
					Check(runtime.StopCapture().has_value(), "post-processing source capture did not stop");
				Check(runtime.StartCapture(config) == StartResult::kStarted, "post-processing successor capture failed");
			});
			Check(nativeCalls == 1, "post-processing native callback count changed");
			runtime.SetCpuFrame(12);
			{
				auto scope = runtime.EnterPostProcessing(resource, resource, 9, 77);
				Check(runtime.IsInsidePostProcessing(), "post-processing successor scope failed");
				const auto fresh = runtime.ForCapture(runtime.ActiveCaptureGeneration());
				fresh.BeginTransferOperation(0xA000, false, 0, { 1, 2, 3, 4, 5, 6 });
				fresh.RecordRasterState(0xA000, 0, { 0, 0, 64, 32, 0, 1 }, { 0, 0, 64, 32 }, 1, 1, false);
				fresh.RecordTransferResourceAccess(0xA000, view, ResourceStage::kOutputMerger, 7, true);
				fresh.RecordTransferCopyRegion(0xA000, 3, 4, { 1, 2, 3 }, { 0, 0, 0, 64, 32, 1 }, true);

				const auto stale = runtime.ForCapture(call.Generation());
				stale.BeginTransferOperation(0xA000, true, 1, { 7, 8, 9, 10, 11, 12 });
				stale.RecordRasterState(0xA000, 1, { 0, 0, 128, 64, 0, 1 }, { 0, 0, 128, 64 }, 1, 1, true);
				stale.RecordTransferResourceAccess(0xA000, view, ResourceStage::kOutputMerger, 11, true);
				stale.RecordTransferCopyRegion(0xA000, 5, 6, { 4, 5, 6 }, { 0, 0, 0, 128, 64, 1 }, true);
			}
			const auto snapshot = runtime.StopCapture();
			Check(snapshot.has_value(), "post-processing successor capture did not stop");
			std::size_t operations = 0;
			std::size_t rasters = 0;
			std::size_t accesses = 0;
			std::size_t copies = 0;
			for (const auto& event : snapshot->events) {
				switch (static_cast<PayloadSchema>(event.payload.schema)) {
				case PayloadSchema::kTransferOperation:
					++operations;
					break;
				case PayloadSchema::kRasterState:
					Check(event.payload.words[0] == 0, "stale callback recorded a successor raster slot");
					++rasters;
					break;
				case PayloadSchema::kTransferResourceAccess:
					Check(event.payload.words[5] == 7, "stale callback recorded a successor resource access");
					++accesses;
					break;
				case PayloadSchema::kTransferCopyRegion:
					Check(event.payload.words[1] == (3u | (std::uint64_t{ 4 } << 32u)),
						"stale callback recorded a successor copy region");
					++copies;
					break;
				default:
					break;
				}
			}
			Check(operations == 1 && rasters == 1 && accesses == 1 && copies == 1,
				"post-processing callback generation lost fresh observations or admitted stale ones");
		}
	}
}
