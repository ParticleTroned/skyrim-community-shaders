#include "Features/Upscaling/NeuralRendering/NgxCapabilityInfo.h"
#include "Features/Upscaling/NeuralRendering/ProviderIdentity.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

void Require(bool condition)
{
	if (!condition)
		throw std::runtime_error("NR provider snapshot regression");
}

namespace NeuralRendering
{
	struct LifecycleMutex
	{
		static inline bool held = false;
		void lock()
		{
			Require(!held);
			held = true;
		}
		void unlock()
		{
			Require(held);
			held = false;
		}
	};

	struct Runtime
	{
		static Runtime& Instance()
		{
			static Runtime runtime;
			return runtime;
		}
		int state = 1;
		NgxDriverRequirements driver{ .needsUpdate = true, .minimumMajor = 616, .minimumMinor = 92 };
		ProviderIdentity identity{ ProviderReleaseMatch::NoMatch, ProviderInitialization::Failed };
		mutable unsigned diagnosticReads = 0;
		int Status() const
		{
			Require(LifecycleMutex::held);
			return state;
		}
		NgxDriverRequirements DriverRequirements() const
		{
			Require(LifecycleMutex::held);
			return driver;
		}
		void CheckDiagnosticRead() const
		{
			Require(LifecycleMutex::held);
			++diagnosticReads;
		}
		ProviderIdentity Identity() const
		{
			CheckDiagnosticRead();
			return identity;
		}
		int Trust() const
		{
			CheckDiagnosticRead();
			return 1;
		}
		std::filesystem::path Path() const
		{
			CheckDiagnosticRead();
			return "provider.dll";
		}
		std::string Hash() const
		{
			CheckDiagnosticRead();
			return "hash";
		}
		std::string Version() const
		{
			CheckDiagnosticRead();
			return "310.9.16";
		}
		std::pair<std::string, std::string> GetInitializationRoute() const
		{
			CheckDiagnosticRead();
			return { "NGX_D3D12_DIRECT_PROVIDER", "SDK discovery not attempted" };
		}
	};
	const char* ToString(int) { return "version-accepted"; }

	struct RendererSnapshot
	{
		int runtimeStatus = 0;
		std::optional<bool> runtimeNeedsUpdatedDriver;
		bool parallelEyesAvailable = false, parallelEyesDisabled = false, parallelEyesLastSubmission = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		int reconstructionOverride = 0;
		ProviderIdentity runtimeIdentity;
		NgxDriverRequirements runtimeDriverRequirements;
		std::pair<std::string, std::string> runtimeInitializationRoute;
		std::string trust, runtimePath, runtimeHash, runtimeVersion;
#endif
	};
	struct Renderer
	{
		struct State
		{
			LifecycleMutex mutex_;
			RendererSnapshot SnapshotLocked()
			{
				Require(LifecycleMutex::held);
				return {};
			}
			struct Experiment
			{
				int Status() { return 2; }
			} reconstructionExperiment_;
			struct Interop
			{
				struct Parallel
				{
					bool available = true, disabled = false, lastSubmission = true;
				};
				Parallel GetParallelStatus()
				{
					Require(LifecycleMutex::held);
					return {};
				}
			} interop_;
		} state;
		State* state_ = &state;
		RendererSnapshot GetSnapshot() const;
	};
#include "neural_provider_snapshot_under_test.h"
}

int main()
{
	using namespace NeuralRendering;
	Renderer renderer;
	auto& runtime = Runtime::Instance();
	const auto captured = renderer.GetSnapshot();
	Require(!LifecycleMutex::held && captured.runtimeStatus == 1 && captured.runtimeNeedsUpdatedDriver == true);
	Require(captured.parallelEyesAvailable && !captured.parallelEyesDisabled && captured.parallelEyesLastSubmission);
	runtime.state = 2;
	runtime.driver = {};
	runtime.identity = {};
	const auto reset = renderer.GetSnapshot();
	Require(reset.runtimeStatus == 2 && !reset.runtimeNeedsUpdatedDriver.has_value());
#ifdef DEVBENCH_BRIDGE_ENABLED
	Require(captured.runtimeIdentity.initialization == ProviderInitialization::Failed && captured.runtimeIdentity.releaseMatch == ProviderReleaseMatch::NoMatch);
	Require(captured.runtimeDriverRequirements.needsUpdate == true && captured.runtimeDriverRequirements.minimumMajor == 616u);
	Require(captured.runtimeInitializationRoute.first == "NGX_D3D12_DIRECT_PROVIDER" && captured.runtimeInitializationRoute.second == "SDK discovery not attempted");
	Require(captured.runtimeHash == "hash" && captured.runtimeVersion == "310.9.16" && captured.runtimePath == "provider.dll");
	Require(reset.runtimeIdentity.initialization == ProviderInitialization::NotAttempted && !reset.runtimeDriverRequirements.needsUpdate.has_value());
#else
	Require(runtime.diagnosticReads == 0);
#endif
}
