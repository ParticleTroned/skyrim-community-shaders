#pragma once

struct ID3D11DeviceContext;

/** Validate the bound vertex shader's COLOR0 linkage for the pointer overlay. */
bool ValidateVRMenuPointerVertexShader(ID3D11DeviceContext* a_context);

/** Release the render thread's retained vertex shader during resource teardown. */
void ResetVRMenuPointerVertexShaderValidation() noexcept;
