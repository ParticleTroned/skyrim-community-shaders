#include "Api/RuntimeThreadAffinity.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}
}

int main()
{
	try {
		CSX::Api::RuntimeThreadAffinity affinity;
		Check(!affinity.IsBound(), "new affinity was already bound");
		Check(!affinity.IsCurrentThread(), "unbound affinity accepted a caller");
		Check(affinity.BindCurrentThread() == CSX::Api::ThreadBindResult::kBound, "first bind failed");
		Check(affinity.IsBound(), "bound affinity reports unbound");
		Check(affinity.IsCurrentThread(), "owner thread was rejected");
		Check(affinity.BindCurrentThread() == CSX::Api::ThreadBindResult::kAlreadyBound, "repeat owner bind was not idempotent");

		auto other = std::async(std::launch::async, [&] {
			return std::pair{ affinity.IsCurrentThread(), affinity.BindCurrentThread() };
		}).get();
		Check(!other.first, "different thread was accepted as owner");
		Check(other.second == CSX::Api::ThreadBindResult::kDifferentThread, "different thread replaced the owner");
		Check(affinity.IsCurrentThread(), "owner changed after rejected bind");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
