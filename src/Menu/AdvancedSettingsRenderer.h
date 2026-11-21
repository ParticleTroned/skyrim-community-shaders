#pragma once

#include <functional>
#include <string>

// Forward declaration
class Menu;

class AdvancedSettingsRenderer
{
public:
	static void RenderAdvancedSettings();
	/** Keep the shared developer navigation around native RenderDoc controls. */
	static void RenderRenderDocSettings(const std::function<void()>& a_draw);

private:
	static void RenderPage(const std::function<void()>& a_renderDoc);
	static void RenderShadersSection();
	static void RenderDiagnosticsSection();
	static void RenderTestingSection();

	// Helpers used by the sections above
	static void RenderShaderCacheStatus();
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
