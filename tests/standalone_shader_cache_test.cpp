#include "Utils/ComIdentity.h"
#include "Utils/ShaderCacheManifest.h"
#include "Utils/ShaderCompiler.h"
#include "Utils/ShaderDefines.h"
#include "Utils/ShaderInclude.h"
#include "Utils/ShaderPreparation.h"
#include "Utils/ShaderSourceProvenance.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <d3dcompiler.h>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <winrt/base.h>

namespace logger = spdlog;
namespace SIE
{
	class ShaderCache;
}
struct TestState
{
	struct Defines
	{
		std::vector<std::pair<std::string, std::string>> defines;
	};
	bool developer = false;
	std::atomic_bool enablePartialPrecision = false, enableAvoidFlowControl = false;
	std::atomic_uint64_t shaderDefinesGeneration = 0;
	std::shared_ptr<Defines> snapshot = std::make_shared<Defines>();
	bool IsDeveloperMode() const { return developer; }
	auto GetShaderDefinesSnapshot() const { return snapshot; }
	uint64_t GetShaderDefinesGeneration() const { return shaderDefinesGeneration.load(); }
};
namespace globals
{
	TestState* state;
	SIE::ShaderCache* shaderCache;
	namespace d3d
	{
		ID3D11Device* device;
	}
}
namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
namespace Util
{
	std::string WStringToString(const std::wstring& value) { return std::filesystem::path(value).string(); }
	std::string DefinesToString(std::span<const D3D_SHADER_MACRO> macros) { return ShaderDefines::Serialize(macros); }
}
namespace Util::FileHelpers
{
	bool WriteTextFileAtomic(const std::filesystem::path&, std::string_view, std::string&, bool = true, bool = true);
	// clang-format off
#include "standalone_file_direct.h"
#include "standalone_file_atomic.h"
	// clang-format on
}
namespace CSX::Api
{
	struct Compatibility
	{
		std::string canonical;
	};
	std::string externalCompatibility = "test-contract";
	Compatibility GetShaderCompatibilityRequirementSet(const std::string&, const std::string&) { return { externalCompatibility }; }
}
namespace SIE
{
#include "standalone_source_digest.h"

