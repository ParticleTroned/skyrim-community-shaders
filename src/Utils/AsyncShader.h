#pragma once

#include "Utils/ShaderCompiler.h"
#include "Utils/ShaderPreparation.h"

#include <atomic>
#include <exception>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace Util
{
	/** Owner-thread shader adoption; changed request inputs require Reset() before preparation. */
	template <typename ShaderT>
	class AsyncShader
	{
	public:
		enum class Status : std::uint8_t
		{
			Empty,
			Pending,
			Ready,
			Failed
		};

		/** Capture and queue one request at a preparation boundary without waiting for its result. */
		void Prepare(const wchar_t* a_path,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_target, const char* a_entry = "main", const char* a_name = nullptr)
		{
			PrepareRequest(a_path, [&]() -> const auto& { return a_defines; }, a_target, a_entry, a_name);
		}

		/** Fixed-macro requests do not allocate while resident, pending or failure-latched. */
		void Prepare(const wchar_t* a_path,
			std::initializer_list<std::pair<const char*, const char*>> a_defines,
			const char* a_target, const char* a_entry = "main", const char* a_name = nullptr)
		{
			PrepareRequest(a_path, [&] { return std::vector<std::pair<const char*, const char*>>(a_defines); }, a_target, a_entry, a_name);
		}

		/** Adopt a current completed object on its owner thread without enqueueing or waiting. */
		ShaderT* Poll()
		{
			if (!operation)
				return shader.get();
			const auto preparationState = operation->state.load(std::memory_order_acquire);
			if (preparationState == ShaderPreparation::State::Pending)
				return nullptr;
			if (preparationState == ShaderPreparation::State::Stale || !IsShaderPreparationCurrent(*operation)) {
				operation.reset();
				status.store(Status::Empty, std::memory_order_release);
				return nullptr;
			}
			if (preparationState == ShaderPreparation::State::Failed) {
				status.store(Status::Failed, std::memory_order_release);
				operation.reset();
				return nullptr;
			}
			winrt::com_ptr<ShaderT> prepared;
			if (!operation->shader || FAILED(operation->shader->QueryInterface(__uuidof(ShaderT), reinterpret_cast<void**>(prepared.put())))) {
				status.store(Status::Failed, std::memory_order_release);
				operation.reset();
				logger::error("Standalone shader preparation returned an incompatible object");
				return nullptr;
			}
			if (!IsShaderPreparationCurrent(*operation)) {
				operation.reset();
				status.store(Status::Empty, std::memory_order_release);
				return nullptr;
			}
			shader = std::move(prepared);
			identity = operation;
			operation.reset();
			status.store(Status::Ready, std::memory_order_release);
			return shader.get();
		}

		/** Complete preparation at an explicit owner boundary before exposing dependent resources. */
		ShaderT* Wait()
		{
			if (operation) {
				while (operation->state.load(std::memory_order_acquire) == ShaderPreparation::State::Pending)
					operation->state.wait(ShaderPreparation::State::Pending, std::memory_order_acquire);
			}
			return Poll();
		}

		/** Return the resident shader on its owner thread without compiling, polling or waiting. */
		ShaderT* get() const noexcept { return shader.get(); }

		/** Drop this owner's request, result and failure latch without waiting for superseded work. */
		void Reset()
		{
			shader = nullptr;
			operation.reset();
			identity.reset();
			status.store(Status::Empty, std::memory_order_release);
		}

		/** Check completed source/device identity at an owner boundary, ignoring engine-only clears. */
		[[nodiscard]] bool IsCurrent() const
		{
			return identity && IsShaderPreparationCurrent(*identity, true);
		}

		/** Latch an exhausted preparation boundary without admitting another request until Reset(). */
		void Fail()
		{
			Reset();
			status.store(Status::Failed, std::memory_order_release);
		}

		/** Thread-safe observation of the last owner-published readiness state. */
		[[nodiscard]] Status GetStatus() const noexcept { return status.load(std::memory_order_acquire); }
		[[nodiscard]] bool IsPending() const noexcept { return GetStatus() == Status::Pending; }
		[[nodiscard]] bool HasFailed() const noexcept { return GetStatus() == Status::Failed; }
		[[nodiscard]] bool IsReady() const noexcept { return GetStatus() == Status::Ready; }
		explicit operator bool() const noexcept { return IsReady(); }

	private:
		template <class GetDefines>
		void PrepareRequest(const wchar_t* a_path, GetDefines&& a_getDefines,
			const char* a_target, const char* a_entry, const char* a_name)
		{
			Poll();
			if (GetStatus() != Status::Empty)
				return;
			status.store(Status::Failed, std::memory_order_release);
			try {
				operation = RequestShaderPreparation(a_path, a_getDefines(), a_target, a_entry, a_name);
				if (operation)
					status.store(Status::Pending, std::memory_order_release);
			} catch (const std::exception& error) {
				logger::error("Failed to request standalone shader {}: {}", a_name ? a_name : a_entry, error.what());
			} catch (...) {
				logger::error("Failed to request standalone shader {}: unknown exception", a_name ? a_name : a_entry);
			}
		}

		winrt::com_ptr<ShaderT> shader;
		std::shared_ptr<ShaderPreparation> operation;
		std::shared_ptr<ShaderPreparation> identity;
		std::atomic<Status> status{ Status::Empty };
	};
}
