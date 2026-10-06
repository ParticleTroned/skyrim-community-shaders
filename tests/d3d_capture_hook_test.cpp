#include "Utils/VRFrameBufferUpload.h"

#include <d3d11_4.h>
#include <detours/detours.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
	using Microsoft::WRL::ComPtr;
	void Require(bool condition, const std::source_location& location = std::source_location::current())
	{
		if (!condition)
			throw std::runtime_error("Capture hook check failed at line " + std::to_string(location.line()));
	}
	void Check(HRESULT result) { Require(SUCCEEDED(result)); }

	struct CaptureHooks
	{
		using Map = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
		using Unmap = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
		static inline Map originalMap;
		static inline Unmap originalUnmap;
		static inline ID3D11Resource* source;
		static inline void* mapped;
		static inline std::array<unsigned, 0x570 / sizeof(unsigned)> snapshot;
		static inline unsigned captures;

		static HRESULT STDMETHODCALLTYPE OnMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
			D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* output)
		{
			const auto result = originalMap(context, resource, subresource, type, flags, output);
			if (SUCCEEDED(result) && resource == source)
				mapped = output->pData;
			return result;
		}
		static void STDMETHODCALLTYPE OnUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
		{
			if (resource == source && mapped) {
				std::memcpy(snapshot.data(), mapped, sizeof(snapshot));
				mapped = nullptr;
				++captures;
			}
			originalUnmap(context, resource, subresource);
		}
		CaptureHooks(ID3D11DeviceContext* context, ID3D11Resource* resource)
		{
			source = resource;
			mapped = nullptr;
			captures = 0;
			auto** methods = *reinterpret_cast<void***>(context);
			originalMap = reinterpret_cast<Map>(methods[14]);
			originalUnmap = reinterpret_cast<Unmap>(methods[15]);
			Require(DetourTransactionBegin() == NO_ERROR);
			const auto update = DetourUpdateThread(GetCurrentThread());
			const auto map = DetourAttach(reinterpret_cast<PVOID*>(&originalMap), reinterpret_cast<PVOID>(OnMap));
			const auto unmap = DetourAttach(reinterpret_cast<PVOID*>(&originalUnmap), reinterpret_cast<PVOID>(OnUnmap));
			if (update != NO_ERROR || map != NO_ERROR || unmap != NO_ERROR) {
				DetourTransactionAbort();
				Require(false);
			}
			Require(DetourTransactionCommit() == NO_ERROR);
		}
		~CaptureHooks()
		{
			if (DetourTransactionBegin() != NO_ERROR || DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
				DetourDetach(reinterpret_cast<PVOID*>(&originalMap), reinterpret_cast<PVOID>(OnMap)) != NO_ERROR ||
				DetourDetach(reinterpret_cast<PVOID*>(&originalUnmap), reinterpret_cast<PVOID>(OnUnmap)) != NO_ERROR ||
				DetourTransactionCommit() != NO_ERROR)
				std::terminate();
		}
		CaptureHooks(const CaptureHooks&) = delete;
		CaptureHooks& operator=(const CaptureHooks&) = delete;
	};

	void ObserveEngineUpload(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const void* source)
	{
		CaptureHooks::mapped = nullptr;
		if (source) {
			std::memcpy(CaptureHooks::snapshot.data(), source, sizeof(CaptureHooks::snapshot));
			++CaptureHooks::captures;
		}
		context->Unmap(resource, subresource);
	}

	struct EngineUploadCaller : Xbyak::CodeGenerator
	{
		explicit EngineUploadCaller(std::uintptr_t observer)
		{
			// Reproduce the validated engine stack and pass its Unmap arguments unchanged.
			sub(rsp, 0x628);
			mov(rax, ptr[rsp + 0x650]);
			mov(ptr[rsp + 0x40], rax);
			mov(r10, r9);
			lea(r11, ptr[rsp + 0x50]);
			mov(r9, sizeof(CaptureHooks::snapshot) / 8);
			L("copy");
			mov(rax, ptr[r10]);
			mov(ptr[r11], rax);
			add(r10, 8);
			add(r11, 8);
			dec(r9);
			jnz("copy");
			mov(rax, observer);
			call(rax);
			add(rsp, 0x628);
			ret();
		}
	};

	void Run(bool useEngineObserver, unsigned protectionMode)
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = sizeof(CaptureHooks::snapshot);
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ComPtr<ID3D11Buffer> buffer;
		Check(device->CreateBuffer(&description, nullptr, buffer.GetAddressOf()));
		ComPtr<ID3D11Multithread> multithread;
		Check(context.As(&multithread));
		if (protectionMode == 1)
			multithread->SetMultithreadProtected(TRUE);
		auto** methods = *reinterpret_cast<void***>(context.Get());
		const auto mapAtInstall = methods[14];
		const auto unmapAtInstall = methods[15];
		const CaptureHooks hooks(context.Get(), buffer.Get());
		Util::VRFrameBufferUploadThunk observer(reinterpret_cast<std::uintptr_t>(ObserveEngineUpload));
		observer.ready();
		EngineUploadCaller caller(reinterpret_cast<std::uintptr_t>(observer.getCode()));
		caller.ready();
		const auto upload = caller.getCode<void (*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const void*, const void*)>();
		for (unsigned frame = 1; frame <= 64; ++frame) {
			// Exercise both ownership-only access and external protection/dispatch changes.
			if (protectionMode == 2 && frame == 2)
				multithread->SetMultithreadProtected(TRUE);
			if (protectionMode == 3)
				multithread->SetMultithreadProtected(frame % 2 == 0);
			const bool protectedBefore = multithread->GetMultithreadProtected() != FALSE;
			std::array<unsigned, 0x570 / sizeof(unsigned)> expected{};
			for (unsigned index = 0; index < expected.size(); ++index)
				expected[index] = (frame << 16) ^ (index * 7);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			std::memcpy(mapped.pData, expected.data(), sizeof(expected));
			if (useEngineObserver)
				upload(context.Get(), buffer.Get(), 0, expected.data(), mapped.pData);
			else
				context->Unmap(buffer.Get(), 0);
			Require((multithread->GetMultithreadProtected() != FALSE) == protectedBefore);
			if (useEngineObserver || frame == 1) {
				Require(CaptureHooks::snapshot == expected);
				Require(CaptureHooks::captures == frame);
			}
		}
		if (!useEngineObserver) {
			const bool dispatchChanged = methods[14] != mapAtInstall || methods[15] != unmapAtInstall;
			if (dispatchChanged)
				Require(CaptureHooks::captures < 64);
			std::cout << "Late protection: dispatchChanged=" << dispatchChanged << ", captures=" << CaptureHooks::captures << "/64\n";
		} else {
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Check(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			const auto before = CaptureHooks::snapshot;
			upload(context.Get(), buffer.Get(), 0, before.data(), nullptr);
			Require(CaptureHooks::captures == 64 && CaptureHooks::snapshot == before);
			std::cout << "Engine upload, protection mode " << protectionMode
					  << ": 64/64 complete fresh snapshots; null Map rejected; protection unchanged\n";
		}
	}
}

int main()
{
	try {
		Run(false, 2);
		for (unsigned mode = 0; mode < 4; ++mode)
			Run(true, mode);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