	std::shared_mutex g_diskCacheMutationMutex;
	std::mutex g_standaloneCacheIOMutex;
	uint64_t testDiskGeneration = 0;
	bool managed = false, managedAvailable = false, failRead = false, failWrite = false;
	int writes = 0, manifestFlushes = 0;
	std::string globalIdentity = "test-abi";
	struct Snapshot
	{
		Util::ContentHash::Hash128 digest;
	};
	Snapshot CaptureGlobalCompileState() { return { Util::ContentHash::HashString(globalIdentity) }; }
	uint64_t GetDiskCacheGeneration() { return testDiskGeneration; }
	bool ManagedShaderPackLayoutPresent() { return managed; }
	std::string GetShaderPackFamily(const std::wstring&) { return "Standalone"; }
	struct Pack
	{
		std::unordered_map<std::string, winrt::com_ptr<ID3DBlob>> blobs;
	};
	Pack packs[2];
	Pack* GetShaderPackStore(bool developer) { return managedAvailable ? &packs[developer ? 1 : 0] : nullptr; }
	ID3DBlob* LoadShaderBlobFromPack(Pack& pack, bool, const std::wstring& path,
		const std::filesystem::path&, const Util::ContentHash::Hash128& compileDigest,
		std::optional<Util::ContentHash::Hash128> sourceDigest)
	{
		if (failRead)
			throw std::runtime_error("injected pack read failure");
		const auto key = GetManifestKey(path) + Util::ContentHash::CombineHashes(*sourceDigest, compileDigest).ToHex();
		const auto it = pack.blobs.find(key);
		if (it == pack.blobs.end())
			return nullptr;
		it->second->AddRef();
		return it->second.get();
	}
	class ShaderCache
	{
	public:
		bool enabled = true, held = false, skipUnchanged = true;
		struct Compilation
		{
			std::atomic_uint64_t generation = 0;
		} compilationSet;
		bool IsDiskCache() const { return enabled; }
		bool IsDiskCacheActive() const { return enabled && !held; }
		bool IsSkipUnchangedShaders() const { return skipUnchanged; }
		bool IsTaskStale(std::optional<uint64_t> value) const { return value && *value != compilationSet.generation; }
		std::optional<Util::OwnedStandaloneShaderRequest> queuedRequest;
		std::shared_ptr<Util::ShaderPreparation> queuedPreparation;
		Util::StandaloneCompileState CaptureStandaloneCompileState() const
		{
			const auto* state = globals::state;
			return { CaptureGlobalCompileState().digest, compilationSet.generation.load(), g_shaderSourceGeneration.load(),
				state->IsDeveloperMode(), REL::Module::IsVR(), state->enablePartialPrecision.load(), state->enableAvoidFlowControl.load(),
				IsDiskCache(), state->GetShaderDefinesSnapshot()->defines, state->GetShaderDefinesGeneration() };
		}
		bool IsStandalonePreparationCurrent(const Util::ShaderPreparation& operation, bool resident = false) const
		{
			return (resident || !IsTaskStale(operation.compilationGeneration)) && operation.sourceGeneration == g_shaderSourceGeneration;
		}
		std::shared_ptr<Util::ShaderPreparation> PrepareStandaloneShader(Util::OwnedStandaloneShaderRequest request)
		{
			queuedPreparation = std::make_shared<Util::ShaderPreparation>();
			queuedPreparation->device = request.device;
			queuedPreparation->compilationGeneration = request.state.compilationGeneration;
			queuedPreparation->sourceGeneration = request.state.sourceGeneration;
			queuedPreparation->shaderDefinesGeneration = request.state.shaderDefinesGeneration;
			queuedPreparation->developerMode = request.state.developerMode;
			queuedPreparation->partialPrecision = request.state.partialPrecision;
			queuedPreparation->avoidFlowControl = request.state.avoidFlowControl;
			queuedRequest = std::move(request);
			return queuedPreparation;
		}
		bool CompileStandaloneShaderCached(const Util::StandaloneShaderRequest&,
			const std::function<winrt::com_ptr<ID3DBlob>()>&, const std::function<bool(ID3DBlob*)>&);
		void PersistCompiledShaderBlob(ID3DBlob* blob, bool developer, const std::wstring& path,
			const std::filesystem::path&, const Util::ContentHash::Hash128& looseDigest,
			const Util::ContentHash::Hash128& packDigest, const Util::ContentHash::Hash128& sourceDigest,
			uint64_t generation, bool flush)
		{
			if (!IsDiskCacheActive() || generation != testDiskGeneration)
				return;
			if (failWrite)
				throw std::runtime_error("injected cache write failure");
			if (auto* pack = GetShaderPackStore(developer)) {
				pack->blobs[GetManifestKey(path) + Util::ContentHash::CombineHashes(sourceDigest, packDigest).ToHex()].copy_from(blob);
			} else if (!managed) {
				std::filesystem::create_directories(std::filesystem::path(path).parent_path());
				if (!WriteCachedShaderBlob(path, blob))
					return;
				auto& manifest = GetShaderCacheManifest();
				manifest.Set(GetManifestKey(path), Util::ContentHash::CombineHashes(sourceDigest, looseDigest).ToHex());
				assert(flush);
				assert(manifest.Save());
				++manifestFlushes;
			} else
				return;
			++writes;
		}
	};
	// clang-format off
#include "standalone_compile_key.h"
#include "standalone_cache_under_test.h"
	// clang-format on
}
#include "d3d_resource_naming.h"
namespace
{
	bool injectSizedError = false;
	HRESULT CompileForTest(LPCWSTR path, const D3D_SHADER_MACRO* macros, ID3DInclude* include,
		LPCSTR entry, LPCSTR target, UINT flags, UINT flags2, ID3DBlob** blob, ID3DBlob** errors)
	{
		if (!injectSizedError)
			return D3DCompileFromFile(path, macros, include, entry, target, flags, flags2, blob, errors);
		constexpr std::string_view message = "unterminated-error";
		assert(SUCCEEDED(D3DCreateBlob(message.size(), errors)));
		std::memcpy((*errors)->GetBufferPointer(), message.data(), message.size());
		return E_FAIL;
	}
}
namespace Util
{
#define D3DCompileFromFile CompileForTest
#include "standalone_d3d_under_test.h"
#undef D3DCompileFromFile
}
namespace
{
	void Write(const std::filesystem::path& path, std::string_view text)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << text;
		assert(file.good());
	}
	void VerifyLooseBlobIO(ID3DBlob* a_blob)
	{
		const std::wstring path = L"Data/ShaderCache/AtomicTests/test.cso";
		assert(!SIE::ReadCachedShaderBlob(path));
		assert(SIE::WriteCachedShaderBlob(path, a_blob));
		const std::string original(static_cast<const char*>(a_blob->GetBufferPointer()), a_blob->GetBufferSize());
		auto verifyOriginal = [&] {
			const auto read = SIE::ReadCachedShaderBlob(path);
			assert(read && read->GetBufferSize() == original.size());
			assert(std::memcmp(read->GetBufferPointer(), original.data(), original.size()) == 0);
		};
		verifyOriginal();

		std::vector<std::string> malformed{ "", original.substr(0, 31), original.substr(0, original.size() - 1), original + "x" };
		auto badMagic = original;
		badMagic[0] = 'X';
		malformed.push_back(badMagic);
		auto badLength = original;
		const uint32_t impossibleSize = 0xffffffff;
		std::memcpy(badLength.data() + 24, &impossibleSize, sizeof(impossibleSize));
		malformed.push_back(badLength);
		for (const auto& bytes : malformed) {
			Write(path, bytes);
			assert(!SIE::ReadCachedShaderBlob(path));
			assert(std::filesystem::exists(path));
			assert(SIE::WriteCachedShaderBlob(path, a_blob));
			verifyOriginal();
		}

		winrt::com_ptr<ID3DBlob> replacement;
		constexpr std::string_view replacementSource = "float4 main() : SV_Target { return 0; }";
		assert(SUCCEEDED(D3DCompile(replacementSource.data(), replacementSource.size(), nullptr, nullptr, nullptr,
			"main", "ps_5_0", 0, 0, replacement.put(), nullptr)));
		{
			winrt::file_handle held(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			assert(held);
			assert(!SIE::WriteCachedShaderBlob(path, replacement.get()));
		}
		verifyOriginal();
		for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(path).parent_path()))
			assert(entry.path().extension() != ".tmp");
		assert(SIE::WriteCachedShaderBlob(path, replacement.get()));
		const auto replaced = SIE::ReadCachedShaderBlob(path);
		assert(replaced && replaced->GetBufferSize() == replacement->GetBufferSize());
		assert(std::memcmp(replaced->GetBufferPointer(), replacement->GetBufferPointer(), replaced->GetBufferSize()) == 0);
	}

	struct IsolatedFiles
	{
		std::filesystem::path previous = std::filesystem::current_path();
		std::filesystem::path root = previous / ("standalone-cache-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		IsolatedFiles()
		{
			assert(std::filesystem::create_directory(root));
			std::filesystem::current_path(root);
			std::filesystem::create_directories("Data/Shaders/Common");
		}
		~IsolatedFiles()
		{
			std::filesystem::current_path(previous);
			std::filesystem::remove_all(root);
		}
	};
}
int main()
{
	IsolatedFiles files;
	TestState state;
	SIE::ShaderCache cache;
	globals::state = &state;
	globals::shaderCache = &cache;
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, device.put(), nullptr, context.put())));
	globals::d3d::device = device.get();
	Write("Data/Shaders/Common/value.hlsli", "#define VALUE 1\n");
	constexpr auto source = "#include \"Common/value.hlsli\"\nfloat4 main() : SV_Target { return VALUE + EXTRA; }\nfloat4 alternate() : SV_Target { return VALUE; }\n";
	Write("Data/Shaders/test.hlsl", source);
	const std::vector<std::pair<const char*, const char*>> defines{ { "EXTRA", "1" } };
	auto run = [&](bool shouldCompile, const auto& macros, const char* entry = "main") {
		Util::ShaderCompileTiming timing;
		winrt::com_ptr<ID3D11DeviceChild> shader;
		shader.attach(Util::CompileShader(L"Data/Shaders/test.hlsl", macros, "ps_5_0", entry, &timing));
		assert(shader);
		assert((timing.bytecodeCompilationQpcTicks > 0) == shouldCompile);
		assert(timing.d3dObjectCreationQpcTicks > 0);
		UINT length = 0;
		assert(SUCCEEDED(shader->GetPrivateData(Util::WKPDID_D3DDebugObjectNameT, &length, nullptr)) && length > 0);
	};
	run(true, defines);
	assert(SIE::writes == 1 && SIE::manifestFlushes == 1);
	SIE::GetShaderCacheManifest().Load("Data/ShaderCache/Manifest.json");
	run(false, defines);
	const auto includeTime = std::filesystem::last_write_time("Data/Shaders/Common/value.hlsli");
	Write("Data/Shaders/Common/value.hlsli", "#define VALUE 2\n");
	std::filesystem::last_write_time("Data/Shaders/Common/value.hlsli", includeTime);
	run(true, defines);
	run(false, defines);
	const auto sourceTime = std::filesystem::last_write_time("Data/Shaders/test.hlsl");
	Write("Data/Shaders/test.hlsl", std::string(source) + "// changed source\n");
	std::filesystem::last_write_time("Data/Shaders/test.hlsl", sourceTime);
	run(true, defines);
	run(true, defines, "alternate");
	run(false, defines, "alternate");
	const std::vector<std::pair<const char*, const char*>> changed{ { "EXTRA", "2" } };
	run(true, changed);
	run(false, changed);
	state.developer = true;
	run(true, defines);
	run(false, defines);
	state.enablePartialPrecision = true;
	run(true, defines);
	state.enablePartialPrecision = false;
	state.enableAvoidFlowControl = true;
	run(true, defines);
	state.enableAvoidFlowControl = false;
	REL::Module::vr = true;
	run(true, defines);
	run(false, defines);
	REL::Module::vr = false;
	state.developer = false;
	state.snapshot->defines.emplace_back("GLOBAL_TEST", "1");
	run(true, defines);
	run(false, defines);
	state.snapshot = std::make_shared<TestState::Defines>();
	cache.skipUnchanged = false;
	run(true, defines);
	run(true, defines);
	cache.skipUnchanged = true;
	run(false, defines);
	int before = SIE::writes;
	cache.enabled = false;
	run(true, defines);
	run(true, defines);
	assert(SIE::writes == before);
	cache.enabled = true;
	cache.held = true;
	run(true, defines);
	assert(SIE::writes == before);
	cache.held = false;
	for (const auto& entry : std::filesystem::directory_iterator("Data/ShaderCache/Standalone"))
		Write(entry.path(), "invalid bytecode");
	run(true, defines);
	run(false, defines);
	SIE::GetShaderCacheManifest().Clear();
	run(true, defines);
	run(false, defines);
	SIE::managed = true;
	before = SIE::writes;
	run(true, defines);
	assert(SIE::writes == before);
	SIE::managedAvailable = true;
	run(true, defines);
	run(false, defines);
	state.developer = true;
	run(true, defines);
	run(false, defines);
	state.developer = false;
	SIE::failRead = true;
	run(true, defines);
	SIE::failRead = false;
	SIE::globalIdentity = "changed-abi";
	SIE::failWrite = true;
	run(true, defines);
	SIE::failWrite = false;
	run(true, defines);
	run(false, defines);
	SIE::managed = SIE::managedAvailable = false;

	std::array<D3D_SHADER_MACRO, 2> macros{ { { "EXTRA", "1" }, { nullptr, nullptr } } };
	Util::StandaloneShaderRequest request{ L"Data/Shaders/test.hlsl", macros, "ps_5_0", "main", D3DCOMPILE_OPTIMIZATION_LEVEL3, false };
	int compiles = 0, creations = 0;
	auto compile = [&] {
		++compiles;
		winrt::com_ptr<ID3DBlob> blob, errors;
		Util::CustomInclude include;
		assert(SUCCEEDED(D3DCompileFromFile(request.source.c_str(), macros.data(), &include, request.entryPoint,
			request.profile, request.flags, 0, blob.put(), errors.put())));
		return blob;
	};
	auto create = [&](ID3DBlob* blob) {
		++creations;
		winrt::com_ptr<ID3D11PixelShader> shader;
		return SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, shader.put()));
	};
	VerifyLooseBlobIO(compile().get());
	before = SIE::writes;
	assert(cache.CompileStandaloneShaderCached(request, [&] { auto blob = compile(); ++SIE::testDiskGeneration; return blob; }, create));
	assert(SIE::writes == before);
	assert(cache.CompileStandaloneShaderCached(request, [&] { auto blob = compile(); ++cache.compilationSet.generation; return blob; }, create));
	assert(SIE::writes == before);
	assert(cache.CompileStandaloneShaderCached(request, [&] {
		auto blob = compile(); Write("Data/Shaders/Common/value.hlsli", "#define VALUE 3\n"); return blob; }, create));
	assert(SIE::writes == before);
	assert(!cache.CompileStandaloneShaderCached(request, compile, [](ID3DBlob*) { return false; }));
	assert(SIE::writes == before);
	assert(!cache.CompileStandaloneShaderCached(request, [] { return winrt::com_ptr<ID3DBlob>{}; }, create));
	assert(SIE::writes == before);
	assert(cache.CompileStandaloneShaderCached(request, compile, create));
	const auto warmCompiles = compiles;
	assert(cache.CompileStandaloneShaderCached(request, compile, create));
	assert(compiles == warmCompiles);
	const auto warmCreates = creations;
	bool rejectOnce = true;
	assert(cache.CompileStandaloneShaderCached(request, compile, [&](ID3DBlob* blob) {
		if (std::exchange(rejectOnce, false)) {
			++creations;
			return false;
		}
		return create(blob);
	}));
	assert(compiles == warmCompiles + 1 && creations == warmCreates + 2);
	const auto identity = SIE::BuildStandaloneCompileKey(request).packDigest;
	auto variant = request;
	variant.entryPoint = "alternate";
	assert(identity != SIE::BuildStandaloneCompileKey(variant).packDigest);
	variant = request;
	variant.profile = "vs_5_0";
	assert(identity != SIE::BuildStandaloneCompileKey(variant).packDigest);
	variant = request;
	variant.flags ^= D3DCOMPILE_DEBUG;
	assert(identity != SIE::BuildStandaloneCompileKey(variant).packDigest);
	variant = request;
	variant.source = L"Data/Shaders/another.hlsl";
	assert(identity != SIE::BuildStandaloneCompileKey(variant).packDigest);
	const auto strictSource = source;
	Write("Data/Shaders/test.hlsl", strictSource);
	Write("Data/Shaders/Common/value.hlsli", "#define VALUE 8\n");
	cache.enabled = false;
	auto strict = request;
	strict.requireStableSource = true;
	strict.compilationGeneration = cache.compilationSet.generation.load();
	strict.sourceGeneration = SIE::g_shaderSourceGeneration.load();
	bool staleSource = false;
	strict.staleSource = &staleSource;
	const auto createsBeforeEdit = creations;
	assert(!cache.CompileStandaloneShaderCached(strict, [&] {
		auto blob = compile();
		Write("Data/Shaders/Common/value.hlsli", "#define VALUE 9\n");
		return blob; }, create));
	assert(staleSource && creations == createsBeforeEdit);
	assert(!cache.CompileStandaloneShaderCached(strict, compile, [&](ID3DBlob* blob) {
		const auto accepted = create(blob);
		Write("Data/Shaders/Common/value.hlsli", "#define VALUE 10\n");
		return accepted;
	}));
	assert(staleSource && creations == createsBeforeEdit + 1);
	assert(cache.CompileStandaloneShaderCached(strict, compile, create) && !staleSource);
	const auto createsBeforeEpoch = creations;
	assert(!cache.CompileStandaloneShaderCached(strict, [&] {
		auto blob = compile();
		++cache.compilationSet.generation;
		return blob; }, create));
	assert(!staleSource && creations == createsBeforeEpoch);
	strict.compilationGeneration = cache.compilationSet.generation.load();
	assert(!cache.CompileStandaloneShaderCached(strict, [&] {
		auto blob = compile();
		++SIE::g_shaderSourceGeneration;
		return blob; }, create));
	assert(!staleSource && creations == createsBeforeEpoch);
	strict.sourceGeneration = SIE::g_shaderSourceGeneration.load();
	assert(!cache.CompileStandaloneShaderCached(strict, [] { return winrt::com_ptr<ID3DBlob>{}; }, create) && !staleSource);
	cache.enabled = true;
	assert(cache.CompileStandaloneShaderCached(strict, compile, create));
	const auto compilesBeforeCachedEdit = compiles;
	assert(!cache.CompileStandaloneShaderCached(strict, compile, [&](ID3DBlob* blob) {
		const auto accepted = create(blob);
		Write("Data/Shaders/Common/value.hlsli", "#define VALUE 11\n");
		return accepted;
	}));
	assert(staleSource && compiles == compilesBeforeCachedEdit);
	assert(cache.CompileStandaloneShaderCached(strict, compile, create) && !staleSource);

	state.developer = true;
	state.enablePartialPrecision = true;
	state.enableAvoidFlowControl = true;
	REL::Module::vr = true;
	state.snapshot = std::make_shared<TestState::Defines>();
	state.snapshot->defines.emplace_back("CAPTURED_GLOBAL", "12");
	++state.shaderDefinesGeneration;
	std::shared_ptr<Util::ShaderPreparation> preparation;
	{
		std::wstring temporaryPath = L"Data/Shaders/test.hlsl";
		std::string temporaryName = "EXTRA", temporaryValue = "13", temporaryProfile = "ps_5_0", temporaryEntry = "main", temporaryResource = "OwnedCapture::Shader";
		const std::vector<std::pair<const char*, const char*>> temporaryDefines{
			{ temporaryName.c_str(), temporaryValue.c_str() }, { "NULL_MACRO", nullptr }, { "EMPTY_MACRO", "" }
		};
		preparation = Util::RequestShaderPreparation(temporaryPath.c_str(), temporaryDefines,
			temporaryProfile.c_str(), temporaryEntry.c_str(), temporaryResource.c_str());
		temporaryPath = L"missing.hlsl";
		temporaryName = "MUTATED";
		temporaryValue = "99";
		temporaryProfile = "vs_5_0";
		temporaryEntry = "missing";
		temporaryResource = "Mutated::Shader";
	}
	assert(preparation && cache.queuedRequest && preparation->state == Util::ShaderPreparation::State::Pending && !preparation->shader);
	auto& captured = *cache.queuedRequest;
	assert(captured.source == L"Data/Shaders/test.hlsl" && captured.profile == "ps_5_0" && captured.entryPoint == "main" && captured.resourceName == "OwnedCapture::Shader");
	assert(captured.requireStableSource && captured.macros[0].name == "EXTRA" && captured.macros[0].definition == "13");
	assert(captured.macros[1].name == "NULL_MACRO" && !captured.macros[1].definition && captured.macros[2].definition && captured.macros[2].definition->empty());
	assert(captured.state.developerMode && captured.state.isVR && captured.state.partialPrecision && captured.state.avoidFlowControl && captured.state.skipValidation);
	assert(captured.state.shaderDefinesGeneration == state.GetShaderDefinesGeneration() && preparation->shaderDefinesGeneration == captured.state.shaderDefinesGeneration);
	assert((captured.state.defines == std::vector<std::pair<std::string, std::string>>({ { "CAPTURED_GLOBAL", "12" } })));
	assert(captured.flags & D3DCOMPILE_DEBUG);
	assert(captured.flags & D3DCOMPILE_PARTIAL_PRECISION);
	assert(captured.flags & D3DCOMPILE_AVOID_FLOW_CONTROL);
	assert(captured.flags & D3DCOMPILE_SKIP_VALIDATION);
	std::vector<D3D_SHADER_MACRO> capturedMacros;
	for (const auto& macro : captured.macros)
		capturedMacros.push_back({ macro.name.c_str(), macro.definition ? macro.definition->c_str() : nullptr });
	capturedMacros.push_back({});
	const Util::StandaloneShaderRequest frozen{ captured.source, capturedMacros, captured.profile.c_str(), captured.entryPoint.c_str(), captured.flags,
		captured.state.developerMode, captured.state.digest, captured.state.compilationGeneration, captured.state.sourceGeneration, true };
	const auto frozenIdentity = SIE::BuildStandaloneCompileKey(frozen).packDigest;
	state.developer = false;
	state.enablePartialPrecision = false;
	state.enableAvoidFlowControl = false;
	state.snapshot = std::make_shared<TestState::Defines>();
	REL::Module::vr = false;
	++state.shaderDefinesGeneration;
	SIE::globalIdentity = "configuration-changed-after-capture";
	assert(SIE::BuildStandaloneCompileKey(frozen).packDigest == frozenIdentity);
	Util::ShaderCompileTiming capturedTiming;
	const auto capturedShader = Util::CompilePreparedShader(captured, cache, &capturedTiming, &staleSource);
	assert(capturedShader && !staleSource && capturedTiming.bytecodeCompilationQpcTicks > 0 && capturedTiming.d3dObjectCreationQpcTicks > 0);
	UINT capturedNameSize = 0;
	assert(SUCCEEDED(capturedShader->GetPrivateData(Util::WKPDID_D3DDebugObjectNameT, &capturedNameSize, nullptr)));
	std::string capturedName(capturedNameSize, '\0');
	assert(SUCCEEDED(capturedShader->GetPrivateData(Util::WKPDID_D3DDebugObjectNameT, &capturedNameSize, capturedName.data())));
	assert(capturedName == "OwnedCapture::Shader");
	assert(!Util::IsShaderPreparationCurrent(*preparation) && !Util::IsShaderPreparationCurrent(*preparation, true));
	assert(cache.IsStandalonePreparationCurrent(*preparation));
	state.shaderDefinesGeneration = preparation->shaderDefinesGeneration;
	state.developer = preparation->developerMode;
	state.enablePartialPrecision = preparation->partialPrecision;
	state.enableAvoidFlowControl = preparation->avoidFlowControl;
	assert(Util::IsShaderPreparationCurrent(*preparation));
	++state.shaderDefinesGeneration;
	assert(!Util::IsShaderPreparationCurrent(*preparation) && !Util::IsShaderPreparationCurrent(*preparation, true));
	state.shaderDefinesGeneration = preparation->shaderDefinesGeneration;
	state.developer = !preparation->developerMode;
	assert(!Util::IsShaderPreparationCurrent(*preparation, true));
	state.developer = preparation->developerMode;
	state.enablePartialPrecision = !preparation->partialPrecision;
	assert(!Util::IsShaderPreparationCurrent(*preparation, true));
	state.enablePartialPrecision = preparation->partialPrecision;
	state.enableAvoidFlowControl = !preparation->avoidFlowControl;
	assert(!Util::IsShaderPreparationCurrent(*preparation, true));
	state.enableAvoidFlowControl = preparation->avoidFlowControl;
	assert(Util::IsShaderPreparationCurrent(*preparation, true));
	++cache.compilationSet.generation;
	assert(!Util::IsShaderPreparationCurrent(*preparation) && Util::IsShaderPreparationCurrent(*preparation, true));
	++SIE::g_shaderSourceGeneration;
	assert(!Util::IsShaderPreparationCurrent(*preparation, true));
	winrt::com_ptr<ID3D11Device> replacementDevice;
	assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		replacementDevice.put(), nullptr, nullptr)));
	globals::d3d::device = replacementDevice.get();
	preparation->sourceGeneration = SIE::g_shaderSourceGeneration.load();
	assert(!Util::IsShaderPreparationCurrent(*preparation, true));
	globals::d3d::device = device.get();
	state.developer = false;
	state.enablePartialPrecision = false;
	state.enableAvoidFlowControl = false;

	std::ostringstream capturedLog;
	auto previousLogger = spdlog::default_logger();
	auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(capturedLog);
	auto sizedLogger = std::make_shared<spdlog::logger>("sized-error", sink);
	sizedLogger->set_pattern("%v");
	spdlog::set_default_logger(sizedLogger);
	cache.enabled = false;
	injectSizedError = true;
	assert(!Util::CompileShader(L"Data/Shaders/test.hlsl", defines, "ps_5_0"));
	constexpr std::string_view expectedError = "Shader compilation failed:\n\nunterminated-error";
	assert(capturedLog.str().starts_with(expectedError));
	const auto ending = capturedLog.str().substr(expectedError.size());
	assert(ending == "\n" || ending == "\r\n");
	injectSizedError = false;
	spdlog::set_default_logger(previousLogger);
	cache.enabled = true;
	assert(!Util::CompileShader(nullptr, defines, "ps_5_0"));
	assert(!Util::CompileShader(L"Data/Shaders/test.hlsl", defines, "unsupported"));
	Write("Data/Shaders/test.hlsl", "not a valid shader");
	assert(!Util::CompileShader(L"Data/Shaders/test.hlsl", defines, "ps_5_0"));
	std::filesystem::remove("Data/Shaders/test.hlsl");
	assert(!Util::CompileShader(L"Data/Shaders/test.hlsl", defines, "ps_5_0"));
	std::cout << "Standalone shader cache: real compiler/WARP, warm reuse, identities, source/include edits, policies, corruption, atomic replacement, owned capture and strict source invalidation passed\n";
}
