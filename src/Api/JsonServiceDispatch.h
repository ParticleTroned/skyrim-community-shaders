#pragma once

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace CSX::Api
{
	struct JsonServiceDepthLimit
	{};
	/** Shared bounded JSON transport; handlers retain domain validation and thread ownership. */
	template <class Request, class Response, class Handler>
	auto DispatchJsonService(const Request* a_request, Response* a_response, std::uint32_t maximumRequestBytes, Handler&& handler) noexcept -> decltype(a_response->status)
	{
		using Status = decltype(a_response->status);
		if (!a_response)
			return Status::kInvalidArgument;
		if (a_response->structSize < sizeof(Response))
			return Status::kStructureTooSmall;
		a_response->status = Status::kInternalError;
		a_response->jsonUtf8 = nullptr;
		a_response->jsonBytes = 0;

		if (!a_request) {
			a_response->status = Status::kInvalidArgument;
			return a_response->status;
		}
		if (a_request->structSize < sizeof(Request)) {
			a_response->status = Status::kStructureTooSmall;
			return a_response->status;
		}
		if (!a_request->jsonUtf8 || a_request->jsonBytes == 0) {
			a_response->status = Status::kInvalidArgument;
			return a_response->status;
		}
		if (a_request->jsonBytes > maximumRequestBytes) {
			a_response->status = Status::kRequestTooLarge;
			return a_response->status;
		}
		try {
			const auto request = nlohmann::json::parse(
				a_request->jsonUtf8,
				a_request->jsonUtf8 + a_request->jsonBytes, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
					if (depth > 64)
						throw JsonServiceDepthLimit{};
					return true;
				});
			if (!request.is_object()) {
				a_response->status = Status::kInvalidJson;
				return a_response->status;
			}

			const auto response = handler(request);
			if (!response) {
				a_response->status = Status::kServiceUnavailable;
				return a_response->status;
			}
			thread_local std::string responseStorage;
			responseStorage = response->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
			if (responseStorage.size() > std::numeric_limits<std::uint32_t>::max()) {
				a_response->status = Status::kInternalError;
				return a_response->status;
			}
			a_response->jsonUtf8 = responseStorage.c_str();
			a_response->jsonBytes = static_cast<std::uint32_t>(responseStorage.size());
			a_response->status = Status::kSuccess;
			return a_response->status;
		} catch (const JsonServiceDepthLimit&) {
			a_response->status = Status::kInvalidJson;
			return a_response->status;
		} catch (const nlohmann::json::parse_error&) {
			a_response->status = Status::kInvalidJson;
			return a_response->status;
		} catch (...) {
			a_response->status = Status::kInternalError;
			return a_response->status;
		}
	}

}
