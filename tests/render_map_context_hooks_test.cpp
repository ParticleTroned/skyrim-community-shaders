#include "RenderMap/D3DContextHooks.h"

#include <exception>
#include <iostream>

int main()
{
	try {
		CSX::RenderMap::RunContextHookTests();
		std::cout << "RenderMapContextHooks PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
