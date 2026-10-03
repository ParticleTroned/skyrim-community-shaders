#include "Api/ServiceRegistry.h"
#include "Features/VR/WorldOverlayInternal.h"
#include "Features/VR/WorldOverlayRenderer.h"
#include "State.h"
#include "Utils/ResourceName.h"
#include <cstring>

namespace CSX::WorldOverlays
{
	SharedState& Shared()
	{
		static SharedState state;
		return state;
	}
	void UpdatePresence()
	{
		auto& s = Shared();
		bool clients = false, content = s.syntheticEnabled || (s.pairActive && s.pairHasContent);
		for (auto& c : s.clients) {
			clients |= c.token != 0;
			content |= c.token && c.count;
		}
		s.clientsPresent.store(clients, std::memory_order_release);
		s.contentPresent.store(content, std::memory_order_release);
	}

	namespace
	{
		using API::Status;
		ClientState* Find(API::Client token)
		{
			for (auto& c : Shared().clients)
				if (token && c.token == token)
					return &c;
			return nullptr;
		}
		void Capabilities(API::Capabilities001* out, const ClientState* client = nullptr)
		{
			if (!out)
				return;
			*out = {};
			out->worldEpoch = Shared().epoch;
			out->resourceGeneration = Shared().scene.generation;
			out->ready = SceneCaptured(Shared().scene) ? 1 : 0;
			if (client && client->epoch == out->worldEpoch && client->generation == out->resourceGeneration) {
				out->presentedFrame = client->presentedFrame;
				out->presentedSequence = client->presentedSequence;
				out->presentationReady = out->ready && client->ReceiptReady(out->worldEpoch, out->resourceGeneration, Shared().scene.frame);
			}
		}
		bool ValidOutput(API::Capabilities001* out) { return !out || out->structSize >= sizeof(*out); }
		Status Register(const char* name, API::Client* out)
		{
			if (!out || !name || !*name || strnlen_s(name, 129) > 128)
				return Status::InvalidArgument;
			*out = 0;
			if (!REL::Module::IsVR())
				return Status::UnsupportedRuntime;
			auto& s = Shared();
			std::lock_guard lock(s.mutex);
			for (auto& c : s.clients) {
				if (c.token || c.pinned >= 0 || c.leased >= 0)
					continue;
				c = {};
				c.token = ++s.serial;
				*out = c.token;
				UpdatePresence();
				return Status::Success;
			}
			return Status::Busy;
		}
		Status GetCapabilities(API::Capabilities001* out)
		{
			if (!out)
				return Status::InvalidArgument;
			if (!ValidOutput(out))
				return Status::StructureTooSmall;
			std::lock_guard lock(Shared().mutex);
			Capabilities(out);
			return REL::Module::IsVR() ? Status::Success : Status::UnsupportedRuntime;
		}
		Status CheckThread()
		{
			if (!Shared().scene.thread)
				return Status::NotReady;
			if (Shared().scene.thread != GetCurrentThreadId())
				return Status::WrongThread;
			return SceneCurrent(Shared().scene) ? Status::Success : Status::NotReady;
		}
		Status Begin(API::Client token, const API::AtlasRequest001* request, API::AtlasLease001* out, API::Capabilities001* status)
		{
			if (!request || !out)
				return Status::InvalidArgument;
			if (request->structSize < sizeof(*request) || out->structSize < sizeof(*out) || !ValidOutput(status))
				return Status::StructureTooSmall;
			*out = {};
			std::lock_guard lock(Shared().mutex);
			auto* c = Find(token);
			Capabilities(status, c);
			if (!c)
				return Status::InvalidClient;
			const auto check = CheckThread();
			if (check != Status::Success)
				return check;
			if (!Policy::ValidAtlas(request->width, request->height) || request->reserved ||
				request->device != globals::d3d::device || request->immediateContext != globals::d3d::context)
				return Status::InvalidArgument;
			if (c->leased >= 0)
				return Status::Busy;
			if (!EnsureRenderer())
				return Status::ResourceFailure;
			for (int i = 0; i < Policy::AtlasPageCount; ++i) {
				if (!c->CanWrite(i))
					continue;
				auto& page = c->pages[i];
				if (!page.texture || page.width != request->width || page.height != request->height) {
					Atlas next;
					D3D11_TEXTURE2D_DESC desc{};
					desc.Width = request->width;
					desc.Height = request->height;
					desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
					desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					desc.Usage = D3D11_USAGE_DEFAULT;
					desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
					auto* device = globals::d3d::device;
					if (FAILED(device->CreateTexture2D(&desc, nullptr, next.texture.put())))
						return Status::ResourceFailure;
					Util::SetResourceName(next.texture.get(), "WorldOverlays::Atlas");
					if (FAILED(device->CreateRenderTargetView(next.texture.get(), nullptr, next.rtv.put())))
						return Status::ResourceFailure;
					Util::SetResourceName(next.rtv.get(), "WorldOverlays::Atlas RTV");
					if (FAILED(device->CreateShaderResourceView(next.texture.get(), nullptr, next.srv.put())))
						return Status::ResourceFailure;
					Util::SetResourceName(next.srv.get(), "WorldOverlays::Atlas SRV");
					next.width = desc.Width;
					next.height = desc.Height;
					page = std::move(next);
				}
				page.revision = ++Shared().serial;
				c->leased = i;
				c->leaseCanceled = false;
				out->width = page.width;
				out->height = page.height;
				out->target = page.rtv.get();
				out->contentRevision = page.revision;
				out->worldEpoch = Shared().epoch;
				out->resourceGeneration = Shared().scene.generation;
				++Shared().atlasLeases;
				return Status::Success;
			}
			return Status::Busy;
		}
		Status Publish(API::Client token, const API::Batch001* batch, API::Capabilities001* status)
		{
			if (!batch)
				return Status::InvalidArgument;
			if (batch->structSize < sizeof(*batch) || !ValidOutput(status))
				return Status::StructureTooSmall;
			std::lock_guard lock(Shared().mutex);
			auto* c = Find(token);
			Capabilities(status, c);
			if (!c)
				return Status::InvalidClient;
			auto check = CheckThread();
			if (check != Status::Success)
				return check;
			if (batch->worldEpoch != Shared().epoch || batch->resourceGeneration != Shared().scene.generation || batch->sequence <= c->sequence)
				return Status::Stale;
			if (batch->count > API::MaxQuads || (batch->count && !batch->quads))
				return Status::InvalidArgument;
			for (std::uint32_t i = 0; i < batch->count; ++i) {
				if (!Policy::ValidQuad(batch->quads[i]))
					return Status::InvalidArgument;
				for (std::uint32_t j = 0; j < i; ++j)
					if (batch->quads[i].id == batch->quads[j].id)
						return Status::InvalidArgument;
			}
			int page = c->leased;
			if (page < 0 || c->pages[page].revision != batch->contentRevision)
				page = c->committed;
			if (batch->count && (page < 0 || c->pages[page].revision != batch->contentRevision))
				return Status::Stale;
			if (page >= 0 && page == c->leased && c->leaseCanceled)
				return Status::Stale;
			if (page >= 0 && page == c->leased) {
				c->leased = -1;
				++Shared().atlasUpdates;
			}
			c->committed = page;
			c->sequence = batch->sequence;
			c->count = batch->count;
			if (!batch->count)
				c->Clear();
			c->frame = Shared().scene.frame;
			c->epoch = batch->worldEpoch;
			c->generation = batch->resourceGeneration;
			if (batch->count)
				std::copy_n(batch->quads, batch->count, c->quads.begin());
			++Shared().publications;
			UpdatePresence();
			Capabilities(status, c);
			return Status::Success;
		}
		Status Cancel(API::Client token, std::uint64_t revision)
		{
			std::lock_guard lock(Shared().mutex);
			auto* c = Find(token);
			if (!c)
				return Status::InvalidClient;
			if (c->leased < 0 || c->pages[c->leased].revision != revision)
				return Status::Stale;
			c->leased = -1;
			return Status::Success;
		}
		Status Clear(API::Client token)
		{
			std::lock_guard lock(Shared().mutex);
			auto* c = Find(token);
			if (!c)
				return Status::InvalidClient;
			c->Clear();
			UpdatePresence();
			return Status::Success;
		}
		Status Unregister(API::Client token)
		{
			std::lock_guard lock(Shared().mutex);
			auto* c = Find(token);
			if (!c)
				return Status::InvalidClient;
			if (c->leased >= 0)
				return Status::Busy;
			c->token = 0;
			c->count = 0;
			c->leased = -1;
			c->committed = -1;
			UpdatePresence();
			return Status::Success;
		}
	}
	template <auto Function, class... Args>
	API::Status Invoke(Args... args) noexcept
	{
		try {
			return Function(args...);
		} catch (...) {
			return API::Status::ResourceFailure;
		}
	}
	const API::Interface001& Interface()
	{
		static const API::Interface001 api{ sizeof(API::Interface001), API::Major, Invoke<Register, const char*, API::Client*>, Invoke<GetCapabilities, API::Capabilities001*>,
			Invoke<Begin, API::Client, const API::AtlasRequest001*, API::AtlasLease001*, API::Capabilities001*>,
			Invoke<Publish, API::Client, const API::Batch001*, API::Capabilities001*>,
			Invoke<Cancel, API::Client, std::uint64_t>, Invoke<Clear, API::Client>, Invoke<Unregister, API::Client> };
		return api;
	}
	void InitializeService()
	{
		if (!REL::Module::IsVR())
			return;
		const auto result = Api::GetProcessServiceRegistry().Register({ .name = API::ServiceName, .major = API::Major, .minor = 0, .schemaRevision = 1, .capabilities = ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityRuntimeMutation, .interfacePointer = &Interface() });
		if (result != ServiceAPI::Status::kSuccess)
			logger::error("World overlay service registration failed ({})", static_cast<unsigned>(result));
	}
}
