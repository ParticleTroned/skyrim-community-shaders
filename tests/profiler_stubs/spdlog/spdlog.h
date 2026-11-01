// Logging has no bearing on the timestamp/query behavior exercised by this fixture.
#pragma once
namespace spdlog { template<class... T> void warn(const char*, T...) {} template<class... T> void error(const char*, T...) {} }
