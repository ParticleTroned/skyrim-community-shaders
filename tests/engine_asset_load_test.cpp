#include "Diagnostics/EngineStutterRecorder.h"
#include "Diagnostics/OptionalAssetRelocation.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace CSX::Diagnostics::Stutters;
void Check(bool ok, const char* message)
{
	if (!ok)
		throw std::runtime_error(message);
}
AssetRecord Asset(AssetKind kind, std::string_view path)
{
	return { .kind = kind, .path = path };
}
int main()
{
	try {
		struct Mapping
		{
			std::uint64_t id, offset;
		};
		const std::array<Mapping, 3> mappings{ Mapping{ 42, 123 }, Mapping{ 7, 99 }, Mapping{ 9, 0 } };
		Check(FindAssetOffset(mappings, 42) == 123 && !FindAssetOffset(mappings, 0) && !FindAssetOffset(mappings, 100) && !FindAssetOffset(mappings, 9), "optional relocation lookup fabricated missing or zero offsets");
		const std::array<Mapping, 2> ambiguous{ Mapping{ 42, 123 }, Mapping{ 42, 124 } };
		Check(!FindAssetOffset(ambiguous, 42), "ambiguous optional relocation accepted");
		auto ownedRecorder = std::make_unique<Recorder>();
		auto scratch = std::make_unique<Batch>();
		auto& recorder = *ownedRecorder;
		const auto drain = [&](Tick now) -> Batch& { return recorder.Drain(now, *scratch); };
		const auto mesh = Asset(AssetKind::Mesh, "meshes/test.nif");
		recorder.Start(50, 1);
		Check(!recorder.Begin(1, "Mesh::Demand", 10, Boundary::None, mesh).phase.id, "assets enabled without opt-in");
		recorder.Start(50, 1, 0, 2, 5);
		Context context{ .qpc = 2, .frame = 42, .cell = 123, .position = { 1, 2, 3 }, .playerAvailable = true };
		recorder.PublishContext(context, recorder.Epoch());
		auto engine = recorder.Begin(1, "Engine", 10, Boundary::Engine);
		auto demand = recorder.Begin(1, "Mesh::Demand", 11, Boundary::None, mesh);
		auto post = recorder.Begin(1, "Mesh::PostCreate", 12, Boundary::None, mesh);
		Check(post.phase.asset.operationId == demand.phase.id && post.phase.parentScopeId == demand.phase.id, "nested mesh correlation lost");
		auto texture = recorder.Begin(1, "Texture::Create", 13, Boundary::None, Asset(AssetKind::Texture, "textures/test.dds"));
		auto code = recorder.Begin(1, "Code", 14);
		auto resource = recorder.Begin(1, "Texture::CreateD3DResource", 15, Boundary::None, Asset(AssetKind::Texture, {}));
		Check(resource.phase.asset.operationId == texture.phase.id && std::string(resource.phase.asset.path.data()) == "textures/test.dds", "texture stage lost path or operation");
		Check(resource.phase.parentScopeId == code.phase.id, "code nesting was not preserved");
		resource.phase.asset.resultKnown = resource.phase.asset.resultCodeKnown = true;
		resource.phase.asset.success = false;
		resource.phase.asset.resultCode = 0;
		resource.phase.asset.bytesKnown = true;
		resource.phase.asset.bytes = 0;
		resource.phase.asset.cacheKnown = true;
		resource.phase.asset.cacheHit = false;
		recorder.End(resource, 17);
		recorder.End(code, 19);
		recorder.End(texture, 20);
		recorder.End(post, 21);
		recorder.End(demand, 22);
		recorder.End(engine, 23);
		auto batch = drain(23);
		Check(batch.count == 5, "independent asset/pass thresholds changed engine threshold");
		const auto& recorded = batch.events[0].phase;
		Check(recorded.asset.resultKnown && !recorded.asset.success && recorded.asset.resultCodeKnown && recorded.asset.resultCode == 0 && recorded.asset.bytesKnown && recorded.asset.bytes == 0 && recorded.asset.cacheKnown && !recorded.asset.cacheHit, "false/zero completion evidence lost");
		Check(recorded.context.frame == 42 && recorded.context.cell == 123, "asset scene context lost");
		recorder.Start(50, 1, 0, 0);
		auto root = recorder.Begin(1, "Texture::Create", 10, Boundary::None, Asset(AssetKind::Texture, "textures/a.dds"));
		auto other = recorder.Begin(2, "Texture::Resource", 11, Boundary::None, Asset(AssetKind::Texture, {}));
		Check(other.phase.asset.operationId != root.phase.id && other.phase.asset.path.empty(), "asset leaked across threads");
		auto shortCode = recorder.Begin(1, "ShortCode", 12);
		batch = drain(70);
		Check(batch.activeAssets[root.slot].id == root.phase.id && batch.overdue[root.slot].id == shortCode.phase.id, "active asset hidden by nested code");
		recorder.End(shortCode, 71);
		recorder.End(root, 72);
		recorder.End(other, 73);
		batch = drain(73);
		Check(batch.count == 3, "zero asset threshold or ordinary CPU scope lost");
		auto truncated = recorder.Begin(1, "Texture", 80, Boundary::None, Asset(AssetKind::Texture, std::string(700, 'x')));
		Check(truncated.phase.asset.pathTruncated && truncated.phase.asset.path.size() == 511 && truncated.phase.asset.path.data()[511] == 0, "asset path not safely bounded");
		recorder.Stop();
		recorder.Start(50, 100, 0, 0);
		auto fresh = recorder.Begin(1, "Fresh", 101, Boundary::None, mesh);
		recorder.End(truncated, 999);
		Check(recorder.StillCurrent(fresh.phase, false), "old asset token corrupted new capture");
		recorder.End(fresh, 101);
		Check(drain(101).count == 1, "zero-duration asset was discarded");
		for (unsigned i = 0; i < 300; ++i) {
			auto token = recorder.Begin(1, "Burst", 110 + i, Boundary::None, mesh);
			recorder.End(token, 110 + i);
		}
		batch = drain(410);
		Check(batch.count == 256 && batch.droppedEvents == 44, "asset overflow was not explicit and bounded");
		recorder.Start(50, 1, 0, 0, 5, true);
		AssetRecord suppliedIdentity;
		Token copied;
		{
			std::string temporaryPath = "textures/temporary.dds";
			suppliedIdentity = Asset(AssetKind::Texture, temporaryPath);
			suppliedIdentity.operationId = 999999;
			copied = recorder.Begin(1, "TemporaryPath", 2, Boundary::None, suppliedIdentity);
			temporaryPath.assign(temporaryPath.size(), 'x');
			Check(copied.phase.asset.path == "textures/temporary.dds", "borrowed path was not copied");
		}
		Check(copied.phase.asset.operationId == copied.phase.id && copied.phase.asset.operationId != 999999, "caller forged asset operation identity");
		recorder.End(copied, 2);
		Check(!recorder.Begin(1, "InvalidKind", 3, Boundary::None, Asset(static_cast<AssetKind>(255), "invalid")).phase.id && recorder.LostUpdates() == 1, "invalid asset kind became mesh evidence");
		drain(3);
		recorder.Start(50, 1, 0, 0, 5, true);
		Check(recorder.CodeEvaluationEnabled(), "explicit code evaluation was not enabled");
		for (unsigned i = 0; i < 16384; ++i) {
			auto token = recorder.Begin(1, "BoundedPath", 2 + i, Boundary::None, mesh);
			Check(token.phase.id != 0, "asset path capacity ended prematurely");
			recorder.End(token, 2 + i);
			if (i % 128 == 0)
				drain(2 + i);
		}
		Check(!recorder.Begin(1, "OverflowPath", 20000, Boundary::None, mesh).phase.id && recorder.LostAssetPaths() == 1, "asset path limit was not explicit");
		recorder.Stop();
		Check(!recorder.CodeEvaluationEnabled(), "stopped capture retained code evaluation");
		recorder.Start(50, 1);
		Check(!recorder.CodeEvaluationEnabled() && recorder.LostAssetPaths() == 0, "capture defaults or loss reset changed");
		std::cout << "Asset loader correlation, thresholds, restart and loss tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
