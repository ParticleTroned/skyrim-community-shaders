#include "Utils/CompileDedupe.h"
#include "Utils/ShaderInclude.h"
#include "Utils/ShaderSourceProvenance.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <winrt/base.h>

namespace BuildProvenance
{
	std::string GetShaderCompilerIdentity() { return "test-d3dcompiler"; }
}

namespace RE
{
	struct BSShader
	{
		enum Type
		{
			Lighting,
			ImageSpace,
			Total
		};
	};
}

namespace
{
	Util::CompileDedupe::Registry& GetCompileDedupe()
	{
		static Util::CompileDedupe::Registry registry;
		return registry;
	}

	class CompilationSet
	{
	public:
		std::mutex compilationMutex;
		std::atomic<uint64_t> generation = 0;
		std::array<std::atomic<uint64_t>, RE::BSShader::Total> typeEpochs{};
#include "shared_compile_generation.h"
		std::atomic<uint64_t> contentDedupeTasks = 0;
		std::atomic<uint64_t> activeReuseTasks = 0;
		std::atomic<uint64_t> previousReuseTasks = 0;
		void IncContentReuse(bool a_previous, RE::BSShader::Type a_type, std::optional<uint64_t> a_taskGeneration);
		void IncContentDedupe(RE::BSShader::Type a_type, std::optional<uint64_t> a_taskGeneration);
	};

	// clang-format off
#include "shared_compile_counter.h"
#include "shared_compile_includes.h"
#include "shared_compile_snapshot.h"
	// clang-format on

	struct Fixture
	{
		std::filesystem::path directory = std::filesystem::temp_directory_path() /
		                                  ("csx-shared-compile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::path root = directory / "Shader.hlsl";
		std::filesystem::path include = directory / "Common.hlsli";
		const std::string shader = "#include \"Common.hlsli\"\nfloat4 main() : SV_Target { return VALUE; }\n";

		Fixture()
		{
			std::filesystem::create_directory(directory);
			Write(root, shader);
			Write(include, "#define VALUE 1\n");
			GetCompileDedupe().Clear();
		}
		~Fixture()
		{
			GetCompileDedupe().Clear();
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
		std::optional<Util::ContentHash::Hash128> Digest() const
		{
			const auto source = Util::ContentHash::HashFile(root);
			const auto included = Util::ContentHash::HashFile(include);
			if (!source || !included)
				return std::nullopt;
			return Util::ContentHash::CombineHashes(*source, *included);
		}
	};

	constexpr uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	winrt::com_ptr<ID3DBlob> Strip(winrt::com_ptr<ID3DBlob> a_blob)
	{
		winrt::com_ptr<ID3DBlob> stripped;
		assert(SUCCEEDED(D3DStripShader(a_blob->GetBufferPointer(), a_blob->GetBufferSize(),
			D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS | D3DCOMPILER_STRIP_PRIVATE_DATA, stripped.put())));
		return stripped;
	}

	bool Equal(ID3DBlob* a_left, ID3DBlob* a_right)
	{
		return a_left && a_right && a_left->GetBufferSize() == a_right->GetBufferSize() &&
		       std::memcmp(a_left->GetBufferPointer(), a_right->GetBufferPointer(), a_left->GetBufferSize()) == 0;
	}

	void SnapshotEdits()
	{
		Fixture fixture;
		D3D_SHADER_MACRO firstDefines[]{ { "UNUSED_DESCRIPTOR_BIT", "1" }, {} };
		D3D_SHADER_MACRO secondDefines[]{ { "UNUSED_DESCRIPTOR_BIT", "2" }, {} };
		TrackingIncludeHandler referenceIncludes(fixture.root);
		winrt::com_ptr<ID3DBlob> expected, errors;
		assert(SUCCEEDED(D3DCompileFromFile(fixture.root.c_str(), firstDefines, &referenceIncludes,
			"main", "ps_5_0", flags, 0, expected.put(), errors.put())));
		expected = Strip(std::move(expected));
		const auto before = fixture.Digest();
		std::optional<SharedCompile> owner;
		TrackingIncludeHandler preprocessing(fixture.root), compilation(fixture.root);
		winrt::com_ptr<ID3DBlob> compiled;
		const auto digest = Util::ShaderSourceProvenance::CompileWithStableDigest(
			[&](bool a_refresh) { return a_refresh ? fixture.Digest() : before; },
			[&] {
				owner.emplace(AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), firstDefines,
					"ps_5_0", flags, false, preprocessing));
				assert(owner->preprocessed && owner->ticket && !owner->blob);
				Fixture::Write(fixture.include, "#define VALUE 2\n");
				errors = nullptr;
				return SUCCEEDED(owner->Compile(fixture.root.wstring(), fixture.root.string(), firstDefines,
					"ps_5_0", flags, compilation, compiled.put(), errors.put()));
			},
			[](std::exception_ptr) { assert(false); });
		assert(!digest);
		assert(!preprocessing.includes.empty() && compilation.includes.empty());
		compiled = Strip(std::move(compiled));
		assert(Equal(compiled.get(), expected.get()));
		owner->ticket->Publish(compiled->GetBufferPointer(), compiled->GetBufferSize());

