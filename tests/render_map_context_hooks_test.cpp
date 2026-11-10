#include "RenderMap/D3DContextHooks.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
	// Report fixture exceptions that cross a noexcept callback boundary.
	std::set_terminate([]() noexcept {
		const char* message = "no active C++ exception";
		try {
			if (auto exception = std::current_exception())
				std::rethrow_exception(exception);
		} catch (const std::exception& error) {
			std::fprintf(stderr, "RenderMapContextHooks terminate: %s\n", error.what());
			std::fflush(stderr);
			std::abort();
		} catch (...) {
			message = "non-standard C++ exception";
		}
		std::fprintf(stderr, "RenderMapContextHooks terminate: %s\n", message);
		std::fflush(stderr);
		std::abort();
	});
	try {
		CSX::RenderMap::RunContextHookTests();
		std::cout << "RenderMapContextHooks PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
