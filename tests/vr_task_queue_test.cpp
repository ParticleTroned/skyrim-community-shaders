#include "EngineFixes/VRTaskQueuePolicy.h"

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	void Require(bool a_value, const char* a_message)
	{
		if (!a_value)
			throw std::runtime_error(a_message);
	}

	struct Task
	{
		virtual void Run() = 0;
		virtual void Dispose() = 0;
	};

	struct QueueMutex
	{
		std::mutex mutex;
		std::atomic<std::thread::id> owner;
		void lock()
		{
			mutex.lock();
			owner.store(std::this_thread::get_id());
		}
		void unlock()
		{
			owner.store({});
			mutex.unlock();
		}
		bool OwnedByCaller() const { return owner.load() == std::this_thread::get_id(); }
	} queueMutex;
	void Invoke(Task* a_task) { VRTaskQueue::RunOutsideLock(queueMutex, *a_task); }

	class Pump : public Xbyak::CodeGenerator
	{
	public:
		explicit Pump(const VRTaskQueue::CallbackPatch& a_patch)
		{
			push(rbx);
			sub(rsp, 32);
			mov(rbx, rcx);
			db(a_patch.getCode(), a_patch.getSize());
			mov(rax, rbx);
			add(rsp, 32);
			pop(rbx);
			ret();
		}
	};

	void TestProducerProgressAndPatch()
	{
		VRTaskQueue::CallbackPatch patch(reinterpret_cast<std::uintptr_t>(Invoke));
		Require(patch.getSize() == 17, "callback replacement does not fit the native pump");
		Pump pump(patch);
		pump.ready();
		std::counting_semaphore<2> requested{ 0 };
		std::array<std::promise<void>, 2> completed;
		std::array<std::future<void>, 2> futures{ completed[0].get_future(), completed[1].get_future() };
		std::jthread producer([&] {
			for (auto& completion : completed) {
				requested.acquire();
				const std::lock_guard guard{ queueMutex };
				completion.set_value();
			}
		});
		struct Callback : Task
		{
			std::counting_semaphore<2>& requested;
			std::array<std::future<void>, 2>& futures;
			unsigned calls = 0;
			bool progressed = true;
			Callback(std::counting_semaphore<2>& a_requested, std::array<std::future<void>, 2>& a_futures) : requested(a_requested), futures(a_futures) {}
			void CheckProducer()
			{
				requested.release();
				progressed &= futures[calls++].wait_for(1s) == std::future_status::ready;
			}
			void Run() override { CheckProducer(); }
			void Dispose() override { CheckProducer(); }
		} task{ requested, futures };

		queueMutex.lock();
		auto* returnedTask = pump.getCode<Task* (*)(Task*)>()(&task);
		const bool lockRestored = queueMutex.OwnedByCaller();
		if (lockRestored)
			queueMutex.unlock();
		producer.join();
		Require(returnedTask == &task, "callback patch did not preserve the task register");
		Require(task.calls == 2 && task.progressed, "a task callback blocked its producer");
		Require(lockRestored, "task pump resumed without its queue lock");
	}

	void TestExceptionReleasesLock(bool a_throwFromDispose)
	{
		struct ThrowingTask : Task
		{
			bool throwFromDispose;
			bool disposed = false;
			explicit ThrowingTask(bool a_dispose) : throwFromDispose(a_dispose) {}
			void Run() override
			{
				if (!throwFromDispose)
					throw std::runtime_error("run");
			}
			void Dispose() override
			{
				disposed = true;
				throw std::runtime_error("dispose");
			}
		} task{ a_throwFromDispose };
		queueMutex.lock();
		bool propagated = false;
		try {
			VRTaskQueue::RunOutsideLock(queueMutex, task);
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		const bool lockLeaked = queueMutex.OwnedByCaller();
		if (lockLeaked)
			queueMutex.unlock();
		Require(propagated && !lockLeaked, "callback exception leaked the queue lock or was swallowed");
		Require(task.disposed == a_throwFromDispose, "failed Run unexpectedly disposed its task");
	}
}

int main()
{
	try {
		TestProducerProgressAndPatch();
		TestExceptionReleasesLock(false);
		TestExceptionReleasesLock(true);
		std::cout << "VR task queue callback tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
