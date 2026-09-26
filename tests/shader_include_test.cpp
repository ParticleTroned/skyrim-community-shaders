#include "Utils/ShaderInclude.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <io.h>
#include <iostream>
#include <limits>
#include <share.h>
#include <source_location>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <winioctl.h>
#include <winrt/base.h>

#include "tracking_include_under_test.h"

namespace
{
	void Require(bool condition, const std::source_location& location = std::source_location::current())
	{
		if (!condition)
			throw std::runtime_error("Shader include check failed at line " + std::to_string(location.line()));
	}

	struct IncludeFixture
	{
		std::filesystem::path directory;
		std::filesystem::path root;
		std::filesystem::path path;
		std::filesystem::path source;
		std::filesystem::path previousDirectory = std::filesystem::current_path();
		std::ostringstream output;
		std::shared_ptr<spdlog::logger> previousLogger = spdlog::default_logger();

		IncludeFixture()
		{
			wchar_t temp[MAX_PATH];
			wchar_t file[MAX_PATH];
			const DWORD length = GetTempPathW(MAX_PATH, temp);
			Require(length != 0 && length < MAX_PATH);
			Require(GetTempFileNameW(temp, L"sio", 0, file) != 0);
			directory = file;
			std::filesystem::remove(directory);
			root = directory / "Data/Shaders";
			std::filesystem::create_directories(root / "Common");
			path = root / "Common/Test.hlsli";
			source = root / "Root.hlsl";
			Write(path, "include content");
			std::filesystem::current_path(directory);
			auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
			auto log = std::make_shared<spdlog::logger>("include-test", sink);
			log->set_pattern("%v");
			spdlog::set_default_logger(log);
		}

		~IncludeFixture()
		{
			spdlog::set_default_logger(previousLogger);
			std::error_code error;
			std::filesystem::current_path(previousDirectory, error);
			std::filesystem::remove_all(directory, error);
		}

