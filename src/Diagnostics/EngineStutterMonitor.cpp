#include "Diagnostics/EngineStutterMonitor.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "Diagnostics/EngineStutterCapture.h"
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
				BuildProvenance::ValidateExpectedBuild, [] { return globals::game::quitGame.load(std::memory_order_relaxed); }, globals::game::isVR });
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
			auto& capture = Service();
			static const auto descriptor = Capture::Descriptor().dump();
			host->RegisterTool("communityshaders.stutters", descriptor.c_str(), Handler, &capture);
			logger::info("Registered DevBench engine stutter diagnostics");
		}
	}
}
#endif
