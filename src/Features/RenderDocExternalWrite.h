/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 ParticleTroned
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

#ifndef RENDERDOC_CC
#	include <Renderdoc/renderdoc_app.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

	// Independent fork contract; the upstream application API table is unchanged.
	typedef uint32_t(RENDERDOC_CC* pRENDERDOC_GetD3D11ExternalWriteVersion)(void);

	// Version 1 records a whole single-sampled DEFAULT color Texture2D subresource
	// written by opaque vendor work. Context, output and up to 16 optional input
	// textures must be wrapped objects from the same device. label and metadata are
	// null-terminated UTF-8, limited to 127 and 16383 bytes respectively.
	//
	// Call on the immediate context after ordering vendor completion before its next
	// copy, including cross-API fence waits, and before another write to the output.
	// Returns HRESULT: S_OK records the actual current output; S_FALSE marks it dirty
	// outside capture, without GPU work or allocation. Failure invalidates an active
	// capture on a valid context. Capture reads output synchronously without changing
	// its contents or pipeline bindings. Active readbacks are limited to 512 MiB each.
	//
	// Replay restores recorded bytes at an explicitly named opaque boundary. It does
	// not re-evaluate inference or propagate edited inputs through it. Inputs record
	// dependencies, not internal accesses. Pixel history covers boundary values only;
	// replay timings measure restoration, not vendor inference. These captures require
	// this fork's replay implementation; unmodified upstream rejects their version.
	typedef int32_t(RENDERDOC_CC* pRENDERDOC_CaptureD3D11ExternalWrite)(
		void* context, void* output, uint32_t subresource, const char* label, const char* metadata,
		uint32_t inputCount, void* const* inputs);

	// Marks an active capture invalid when the application cannot supply complete
	// evaluation metadata. Returns the failed HRESULT (or E_FAIL for a success code).
	// It submits no GPU work and changes no rendering state.
	typedef int32_t(RENDERDOC_CC* pRENDERDOC_FailD3D11ExternalWrite)(void* context, int32_t error);

#ifdef __cplusplus
}
#endif
