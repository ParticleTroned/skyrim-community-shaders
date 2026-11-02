#include "reverse_z_test_stub.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Shader blocking assertion at line " + std::to_string(a_location.line()));
	}
}

namespace RE
{
	namespace BSGraphics
	{
		struct VertexShader
		{};
		struct PixelShader
		{};
		struct ComputeShader
		{};
	}
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Total
		};
		struct TypeValue
		{
			Type get() const { return Type::Lighting; }
			unsigned underlying() const { return 0; }
		} shaderType;
		const char* fxpFilename = "Lighting";
		bool supported = true;
	};
}

namespace logger
{
	unsigned messages{};
	template <class... Args>
	void debug(const char*, Args&&...)
	{
		++messages;
	}
}

struct TestState
{
	bool developer{}, enableVShaders = true, enablePShaders = true, enableCShaders = true;
	bool shaderEnabled = true;
	bool deferredPixelShaderMissing = true;
	bool IsDeveloperMode() const { return developer; }
	bool IsShaderEnabled(const RE::BSShader&) const { return shaderEnabled; }
};
struct TestDeferred
{
	bool deferredPass = false;
	void InvalidateCharacterCategoryCapture() { throw std::runtime_error("Unexpected category fallback"); }
};
namespace globals
{
	TestState* state;
	TestDeferred* deferred;
	namespace game
	{
		bool isVR{};
	}
}

namespace SIE
{
	enum class ShaderClass
	{
		Vertex,
		Pixel,
		Compute
	};
	unsigned formats{}, tracks{}, resolutions{}, compiles{}, queued{};
	ShaderClass expectedStage;
	std::uint32_t expectedDescriptor{};
	bool descriptorValid = true;
	std::uint32_t descriptorOffset{};
	std::string RequestedKey() { return std::to_string(static_cast<unsigned>(expectedStage)) + ":" + std::to_string(expectedDescriptor); }
	namespace SShaderCache
	{
		bool ResolveImageSpaceDescriptor(const RE::BSShader&, std::uint32_t& descriptor)
		{
			++resolutions;
			descriptor += descriptorOffset;
			return descriptorValid;
		}
		std::string GetShaderString(ShaderClass stage, const RE::BSShader&, std::uint32_t descriptor, bool defines)
		{
			Require(stage == expectedStage && descriptor == expectedDescriptor && defines);
			Require(tracks == 1 && resolutions == 1);
			++formats;
			return RequestedKey();
		}
	}
	struct ShaderCache
	{
		enum class VertexShaderLookup
		{
			Compile,
			Draw
		};
		enum class PixelShaderLookup
		{
			Compile,
			Cached,
			Draw
		};
		template <class T>
		using Maps = std::array<std::unordered_map<std::uint32_t, std::unique_ptr<T>>, 1>;
		Maps<RE::BSGraphics::VertexShader> vertexShaders;
		Maps<RE::BSGraphics::PixelShader> pixelShaders;
		Maps<RE::BSGraphics::ComputeShader> computeShaders;
		std::mutex vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		std::atomic<unsigned> activeShaderCaptureFramesRemaining{};
		int blockedKeyIndex = -1;
		std::string blockedKey;
		std::vector<std::uint32_t> blockedIDs;
		struct Task
		{
			ShaderClass stage;
			const RE::BSShader& shader;
			std::uint32_t descriptor;
		};
		struct Queue
		{
			void Add(Task) { ++queued; }
		} compilationSet;
		bool async = true, safeMode = false;
		static bool IsSupportedShader(const RE::BSShader& shader) { return shader.supported; }
		static unsigned GetCharacterCategoryFlag(RE::BSShader::Type) { return 0; }
		bool IsSaveLoadSafeModeActive() const { return safeMode; }
		bool ShouldUseAsyncCompilation() const { return async; }
		void TrackActiveShader(ShaderClass stage, const RE::BSShader&, std::uint32_t descriptor)
		{
			Require(stage == expectedStage && descriptor == expectedDescriptor && resolutions == 1);
			++tracks;
		}
		RE::BSGraphics::VertexShader* MakeAndAddVertexShader(const RE::BSShader&, std::uint32_t)
		{
			++compiles;
			return nullptr;
		}
		RE::BSGraphics::PixelShader* MakeAndAddPixelShader(const RE::BSShader&, std::uint32_t)
		{
			++compiles;
			return nullptr;
		}
		RE::BSGraphics::ComputeShader* MakeAndAddComputeShader(const RE::BSShader&, std::uint32_t)
		{
			++compiles;
			return nullptr;
		}
		RE::BSGraphics::VertexShader* GetVertexShaderIfCached(const RE::BSShader&, std::uint32_t);
		RE::BSGraphics::VertexShader* GetVertexShader(const RE::BSShader&, std::uint32_t, VertexShaderLookup = VertexShaderLookup::Draw);
		RE::BSGraphics::PixelShader* GetPixelShader(const RE::BSShader&, std::uint32_t, PixelShaderLookup);
		RE::BSGraphics::ComputeShader* GetComputeShader(const RE::BSShader&, std::uint32_t);
	};
#include "shader_block_pixel_compute_under_test.h"
#include "shader_block_vertex_under_test.h"
}

