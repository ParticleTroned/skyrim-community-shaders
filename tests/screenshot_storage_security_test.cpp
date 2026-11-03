// One translation unit exposes private path normalization to the custody fixtures.
#include "screenshot_storage_security_test.h"

#include <iostream>

int main()
{
	try {
		RunScreenshotStorageSecurityTests();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
