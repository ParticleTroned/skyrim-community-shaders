#pragma once

#include <cstdint>

namespace CSX::ScreenshotAPI
{
	inline constexpr char ServiceName[] = "csx.screenshot";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;
	inline constexpr std::uint32_t MaximumRequestBytes = 256u * 1024u;

	/** Transport-level result. Command acceptance and operation state are in the JSON response. */
	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kWrongThread = 3,
		kInvalidJson = 4,
		kServiceUnavailable = 5,
		kInternalError = 6,
		kRequestTooLarge = 7
	};

	/** UTF-8 JSON request. jsonBytes excludes any optional trailing NUL. */
	struct Request001
	{
		std::uint32_t structSize = sizeof(Request001);
		const char* jsonUtf8 = nullptr;
		std::uint32_t jsonBytes = 0;
	};

	/**
	 * UTF-8 JSON response owned by CSX. The bytes remain valid until the next
	 * Dispatch call made on the same thread. Copy them before that call.
	 */
	struct Response001
	{
		std::uint32_t structSize = sizeof(Response001);
		Status status = Status::kInternalError;
		const char* jsonUtf8 = nullptr;
		std::uint32_t jsonBytes = 0;
	};

	/**
	 * Native adapter for the asynchronous Screenshot API v1 JSON contract.
	 * Dispatch is synchronous only for validation/admission. Calls from other
	 * threads are marshalled to Skyrim's runtime main thread with a bounded
	 * queue-admission wait. Once execution begins, Dispatch waits for its
	 * admission response so a transport failure can never precede mutation.
	 * Accepted capture work remains asynchronous and is observed through
	 * request_get or events_poll.
	 */
	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = ServiceMajor;
		std::uint32_t minor = ServiceMinor;
		std::uint32_t schemaRevision = SchemaRevision;
		const void* context = nullptr;
		Status (*Dispatch)(const void* context, const Request001* request, Response001* response) = nullptr;
	};
}
