#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

// This target compiles the production profiler against deterministic query and clock inputs.
using UINT = unsigned int;
using UINT64 = std::uint64_t;
using HRESULT = long;
inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_FAIL = -1;
inline constexpr HRESULT DXGI_STATUS_OCCLUDED = 0x087A0001;
inline constexpr UINT DXGI_PRESENT_TEST = 1;
inline bool FAILED(HRESULT value) { return value < 0; }
inline constexpr UINT D3D11_ASYNC_GETDATA_DONOTFLUSH = 1;
enum D3D11_QUERY
{
	D3D11_QUERY_TIMESTAMP,
	D3D11_QUERY_TIMESTAMP_DISJOINT
};
struct D3D11_QUERY_DESC
{
	D3D11_QUERY Query{};
	UINT MiscFlags{};
};
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT
{
	UINT64 Frequency = 1000;
	bool Disjoint = false;
};
struct LARGE_INTEGER
{
	std::int64_t QuadPart{};
};
inline std::int64_t profilerTestClock = 0;
inline bool QueryPerformanceFrequency(LARGE_INTEGER* value)
{
	value->QuadPart = 1000;
	return true;
}
inline bool QueryPerformanceCounter(LARGE_INTEGER* value)
{
	value->QuadPart = ++profilerTestClock;
	return true;
}

struct ID3D11DeviceChild
{};
struct ID3D11Query : ID3D11DeviceChild
{
	explicit ID3D11Query(D3D11_QUERY value) : type(value) {}
	D3D11_QUERY type{};
	UINT64 timestamp = 0;
};

struct ID3D11Device
{
	int failedQueryIndex = -1;
	int queryCreations = 0;
	HRESULT CreateQuery(const D3D11_QUERY_DESC* desc, ID3D11Query** output)
	{
		if (queryCreations++ == failedQueryIndex) {
			*output = nullptr;
			return E_FAIL;
		}
		*output = new ID3D11Query{ desc->Query };
		return S_OK;
	}
};

struct ID3D11DeviceContext
{
	void GetDevice(ID3D11Device** output) { *output = new ID3D11Device; }
	bool pending = false;
	bool disjoint = false;
	bool failed = false;
	UINT64 clock = 0;
	UINT writes = 0;
	UINT reads = 0;
	UINT activeDisjointQueries = 0;
	void Begin(ID3D11Query* query)
	{
		if (!query)
			throw std::runtime_error("Begin received a missing query");
		++writes;
		if (query->type == D3D11_QUERY_TIMESTAMP_DISJOINT && ++activeDisjointQueries != 1)
			throw std::runtime_error("overlapping disjoint queries");
	}
	void End(ID3D11Query* query)
	{
		if (!query)
			throw std::runtime_error("End received a missing query");
		++writes;
		query->timestamp = ++clock;
		if (query->type == D3D11_QUERY_TIMESTAMP_DISJOINT)
			--activeDisjointQueries;
	}
	HRESULT GetData(ID3D11Query* query, void* output, UINT, UINT)
	{
		if (!query)
			throw std::runtime_error("GetData received a missing query");
		++reads;
		if (pending)
			return S_FALSE;
		if (failed)
			return E_FAIL;
		if (query->type == D3D11_QUERY_TIMESTAMP_DISJOINT) {
			const D3D11_QUERY_DATA_TIMESTAMP_DISJOINT value{ 1000, disjoint };
			std::memcpy(output, &value, sizeof(value));
		} else {
			std::memcpy(output, &query->timestamp, sizeof(query->timestamp));
		}
		return S_OK;
	}
};
