#pragma once

#include <atomic>
#include <memory>
#include <utility>
#include <winrt/base.h>

namespace Util
{
	/** Render-thread shader slot with nonblocking, reset-safe worker publication. */
	template <typename ShaderT>
	class AsyncShader
	{
	public:
		AsyncShader() = default;
		AsyncShader(const AsyncShader&) = delete;
		AsyncShader& operator=(const AsyncShader&) = delete;
		~AsyncShader() { Reset(); }

		class Preparation
		{
		public:
			/** A reset abandons this result without waiting for its worker. */
			bool IsCancelled() const { return cancelled.load(std::memory_order_acquire); }
			/** Called once by the worker, including with null after failure. */
			void Complete(winrt::com_ptr<ShaderT> result)
			{
				shader = std::move(result);
				complete.store(true, std::memory_order_release);
			}

		private:
			friend class AsyncShader;
			std::atomic_bool cancelled{ false }, complete{ false };
			winrt::com_ptr<ShaderT> shader;
		};

		/** Polls once; never waits, creates a device object, or requests compilation. */
		ShaderT* get()
		{
			if (pending && pending->complete.load(std::memory_order_acquire)) {
				shader = std::move(pending->shader);
				failed = !shader;
				pending.reset();
			}
			return shader.get();
		}

		bool CanPrepare() const { return !shader && !pending && !failed; }
		/** Reserve before enqueueing; queue failures must complete the ticket with null. */
		std::shared_ptr<Preparation> BeginPreparation()
		{
			if (!CanPrepare())
				return {};
			failed = true;
			pending = std::make_shared<Preparation>();
			failed = false;
			return pending;
		}
		/** Render-thread reset; a previous worker cannot populate the new slot. */
		void Reset()
		{
			if (pending)
				pending->cancelled.store(true, std::memory_order_release);
			pending.reset();
			shader = nullptr;
			failed = false;
		}

	private:
		winrt::com_ptr<ShaderT> shader;
		std::shared_ptr<Preparation> pending;
		bool failed = false;
	};
}
