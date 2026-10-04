#include "KernelReplacementSet.h"

#include <Windows.h>

#include <iostream>

namespace
{
	using namespace NrReplay::KernelReplacement;
	using Json = nlohmann::json;
	int checks = 0;
	void Check(bool result)
	{
		++checks;
		if (!result)
			throw std::runtime_error("replacement set check failed: " + std::to_string(checks));
	}
	template <class Callback>
	void Reject(Callback&& call)
	{
		bool rejected = false;
		try {
			call();
		} catch (const std::exception&) {
			rejected = true;
		}
		Check(rejected);
	}
	std::string Digest(std::span<const std::uint8_t> bytes)
	{
		return Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(std::as_bytes(bytes)));
	}
	struct Fixture
	{
		std::filesystem::path directory, path;
		Json manifest;
		std::string fingerprint;
		explicit Fixture(unsigned batch = 1)
		{
			static unsigned serial = 0;
			directory = std::filesystem::absolute("build/validation/nr-replacement-set-cpu") / (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial));
			Check(std::filesystem::create_directories(directory));
			path = directory / "manifest.json";
			manifest = { { "schema", "nr-model-kernel-replacement-v1" }, { "batchCount", batch }, { "modules", Json::array() } };
			for (unsigned m = 0; m < 9; ++m) {
				std::vector<std::uint8_t> bytes(64, static_cast<std::uint8_t>(m));
				bytes[0] = 0x7f;
				bytes[1] = 'E';
				bytes[2] = 'L';
				bytes[3] = 'F';
				const auto file = directory / ("module-" + std::to_string(m) + ".cubin");
				std::ofstream output(file, std::ios::binary);
				output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				Json functions = Json::array();
				for (unsigned f = 0; f < (m == 8 ? 4u : 5u); ++f)
					functions.push_back({ { "name", "entry" + std::to_string(f) }, { "paramSize", 72u }, { "gridZ", 4u } });
				manifest["modules"].push_back({ { "originalSha256", std::string(63, '0') + char('1' + m) }, { "path", file.string() }, { "sha256", Digest(bytes) }, { "functions", functions } });
			}
			Save();
		}
		void Save()
		{
			Json semantic = manifest;
			semantic.erase("schema");
			for (auto& m : semantic["modules"]) m.erase("path");
			fingerprint = Util::CryptoHash::Sha256Hex(semantic.dump());
			std::ofstream(path) << manifest.dump(2);
		}
	};
	struct FakeState
	{
		std::shared_ptr<Device> device = std::make_shared<Device>(100);
		std::vector<std::string> calls;
		std::vector<const void*> byteOwners;
		std::vector<std::pair<const char*, std::string>> nameOwners;
		Handle next = 10000, forcedModule = 0, forcedFunction = 0;
		int moduleStatus = 0, functionStatus = 0, destroyStatus = 0;
		bool mutateBytes = false, mutateName = false, throwModule = false, throwDestroy = false;
	};
	struct Fake
	{
		using DeviceOwner = std::shared_ptr<Device>;
		std::shared_ptr<FakeState> state;
		DeviceOwner RetainDevice(Device device)
		{
			Check(device == *state->device);
			return state->device;
		}
		Device DeviceIdentity(const DeviceOwner& owner) { return owner ? *owner : 0; }
		NativeResult CreateModule(Device device, const void* bytes, std::uint32_t size)
		{
			Check(device == *state->device && bytes && size == 64);
			state->calls.push_back("create-module");
			state->byteOwners.push_back(bytes);
			if (state->throwModule)
				throw std::runtime_error("native callback threw");
			if (state->mutateBytes)
				const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(bytes))[10] ^= 1;
			return { state->moduleStatus, state->forcedModule ? state->forcedModule : state->next++ };
		}
		NativeResult CreateFunction(Device device, Handle module, const char* name)
		{
			Check(device == *state->device && module && name);
			state->calls.push_back("create-function");
			state->nameOwners.emplace_back(name, name);
			if (state->mutateName)
				const_cast<char*>(name)[0] = 'X';
			return { state->functionStatus, state->forcedFunction ? state->forcedFunction : state->next++ };
		}
		int DestroyFunction(Device device, Handle function)
		{
			Check(device == *state->device && function);
			state->calls.push_back("destroy-function");
			if (state->throwDestroy)
				throw std::runtime_error("destroy threw");
			return state->destroyStatus;
		}
		int DestroyModule(Device device, Handle module)
		{
			Check(device == *state->device && module);
			state->calls.push_back("destroy-module");
			return state->destroyStatus;
		}
	};
	using Subject = Set<Fake>;
	std::string Original(const Fixture& f, unsigned module = 0) { return f.manifest["modules"][module]["originalSha256"].get<std::string>(); }
	void Observe(Subject& set, const Fixture& f, unsigned module = 0, unsigned function = 0)
	{
		set.ObserveOriginalModule(100, 1000 + module, Original(f, module));
		set.ObserveOriginalFunction(100, 1000 + module, 2000 + module * 10 + function, "entry" + std::to_string(function));
	}
	Binding Resolve(Subject& set, const Fixture& f, unsigned module = 0, unsigned function = 0)
	{
		return set.Resolve(100, 2000 + module * 10 + function, Original(f, module), "entry" + std::to_string(function), 72, 4);
	}
	void Happy(unsigned batch)
	{
		Fixture f(batch);
		auto fake = std::make_shared<FakeState>();
		Subject set(f.path, f.fingerprint, batch, Fake{ fake });
		Check(fake->calls.empty());
		for (unsigned sample = 0; sample < 2; ++sample) {
			set.BeginSample();
			for (unsigned m = 0; m < 9; ++m)
				for (unsigned k = 0; k < (m == 8 ? 4u : 5u); ++k) {
					Observe(set, f, m, k);
					const auto binding = Resolve(set, f, m, k);
					Check(binding.function != 2000 + m * 10 + k && binding.paramSize == 72 && binding.gridZ == 4 && binding.batchCount == batch);
					Check(binding.packetStride == (batch == 1 ? 72u : 80u) && binding.candidateParameterBytes == (batch == 1 ? 72u : 160u));
					set.RecordSubmission(binding.index, 0);
				}
			set.RequireCompleteCoverage();
		}
		Check(fake->calls.size() == 53 && fake->device.use_count() == 2);
		for (const auto& [pointer, name] : fake->nameOwners) Check(std::string(pointer) == name);
		Check(set.Healthy() && set.Retire(true, true) && set.Retire(true, true));
		Check(fake->calls.size() == 106 && fake->device.use_count() == 1);
		for (std::size_t i = 53; i < 97; ++i) Check(fake->calls[i] == "destroy-function");
		for (std::size_t i = 97; i < 106; ++i) Check(fake->calls[i] == "destroy-module");
		const auto receipt = set.Receipt();
		Check(receipt["retired"] == true && receipt["retainedUntilProcessExit"] == false && receipt["failed"] == false);
		for (const auto& function : receipt["functions"]) Check(function["successful"] == 2 && function["activeHandle"] == 0 && function["destroyStatus"] == 0);
		Reject([&] { Resolve(set, f); });
	}
	void Admission()
	{
		for (unsigned variant = 0; variant < 12; ++variant) {
			Fixture f;
			auto fake = std::make_shared<FakeState>();
			if (variant == 0)
				f.manifest["extra"] = 1;
			if (variant == 1)
				f.manifest["modules"].erase(0);
			if (variant == 2)
				f.manifest["modules"][0]["functions"].erase(0);
			if (variant == 3)
				f.manifest["modules"][1]["originalSha256"] = f.manifest["modules"][0]["originalSha256"];
			if (variant == 4)
				f.manifest["modules"][0]["functions"][0]["paramSize"] = 4097;
			if (variant == 5)
				f.manifest["modules"][0]["functions"][0]["gridZ"] = 65536;
			if (variant == 6)
				f.manifest["modules"][0]["functions"][0]["name"] = "bad\nname";
			if (variant == 7)
				f.manifest["modules"][0]["path"] = "relative.cubin";
			if (variant == 8)
				f.manifest["modules"][0]["sha256"] = std::string(64, 'a');
			if (variant == 9)
				f.manifest["batchCount"] = 2;
			if (variant == 10)
				f.manifest["modules"][0]["functions"][0]["paramSize"] = -1;
			f.Save();
			if (variant == 11)
				f.fingerprint = std::string(64, '0');
			Reject([&] { Subject set(f.path, f.fingerprint, 1, Fake{ fake }); });
			Check(fake->calls.empty());
		}
	}
	void CatalogChoices()
	{
		Fixture f;
		const std::string absent(64, '0');
		for (unsigned variant = 0; variant < 4; ++variant) {
			auto fake = std::make_shared<FakeState>();
			std::array<std::string_view, 9> accepted;
			accepted.fill(absent);
			std::size_t count = 2;
			if (variant == 0)
				count = 0;
			if (variant == 1)
				count = accepted.size();
			if (variant == 3) {
				accepted[0] = f.fingerprint;
				accepted[1] = "invalid";
			}
			Reject([&] { Subject set(f.path, std::span<const std::string_view>(accepted).first(count), 1, Fake{ fake }); });
			Check(fake->calls.empty());
		}
		auto fake = std::make_shared<FakeState>();
		const std::array<std::string_view, 2> accepted{ absent, f.fingerprint };
		Subject set(f.path, accepted, 1, Fake{ fake });
		Check(set.Receipt()["semanticCatalogSha256"] == f.fingerprint && fake->calls.empty());
		Check(set.Retire(true, true));
	}
	void Preparation()
	{
		for (unsigned prepared = 0; prepared < 2; ++prepared) {
			Fixture f(2);
			auto fake = std::make_shared<FakeState>();
			Subject set(f.path, f.fingerprint, 2, Fake{ fake });
			if (prepared) {
				Observe(set, f);
				Resolve(set, f);
			}
			Reject([&] { set.RequirePrepared(); });
			Check(!set.Healthy() && set.Retire(true, true));
		}
		Fixture f(2);
		auto fake = std::make_shared<FakeState>();
		Subject set(f.path, f.fingerprint, 2, Fake{ fake });
		for (unsigned m = 0; m < 9; ++m)
			for (unsigned k = 0; k < (m == 8 ? 4u : 5u); ++k) {
				Observe(set, f, m, k);
				Resolve(set, f, m, k);
			}
		const auto before = fake->calls.size();
		set.RequirePrepared();
		set.RequirePrepared();
		Check(before == 53 && fake->calls.size() == before && set.Healthy());
		const auto receipt = set.Receipt();
		Check(receipt["sampleOpen"] == false);
		for (const auto& function : receipt["functions"])
			Check(function["attempted"] == 0 && function["successful"] == 0 && function["sampleSuccessful"] == 0);
		Check(set.Retire(true, true));
	}
	void Failures()
	{
		for (unsigned variant = 0; variant < 13; ++variant) {
			Fixture f;
			auto fake = std::make_shared<FakeState>();
			Subject set(f.path, f.fingerprint, 1, Fake{ fake });
			Observe(set, f);
			if (variant == 0)
				fake->moduleStatus = -3;
			if (variant == 1)
				fake->functionStatus = -4;
			if (variant == 2)
				fake->forcedModule = 1000;
			if (variant == 3)
				fake->forcedFunction = 2000;
			if (variant == 4)
				fake->forcedFunction = 10000;
			if (variant == 5)
				fake->throwModule = true;
			if (variant == 6)
				fake->mutateBytes = true;
			if (variant == 7)
				fake->mutateName = true;
			if (variant < 8)
				Reject([&] { Resolve(set, f); });
			else {
				const auto binding = Resolve(set, f);
				if (variant == 8)
					Reject([&] { set.ObserveOriginalModule(100, binding.function, Original(f)); });
				if (variant == 9)
					Reject([&] { set.ObserveOriginalFunction(100, 1000, binding.function, "foreign"); });
				if (variant == 10) {
					Observe(set, f, 1, 0);
					fake->forcedModule = 10000;
					Reject([&] { Resolve(set, f, 1, 0); });
				}
				if (variant == 11) {
					Observe(set, f, 0, 1);
					fake->forcedFunction = binding.function;
					Reject([&] { Resolve(set, f, 0, 1); });
				}
				if (variant == 12) {
					fake->destroyStatus = -5;
					Check(!set.Retire(true, true));
				}
			}
			Check(!set.Healthy());
			const auto prior = fake->calls.size();
			const bool knownOwnership = variant == 6 || variant == 7;
			Check(set.Retire(true, true) == knownOwnership);
			Check(set.Receipt()["retainedUntilProcessExit"] == !knownOwnership);
			if (!knownOwnership)
				Check(fake->calls.size() == prior && fake->device.use_count() == 2);
		}
	}
	void SubmissionCounts()
	{
		Fixture f(2);
		auto fake = std::make_shared<FakeState>();
		Subject set(f.path, f.fingerprint, 2, Fake{ fake });
		Observe(set, f);
		Observe(set, f, 1, 1);
		const auto first = Resolve(set, f);
		const auto second = Resolve(set, f, 1, 1);
		const auto creationCalls = fake->calls.size();
		std::array<std::size_t, kFunctionCount> expected{};
		set.BeginSample();
		set.RecordSubmission(first.index, 0);
		set.RecordSubmission(first.index, 0);
		set.RecordSubmission(second.index, 0);
		expected[first.index] = 2;
		expected[second.index] = 1;
		set.RequireSubmissionCounts(expected);
		Check(set.Healthy() && set.Receipt()["sampleOpen"] == false);
		set.BeginSample();
		set.RecordSubmission(second.index, 0);
		expected[first.index] = 0;
		set.RequireSubmissionCounts(expected);
		Check(set.Healthy() && set.Receipt()["sampleOpen"] == false && fake->calls.size() == creationCalls);
		Check(set.Receipt()["functions"][first.index]["sampleSuccessful"] == 0);
		Check(set.Receipt()["functions"][second.index]["sampleSuccessful"] == 1);
		Check(set.Retire(true, true));
		for (unsigned variant = 0; variant < 8; ++variant) {
			auto rejectedFake = std::make_shared<FakeState>();
			Subject rejected(f.path, f.fingerprint, 2, Fake{ rejectedFake });
			Observe(rejected, f);
			Observe(rejected, f, 1, 1);
			const auto a = Resolve(rejected, f);
			const auto b = Resolve(rejected, f, 1, 1);
			std::vector<std::size_t> counts(kFunctionCount, 0);
			counts[a.index] = 1;
			if (variant != 0) {
				rejected.BeginSample();
				rejected.RecordSubmission(a.index, 0);
			}
			if (variant == 1)
				counts.pop_back();
			if (variant == 2)
				counts.push_back(0);
			if (variant == 3)
				counts[a.index] = 0;
			if (variant == 4)
				counts[a.index] = 2;
			if (variant == 5)
				rejected.RecordSubmission(b.index, 0);
			if (variant == 6)
				rejected.RecordSubmission(a.index, 0);
			if (variant == 7)
				rejected.RequireSubmissionCounts(counts);
			Reject([&] { rejected.RequireSubmissionCounts(counts); });
			Check(!rejected.Healthy());
			Check(rejected.Receipt()["sampleOpen"] == (variant != 0 && variant != 7));
			Check(rejected.Retire(true, true));
		}
	}
	void Lifecycle()
	{
		for (unsigned variant = 0; variant < 8; ++variant) {
			Fixture f;
			auto fake = std::make_shared<FakeState>();
			{
				Subject set(f.path, f.fingerprint, 1, Fake{ fake });
				Observe(set, f);
				const auto binding = Resolve(set, f);
				if (variant == 0)
					Check(!set.Retire(false, true));
				if (variant == 1)
					Check(!set.Retire(true, false));
				if (variant == 2) {
					fake->throwDestroy = true;
					Check(!set.Retire(true, true));
				}
				if (variant == 3) {
					set.BeginSample();
					set.RecordSubmission(binding.index, 0);
					Reject([&] { set.RequireCompleteCoverage(); });
					Check(set.Retire(true, true));
				}
				if (variant == 4) {
					set.BeginSample();
					Reject([&] { set.RecordSubmission(binding.index, -1); });
					Check(set.Retire(true, true));
				}
				if (variant == 5) {
					Reject([&] { set.Resolve(101, 2000, Original(f), "entry0", 72, 4); });
					Check(set.Retire(true, true));
				}
				if (variant == 6) {
					std::thread foreign([&] { Reject([&] { set.Resolve(100, 2000, Original(f), "entry0", 72, 4); }); });
					foreign.join();
					Check(set.Retire(true, true));
				}
			}
			Check(fake->device.use_count() == ((variant <= 2 || variant == 7) ? 2 : 1));
		}
		Fixture f;
		auto fake = std::make_shared<FakeState>();
		Subject set(f.path, f.fingerprint, 1, Fake{ fake });
		Reject([&] { Resolve(set, f); });
		Check(fake->calls.empty() && set.Retire(true, true));
	}
}

int main(int argc, char** argv)
{
	try {
		Happy(1);
		Happy(2);
		Admission();
		CatalogChoices();
		Preparation();
		SubmissionCounts();
		Failures();
		Lifecycle();
		if (argc == 3) {
			auto fake = std::make_shared<FakeState>();
			Subject catalog(argv[1], argv[2], 1, Fake{ fake });
			Check(catalog.Receipt()["functions"].size() == 44 && catalog.Receipt()["modules"].size() == 9 && fake->calls.empty());
			Check(catalog.Retire(true, true));
		}
		std::cout << checks << " replacement-set CPU checks passed; no native GPU API invoked\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
