#pragma once

#include "Features/TextureStreaming/Categories.h"
#include "Features/TextureStreaming/TextureData.h"
#include "d3d11_shader_test.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
	using D3D11ShaderTest::Check;
	using Microsoft::WRL::ComPtr;
	namespace Policy = TextureStreamingPolicy;
	namespace SKSE::stl
	{
		template <class F>
		struct scope_exit
		{
			F action;
			explicit scope_exit(F f) : action(std::move(f)) {}
			~scope_exit() { action(); }
		};
	}
	namespace logger
	{
		template <class... T>
		void info(const char*, T&&...)
		{}
	}
	struct Shadow
	{
		std::array<ID3D11ShaderResourceView*, 16> PSTexture{}, CSTexture{};
		unsigned PSResourceModifiedBits = 0, CSResourceModifiedBits = 0;
	};
	struct ShadowState
	{
		Shadow vr, flat;
		Shadow& GetVRRuntimeData() { return vr; }
		Shadow& GetRuntimeData() { return flat; }
	};
	namespace globals
	{
		namespace d3d
		{
			ID3D11DeviceContext* context = nullptr;
		}
		namespace game
		{
			bool isVR = false;
			ShadowState* shadowState = nullptr;
		}
	}
#include "streaming_bindings_under_test.h"
	struct Engine
	{
		ID3D11Resource* texture = nullptr;
		ID3D11ShaderResourceView* resourceView = nullptr;
		std::uint16_t width = 512, height = 512;
		std::uint8_t mips = 10;
		~Engine()
		{
			if (resourceView)
				resourceView->Release();
			if (texture)
				texture->Release();
		}
	};
	struct TextureStreaming
	{
		struct State
		{
			struct Record
			{
				Engine* engine = nullptr;
				ID3D11Resource* current = nullptr;
				ID3D11ShaderResourceView* currentView = nullptr;
				std::array<std::uint64_t, 4> bytes{ 1024, 256, 64, 16 };
				unsigned drop = 1;
				bool protectedConsumer = false;
				std::uint64_t stableSince = 0, retryAfter = 0;
			};
			struct Transaction
			{
				struct Payload
				{
					struct Request
					{
						unsigned drop = 0;
						std::uint64_t serial = 42;
					} request;
					DirectX::ScratchImage image;
				} payload;
				StreamingTextures::Upload upload;
				ComPtr<ID3D11Resource> oldTexture;
				ComPtr<ID3D11ShaderResourceView> oldView;
				ComPtr<ID3D11Query> retirement;
				bool published = false, replacementCommitted = false;
				std::uint64_t logicalReductionBytes = 0;
			};
			std::unordered_set<std::uint64_t> reducedRecords{ 42 };
			unsigned restores = 0, shrinks = 0, failures = 0;
			bool firstPressureReductionPending = false, cancelled = false, enabled = true;
			std::string detail;
			void Fail(const char*) { ++failures; }
			void CancelTransaction() { cancelled = true; }
			void Publish(Record&, Transaction&);
		};
	};
