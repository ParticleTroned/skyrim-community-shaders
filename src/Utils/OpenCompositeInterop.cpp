#include "OpenCompositeInterop.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <cmath>
#include <cstring>

namespace
{
	static constexpr wchar_t kOpenCompositeUnleashedUpscalingStateName[] = L"Local\\OpenCompositeUnleashedUpscalingState";
	static constexpr uint32_t kOpenCompositeUnleashedUpscalingStateMagic = 0x4F435553;
	static constexpr uint32_t kOpenCompositeUnleashedUpscalingStateVersion = 1;

	struct OCUExternalUpscalerStateView
	{
		uint32_t magic;
		uint32_t version;
		uint32_t byteSize;
		uint32_t updateCounter;

		uint32_t active;
		uint32_t method;
		uint32_t dlssPreset;
		uint32_t flags;

		float renderScale;
		float mipBias;
		float mipBiasOffset;
		float reservedFloat0;

		uint32_t reserved[8];
	};

	static_assert(sizeof(OCUExternalUpscalerStateView) == 80);

	uint32_t ReadVolatileUInt32(const uint32_t* a_value)
	{
		return *reinterpret_cast<volatile const uint32_t*>(a_value);
	}

	class OCUExternalUpscalerStateMapping
	{
	public:
		~OCUExternalUpscalerStateMapping()
		{
			if (view) {
				UnmapViewOfFile(view);
				view = nullptr;
			}
			if (mapping) {
				CloseHandle(mapping);
				mapping = nullptr;
			}
		}

		const OCUExternalUpscalerStateView* GetView()
		{
			if (view)
				return view;

			const ULONGLONG now = GetTickCount64();
			if (now < nextOpenAttempt)
				return nullptr;

			mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kOpenCompositeUnleashedUpscalingStateName);
			if (!mapping) {
				nextOpenAttempt = now + 1000;
				return nullptr;
			}

			view = static_cast<const OCUExternalUpscalerStateView*>(
				MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(OCUExternalUpscalerStateView)));
			if (!view) {
				CloseHandle(mapping);
				mapping = nullptr;
				nextOpenAttempt = now + 1000;
			}
			return view;
		}

	private:
		HANDLE mapping = nullptr;
		const OCUExternalUpscalerStateView* view = nullptr;
		ULONGLONG nextOpenAttempt = 0;
	};

	OCUExternalUpscalerStateMapping& GetOCUExternalUpscalerStateMapping()
	{
		static OCUExternalUpscalerStateMapping mapping;
		return mapping;
	}
}

namespace Util
{
	bool TryReadOCUExternalUpscalerState(OCUExternalUpscalerState& o_state)
	{
		const auto* view = GetOCUExternalUpscalerStateMapping().GetView();
		if (!view)
			return false;

		for (uint32_t attempt = 0; attempt < 8; ++attempt) {
			const uint32_t beforeCounter = ReadVolatileUInt32(&view->updateCounter);
			if ((beforeCounter & 1u) != 0)
				continue;

			OCUExternalUpscalerStateView snapshot{};
			MemoryBarrier();
			std::memcpy(&snapshot, view, sizeof(snapshot));
			MemoryBarrier();

			const uint32_t afterCounter = ReadVolatileUInt32(&view->updateCounter);
			if (beforeCounter != afterCounter || (afterCounter & 1u) != 0)
				continue;

			if (snapshot.magic != kOpenCompositeUnleashedUpscalingStateMagic ||
				snapshot.version != kOpenCompositeUnleashedUpscalingStateVersion ||
				snapshot.byteSize < sizeof(OCUExternalUpscalerStateView) ||
				snapshot.active == 0 ||
				!std::isfinite(snapshot.mipBias)) {
				return false;
			}

			o_state.mipBias = snapshot.mipBias;
			o_state.renderScale = std::isfinite(snapshot.renderScale) ? snapshot.renderScale : 1.0f;
			o_state.method = snapshot.method;
			o_state.flags = snapshot.flags;
			return true;
		}

		return false;
	}
}