		TrackingIncludeHandler changedIncludes(fixture.root);
		auto changed = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), secondDefines,
			"ps_5_0", flags, false, changedIncludes);
		assert(changed.ticket && !changed.blob);
		winrt::com_ptr<ID3DBlob> changedBlob;
		errors = nullptr;
		assert(SUCCEEDED(changed.Compile(fixture.root.wstring(), fixture.root.string(), secondDefines,
			"ps_5_0", flags, compilation, changedBlob.put(), errors.put())));
		changedBlob = Strip(std::move(changedBlob));
		assert(!Equal(changedBlob.get(), expected.get()));
		changed.ticket->Publish(changedBlob->GetBufferPointer(), changedBlob->GetBufferSize());

		Fixture::Write(fixture.include, "#define VALUE 1\n");
		TrackingIncludeHandler reuseIncludes(fixture.root);
		auto reused = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), secondDefines,
			"ps_5_0", flags, false, reuseIncludes);
		assert(reused.blob && !reused.ticket && !reuseIncludes.includes.empty());
		assert(Equal(reused.blob.get(), expected.get()));
	}

	void DebugAndCompilerInputs()
	{
		Fixture fixture;
		const auto secondPath = fixture.directory / "Other.hlsl";
		Fixture::Write(secondPath, fixture.shader);
		for (const bool developer : { false, true }) {
			GetCompileDedupe().Clear();
			const auto compileFlags = developer ? D3DCOMPILE_DEBUG : flags;
			TrackingIncludeHandler firstIncludes(fixture.root), secondIncludes(secondPath);
			auto first = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), nullptr,
				"ps_5_0", compileFlags, developer, firstIncludes);
			winrt::com_ptr<ID3DBlob> blob, errors;
			assert(SUCCEEDED(first.Compile(fixture.root.wstring(), fixture.root.string(), nullptr,
				"ps_5_0", compileFlags, firstIncludes, blob.put(), errors.put())));
			if (!developer)
				blob = Strip(std::move(blob));
			first.ticket->Publish(blob->GetBufferPointer(), blob->GetBufferSize());
			auto second = AcquireSharedCompile(secondPath.wstring(), secondPath.string(), nullptr,
				"ps_5_0", compileFlags, developer, secondIncludes);
			assert(second.ticket.has_value() == developer);
			assert(static_cast<bool>(second.blob) == !developer);
			auto otherFlags = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), nullptr,
				"ps_5_0", compileFlags | D3DCOMPILE_AVOID_FLOW_CONTROL, developer, firstIncludes);
			assert(otherFlags.ticket && !otherFlags.blob);
			auto otherProfile = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), nullptr,
				"vs_5_0", compileFlags, developer, firstIncludes);
			assert(otherProfile.ticket && !otherProfile.blob);
		}
	}

	void RuntimeMacrosAndFallback()
	{
		Fixture fixture;
		Fixture::Write(fixture.root, "#ifdef VR\n#define OUTPUT 2\n#else\n#define OUTPUT 1\n#endif\nfloat4 main() : SV_Target { return OUTPUT; }\n");
		winrt::com_ptr<ID3DBlob> last;
		for (const bool vr : { false, true }) {
			D3D_SHADER_MACRO macros[]{ { "VR", nullptr }, {} };
			const auto* defines = vr ? macros : nullptr;
			TrackingIncludeHandler includes(fixture.root);
			auto shared = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), defines, "ps_5_0", flags, false, includes);
			assert(shared.ticket && !shared.blob);
			winrt::com_ptr<ID3DBlob> expected, compiled, errors;
			assert(SUCCEEDED(D3DCompileFromFile(fixture.root.c_str(), defines, &includes, "main", "ps_5_0", flags, 0, expected.put(), errors.put())));
			errors = nullptr;
			assert(SUCCEEDED(shared.Compile(fixture.root.wstring(), fixture.root.string(), defines, "ps_5_0", flags, includes, compiled.put(), errors.put())));
			expected = Strip(std::move(expected));
			compiled = Strip(std::move(compiled));
			assert(Equal(expected.get(), compiled.get()));
			if (last)
				assert(!Equal(last.get(), compiled.get()));
			shared.ticket->Publish(compiled->GetBufferPointer(), compiled->GetBufferSize());
			last = std::move(compiled);
		}
		Fixture::Write(fixture.root, "#include \"Missing.hlsli\"\n");
		TrackingIncludeHandler preprocessing(fixture.root), compilation(fixture.root);
		auto failed = AcquireSharedCompile(fixture.root.wstring(), fixture.root.string(), nullptr, "ps_5_0", flags, false, preprocessing);
		assert(!failed.preprocessed && !failed.ticket && !failed.blob);
		winrt::com_ptr<ID3DBlob> blob, errors;
		assert(FAILED(failed.Compile(fixture.root.wstring(), fixture.root.string(), nullptr, "ps_5_0", flags, compilation, blob.put(), errors.put())));
		assert(errors && !compilation.includes.empty());
	}
}