namespace
{
	using Stage = SIE::ShaderClass;
	using Lookup = SIE::ShaderCache::PixelShaderLookup;
	struct Fixture
	{
		TestState state;
		TestDeferred deferred;
		SIE::ShaderCache cache;
		RE::BSShader shader;
		static constexpr std::uint32_t descriptor = 42;
		Fixture()
		{
			globals::state = &state;
			globals::deferred = &deferred;
			cache.vertexShaders[0][descriptor] = std::make_unique<RE::BSGraphics::VertexShader>();
			cache.pixelShaders[0][descriptor] = std::make_unique<RE::BSGraphics::PixelShader>();
			cache.computeShaders[0][descriptor] = std::make_unique<RE::BSGraphics::ComputeShader>();
			SIE::expectedDescriptor = descriptor;
			SIE::descriptorValid = true;
			SIE::descriptorOffset = 0;
		}
		const void* Resident(Stage stage) const
		{
			switch (stage) {
			case Stage::Vertex:
				return cache.vertexShaders[0].at(descriptor).get();
			case Stage::Pixel:
				return cache.pixelShaders[0].at(descriptor).get();
			case Stage::Compute:
				return cache.computeShaders[0].at(descriptor).get();
			}
			throw std::runtime_error("Unknown shader stage");
		}
		const void* Get(Stage stage, Lookup lookup)
		{
			SIE::formats = SIE::tracks = SIE::resolutions = SIE::compiles = SIE::queued = logger::messages = 0;
			SIE::expectedStage = stage;
			const auto requested = descriptor - SIE::descriptorOffset;
			switch (stage) {
			case Stage::Vertex:
				return cache.GetVertexShader(shader, requested, lookup == Lookup::Compile ? SIE::ShaderCache::VertexShaderLookup::Compile : SIE::ShaderCache::VertexShaderLookup::Draw);
			case Stage::Pixel:
				return cache.GetPixelShader(shader, requested, lookup);
			case Stage::Compute:
				return cache.GetComputeShader(shader, requested);
			}
			throw std::runtime_error("Unknown shader stage");
		}
		void Check(Stage stage, Lookup lookup, bool blocked, unsigned formats)
		{
			state.deferredPixelShaderMissing = true;
			Require(Get(stage, lookup) == (blocked ? nullptr : Resident(stage)));
			Require(SIE::formats == formats && SIE::compiles == 0 && SIE::queued == 0);
			Require(SIE::tracks == unsigned(state.developer || cache.activeShaderCaptureFramesRemaining > 0));
			Require(state.deferredPixelShaderMissing == !(stage == Stage::Pixel && lookup == Lookup::Draw));
		}
		void CheckRejected(Stage stage, Lookup lookup, unsigned resolutions)
		{
			state.deferredPixelShaderMissing = true;
			Require(Get(stage, lookup) == nullptr);
			Require(SIE::resolutions == resolutions && SIE::tracks == 0 && SIE::formats == 0 && SIE::compiles == 0 && SIE::queued == 0);
			Require(cache.blockedIDs.empty() && logger::messages == 0);
			Require(state.deferredPixelShaderMissing == !(stage == Stage::Pixel && lookup == Lookup::Draw));
		}
	};

	void TestLookupScheduling()
	{
		for (bool reversed : { false, true })
			for (bool async : { false, true })
				for (bool safeMode : { false, true })
					for (auto stage : { Stage::Vertex, Stage::Pixel })
						for (auto lookup : { Lookup::Compile, Lookup::Draw }) {
							Fixture f;
							globals::features::reverseZ.active = reversed;
							f.cache.async = async;
							f.cache.safeMode = safeMode;
							f.Check(stage, lookup, false, 0);
							f.cache.vertexShaders[0].clear();
							f.cache.pixelShaders[0].clear();
							Require(!f.Get(stage, lookup));
							const bool queue = async && (!reversed || lookup == Lookup::Compile);
							Require(SIE::queued == unsigned(!safeMode && queue));
							Require(SIE::compiles == unsigned(!safeMode && !queue));
							if (stage == Stage::Pixel) {
								Require(!f.Get(stage, Lookup::Cached));
								Require(SIE::queued == 0 && SIE::compiles == 0);
							}
						}
		globals::features::reverseZ.active = false;
	}

