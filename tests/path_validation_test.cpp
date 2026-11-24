#include "../src/Utils/PathValidation.cpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
try {
	namespace fs = std::filesystem;
	const auto root = fs::current_path() / ("csx-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto library = root / "Library";
	fs::create_directories(library);
	struct Cleanup
	{
		fs::path root;
		~Cleanup()
		{
			std::error_code error;
			fs::remove(root / "Library", error);
			fs::remove(root, error);
		}
	} cleanup{ root };
	Require(Util::IsPathWithinDirectory(library, library / "New profile" / "Baseline" / "file.json"), "new nested destinations remain inside their library");
	Require(!Util::IsPathWithinDirectory(library, library / ".." / "Outside" / "file.json"), "parent traversal is rejected");
	Require(!Util::IsPathWithinDirectory(root / "Missing", root / "Missing" / "file.json"), "a missing library is rejected");

	const auto virtualLibrary = root / "Virtual" / "Presets";
	const auto physicalLibrary = root / "ProviderB" / "Presets";
	unsigned parentQueries = 0;
	const auto mappedCanonical = [&](const fs::path& path, std::error_code& error) -> fs::path {
		error.clear();
		if (path == virtualLibrary)
			return physicalLibrary;
		if (path == virtualLibrary.parent_path()) {
			++parentQueries;
			return root / "ProviderA";
		}
		if (path == virtualLibrary / "EscapingLink")
			return root / "Outside";
		error = std::make_error_code(std::errc::no_such_file_or_directory);
		return {};
	};
	std::error_code error;
	const auto resolved = Util::ResolvePathWithMissingTail(virtualLibrary / "New profile" / "file.json", error, mappedCanonical);
	Require(!error && resolved == physicalLibrary / "New profile" / "file.json" && parentQueries == 0,
		"new profiles use the longest existing virtual mapping, even when its parent belongs to another mod");
	const auto escaped = Util::ResolvePathWithMissingTail(virtualLibrary / "EscapingLink" / "file.json", error, mappedCanonical);
	Require(!error && escaped == root / "Outside" / "file.json", "existing link destinations remain resolved for containment checks");
	unsigned deniedQueries = 0;
	const auto denied = Util::ResolvePathWithMissingTail(virtualLibrary / "Denied", error, [&](const fs::path&, std::error_code& failure) -> fs::path {
		++deniedQueries;
		failure = std::make_error_code(std::errc::permission_denied);
		return {};
	});
	Require(denied.empty() && error == std::errc::permission_denied && deniedQueries == 1, "access errors must not fall back to a parent directory");
	std::cout << "Mapped preset paths, missing destinations and containment checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
