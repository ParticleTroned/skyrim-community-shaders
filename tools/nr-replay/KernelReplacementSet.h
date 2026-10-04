#pragma once

#include "ProviderKernelChainContract.h"
#include "Utils/CryptoHash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace NrReplay::KernelReplacement
{
	inline constexpr std::size_t kFunctionCount = 44;
	using Device = std::uintptr_t;
	using Handle = std::uintptr_t;
	struct NativeResult
	{
		int status = -1;
		Handle handle = 0;
	};
	struct Binding
	{
		std::size_t index = 0;
		Handle function = 0;
		unsigned paramSize = 0, gridZ = 0, batchCount = 0, packetStride = 0, candidateParameterBytes = 0;
		std::string candidateSha256;
	};

	/** Owns a pinned replay-only replacement catalog; native creation remains lazy. */
	template <class Operations>
	class Set
	{
		using Json = nlohmann::json;
		using DeviceOwner = typename Operations::DeviceOwner;
		struct Module
		{
			std::string originalSha256, sha256;
			std::filesystem::path path;
			std::vector<std::uint8_t> bytes;
			Handle handle = 0, createdHandle = 0, returnedHandle = 0;
			std::optional<int> createStatus, destroyStatus;
		};
		struct Function
		{
			std::size_t module = 0;
			std::string name;
			unsigned paramSize = 0, gridZ = 0;
			Handle handle = 0, createdHandle = 0, returnedHandle = 0;
			std::optional<int> createStatus, destroyStatus;
			std::size_t attempted = 0, successful = 0, sampleSuccessful = 0;
		};
		struct OriginalModule
		{
			Handle handle;
			std::string sha256;
		};
		struct OriginalFunction
		{
			Handle handle, module;
			std::string name;
		};
		struct State
		{
			explicit State(Operations value) : operations(std::move(value)) {}
			Operations operations;
			DeviceOwner deviceOwner{};
			Device device = 0;
			std::thread::id thread = std::this_thread::get_id();
			std::vector<Module> modules;
			std::vector<Function> functions;
			std::vector<OriginalModule> originals;
			std::vector<OriginalFunction> originalFunctions;
			std::filesystem::path manifest;
			std::string manifestSha256, catalogSha256;
			unsigned batchCount = 0;
			bool failed = false, uncertain = false, nativeStarted = false, closed = false, retained = false, sampleOpen = false;
			std::array<char, 256> reason{};
		};
		std::unique_ptr<State> storage_;
		State* state_;

		static std::string Hash(std::span<const std::uint8_t> bytes)
		{
			return Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(bytes)));
		}
		static std::vector<std::uint8_t> Read(const std::filesystem::path& path, std::size_t maximum)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			const auto size = file.tellg();
			ProviderFloor::Require(file && size > 0 && static_cast<std::uint64_t>(size) <= maximum, "replacement file size/open invalid");
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
			file.seekg(0);
			file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			ProviderFloor::Require(file && file.peek() == std::char_traits<char>::eof(), "replacement file read changed or failed");
			return bytes;
		}
		static void Name(std::string_view name)
		{
			ProviderFloor::Require(!name.empty() && name.size() < KernelChain::kMaximumFunctionNameBytes &&
									   std::ranges::all_of(name, [](unsigned char c) { return c >= 0x21 && c <= 0x7e; }),
				"replacement entry name invalid");
		}
		void Fail(std::string_view reason, bool uncertain = false) noexcept
		{
			state_->failed = true;
			state_->uncertain |= uncertain;
			if (!state_->reason[0]) {
				const auto size = std::min(reason.size(), state_->reason.size() - 1);
				std::memcpy(state_->reason.data(), reason.data(), size);
			}
		}
		template <class Callback>
		decltype(auto) Checked(Callback&& callback)
		{
			try {
				ProviderFloor::Require(state_->thread == std::this_thread::get_id() && !state_->failed && !state_->closed && !state_->retained,
					"replacement set failed, closed, or called from another thread");
				return callback();
			} catch (const std::exception& error) {
				Fail(error.what());
				throw;
			} catch (...) {
				Fail("replacement operation threw an unknown exception", true);
				throw;
			}
		}
		void BindDevice(Device device)
		{
			ProviderFloor::Require(device && (!state_->device || state_->device == device), "replacement device changed or null");
			if (!state_->device) {
				state_->deviceOwner = state_->operations.RetainDevice(device);
				ProviderFloor::Require(state_->operations.DeviceIdentity(state_->deviceOwner) == device, "replacement device retention failed");
				state_->device = device;
			}
		}
		bool PrivateAlias(Handle handle) const
		{
			return std::ranges::any_of(state_->modules, [=](const auto& m) { return m.createdHandle == handle; }) ||
			       std::ranges::any_of(state_->functions, [=](const auto& f) { return f.createdHandle == handle; });
		}
		bool OriginalAlias(Handle handle) const
		{
			return std::ranges::any_of(state_->originals, [=](const auto& m) { return m.handle == handle; }) ||
			       std::ranges::any_of(state_->originalFunctions, [=](const auto& f) { return f.handle == handle; });
		}
		void AdmitPrivate(Handle handle)
		{
			ProviderFloor::Require(handle && !PrivateAlias(handle) && !OriginalAlias(handle), "replacement private handle aliases existing ownership");
		}
		void Ensure(Function& function)
		{
			auto& s = *state_;
			auto& module = s.modules.at(function.module);
			if (!module.handle) {
				s.nativeStarted = s.uncertain = true;
				const auto result = s.operations.CreateModule(s.device, module.bytes.data(), static_cast<std::uint32_t>(module.bytes.size()));
				module.createStatus = result.status;
				module.returnedHandle = result.handle;
				ProviderFloor::Require(result.status == 0, "replacement native module creation failed");
				AdmitPrivate(result.handle);
				module.handle = module.createdHandle = result.handle;
				s.uncertain = false;
				ProviderFloor::Require(Hash(module.bytes) == module.sha256, "replacement owned module bytes changed");
			}
			if (!function.handle) {
				s.nativeStarted = s.uncertain = true;
				const auto name = function.name;
				const auto result = s.operations.CreateFunction(s.device, module.handle, function.name.c_str());
				function.createStatus = result.status;
				function.returnedHandle = result.handle;
				ProviderFloor::Require(result.status == 0, "replacement native function creation failed");
				AdmitPrivate(result.handle);
				function.handle = function.createdHandle = result.handle;
				s.uncertain = false;
				ProviderFloor::Require(function.name == name && Hash(module.bytes) == module.sha256, "replacement owned creation arguments changed");
			}
		}
		void Retain() noexcept
		{
			state_->retained = true;
			(void)storage_.release();
		}
		void Load(const std::filesystem::path& manifestPath, std::span<const std::string_view> expectedShas, unsigned batchCount)
		{
			using ProviderFloor::Require;
			auto& s = *state_;
			Require(batchCount == 1 || batchCount == 2, "replacement batch count invalid");
			Require(!expectedShas.empty() && expectedShas.size() <= 8, "replacement accepted catalog count invalid");
			std::array<std::string, 8> accepted;
			for (std::size_t i = 0; i < expectedShas.size(); ++i)
				accepted[i] = ProviderFloor::CanonicalSha256(expectedShas[i]);
			const auto bytes = Read(manifestPath, 128 * 1024);
			const auto manifest = Json::parse(bytes);
			Require(manifest.is_object() && manifest.size() == 3 && manifest.at("schema") == "nr-model-kernel-replacement-v1" &&
						manifest.at("batchCount").is_number_integer() && manifest.at("batchCount") == batchCount &&
						manifest.at("modules").is_array() && manifest.at("modules").size() == 9,
				"replacement catalog schema/count invalid");
			Json semantic = manifest;
			semantic.erase("schema");
			for (auto& module : semantic["modules"])
				module.erase("path");
			s.catalogSha256 = Util::CryptoHash::Sha256Hex(semantic.dump());
			Require(std::ranges::any_of(std::span(accepted).first(expectedShas.size()), [&](const auto& expected) { return s.catalogSha256 == expected; }),
				"replacement semantic catalog SHA256 mismatch");
			s.manifest = manifestPath;
			s.manifestSha256 = Hash(bytes);
			s.batchCount = batchCount;
			s.modules.reserve(9);
			s.functions.reserve(kFunctionCount);
			std::size_t totalBytes = 0;
			std::string priorModule;
			for (const auto& item : manifest.at("modules")) {
				Require(item.is_object() && item.size() == 4 && item.at("functions").is_array() && !item.at("functions").empty(), "replacement module record invalid");
				Module module;
				module.originalSha256 = ProviderFloor::CanonicalSha256(item.at("originalSha256").template get<std::string>());
				module.sha256 = ProviderFloor::CanonicalSha256(item.at("sha256").template get<std::string>());
				Require(module.originalSha256 > priorModule, "replacement original modules duplicate or unsorted");
				priorModule = module.originalSha256;
				module.path = item.at("path").template get<std::string>();
				Require(module.path.is_absolute(), "replacement module path must be absolute");
				module.bytes = Read(module.path, KernelChain::kMaximumModuleBytes);
				KernelChain::ValidateModuleBudget(module.bytes.size(), s.modules.size(), totalBytes);
				totalBytes += module.bytes.size();
				Require(module.bytes.size() >= 64 && module.bytes[0] == 0x7f && module.bytes[1] == 'E' && module.bytes[2] == 'L' && module.bytes[3] == 'F' &&
							Hash(module.bytes) == module.sha256,
					"replacement module ELF/hash mismatch");
				std::string priorName;
				for (const auto& f : item.at("functions")) {
					Require(f.is_object() && f.size() == 3 && f.at("paramSize").is_number_unsigned() && f.at("gridZ").is_number_unsigned(), "replacement function metadata invalid");
					Function function;
					function.module = s.modules.size();
					function.name = f.at("name").template get<std::string>();
					Name(function.name);
					Require(function.name > priorName && s.functions.size() < kFunctionCount, "replacement functions duplicate, unsorted, or excessive");
					priorName = function.name;
					const auto paramSize = f.at("paramSize").template get<std::uint64_t>();
					const auto gridZ = f.at("gridZ").template get<std::uint64_t>();
					const auto stride = batchCount == 1 ? paramSize : (paramSize + 15) / 16 * 16;
					Require(paramSize && paramSize <= KernelChain::kMaximumParameterBytes && stride <= KernelChain::kMaximumParameterBytes / batchCount && gridZ && gridZ <= 65535 / batchCount,
						"replacement parameter size/gridZ out of bounds");
					function.paramSize = static_cast<unsigned>(paramSize);
					function.gridZ = static_cast<unsigned>(gridZ);
					s.functions.push_back(std::move(function));
				}
				s.modules.push_back(std::move(module));
			}
			Require(s.functions.size() == kFunctionCount, "replacement catalog must cover exactly44 entries");
		}

	public:
		Set(const std::filesystem::path& manifest, std::span<const std::string_view> expectedSemanticShas, unsigned batchCount, Operations operations) :
			storage_(std::make_unique<State>(std::move(operations))), state_(storage_.get())
		{
			Load(manifest, expectedSemanticShas, batchCount);
		}
		Set(const std::filesystem::path& manifest, std::string_view expectedSemanticSha, unsigned batchCount, Operations operations) :
			Set(manifest, std::span<const std::string_view>(&expectedSemanticSha, 1), batchCount, std::move(operations)) {}
		Set(const Set&) = delete;
		Set& operator=(const Set&) = delete;
		~Set()
		{
			if (storage_ && state_->nativeStarted && !state_->closed)
				Retain();
		}
		/** Records provider ownership without replacing any provider-returned handle. */
		void ObserveOriginalModule(Device device, Handle handle, std::string_view sha)
		{
			Checked([&] {
				BindDevice(device);
				ProviderFloor::Require(handle && ProviderFloor::CanonicalSha256(sha) == sha, "original module identity invalid");
				if (PrivateAlias(handle)) {
					Fail("later original module aliases private ownership", true);
					throw std::runtime_error(state_->reason.data());
				}
				for (const auto& m : state_->originals) {
					if (m.handle == handle) {
						ProviderFloor::Require(m.sha256 == sha, "original module handle identity changed");
						return;
					}
				}
				ProviderFloor::Require(state_->originals.size() < KernelChain::kMaximumModules, "original module budget exceeded");
				state_->originals.push_back({ handle, std::string(sha) });
			});
		}
		void ObserveOriginalFunction(Device device, Handle module, Handle handle, std::string_view name)
		{
			Checked([&] {
				BindDevice(device);
				Name(name);
				ProviderFloor::Require(handle && std::ranges::any_of(state_->originals, [=](const auto& m) { return m.handle == module; }), "original function module unknown");
				if (PrivateAlias(handle)) {
					Fail("later original function aliases private ownership", true);
					throw std::runtime_error(state_->reason.data());
				}
				for (const auto& f : state_->originalFunctions) {
					if (f.handle == handle) {
						ProviderFloor::Require(f.module == module && f.name == name, "original function handle identity changed");
						return;
					}
				}
				ProviderFloor::Require(state_->originalFunctions.size() < KernelChain::kMaximumFunctions, "original function budget exceeded");
				state_->originalFunctions.push_back({ handle, module, std::string(name) });
			});
		}
		/** Resolves only a fully observed original tuple to a separate private launch handle. */
		Binding Resolve(Device device, Handle original, std::string_view moduleSha, std::string_view name, unsigned paramSize, unsigned gridZ)
		{
			return Checked([&]() -> Binding {
				BindDevice(device);
				const auto observed = std::ranges::find_if(state_->originalFunctions, [=](const auto& f) { return f.handle == original && f.name == name; });
				ProviderFloor::Require(observed != state_->originalFunctions.end(), "replacement original function was not observed");
				const auto module = std::ranges::find_if(state_->originals, [&](const auto& m) { return m.handle == observed->module && m.sha256 == moduleSha; });
				ProviderFloor::Require(module != state_->originals.end(), "replacement original module tuple mismatch");
				for (std::size_t index = 0; index < state_->functions.size(); ++index) {
					auto& f = state_->functions[index];
					auto& m = state_->modules[f.module];
					if (m.originalSha256 != moduleSha || f.name != name)
						continue;
					ProviderFloor::Require(f.paramSize == paramSize && f.gridZ == gridZ, "replacement original packet/grid contract mismatch");
					Ensure(f);
					const auto stride = state_->batchCount == 1 ? f.paramSize : (f.paramSize + 15) / 16 * 16;
					return { index, f.handle, f.paramSize, f.gridZ, state_->batchCount, stride, stride * state_->batchCount, m.sha256 };
				}
				throw std::runtime_error("replacement original entry absent from pinned catalog");
			});
		}
		void BeginSample()
		{
			Checked([&] {
				ProviderFloor::Require(!state_->sampleOpen, "replacement prior sample coverage was not concluded");
				for (auto& f : state_->functions) f.sampleSuccessful = 0;
				state_->sampleOpen = true;
			});
		}
		/** Requires every private kernel to exist without asserting GPU submission or completion. */
		void RequirePrepared()
		{
			Checked([&] {
				ProviderFloor::Require(std::ranges::all_of(state_->modules, [](const auto& module) { return module.handle && module.createStatus == 0; }) &&
										   std::ranges::all_of(state_->functions, [](const auto& function) { return function.handle && function.createStatus == 0; }),
					"replacement catalog private kernels are not fully prepared");
			});
		}
		void RecordSubmission(std::size_t index, int status)
		{
			Checked([&] {
				ProviderFloor::Require(state_->sampleOpen && index < state_->functions.size() && state_->functions[index].handle, "replacement submission lacks a resolved sample entry");
				auto& f = state_->functions[index];
				++f.attempted;
				ProviderFloor::Require(status == 0, "replacement native launch failed");
				++f.successful;
				++f.sampleSuccessful;
			});
		}
		/** Requires positive API submission coverage, independently of later GPU completion. */
		void RequireCompleteCoverage()
		{
			Checked([&] {
				ProviderFloor::Require(state_->sampleOpen && std::ranges::all_of(state_->functions, [](const auto& f) { return f.sampleSuccessful != 0; }), "replacement sample lacks complete44-entry coverage");
				state_->sampleOpen = false;
			});
		}
		/** Concludes a diagnostic sample only when every submitted entry matches its explicit plan. */
		void RequireSubmissionCounts(std::span<const std::size_t> expected)
		{
			Checked([&] {
				ProviderFloor::Require(state_->sampleOpen && expected.size() == state_->functions.size() &&
										   std::ranges::any_of(expected, [](std::size_t count) { return count != 0; }),
					"replacement submission plan must cover all entries with at least one planned call");
				for (std::size_t i = 0; i < expected.size(); ++i)
					ProviderFloor::Require(state_->functions[i].sampleSuccessful == expected[i], "replacement entry submission count differs from its explicit plan");
				state_->sampleOpen = false;
			});
		}
		bool Healthy() const noexcept { return !state_->failed && !state_->uncertain && !state_->retained; }
		/** Retires private handles only after both external retirement proofs are explicit. */
		bool Retire(bool gpuIdle, bool cachesRestored) noexcept
		{
			auto& s = *state_;
			if (s.closed)
				return true;
			if (s.retained)
				return false;
			try {
				ProviderFloor::Require(s.thread == std::this_thread::get_id() && gpuIdle && cachesRestored && !s.uncertain, "replacement retirement proof missing or ownership uncertain");
				for (auto& f : s.functions)
					if (f.handle) {
						s.uncertain = true;
						f.destroyStatus = s.operations.DestroyFunction(s.device, f.handle);
						ProviderFloor::Require(*f.destroyStatus == 0, "replacement private function retirement failed");
						f.handle = 0;
						s.uncertain = false;
					}
				for (auto& m : s.modules)
					if (m.handle) {
						s.uncertain = true;
						m.destroyStatus = s.operations.DestroyModule(s.device, m.handle);
						ProviderFloor::Require(*m.destroyStatus == 0, "replacement private module retirement failed");
						m.handle = 0;
						s.uncertain = false;
					}
				s.deviceOwner = DeviceOwner{};
				s.closed = true;
				return true;
			} catch (const std::exception& error) {
				Fail(error.what(), true);
			} catch (...) {
				Fail("replacement retirement threw", true);
			}
			Retain();
			return false;
		}
		Json Receipt() const
		{
			const auto& s = *state_;
			Json modules = Json::array(), functions = Json::array();
			const auto status = [](const std::optional<int>& value) { return value ? Json(*value) : Json(nullptr); };
			for (const auto& m : s.modules) modules.push_back({ { "originalSha256", m.originalSha256 }, { "sha256", m.sha256 }, { "path", m.path.string() },
				{ "createdHandle", m.createdHandle }, { "returnedHandle", m.returnedHandle }, { "activeHandle", m.handle }, { "createStatus", status(m.createStatus) }, { "destroyStatus", status(m.destroyStatus) } });
			for (const auto& f : s.functions) functions.push_back({ { "module", f.module }, { "name", f.name }, { "paramSize", f.paramSize }, { "gridZ", f.gridZ },
				{ "createdHandle", f.createdHandle }, { "returnedHandle", f.returnedHandle }, { "activeHandle", f.handle }, { "createStatus", status(f.createStatus) }, { "destroyStatus", status(f.destroyStatus) },
				{ "attempted", f.attempted }, { "successful", f.successful }, { "sampleSuccessful", f.sampleSuccessful } });
			return { { "requested", true }, { "manifest", s.manifest.string() }, { "manifestSha256", s.manifestSha256 }, { "semanticCatalogSha256", s.catalogSha256 },
				{ "batchCount", s.batchCount }, { "device", s.device }, { "failed", s.failed }, { "reason", s.reason.data() }, { "ownershipUncertain", s.uncertain },
				{ "retainedUntilProcessExit", s.retained }, { "retired", s.closed }, { "sampleOpen", s.sampleOpen }, { "qualityQualified", false }, { "performanceQualified", false },
				{ "modules", modules }, { "functions", functions } };
		}
	};
}
