#pragma once

#include <functional>
#include <string>

// Forward declaration
class Menu;

class AdvancedSettingsRenderer
{
public:
	static void RenderAdvancedSettings();

private:
	static void RenderShadersSection();
	static void RenderDiagnosticsSection();
	static void RenderTestingSection();

	// Helpers used by the sections above
	static void RenderShaderCompileFlags();
	static void RenderShaderThreading();
	static void RenderShaderCacheControls();
	static void RenderShaderReplacementTable();
	static void RenderShaderCompileStatistics();

	static void RenderLoggingControls();
	static void RenderRuntimeDebugControls();
	static void RenderShaderBlockingPanel();
	static void RenderCompileTraceExport();
};
