#pragma once

#include "PointShadowCorrectionMode.h"

#include <array>
#include <d3d11_1.h>
#include <utility>
#include <winrt/base.h>

namespace ParaboloidShadow
{
	using Mode = PointShadowCorrection::Mode;
	inline constexpr GUID ClipOnlyShaderKey{ 0xf4134bb6, 0xc9e5, 0x4a08, { 0x90, 0xd3, 0x75, 0x93, 0x40, 0x5d, 0xce, 0xb6 } };

	/** The matching vertex object owns the prepared clip stage through cache eviction. */
	inline HRESULT Attach(ID3D11VertexShader& vertex, ID3D11GeometryShader* geometry)
	{
		return vertex.SetPrivateDataInterface(ClipOnlyShaderKey, geometry);
	}

	/** Exact resident pairing; never compiles, queues work or consults a mutable shader map. */
	inline winrt::com_ptr<ID3D11GeometryShader> Retain(ID3D11VertexShader* vertex)
	{
		winrt::com_ptr<ID3D11GeometryShader> geometry;
		UINT size = sizeof(ID3D11GeometryShader*);
		if (vertex && FAILED(vertex->GetPrivateData(ClipOnlyShaderKey, &size, geometry.put())))
			geometry = nullptr;
		return geometry;
	}

	/** Restricts clipping to native point-shadow passes and restores nested graphics state. */
	class Scope
	{
	public:
		explicit Scope(ID3D11DeviceContext* context, Mode mode) : ctx(context), previous(std::exchange(current, this)), enabled(mode == Mode::ClipOnly)
		{
			if (previous && previous->bound) {
				Capture();
				if (saved == previous->bound) {
					ctx->GSSetShader(nullptr, nullptr, 0);
					changed = true;
				}
			}
		}
		~Scope()
		{
			if (buffersSaved)
				RestoreBuffers();
			if (changed)
				ctx->GSSetShader(saved.get(), nullptr, 0);
			current = previous;
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

		/** Follow every vertex selection, including native and unrelated shader fallbacks. */
		static void Bind(ID3D11VertexShader* vertex)
		{
			if (!current || !current->enabled)
				return;
			auto& scope = *current;
			if (vertex == scope.lastVertex.get())
				return;
			scope.lastVertex.copy_from(vertex);
			auto geometry = Retain(vertex);
			if (geometry) {
				scope.Capture();
				// A foreign geometry stage retains ownership of this pass.
				if (scope.saved && (!scope.previous || scope.saved != scope.previous->bound)) {
					scope.enabled = false;
					return;
				}
			}
			if (scope.bound != geometry) {
				scope.ctx->GSSetShader(geometry.get(), nullptr, 0);
				scope.bound = std::move(geometry);
				scope.changed = true;
			}
		}

		/** Copy final VS constant-buffer windows after the engine's dirty-state flush. */
		static void SyncConstants(bool compute)
		{
			if (compute || !current || !current->bound)
				return;
			auto& scope = *current;
			if (!scope.buffersSaved) {
				scope.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), scope.ctx1.put_void());
				for (size_t i = 0; i < slots.size(); ++i) {
					if (scope.ctx1)
						scope.ctx1->GSGetConstantBuffers1(slots[i], 1, scope.buffers[i].put(), &scope.first[i], &scope.count[i]);
					else
						scope.ctx->GSGetConstantBuffers(slots[i], 1, scope.buffers[i].put());
				}
				scope.buffersSaved = true;
			}
			std::array<ID3D11Buffer*, slots.size()> buffers{};
			std::array<UINT, slots.size()> first{}, count{};
			if (scope.ctx1) {
				scope.ctx1->VSGetConstantBuffers1(0, 1, buffers.data(), first.data(), count.data());
				scope.ctx1->VSGetConstantBuffers1(12, 2, buffers.data() + 1, first.data() + 1, count.data() + 1);
			} else {
				scope.ctx->VSGetConstantBuffers(0, 1, buffers.data());
				scope.ctx->VSGetConstantBuffers(12, 2, buffers.data() + 1);
			}
			for (size_t i = 0; i < slots.size(); ++i) {
				winrt::com_ptr<ID3D11Buffer> buffer;
				buffer.attach(buffers[i]);
				if (!scope.copied || buffer != scope.activeBuffers[i] || first[i] != scope.activeFirst[i] || count[i] != scope.activeCount[i]) {
					if (scope.ctx1)
						scope.ctx1->GSSetConstantBuffers1(slots[i], 1, &buffers[i], &first[i], &count[i]);
					else
						scope.ctx->GSSetConstantBuffers(slots[i], 1, &buffers[i]);
					scope.activeBuffers[i] = std::move(buffer);
					scope.activeFirst[i] = first[i];
					scope.activeCount[i] = count[i];
				}
			}
			scope.copied = true;
		}

	private:
		void Capture()
		{
			if (captured)
				return;
			// Only our own class-free stage may be replaced; foreign stages stay untouched.
			ctx->GSGetShader(saved.put(), nullptr, nullptr);
			captured = true;
		}
		void RestoreBuffers()
		{
			for (size_t i = 0; i < slots.size(); ++i) {
				auto* raw = buffers[i].get();
				if (ctx1)
					ctx1->GSSetConstantBuffers1(slots[i], 1, &raw, &first[i], &count[i]);
				else
					ctx->GSSetConstantBuffers(slots[i], 1, &raw);
			}
		}

		inline static thread_local Scope* current = nullptr;
		inline static constexpr std::array<UINT, 3> slots{ 0, 12, 13 };
		ID3D11DeviceContext* ctx;
		Scope* previous;
		bool enabled, captured = false, changed = false, buffersSaved = false, copied = false;
		winrt::com_ptr<ID3D11DeviceContext1> ctx1;
		winrt::com_ptr<ID3D11GeometryShader> saved, bound;
		winrt::com_ptr<ID3D11VertexShader> lastVertex;
		std::array<winrt::com_ptr<ID3D11Buffer>, slots.size()> buffers, activeBuffers;
		std::array<UINT, slots.size()> first{}, count{}, activeFirst{}, activeCount{};
	};

	/** Keeps the optional geometry stage paired with the vertex stage actually bound. */
	inline void BindVertexShader(ID3D11DeviceContext* context, ID3D11VertexShader* vertex)
	{
		context->VSSetShader(vertex, nullptr, 0);
		Scope::Bind(vertex);
	}
}