#include "streaming_publication_under_test.h"
	using State = TextureStreaming::State;
	void RequirePublication(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	constexpr std::array getters{
		&ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::HSGetShaderResources,
		&ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::GSGetShaderResources,
		&ID3D11DeviceContext::CSGetShaderResources, &ID3D11DeviceContext::PSGetShaderResources
	};
	constexpr std::array setters{
		&ID3D11DeviceContext::VSSetShaderResources, &ID3D11DeviceContext::HSSetShaderResources,
		&ID3D11DeviceContext::DSSetShaderResources, &ID3D11DeviceContext::GSSetShaderResources,
		&ID3D11DeviceContext::CSSetShaderResources, &ID3D11DeviceContext::PSSetShaderResources
	};
	struct Fixture
	{
		Engine engine;
		ShadowState shadow;
		State state;
		State::Record record;
		State::Transaction work;
		explicit Fixture(ID3D11Device* device, ID3D11DeviceContext* context, unsigned target = 0)
		{
			context->ClearState();
			globals::d3d::context = context;
			globals::game::shadowState = &shadow;
			D3D11_TEXTURE2D_DESC full{ 1024, 1024, 11, 1, DXGI_FORMAT_BC1_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = full.Format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = full.MipLevels;
			StreamingTextures::Upload old;
			Check(old.Begin(device, full, view, 1));
			engine.texture = old.texture.Detach();
			engine.resourceView = old.view.Detach();
			record.engine = &engine;
			record.current = engine.texture;
			record.currentView = engine.resourceView;
			work.payload.request.drop = target;
			Check(work.upload.Begin(device, full, view, target));
			const D3D11_QUERY_DESC query{ D3D11_QUERY_EVENT, 0 };
			Check(device->CreateQuery(&query, &work.retirement));
			Util::SetResourceName(work.retirement.Get(), "TextureStreamingTest::PublicationFence");
			shadow.vr.PSTexture[3] = shadow.flat.PSTexture[3] = engine.resourceView;
			shadow.vr.CSTexture[4] = shadow.flat.CSTexture[4] = engine.resourceView;
		}
		~Fixture()
		{
			globals::d3d::context->ClearState();
			globals::game::shadowState = nullptr;
		}
		void Bind(unsigned stage) { (globals::d3d::context->*setters[stage])(3, 1, &engine.resourceView); }
		void ExpectBound(unsigned stage, ID3D11ShaderResourceView* expected)
		{
			ComPtr<ID3D11ShaderResourceView> observed;
			(globals::d3d::context->*getters[stage])(3, 1, &observed);
			RequirePublication(observed.Get() == expected, "Publication failure changed a live shader binding");
		}
	};
}
void CheckStreamingPublication(ID3D11Device* device, ID3D11DeviceContext* context)
{
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		for (unsigned failure = 0; failure < 3; ++failure) {
			Fixture f(device, context);
			auto* old = f.engine.resourceView;
			for (unsigned stage = 0; stage < setters.size(); ++stage) f.Bind(stage);
			if (failure) {
				Check(StreamingTextures::WriteOrigin(f.engine.texture, { failure == 2 ? 99u : 42u, false, StreamingTextures::LandscapeStatics }));
				if (failure == 1)
					f.work.upload.texture.Reset();
			}
			f.state.Publish(f.record, f.work);
			RequirePublication(f.state.cancelled && f.state.failures == 1 && !f.work.replacementCommitted && f.record.retryAfter > GetTickCount64(), "Provenance failure did not cancel publication");
			RequirePublication(f.engine.resourceView == old && f.state.reducedRecords.contains(42), "Failed publication altered engine ownership or residency bookkeeping");
			for (unsigned stage = 0; stage < getters.size(); ++stage) f.ExpectBound(stage, old);
			RequirePublication(f.shadow.vr.PSTexture[3] == old && f.shadow.flat.PSTexture[3] == old, "Failed publication changed cached shader bindings");
		}
		{
			Fixture f(device, context);
			auto* old = f.engine.resourceView;
			auto* replacement = f.work.upload.view.Get();
			for (unsigned stage = 0; stage < setters.size(); ++stage) f.Bind(stage);
			Check(StreamingTextures::WriteOrigin(f.engine.texture, { 42, false, StreamingTextures::LandscapeStatics }));
			Check(StreamingTextures::WriteOrigin(f.work.upload.texture.Get(), { 42, false, 0 }));
			Check(StreamingTextures::WriteOrigin(f.engine.texture, { 42, false, StreamingTextures::EmissiveStatics }));
			f.record.protectedConsumer = true;
			f.state.Publish(f.record, f.work);
			RequirePublication(f.work.replacementCommitted && !f.state.cancelled && f.engine.resourceView == replacement && !f.state.reducedRecords.contains(42), "Full restoration did not publish atomically");
			RequirePublication(f.work.oldView.Get() == old && f.work.oldTexture && f.engine.width == 1024 && f.engine.mips == 11, "Restoration lost retirement ownership or engine dimensions");
			for (unsigned stage = 0; stage < getters.size(); ++stage) f.ExpectBound(stage, replacement);
			const auto& active = vr ? f.shadow.vr : f.shadow.flat;
			const auto& inactive = vr ? f.shadow.flat : f.shadow.vr;
			RequirePublication(active.PSTexture[3] == replacement && active.CSTexture[4] == replacement && inactive.PSTexture[3] == old, "Publication updated the wrong runtime shadow state");
			StreamingTextures::Origin observed;
			RequirePublication(StreamingTextures::ReadOrigin(f.engine.texture, observed) && observed.serial == 42 && observed.protectedConsumer && observed.requiredCategories == (StreamingTextures::LandscapeStatics | StreamingTextures::EmissiveStatics), "Publication lost consumer requirements discovered during upload");
		}
		{
			Fixture f(device, context, 2);
			auto* replacement = f.work.upload.view.Get();
			(vr ? f.shadow.vr : f.shadow.flat).CSTexture[4] = nullptr;
			f.Bind(5);
			Check(StreamingTextures::WriteOrigin(f.engine.texture, { 42, false, StreamingTextures::AlphaTestedStatics }));
			f.state.Publish(f.record, f.work);
			RequirePublication(f.work.replacementCommitted && !f.state.cancelled && f.engine.width == 256 && f.record.drop == 2 && f.state.reducedRecords.contains(42), "Ordinary pixel-only reduction was blocked");
			f.ExpectBound(5, replacement);
			for (unsigned stage = 0; stage < 5; ++stage)
				f.ExpectBound(stage, nullptr);
		}
		for (unsigned special = 0; special < 6; ++special) {
			Fixture f(device, context, 2);
			f.shadow.vr.CSTexture[4] = f.shadow.flat.CSTexture[4] = nullptr;
			if (special < 5)
				f.Bind(special);
			else
				(vr ? f.shadow.vr : f.shadow.flat).CSTexture[4] = f.engine.resourceView;
			f.Bind(5);
			auto* old = f.engine.resourceView;
			Check(StreamingTextures::WriteOrigin(f.engine.texture, { 42, false, 0 }));
			f.state.Publish(f.record, f.work);
			RequirePublication(f.state.cancelled && f.record.protectedConsumer && !f.work.replacementCommitted, "Special-stage consumer admitted a reduced texture");
			if (special < 5)
				f.ExpectBound(special, old);
			f.ExpectBound(5, old);
			StreamingTextures::Origin observed;
			RequirePublication(StreamingTextures::ReadOrigin(f.engine.texture, observed) && observed.protectedConsumer, "Special-stage protection did not survive inventory removal");
		}
	}
}
