#pragma once

#include "RenderMap/ShaderBytecodeCatalogue.h"

#include <Windows.h>
#include <utility>

namespace Util
{
	/** Return the original result and output even when post-success diagnostics fail. */
	template <class Shader, class Observer>
	HRESULT ObserveSuccessfulShaderCreation(
		HRESULT a_result, Shader** a_output, Observer&& a_observer) noexcept
	{
		if (SUCCEEDED(a_result) && a_output && *a_output) {
			try {
				std::forward<Observer>(a_observer)(*a_output);
			} catch (...) {
				CSX::RenderMap::RecordShaderMetadataFailure();
			}
		}
		return a_result;
	}
}
