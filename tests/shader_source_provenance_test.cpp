#include "Utils/ShaderSourceProvenance.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	struct SourceFixture
	{
		std::filesystem::path directory = std::filesystem::temp_directory_path() /
		                                  ("csx-source-provenance-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::path root = directory / "Water.hlsl";
		std::filesystem::path include = directory / "Common.hlsli";

		SourceFixture()
		{
			std::filesystem::create_directory(directory);
			Write(root, "#include \"Common.hlsli\"\nfloat4 main() { return VALUE; }\n");
			Write(include, "#define VALUE 1\n");
		}

		~SourceFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(directory, error);
		}

		static void Write(const std::filesystem::path& a_path, const std::string& a_text)
		{
			std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
			stream << a_text;
			stream.close();
			assert(stream);
		}

		std::optional<std::string> ReadClosure() const
		{
			std::string closure;
			for (const auto& path : { root, include }) {
				std::ifstream stream(path, std::ios::binary);
				if (!stream)
					return std::nullopt;
				const std::string contents{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
				closure += std::to_string(contents.size()) + ':' + contents;
			}
			return closure;
		}
	};
}

int main()
{
	int readFailures = 0;
	auto CompileWithStableDigest = [&](auto&& a_readDigest, auto&& a_compile) {
		return Util::ShaderSourceProvenance::CompileWithStableDigest(
			a_readDigest, a_compile, [&](std::exception_ptr a_error) {
				assert(a_error);
				++readFailures;
			});
	};
	SourceFixture fixture;
	const auto cachedClosure = fixture.ReadClosure();
	assert(cachedClosure);
	std::vector<bool> refreshed;
	auto readDigest = [&](bool a_refresh) {
		refreshed.push_back(a_refresh);
		return a_refresh ? fixture.ReadClosure() : cachedClosure;
	};

	// Persistence can run after another file version has replaced the compiler input.
	const auto deferredDigest = CompileWithStableDigest(readDigest, [] { return true; });
	assert(deferredDigest == cachedClosure);
	SourceFixture::Write(fixture.include, "#define VALUE 2\n");
	assert(fixture.ReadClosure() != cachedClosure);
	assert(deferredDigest == cachedClosure);
	assert((refreshed == std::vector<bool>{ false, true }));

	// Edits during compilation must be detected even when the timestamp cache is stale.
	for (const bool editRoot : { false, true }) {
		SourceFixture edited;
		const auto before = edited.ReadClosure();
		const auto& path = editRoot ? edited.root : edited.include;
		const auto timestamp = std::filesystem::last_write_time(path);
		const auto digest = CompileWithStableDigest(
			[&](bool a_refresh) { return a_refresh ? edited.ReadClosure() : before; },
			[&] {
				SourceFixture::Write(path, editRoot ? "float4 main() { return 2; }\n" : "#define VALUE 2\n");
				std::filesystem::last_write_time(path, timestamp);
				return true;
			});
		assert(!digest);
		assert(std::filesystem::last_write_time(path) == timestamp);
	}

	SourceFixture unavailable;
	const auto unavailableDigest = CompileWithStableDigest(
		[&](bool) { return unavailable.ReadClosure(); },
		[&] {
			std::filesystem::remove(unavailable.include);
			return true;
		});
	assert(!unavailableDigest);
	bool compiledWithoutDigest = false;
	assert(!CompileWithStableDigest(
		[](bool) { return std::optional<std::string>{}; },
		[&] { compiledWithoutDigest = true; return true; }));
	assert(compiledWithoutDigest);
	assert(!CompileWithStableDigest(readDigest, [] { return false; }));
	assert(readFailures == 0);
	for (const bool failAfterCompile : { false, true }) {
		bool compiled = false;
		assert(!CompileWithStableDigest(
			[&](bool a_refresh) -> std::optional<std::string> {
				if (a_refresh == failAfterCompile)
					throw std::runtime_error("source inspection failed");
				return cachedClosure;
			},
			[&] { compiled = true; return true; }));
		assert(compiled);
	}
	assert(readFailures == 2);
	return 0;
}
