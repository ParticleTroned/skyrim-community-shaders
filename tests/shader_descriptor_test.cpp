#include "Utils/ShaderDescriptor.h"

using Util::ShaderDescriptor::UploadedPixelDescriptor;
static_assert(UploadedPixelDescriptor(0b1111, 0b0110, 0) == 0b1001);
static_assert(UploadedPixelDescriptor(0b1111, 0b0110, 0b0010) == 0b1011);
static_assert(UploadedPixelDescriptor(0b1101, 0b0110, 0b0010) == 0b1001);
static_assert(UploadedPixelDescriptor(0, ~0u, ~0u) == 0);
static_assert(UploadedPixelDescriptor(~0u, 0, 0) == ~0u);

int main() {}