int main()
{
	SnapshotEdits();
	DebugAndCompilerInputs();
	RuntimeMacrosAndFallback();
	CompilationSet counters;
	counters.generation = 2;
	counters.IncContentDedupe(RE::BSShader::Lighting, 1);
	assert(counters.contentDedupeTasks == 0);
	counters.IncContentDedupe(RE::BSShader::Lighting, 2);
	counters.IncContentDedupe(RE::BSShader::Lighting, std::nullopt);
	assert(counters.contentDedupeTasks == 2);
	counters.IncContentReuse(false, RE::BSShader::Lighting, 1);
	counters.IncContentReuse(true, RE::BSShader::Lighting, 1);
	assert(counters.activeReuseTasks == 0 && counters.previousReuseTasks == 0);
	counters.IncContentReuse(false, RE::BSShader::Lighting, 2);
	counters.IncContentReuse(true, RE::BSShader::Lighting, 2);
	assert(counters.activeReuseTasks == 1 && counters.previousReuseTasks == 1);
	++counters.typeEpochs[RE::BSShader::ImageSpace];
	counters.IncContentDedupe(RE::BSShader::ImageSpace, 2);
	counters.IncContentReuse(false, RE::BSShader::ImageSpace, 2);
	counters.IncContentReuse(true, RE::BSShader::ImageSpace, 2);
	assert(counters.contentDedupeTasks == 2 && counters.activeReuseTasks == 1 && counters.previousReuseTasks == 1);
	counters.IncContentDedupe(RE::BSShader::Lighting, 2);
	counters.IncContentDedupe(RE::BSShader::ImageSpace, 3);
	counters.IncContentReuse(false, RE::BSShader::ImageSpace, 3);
	counters.IncContentReuse(true, RE::BSShader::Lighting, 2);
	assert(counters.contentDedupeTasks == 4 && counters.activeReuseTasks == 2 && counters.previousReuseTasks == 2);
}
