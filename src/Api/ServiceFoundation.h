#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CSX::Api
{
	struct ContractDescriptor
	{
		std::string name;
		uint32_t major = 1;
		uint32_t minor = 0;
		uint32_t schemaRevision = 1;
	};

	struct ServiceLimits
	{
		std::size_t maximumCommands = 1024;
		std::size_t maximumEvents = 4096;
		std::chrono::steady_clock::duration commandRetention = std::chrono::hours(1);
	};

	/**
	 * Common process-local services for versioned asynchronous DevBench APIs.
	 *
	 * Feature APIs retain ownership of their domain state and work scheduling.
	 * This class owns the cross-cutting protocol mechanics: envelopes, command
	 * validation and idempotency, session identity, and the ordered event journal.
	 */
	class ServiceFoundation
	{
	public:
		using json = nlohmann::json;
		using Handler = std::function<json(const json&)>;
		using ReceiptLookup = std::function<json(std::string_view)>;
		using ServerMetadataProvider = std::function<json()>;

		explicit ServiceFoundation(ContractDescriptor a_contract, ServiceLimits a_limits = {});

		void SetServerMetadataProvider(ServerMetadataProvider a_provider);

		json Dispatch(const json& a_request, const Handler& a_handler, const ReceiptLookup& a_receiptLookup = {});
		json MakeEnvelope(const json& a_request, bool a_ok) const;
		json MakeError(
			const json& a_request,
			std::string_view a_code,
			std::string_view a_message,
			std::string_view a_phase = "validation",
			bool a_retryable = false,
			std::string_view a_field = {},
			std::string_view a_requestId = {}) const;

		uint64_t AppendEvent(
			std::string_view a_requestId,
			uint64_t a_eventIndex,
			std::string_view a_type,
			json a_payload = json::object());
		json PollEvents(uint64_t a_afterEventId, uint32_t a_limit, std::string_view a_requestId = {}) const;
		uint64_t AcknowledgeEvents(uint64_t a_throughEventId);
		json JournalStatus() const;
		void ForgetRequest(std::string_view a_requestId);
		void Trim();

		const std::string& SessionId() const noexcept { return sessionId; }
		const ContractDescriptor& Contract() const noexcept { return contract; }
		const ServiceLimits& Limits() const noexcept { return limits; }

		static std::string NewId();
		static std::string TimestampUtc();
		static std::string Canonicalize(const json& a_value);

	private:
		struct CommandRecord
		{
			std::string canonicalRequest;
			std::string action;
			std::string requestId;
			json response = json::object();
			bool completed = false;
			std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
		};

		ContractDescriptor contract;
		ServiceLimits limits;
		std::string sessionId;
		mutable std::mutex mutex;
		ServerMetadataProvider serverMetadataProvider;
		std::unordered_map<std::string, CommandRecord> commands;
		std::deque<std::string> commandOrder;
		std::deque<json> events;
		uint64_t nextEventId = 1;
		uint64_t acknowledgedEventId = 0;

		void TrimLocked(std::chrono::steady_clock::time_point a_now);
	};
}
