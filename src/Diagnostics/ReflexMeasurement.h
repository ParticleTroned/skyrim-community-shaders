#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/ReflexMeasurementState.h"

namespace Reflex
{
	class Backend;
	struct Status;
	struct Outcome;
}
namespace CSX::Diagnostics::ReflexMeasurement
{
	/// Registers the DevBench-only measurement contract when its host is available.
	void Install();
	/// Returns the capture's monotonic QPC clock, or zero when unavailable.
	Tick Clock() noexcept;
	Journal& Records();
	/// Native work inherits an admitted application frame while capture drains.
	Journal::Ticket BeginNativeOperation();
	bool NativeTracingActive();
	/// Captures every UpdateReflex entry, including availability guards.
	class UpdateScope
	{
	public:
		UpdateScope(uint32_t frame, bool available);
		UpdateScope(const UpdateScope&) = delete;
		UpdateScope& operator=(const UpdateScope&) = delete;
		~UpdateScope();
		/// A excludes native context; B/C retain returned state without a driver getter.
		bool CapturesBackend() const;
		void SetBackend(const Reflex::Status& status, uint64_t uptimeMs);
		void SetBackendOutcome(const Reflex::Outcome& outcome, uint64_t uptimeMs);
		Exit reason = Exit::Unavailable;

	private:
		Journal::Ticket ticket;
		Journal::Ticket previous;
		AppRecord row{};
	};
	/// Observes requested preferences and confirms restoration after native acceptance.
	void ObservePreferences(Reflex::Backend& backend, bool backendUpdated);
}
#endif
