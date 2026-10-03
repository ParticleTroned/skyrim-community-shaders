#pragma once

#include "Globals.h"
#include "WorldOverlayPolicy.h"
#include <array>
#include <atomic>
#include <d3d11_1.h>
#include <mutex>
#include <winrt/base.h>

namespace CSX::WorldOverlays
{
	namespace API = WorldOverlayAPI;
	struct Atlas
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		std::uint32_t width = 0, height = 0;
		std::uint64_t revision = 0;
	};
	struct ClientState : Policy::PublicationState
	{
		API::Client token = 0;
		std::array<Atlas, Policy::AtlasPageCount> pages;
		std::array<API::Quad001, API::MaxQuads> quads;
	};
	struct Scene
	{
		globals::FrameBufferVR camera{};
		winrt::com_ptr<ID3D11ShaderResourceView> depth;
		std::array<Policy::Rect, 2> depthRects;
		std::uint64_t epoch = 0, generation = 0, cycle = 0, revision = 0;
		std::uint32_t frame = 0, thread = 0, vendorGeneration = 0;
		bool valid = false, completed = false, temporal = false;
	};
	struct PairBatch
	{
		API::Client client = 0;
		std::uint64_t sequence = 0, clearSerial = 0;
		winrt::com_ptr<ID3D11ShaderResourceView> atlas;
		std::array<API::Quad001, API::MaxQuads> quads;
		std::uint32_t count = 0;
	};
	struct SharedState
	{
		std::mutex mutex;
		std::array<ClientState, API::MaxClients> clients;
		std::atomic_bool clientsPresent = false, contentPresent = false;
		std::uint64_t epoch = 1, serial = 0, cycle = 0;
		Scene scene, pairScene;
		std::array<PairBatch, API::MaxClients> pair;
		std::uint32_t pairEyes = 0, acceptedEyes = 0;
		bool pairActive = false, pairHasContent = false;
		bool syntheticEnabled = false;
		API::Quad001 synthetic{};
		API::Client syntheticClient = 0;
		std::uint64_t publications = 0, atlasUpdates = 0, atlasLeases = 0, draws = 0, rejected = 0, acceptedPairs = 0, compositionCopies = 0, captureCopies = 0;
	};
	SharedState& Shared();
	bool EnsureRenderer();
	void ResetRenderer();
	bool SceneCurrent(const Scene& scene);
	bool SceneCaptured(const Scene& scene);
	bool CanDrawLocked(std::uint32_t eye);
	void UpdatePresence();
	void ServiceSynthetic();
	const API::Interface001& Interface();
}
