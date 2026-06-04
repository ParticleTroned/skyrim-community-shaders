#pragma once

#include <array>
#include <cstdint>
#include <d3d11_4.h>
#include <vector>
#include <winrt/base.h>

class AAVRSController final
{
public:
	static constexpr uint32_t kTileWidth = 16;
	static constexpr uint32_t kTileHeight = 16;
	static constexpr float kDefaultCenterScale = 0.30f;
	static constexpr float kDefaultOuterScale = 1.0f;

	struct CenterOffset
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Settings
	{
		bool enabled = false;
		bool stereo = false;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		float centerScale = kDefaultCenterScale;
		float centerHorizontalScale = 1.0f;
		// When coarseOutsideMask is true, outerScale is the filled 1x1 protected mask.
		float outerScale = kDefaultOuterScale;
		bool coarseOutsideMask = false;
		std::array<CenterOffset, 2> centerOffsets{};
	};

	struct Status
	{
		bool active = false;
		bool suspended = false;
		const char* lastDisableReason = "Not initialized";
		uint32_t maskWidth = 0;
		uint32_t maskHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		bool hasSettings = false;
	};

	bool Update(const Settings& a_settings, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	void Disable(ID3D11DeviceContext* a_context, const char* a_reason = "Disabled");
	void Suspend(ID3D11DeviceContext* a_context);
	void Resume(ID3D11DeviceContext* a_context);
	void ReleaseResources();

	bool IsSupported(ID3D11Device* a_device);
	bool IsActive() const { return active; }
	bool IsSuspended() const { return suspendDepth != 0; }
	const char* GetLastDisableReason() const { return lastDisableReason; }
	Status GetStatus() const;

private:
	struct PatternKey
	{
		bool stereo = false;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		int32_t centerScaleQ = 0;
		int32_t centerHorizontalScaleQ = 0;
		int32_t outerScaleQ = 0;
		bool coarseOutsideMask = false;
		std::array<int32_t, 4> centerOffsetQ{};

		bool operator==(const PatternKey&) const = default;
	};

	bool EnsureSurface(ID3D11Device* a_device, const Settings& a_settings);
	static PatternKey MakePatternKey(const Settings& a_settings);
	bool EnsurePattern(const Settings& a_settings);
	bool UploadPattern(ID3D11DeviceContext* a_context);
	bool Bind(ID3D11DeviceContext* a_context);
	void DisableInternal(ID3D11DeviceContext* a_context);
	void ReleaseView();

	winrt::com_ptr<ID3D11Texture2D> shadingRateSurface;
	IUnknown* shadingRateView = nullptr;
	uint32_t surfaceWidth = 0;
	uint32_t surfaceHeight = 0;
	std::vector<uint8_t> patternData;
	PatternKey patternKey{};
	bool patternValid = false;
	bool patternUploaded = false;

	Settings lastSettings{};
	bool hasLastSettings = false;
	ID3D11Device* supportDevice = nullptr;
	bool nvapiInitAttempted = false;
	bool nvapiReady = false;
	bool supportChecked = false;
	bool supported = false;
	bool loggedNvapiFailure = false;
	bool loggedSupportFailure = false;
	bool loggedResourceFailure = false;
	bool allow4x4Rate = true;
	bool logged4x4Fallback = false;
	bool loggedViewportBindMode = false;
	bool active = false;
	uint32_t suspendDepth = 0;
	const char* lastDisableReason = "Not initialized";
};
