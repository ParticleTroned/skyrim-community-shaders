#pragma once

#include <cstddef>
#include <utility>

// GetFeatureBufferData returns non-owning bytes in reused thread-local storage.
// The contents are overwritten by the next GetFeatureBufferData call on the same thread.
std::pair<const unsigned char*, std::size_t> GetFeatureBufferData(bool a_inWorld);
