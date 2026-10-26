#pragma once

#include "Api/ServiceFoundation.h"
#include "ScreenshotApiPolicy.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace CSX::ScreenshotStorage
{
	class DirectoryLease;
}

namespace CSX::Screenshot
{
	struct ManifestChildNode;

	struct ManifestJob
	{
		std::string requestId;
		std::uint64_t generation = 0;
		bool final = false;
		std::filesystem::path destination;
		std::filesystem::path partialPath;
		std::shared_ptr<ScreenshotStorage::DirectoryLease> directoryLease;
		nlohmann::json header = nlohmann::json::object();
		std::shared_ptr<const ManifestChildNode> children;
	};

	/** Advance request event identity only after the real journal commits. */
	template <class Record>
	void AppendRequestEvent(Api::ServiceFoundation& a_service, Record& a_record,
		std::string_view a_type, nlohmann::json a_payload)
	{
		const auto eventIndex = a_record.eventIndex + 1;
		a_service.AppendEvent(a_record.requestId, eventIndex, a_type, std::move(a_payload));
		a_record.eventIndex = eventIndex;
	}

	/** Commit queue, generation and event before allowing worker consumption. */
	template <class State, class Sequence, class Record>
	void AdmitManifestJob(State& a_state, Sequence& a_sequence, ManifestJob a_job,
		Api::ServiceFoundation& a_service, Record& a_record, nlohmann::json a_payload)
	{
		const std::lock_guard lock(a_state.mutex);
		if (a_state.stopRequested || !ScreenshotPolicy::CanAdmitManifestJob(a_state.outstanding, a_job.final))
			throw std::runtime_error("bounded manifest publication lane is full or stopping");
		const auto previousGeneration = a_sequence.manifestGeneration;
		const auto previousFinalGeneration = a_sequence.finalManifestGeneration;
		a_state.jobs.push_back(std::move(a_job));
		++a_state.outstanding;
		a_sequence.manifestGeneration = a_state.jobs.back().generation;
		if (a_state.jobs.back().final)
			a_sequence.finalManifestGeneration = a_sequence.manifestGeneration;
		try {
			AppendRequestEvent(a_service, a_record, "packaging.queued", std::move(a_payload));
		} catch (...) {
			a_sequence.manifestGeneration = previousGeneration;
			a_sequence.finalManifestGeneration = previousFinalGeneration;
			--a_state.outstanding;
			a_state.jobs.pop_back();
			throw;
		}
	}
}
