#pragma once

#include <cstdint>

struct ID3D11DeviceContext;
struct ID3D11Texture2D;

/** Windows x64 C-style ABI; default packing, no ownership transfer or STL. */
namespace CSXAcceptedDrawAPI
{
	inline constexpr char ExportName[] = "CSX_GetAcceptedDrawAPI";
	inline constexpr uint32_t Version = 1;
	enum Result : uint32_t
	{
		Success = 0,
		Unsupported = 1,
		NotReady = 2,
		InvalidArgument = 3,
		Failed = 4
	};
	enum Capability : uint64_t
	{
		FullSceneIndexedCoverage = 1ull << 0,  // Main-scene indexed lighting/effect draws, including instancing.
		NativeVRGeometry = 1ull << 1,
		PostDrawLiveState = 1ull << 2,
		IsolatedReplay = 1ull << 3,
		QuiescentUnregister = 1ull << 4,
		PackedStereoDepth = 1ull << 5
	};
	inline constexpr uint64_t RequiredCapabilities = FullSceneIndexedCoverage | NativeVRGeometry |
	                                                 PostDrawLiveState | IsolatedReplay | QuiescentUnregister | PackedStereoDepth;
	enum DrawKind : uint32_t
	{
		Indexed = 1,
		IndexedInstanced = 2
	};
	enum PassKind : uint32_t
	{
		MainScene = 1,
		Shadow = 2,
		Reflection = 3,
		UICapture = 4,
		Other = 5
	};
	enum StereoLayout : uint32_t
	{
		PackedStereo2D = 1
	};
	struct Arguments
	{
		uint32_t kind, indexCount, instanceCount, startIndex;
		int32_t baseVertex;
		uint32_t startInstance;
	};
	/** Synchronous, originating thread only, once per observer callback. */
	using ReplayFn = uint32_t(__cdecl*)(void* token);
	struct Draw
	{
		uint32_t structSize, version;
		uint64_t drawId;
		ID3D11DeviceContext* context;
		const void* geometry;  // Borrowed native Skyrim VR 1.4.15 BSGeometry, callback lifetime only.
		ID3D11Texture2D* sceneDepth;
		uint32_t pass, stereoLayout;
		Arguments arguments;
		ReplayFn replay;
		void* replayToken;
	};
	/** Restore all changed D3D state before returning; never retain the draw. */
	using ObserverFn = void(__cdecl*)(const Draw* draw, void* user);
	using RegisterFn = uint32_t(__cdecl*)(ObserverFn observer, void* user, uint64_t* subscription);
	/** Not callable from an observer. Success means all callbacks have returned. */
	using UnregisterFn = uint32_t(__cdecl*)(uint64_t subscription);
	struct API
	{
		uint32_t structSize, version;
		uint64_t capabilities;
		RegisterFn registerObserver;
		UnregisterFn unregisterObserver;
	};
	/** Unsupported version/size/runtime returns null; registration may return NotReady. */
	using QueryFn = const API*(__cdecl*)(uint32_t version, uint32_t minimumTableSize);
	static_assert(sizeof(void*) == 8 && sizeof(Arguments) == 24 && sizeof(Draw) == 88 && sizeof(API) == 32);
}