		static void Write(const std::filesystem::path& path, const std::string& content)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(content.data(), static_cast<std::streamsize>(content.size()));
			file.close();
			Require(!file.fail());
		}
	};

	struct CrtExhaustion
	{
		std::vector<FILE*> streams;
		std::vector<int> descriptors;
		int error = 0;

		explicit CrtExhaustion(bool lowLevel)
		{
			if (lowLevel) {
				for (;;) {
					int descriptor = -1;
					const auto result = _sopen_s(&descriptor, "NUL", _O_RDONLY | _O_BINARY, _SH_DENYNO, 0);
					if (result != 0) {
						error = result;
						break;
					}
					descriptors.push_back(descriptor);
				}
			} else {
				for (;;) {
					FILE* stream = nullptr;
					const auto result = fopen_s(&stream, "NUL", "rb");
					if (result != 0) {
						error = result;
						break;
					}
					streams.push_back(stream);
				}
			}
		}

		~CrtExhaustion()
		{
			for (auto* stream : streams)
				fclose(stream);
			for (int descriptor : descriptors)
				_close(descriptor);
		}
	};

	struct IncludeBuffer
	{
		ID3DInclude& handler;
		LPCVOID data = nullptr;
		UINT size = 0;
		~IncludeBuffer() { handler.Close(data); }
		std::string Text() const { return std::string(static_cast<const char*>(data), size); }
	};

	template <class Handler>
	Handler MakeHandler(const IncludeFixture& fixture)
	{
		if constexpr (std::is_same_v<Handler, Util::CustomInclude>)
			return Handler(fixture.root, fixture.source);
		else
			return Handler(fixture.source);
	}

	template <class Handler>
	void TestExhaustion(bool lowLevel)
	{
		IncludeFixture fixture;
		auto handler = MakeHandler<Handler>(fixture);
		IncludeBuffer included{ handler };
		Util::ShaderInclude::File file;
		Util::ShaderInclude::ReadError error;
		bool readOK;
		bool crtOpened;
		int crtError;
		int exhaustionError;
		HRESULT result;
		{
			CrtExhaustion exhausted(lowLevel);
			exhaustionError = exhausted.error;
			std::ifstream stream(fixture.path, std::ios::binary);
			crtError = errno;
			crtOpened = stream.is_open();
			readOK = Util::ShaderInclude::Read(fixture.path, file, error);
			result = handler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, &included.data, &included.size);
		}
		Require(exhaustionError == EMFILE && !crtOpened && crtError == EMFILE);
		Require(readOK && std::string(file.data.get(), file.size) == "include content");
		Require(SUCCEEDED(result) && included.Text() == "include content");
		Require(fixture.output.str().empty());
	}

	template <class Handler>
	void TestNestedCompilation(bool lowLevel)
	{
		IncludeFixture fixture;
		fixture.Write(fixture.source, "#include \"Common/Parent.hlsli\"\nfloat4 main() : SV_Target { return VALUE; }");
		fixture.Write(fixture.root / "Common/Parent.hlsli", "#include \"Common/Empty.hlsli\"\n#include \"Common/Child.hlsli\"\n");
		fixture.Write(fixture.root / "Common/Empty.hlsli", "");
		fixture.Write(fixture.root / "Common/Child.hlsli", "#define VALUE float4(1, 2, 3, 4)\n");
		winrt::com_ptr<ID3DBlob> baseline;
		winrt::com_ptr<ID3DBlob> baselineErrors;
		auto baselineHandler = MakeHandler<Handler>(fixture);
		Require(SUCCEEDED(D3DCompileFromFile(fixture.source.c_str(), nullptr, &baselineHandler, "main", "ps_5_0",
			0, 0, baseline.put(), baselineErrors.put())));
		auto handler = MakeHandler<Handler>(fixture);
		winrt::com_ptr<ID3DBlob> compiled;
		winrt::com_ptr<ID3DBlob> compileErrors;
		HRESULT result;
		int exhaustionError;
		{
			CrtExhaustion exhausted(lowLevel);
			exhaustionError = exhausted.error;
			result = D3DCompileFromFile(fixture.source.c_str(), nullptr, &handler, "main", "ps_5_0",
				0, 0, compiled.put(), compileErrors.put());
		}
		if (FAILED(result) && compileErrors)
			std::cerr << static_cast<const char*>(compileErrors->GetBufferPointer());
		Require(exhaustionError == EMFILE && SUCCEEDED(result));
		Require(compiled->GetBufferSize() == baseline->GetBufferSize());
		Require(std::memcmp(compiled->GetBufferPointer(), baseline->GetBufferPointer(), compiled->GetBufferSize()) == 0);
	}

	template <class Handler>
	void TestFailuresAndEmptyIncludes()
	{
		IncludeFixture fixture;
		auto handler = MakeHandler<Handler>(fixture);
		const char sentinel = 0;
		LPCVOID data = &sentinel;
		UINT size = 10;
		Require(handler.Open(D3D_INCLUDE_LOCAL, "Common/Missing.hlsli", nullptr, &data, &size) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
		Require(data == nullptr && size == 0);
		const auto first = fixture.output.str();
		Require(first.find("operation=open") != std::string::npos);
		Require(first.find("win32_error=2 (") != std::string::npos);
		Require(first.find("source='" + fixture.source.string() + "'") != std::string::npos);
		Require(FAILED(handler.Open(D3D_INCLUDE_LOCAL, "Common/Missing.hlsli", nullptr, &data, &size)));
		Require(fixture.output.str() == first);
		data = &sentinel;
		size = 10;
		Require(handler.Open(D3D_INCLUDE_LOCAL, nullptr, nullptr, &data, &size) == E_INVALIDARG);
		Require(data == nullptr && size == 0);
		Require(handler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, nullptr, &size) == E_INVALIDARG);
		Require(handler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, &data, nullptr) == E_INVALIDARG);
		fixture.Write(fixture.path, "");
		IncludeBuffer included{ handler };
		Require(SUCCEEDED(handler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, &included.data, &included.size)));
		Require(included.data != nullptr && included.size == 0);
	}

	void TestReaderOwnershipAndErrors()
	{
		IncludeFixture fixture;
		Util::ShaderInclude::File first;
		Util::ShaderInclude::ReadError error;
		Require(Util::ShaderInclude::Read(fixture.path, first, error));
		winrt::file_handle writer{ CreateFileW(fixture.path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
		Require(static_cast<bool>(writer));
		const char edited[] = "edited\r\n\0bytes";
		DWORD written = 0;
		Require(WriteFile(writer.get(), edited, sizeof(edited) - 1, &written, nullptr) && written == sizeof(edited) - 1);
		Util::ShaderInclude::File second;
		Require(!Util::ShaderInclude::Read(fixture.path, second, error));
		Require(error.code == ERROR_SHARING_VIOLATION && !second.data && second.size == 0);
		writer.close();
		Require(Util::ShaderInclude::Read(fixture.path, second, error));
		Require(std::string(first.data.get(), first.size) == "include content");
		Require(std::string(second.data.get(), second.size) == std::string(edited, sizeof(edited) - 1));
		Require(error.code == ERROR_SUCCESS);
		Require(!Util::ShaderInclude::Read(fixture.root / "missing.hlsli", second, error));
		Require(error.code == ERROR_FILE_NOT_FOUND && !second.data && second.size == 0);
	}

	void TestOversizedInclude()
	{
		IncludeFixture fixture;
		winrt::file_handle writer{ CreateFileW(fixture.path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
		Require(static_cast<bool>(writer));
		DWORD returned = 0;
		Require(DeviceIoControl(writer.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr));
		LARGE_INTEGER size;
		size.QuadPart = static_cast<int64_t>((std::numeric_limits<UINT>::max)()) + 1;
		Require(SetFilePointerEx(writer.get(), size, nullptr, FILE_BEGIN));
		Require(SetEndOfFile(writer.get()));
		writer.close();
		Util::ShaderInclude::File file;
		Util::ShaderInclude::ReadError error;
		Require(!Util::ShaderInclude::Read(fixture.path, file, error));
		Require(error.code == ERROR_FILE_TOO_LARGE && error.expectedBytes == static_cast<uint64_t>(size.QuadPart));
		Require(!file.data && file.size == 0);
	}

	void TestRootAndDependencyContracts()
	{
		IncludeFixture fixture;
		Util::CustomInclude defaultHandler;
		IncludeBuffer defaultInclude{ defaultHandler };
		Require(SUCCEEDED(defaultHandler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, &defaultInclude.data, &defaultInclude.size)));
		Require(defaultInclude.Text() == "include content");
		const auto alternateRoot = fixture.directory / "Feature Shaders";
		std::filesystem::create_directories(alternateRoot / "Common");
		fixture.Write(alternateRoot / "Common/Test.hlsli", "alternate root");
		Util::CustomInclude alternateHandler(alternateRoot);
		IncludeBuffer alternateInclude{ alternateHandler };
		Require(SUCCEEDED(alternateHandler.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", nullptr, &alternateInclude.data, &alternateInclude.size)));
		Require(alternateInclude.Text() == "alternate root");

		TrackingIncludeHandler tracking(fixture.source);
		IncludeBuffer tracked{ tracking };
		Require(SUCCEEDED(tracking.Open(D3D_INCLUDE_LOCAL, "Common/../Common/Test.hlsli", nullptr, &tracked.data, &tracked.size)));
		const auto normalize = [](const std::filesystem::path& path) {
			auto result = std::filesystem::weakly_canonical(path).string();
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
			return result;
		};
		Require(tracking.includes.size() == 1 && tracking.includes[0] == normalize(fixture.path));
		Require(SUCCEEDED(tracking.Close(tracked.data)));
		for (int index = 0; index < 32; ++index) {
			IncludeBuffer next{ tracking };
			Require(SUCCEEDED(tracking.Open(D3D_INCLUDE_LOCAL, "Common/Test.hlsli", tracked.data, &next.data, &next.size)));
		}
		Require(tracked.Text() == "include content");
		IncludeBuffer missing{ tracking };
		Require(FAILED(tracking.Open(D3D_INCLUDE_LOCAL, "Common/Missing.hlsli", nullptr, &missing.data, &missing.size)));
		Require(tracking.includes.back() == normalize(fixture.root / "Common/Missing.hlsli"));
	}

	template <class Handler>
	void TestHandler()
	{
		TestFailuresAndEmptyIncludes<Handler>();
		for (bool lowLevel : { false, true }) {
			TestExhaustion<Handler>(lowLevel);
			TestNestedCompilation<Handler>(lowLevel);
		}
	}

	void TestDiagnosticLimit()
	{
		IncludeFixture fixture;
		for (int index = 0; index < 256; ++index)
			Util::ShaderInclude::Report(fixture.source, fixture.root / std::to_string(index), { "open", ERROR_FILE_NOT_FOUND });
		const auto capped = fixture.output.str();
		const auto lines = std::count(capped.begin(), capped.end(), '\n');
		Require(lines > 0 && lines <= 128);
		Util::ShaderInclude::Report(fixture.source, fixture.root / "additional.hlsli", { "open", ERROR_FILE_NOT_FOUND });
		Require(fixture.output.str() == capped);
	}
}

int main()
{
	try {
		TestHandler<Util::CustomInclude>();
		TestHandler<TrackingIncludeHandler>();
		TestReaderOwnershipAndErrors();
		TestOversizedInclude();
		TestRootAndDependencyContracts();
		TestDiagnosticLimit();
		std::cout << "Shader include contracts passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
