#include "ShaderCompilationSchedulingPolicy.h"

#include <Windows.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
	DWORD processClass = NORMAL_PRIORITY_CLASS;
	BOOL applyResult = TRUE;
	int appliedPriority = THREAD_PRIORITY_ERROR_RETURN;
	unsigned applyCalls = 0;
	DWORD ReadProcessClass(HANDLE) { return processClass; }
	BOOL ApplyThreadPriority(HANDLE, int priority)
	{
		appliedPriority = priority;
		++applyCalls;
		return applyResult;
	}
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

namespace Util
{
#define GetPriorityClass ReadProcessClass
#define SetThreadPriority ApplyThreadPriority
#include "shader_compilation_priority_under_test.h"
#undef SetThreadPriority
#undef GetPriorityClass
}

int main()
{
	try {
		using namespace ShaderCompilationSchedulingPolicy;
		struct Case
		{
			DWORD processClass;
			int startupPriority;
			bool startupAvailable;
		};
		constexpr std::array cases{
			Case{ NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_BELOW_NORMAL, true },
			Case{ BELOW_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_BELOW_NORMAL, true },
			Case{ IDLE_PRIORITY_CLASS, THREAD_PRIORITY_BELOW_NORMAL, true },
			Case{ ABOVE_NORMAL_PRIORITY_CLASS, THREAD_PRIORITY_LOWEST, true },
			Case{ HIGH_PRIORITY_CLASS, THREAD_PRIORITY_IDLE, true },
			Case{ REALTIME_PRIORITY_CLASS, THREAD_PRIORITY_IDLE, false },
			Case{ DWORD{ 0 }, THREAD_PRIORITY_BELOW_NORMAL, false },
		};
		for (const auto& condition : cases) {
			processClass = condition.processClass;
			applyResult = TRUE;
			Require(Util::GetCooperativeBackgroundThreadPriority(processClass) == condition.startupPriority,
				"Startup priority must use the actual Windows process-class mapping");
			for (const auto phase : { CompilationPhase::Startup, CompilationPhase::InGame }) {
				const auto before = applyCalls;
				const auto available = Util::SetCurrentThreadShaderCompilationPriority(phase);
				Require(applyCalls == before + 1, "Every phase must apply its priority to the current worker");
				Require(appliedPriority == (phase == CompilationPhase::Startup ? condition.startupPriority : THREAD_PRIORITY_BELOW_NORMAL),
					"Gameplay workers must yield below their process normal priority, including elevated game processes");
				Require(available == (phase == CompilationPhase::Startup ? condition.startupAvailable : processClass != 0),
					"Realtime startup cooperation and failed process discovery must be reported accurately");
				applyResult = FALSE;
				Require(!Util::SetCurrentThreadShaderCompilationPriority(phase),
					"A failed Windows thread-priority change must remain a failure");
				applyResult = TRUE;
			}
			const auto before = applyCalls;
			Require(Util::SetCurrentThreadCooperativeBackgroundPriority() == condition.startupAvailable &&
						applyCalls == before + 1 && appliedPriority == condition.startupPriority,
				"Other background workers must preserve the existing startup cooperation contract");
		}
		Require(Util::GetCooperativeBackgroundThreadPriority(DWORD{ 0xFFFFFFFF }) == THREAD_PRIORITY_BELOW_NORMAL,
			"An unknown process class must retain the existing conservative mapping");
		processClass = HIGH_PRIORITY_CLASS;
		Require(Util::SetCurrentThreadShaderCompilationPriority(CompilationPhase::Startup) && appliedPriority == THREAD_PRIORITY_IDLE,
			"Startup work must use cooperative priority on the reused worker");
		Require(Util::SetCurrentThreadShaderCompilationPriority(CompilationPhase::InGame) && appliedPriority == THREAD_PRIORITY_BELOW_NORMAL,
			"The same worker must leave startup idle priority for gameplay background priority");
		std::cout << "Shader compilation priority: Windows mapping, phase changes and failure reporting passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