	void TestEligibilityBeforeBlocking(Stage stage, Lookup lookup)
	{
		Fixture disabled;
		disabled.state.developer = true;
		disabled.cache.blockedKeyIndex = -2;
		SIE::expectedStage = stage;
		disabled.cache.blockedKey = SIE::RequestedKey();
		bool& enabled = stage == Stage::Vertex ? disabled.state.enableVShaders :
		                stage == Stage::Pixel  ? disabled.state.enablePShaders :
		                                         disabled.state.enableCShaders;
		enabled = false;
		disabled.CheckRejected(stage, lookup, stage == Stage::Vertex ? 1 : 0);
		enabled = true;
		disabled.Check(stage, lookup, true, 1);

		for (const bool developer : { false, true }) {
			for (const bool shaderEnabled : { false, true }) {
				Fixture unsupported;
				unsupported.shader.supported = false;
				unsupported.state.developer = developer;
				unsupported.state.shaderEnabled = shaderEnabled;
				unsupported.cache.blockedKeyIndex = -2;
				unsupported.cache.blockedKey = SIE::RequestedKey();
				if (developer && shaderEnabled)
					unsupported.Check(stage, lookup, true, 1);
				else
					unsupported.CheckRejected(stage, lookup, stage == Stage::Vertex ? 1 : 0);
			}
		}

		Fixture occlusion;
		occlusion.shader.fxpFilename = "OBBOcclusionTesting";
		occlusion.state.developer = true;
		occlusion.cache.blockedKeyIndex = -2;
		occlusion.cache.blockedKey = SIE::RequestedKey();
		if (globals::game::isVR && stage != Stage::Compute)
			occlusion.CheckRejected(stage, lookup, stage == Stage::Vertex ? 1 : 0);
		else
			occlusion.Check(stage, lookup, true, 1);

		Fixture invalid;
		invalid.state.developer = true;
		invalid.cache.blockedKeyIndex = -2;
		invalid.cache.blockedKey = SIE::RequestedKey();
		SIE::descriptorValid = false;
		invalid.CheckRejected(stage, lookup, 1);
	}

	void TestBlocking(Stage stage, Lookup lookup)
	{
		for (const bool developer : { false, true }) {
			for (const int index : { -1, 0, -2 }) {
				for (const int keyKind : { 0, 1, 2 }) {
					Fixture f;
					SIE::expectedStage = stage;
					f.state.developer = developer;
					f.cache.blockedKeyIndex = index;
					f.cache.blockedKey = keyKind == 0 ? "" : keyKind == 1 ? "different" :
					                                                        SIE::RequestedKey();
					const bool eligible = developer && index != -1 && keyKind != 0;
					const bool blocked = eligible && keyKind == 2;
					f.Check(stage, lookup, blocked, unsigned(eligible));
					Require(f.cache.blockedIDs.size() == unsigned(blocked));
					Require(logger::messages == unsigned(blocked));
					if (blocked) {
						Require(f.cache.blockedIDs.front() == f.descriptor);
						f.Check(stage, lookup, true, 1);
						Require(f.cache.blockedIDs.size() == 1 && logger::messages == 0);
					}
				}
			}
		}

		for (unsigned clear = 0; clear < 3; ++clear) {
			Fixture f;
			SIE::expectedStage = stage;
			f.state.developer = true;
			f.cache.blockedKeyIndex = -2;
			f.cache.blockedKey = SIE::RequestedKey();
			SIE::descriptorOffset = 1;
			f.Check(stage, lookup, true, 1);
			if (clear == 0)
				f.cache.blockedKeyIndex = -1;
			if (clear == 1)
				f.cache.blockedKey.clear();
			if (clear == 2)
				f.state.developer = false;
			f.Check(stage, lookup, false, 0);
			Require(f.cache.blockedIDs.size() == 1);
		}

		Fixture capture;
		capture.cache.activeShaderCaptureFramesRemaining = 1;
		capture.Check(stage, lookup, false, 0);
		SIE::descriptorValid = false;
		Require(capture.Get(stage, lookup) == nullptr && SIE::tracks == 0 && SIE::formats == 0);
	}
}

int main()
{
	try {
		for (const bool vr : { false, true }) {
			globals::game::isVR = vr;
			TestLookupScheduling();
			for (const auto stage : { Stage::Vertex, Stage::Pixel, Stage::Compute }) {
				for (const auto lookup : { Lookup::Compile, Lookup::Cached, Lookup::Draw }) {
					TestBlocking(stage, lookup);
					TestEligibilityBeforeBlocking(stage, lookup);
				}
			}
		}
		std::cout << "Shader blocking matrix passed for all stages, lookup modes and flat/VR routes\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
