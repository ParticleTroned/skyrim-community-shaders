#pragma once

#include <cstdint>

namespace CSX::PerformanceAPI
{
	inline constexpr char ServiceName[] = "csx.performance";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;
	inline constexpr std::uint32_t MaximumRequestBytes = 64u * 1024u;

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

	/** Compile-time diagnostics present in the loaded producer. */
	enum Instrumentation : std::uint32_t
	{
		kDeveloperDiagnostics = 1u << 0,
		kTracy = 1u << 1
	};

	/** On-demand production measurement control; no tracing or scheduling is added by discovery.
	 * Dispatch uses the same main-thread admission and restoration as the UI.
	 * Mutations require stable clientId/commandId values for bounded retry deduplication.
	 * Instrumentation flags identify builds unsuitable for production comparisons.
	 */
	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = ServiceMajor;
		std::uint32_t minor = ServiceMinor;
		std::uint32_t schemaRevision = SchemaRevision;
		std::uint32_t instrumentationFlags = 0;
		const void* context = nullptr;
		Status (*Dispatch)(const void* context, const Request001* request, Response001* response) = nullptr;
	};
}
