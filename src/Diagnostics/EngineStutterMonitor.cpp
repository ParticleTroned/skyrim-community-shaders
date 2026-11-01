#include "Diagnostics/EngineStutterMonitor.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "Diagnostics/EngineStutterCapture.h"
#	include "Diagnostics/AssetLoadHooks.h"
#	include "Globals.h"
#	include "ShaderCache.h"
#	include "State.h"
#	include <DevBenchAPI.h>
#	include <Windows.h>

namespace CSX::Diagnostics::Stutters
{
	namespace
	{
		std::atomic<Capture*> current{ nullptr };
		Capture& Service()
		{
			// SKSE owns this DLL until exit; joining a watchdog under the loader lock is unsafe.
			static auto* capture = new Capture({ SKSE::log::log_directory, BuildProvenance::GetProducer,
				BuildProvenance::ValidateExpectedBuild, [] { return globals::game::quitGame.load(std::memory_order_relaxed); }, AssetLoadHooks::Status });
			current.store(capture, std::memory_order_release);
			return *capture;
		}
		void Handler(void* context, const char* input, void* sink, DevBenchAPI::WriteFn write) noexcept
		{
			static_cast<Capture*>(context)->Respond(input, sink, write);
		}
	}
	Scope::Scope(std::string_view name, Boundary boundary) noexcept
	{
		if (auto* capture = current.load(std::memory_order_acquire); capture && capture->recorder.Enabled()) {
			recorder_ = &capture->recorder;
			token_ = recorder_->Begin(GetCurrentThreadId(), name, ReadCounter(), boundary);
		}
	}
	Scope::~Scope() noexcept
	{
		if (recorder_ && token_.phase.id)
			recorder_->End(token_, ReadCounter());
	}
	AssetScope::AssetScope(AssetKind kind, std::string_view path, std::string_view stage) noexcept
	{
		if (auto* capture = current.load(std::memory_order_acquire); capture && capture->recorder.AssetsEnabled()) {
			AssetRecord asset{ .kind = kind, .path = path };
			recorder_ = &capture->recorder;
			token_ = recorder_->Begin(GetCurrentThreadId(), stage, ReadCounter(), Boundary::None, asset);
		}
	}
	AssetScope::~AssetScope() noexcept
	{
		if (recorder_ && token_.phase.id)
			recorder_->End(token_, ReadCounter());
	}
	void AssetScope::Result(bool success) noexcept
	{
		token_.phase.asset.resultKnown = true;
		token_.phase.asset.success = success;
	}
	void AssetScope::Result(bool success, std::int64_t code) noexcept
	{
		Result(success);
		token_.phase.asset.resultCodeKnown = true;
		token_.phase.asset.resultCode = code;
	}
	void AssetScope::CacheHit(bool hit) noexcept
	{
		token_.phase.asset.cacheKnown = true;
		token_.phase.asset.cacheHit = hit;
	}
	void AssetScope::Bytes(std::uint64_t bytes) noexcept
	{
		token_.phase.asset.bytesKnown = true;
		token_.phase.asset.bytes = bytes;
	}
	void AssetScope::Dimensions(std::uint32_t width, std::uint32_t height, std::uint32_t mips, std::uint32_t format) noexcept
	{
		auto& asset = token_.phase.asset;
		asset.dimensionsKnown = true;
		asset.width = width;
		asset.height = height;
		asset.mipLevels = mips;
		asset.format = format;
	}
	bool AssetRecordingEnabled() noexcept
	{
		const auto* capture = current.load(std::memory_order_acquire);
		return capture && capture->recorder.AssetsEnabled();
	}
	bool CodeEvaluationEnabled() noexcept
	{
		const auto* capture = current.load(std::memory_order_acquire);
		return capture && capture->recorder.CodeEvaluationEnabled();
	}
	void PublishGameContext() noexcept
	{
		auto* capture = current.load(std::memory_order_acquire);
		if (!capture || !capture->recorder.Enabled())
			return;
		const auto epoch = capture->recorder.Epoch();
		Context context{ .qpc = ReadCounter(), .frame = globals::state ? globals::state->frameCount : 0 };
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			context.playerAvailable = true;
			const auto position = player->GetPosition();
			context.position = { position.x, position.y, position.z };
			if (const auto* cell = player->GetParentCell())
				context.cell = cell->GetFormID();
		}
		if (auto* ui = globals::game::ui) {
			context.loading = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
			context.paused = ui->GameIsPaused();
		}
		context.compiling = globals::shaderCache && globals::shaderCache->IsCompiling();
		capture->recorder.PublishContext(context, epoch);
	}
	nlohmann::json Handle(const nlohmann::json& args) { return Service().Dispatch(args); }
	void Install()
	{
		if (auto* host = DevBenchAPI::GetDevBenchInterface001()) {
			AssetLoadHooks::Install();
			auto& capture = Service();
			static const auto descriptor = Capture::Descriptor().dump();
			host->RegisterTool("communityshaders.stutters", descriptor.c_str(), Handler, &capture);
			logger::info("Registered DevBench engine stutter diagnostics");
		}
	}
}
#endif
