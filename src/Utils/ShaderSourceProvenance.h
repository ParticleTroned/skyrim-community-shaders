#pragma once

#include <exception>

namespace Util::ShaderSourceProvenance
{
	/** Runs compilation and retains its input digest only when a fresh closure read agrees. */
	template <class ReadDigest, class Compile, class ReadFailure>
	auto CompileWithStableDigest(ReadDigest&& a_readDigest, Compile&& a_compile, ReadFailure&& a_readFailure)
	{
		auto readDigest = [&](bool a_refresh) {
			try {
				return a_readDigest(a_refresh);
			} catch (...) {
				a_readFailure(std::current_exception());
				return decltype(a_readDigest(a_refresh)){};
			}
		};
		const auto before = readDigest(false);
		if (!a_compile())
			return decltype(before){};
		const auto after = readDigest(true);
		return before && before == after ? before : decltype(before){};
	}
}
