#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "State.h"
#include "Utils/Game.h"
#include "WorldOverlayInternal.h"
#include "WorldOverlayRenderer.h"
#include <algorithm>
#include <limits>

#ifdef _MSC_VER
#	pragma float_control(precise, on, push)
#endif

namespace CSX::WorldOverlays
{
	namespace
	{
		thread_local Scene cameraSource;
		thread_local float2 activeSize{};
		bool ValidMatrix(const Matrix& matrix)
		{
			for (const auto& row : matrix.m)
				for (float value : row)
					if (!std::isfinite(value))
						return false;
			return std::isfinite(matrix.Determinant()) && std::abs(matrix.Determinant()) > 1e-12f;
		}
	}
	bool SceneCaptured(const Scene& scene)
	{
		return scene.valid && scene.completed && scene.epoch == Shared().epoch && scene.cycle == Shared().cycle;
	}
	void CompleteScene() noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(Shared().mutex);
		auto& scene = Shared().scene;
		scene.completed = globals::state && scene.frame == globals::state->lastCompletedWorldRenderFrame &&
		                  !globals::state->isMainMenuOpen && !globals::state->isLoadingMenuOpen && !globals::state->isMapMenuOpen;
	}
	bool SceneCurrent(const Scene& scene)
	{
		return globals::game::isVR && globals::state && SceneCaptured(scene) &&
		       scene.generation == globals::state->GetCompletedRenderTargetResourcePublicationGeneration() &&
		       scene.frame == globals::state->lastCompletedWorldRenderFrame &&
		       !globals::state->isMainMenuOpen && !globals::state->isLoadingMenuOpen && !globals::state->isMapMenuOpen;
	}
	void CaptureCamera() noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_relaxed) || !globals::game::isVR || !globals::state)
			return;
		cameraSource.valid = false;
		if (!globals::state->inWorld || !globals::game::graphicsState)
			return;
		cameraSource.camera = globals::game::frameBufferCached.vr;
		cameraSource.frame = globals::state->frameCount;
		cameraSource.thread = GetCurrentThreadId();
		cameraSource.vendorGeneration = globals::features::upscaling.GetVRVendorEvaluationContractGeneration(globals::features::upscaling.GetRuntimeUpscaleMethod());
		cameraSource.temporal = Util::GetTemporal() || globals::features::upscaling.GetRuntimeUpscaleMethod() != Upscaling::UpscaleMethod::kNONE;
		// screenSize already describes physically resized targets in render-scale mode.
		activeSize = Util::ConvertToDynamic(globals::state->screenSize);
		cameraSource.valid = std::isfinite(activeSize.x) && std::isfinite(activeSize.y) && activeSize.x >= 2 && activeSize.y >= 1;
	}
	void CaptureDepth(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view) noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_relaxed))
			return;
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		s.scene.valid = false;
		if (!texture || !view || !cameraSource.valid || !globals::state || !globals::state->inWorld ||
			cameraSource.frame != globals::state->frameCount || cameraSource.thread != GetCurrentThreadId())
			return;
		D3D11_TEXTURE2D_DESC desc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		texture->GetDesc(&desc);
		view->GetDesc(&viewDesc);
		winrt::com_ptr<ID3D11Resource> source;
		view->GetResource(source.put());
		winrt::com_ptr<ID3D11Device> device;
		texture->GetDevice(device.put());
		if (source.get() != texture || device.get() != globals::d3d::device || desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
			viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || viewDesc.Texture2D.MostDetailedMip != 0 ||
			(viewDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS && viewDesc.Format != DXGI_FORMAT_R32_FLOAT && viewDesc.Format != DXGI_FORMAT_R16_UNORM && viewDesc.Format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS))
			return;
		if (activeSize.x > desc.Width || activeSize.y > desc.Height)
			return;
		Scene next = cameraSource;
		for (std::uint32_t eye = 0; eye < 2; ++eye) {
			next.depthRects[eye] = { eye * static_cast<std::uint32_t>(activeSize.x / 2), 0,
				static_cast<std::uint32_t>(activeSize.x / 2), static_cast<std::uint32_t>(activeSize.y) };
			if (!Policy::Contains(next.depthRects[eye], desc.Width, desc.Height) ||
				!ValidMatrix(next.camera.CameraViewProj[eye]) || !ValidMatrix(next.camera.CameraViewProjUnjittered[eye]) ||
				!ValidMatrix(next.camera.CameraView[eye]) || !ValidMatrix(next.camera.CameraProjInverse[eye]) ||
				!ValidMatrix(next.camera.CameraViewInverse[eye]))
				return;
			next.camera.CameraProjInverse[eye] = (next.camera.CameraViewProj[eye] * next.camera.CameraViewInverse[eye]).Invert();
			if (!ValidMatrix(next.camera.CameraProjInverse[eye]))
				return;
			const auto& origin = next.camera.CameraPosAdjust[eye];
			if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
				return;
		}
		next.epoch = s.epoch;
		next.generation = globals::state->GetCompletedRenderTargetResourcePublicationGeneration();
		next.cycle = s.cycle;
		next.revision = ++s.serial;
		if (!next.generation)
			return;
		next.depth.copy_from(view);
		s.scene = std::move(next);
	}
	void InvalidateDepth() noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(Shared().mutex);
		Shared().scene.valid = false;
	}
	void ResetWorld() noexcept
	{
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		++s.epoch;
		s.scene = {};
		s.pairScene = {};
		s.pairHasContent = false;
		s.syntheticEnabled = false;
		for (auto& c : s.clients) c.Clear();
		UpdatePresence();
	}
	void ReleaseResources() noexcept
	{
		ResetWorld();
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		for (auto& c : s.clients) {
			for (auto& page : c.pages) page = {};
			c.pinned = -1;
			c.leased = -1;
		}
		for (auto& batch : s.pair) batch = {};
		ResetRenderer();
	}
	void BeginCycle(std::uint64_t cycle) noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(Shared().mutex);
		Shared().cycle = cycle;
	}
	void BeginPair(std::uint64_t cycle) noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_acquire))
			return;
		ServiceSynthetic();
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		s.pairHasContent = false;
		s.pairEyes = 0;
		s.acceptedEyes = 0;
		s.pairActive = true;
		if (cycle != s.cycle || !SceneCurrent(s.scene) || s.scene.thread != GetCurrentThreadId()) {
			++s.rejected;
			return;
		}
		s.pairScene = s.scene;
		for (std::size_t i = 0; i < s.clients.size(); ++i) {
			auto& c = s.clients[i];
			auto& batch = s.pair[i];
			batch.count = 0;
			if (!c.token || !c.count || c.committed < 0 || c.epoch != s.scene.epoch ||
				c.generation != s.scene.generation || !Policy::Fresh(c.frame, s.scene.frame))
				continue;
			c.pinned = c.committed;
			batch.atlas = c.pages[c.pinned].srv;
			batch.count = c.count;
			batch.client = c.token;
			batch.sequence = c.sequence;
			batch.clearSerial = c.clearSerial;
			std::copy_n(c.quads.begin(), c.count, batch.quads.begin());
			s.pairHasContent = true;
		}
	}
	void EndPair() noexcept
	{
		if (!Shared().clientsPresent.load(std::memory_order_acquire) && !Shared().pairActive)
			return;
		std::lock_guard lock(Shared().mutex);
		auto& s = Shared();
		if (s.acceptedEyes == 3 && s.pairEyes == 3 && SceneCurrent(s.pairScene)) {
			++s.acceptedPairs;
			for (std::size_t i = 0; i < s.clients.size(); ++i) {
				auto& c = s.clients[i];
				const auto& batch = s.pair[i];
				if (batch.count && c.token == batch.client)
					c.Accept(batch.clearSerial, batch.sequence, s.epoch, s.pairScene.generation, s.pairScene.frame);
			}
		}
		for (auto& c : Shared().clients) c.pinned = -1;
		for (auto& batch : Shared().pair) {
			batch.atlas = nullptr;
			batch.count = 0;
		}
		Shared().pairScene = {};
		Shared().pairActive = false;
		Shared().pairHasContent = false;
		UpdatePresence();
	}
	bool HasContent() noexcept { return Shared().contentPresent.load(std::memory_order_acquire); }
	bool MatchesVendor(std::uint32_t frame, std::uint64_t cycle, std::uint32_t generation, std::uint32_t eye, std::uint32_t width, std::uint32_t height) noexcept
	{
		if (!HasContent() || eye >= 2)
			return false;
		auto& s = Shared();
		std::lock_guard lock(s.mutex);
		const auto& scene = s.pairScene;
		const bool match = s.pairActive && SceneCurrent(scene) && scene.frame == frame && scene.cycle == cycle &&
		                   scene.vendorGeneration == generation && scene.depthRects[eye].width == width && scene.depthRects[eye].height == height;
		if (!match)
			++s.rejected;
		return match;
	}
	void AcceptEye(std::uint32_t eye) noexcept
	{
		if (!HasContent() || eye >= 2)
			return;
		std::lock_guard lock(Shared().mutex);
		if (Shared().pairActive && (Shared().pairEyes & (1u << eye)))
			Shared().acceptedEyes |= 1u << eye;
	}
	void RecordCopy(bool capture) noexcept
	{
		std::lock_guard lock(Shared().mutex);
		if (capture)
			++Shared().captureCopies;
		else
			++Shared().compositionCopies;
	}
	bool EyeDrawn(std::uint32_t eye) noexcept
	{
		if (!HasContent() || eye >= 2)
			return false;
		std::lock_guard lock(Shared().mutex);
		return Shared().pairActive && (Shared().pairEyes & (1u << eye));
	}
	bool CanDrawLocked(std::uint32_t eye)
	{
		const auto& s = Shared();
		return eye < 2 && s.pairActive && s.pairHasContent && !(s.pairEyes & (1u << eye)) &&
		       s.pairScene.thread == GetCurrentThreadId() && SceneCurrent(s.pairScene) &&
		       s.scene.valid && s.scene.revision == s.pairScene.revision;
	}
	bool CanDraw(std::uint32_t eye) noexcept
	{
		auto& s = Shared();
		if (!s.contentPresent.load(std::memory_order_acquire))
			return false;
		std::lock_guard lock(s.mutex);
		return CanDrawLocked(eye);
	}
}

#ifdef _MSC_VER
#	pragma float_control(pop)
#endif
