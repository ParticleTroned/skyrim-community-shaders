#include "Features/Upscaling/NeuralRendering/ReplayCapture.h"
#include "Utils/CryptoHash.h"
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <wrl/client.h>

// The isolated test has no renderer logger; debug names still reach D3D11.
namespace Util
{
	void SetResourceName(ID3D11DeviceChild* resource, const char* name, ...)
	{
		if (resource)
			resource->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
	}
}

using namespace NeuralRendering::Replay;
using Microsoft::WRL::ComPtr;
namespace
{
	unsigned checks = 0;
	void Require(bool value)
	{
		++checks;
		if (!value) {
			std::fprintf(stderr, "Replay capture check %u failed\n", checks);
			std::abort();
		}
	}
	struct Fixture
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		std::array<ComPtr<ID3D11Texture2D>, 4> textures;
		std::vector<std::byte> expected;
		Batch batch;
		Fixture()
		{
			Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
			expected.resize(8 * 9 * 4);
			for (std::size_t i = 0; i < expected.size(); ++i)
				expected[i] = static_cast<std::byte>(i % 251);
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 8;
			desc.Height = 9;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.Usage = D3D11_USAGE_DEFAULT;
			D3D11_SUBRESOURCE_DATA data{ expected.data(), 8 * 4, 0 };
			for (auto& texture : textures) {
				Require(SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)));
				Util::SetResourceName(texture.Get(), "ReplayCaptureTest::Input");
			}
			batch.supported = true;
			batch.runtime = { { "path", "fixture-only-not-a-runtime.dll" }, { "version", "test-only" }, { "sha256", std::string(64, '0') } };
			batch.adapter = { { "description", "WARP readback test; no NR performance evidence" } };
			batch.metadata = { { "frame", 10 }, { "sourceWorldFrame", 10 }, { "generation", 1 },
				{ "insertionPoint", 0 }, { "mode", 0 }, { "arrangement", 0 }, { "colorRevision", 1 },
				{ "inputEpoch", 1 }, { "colorConfiguration", Json::object() }, { "tuning", Json::object() } };
			Eye eye;
			eye.outputSubrect = { 0, 0, 8, 9 };
			eye.motionVectorScale = { 8, 9 };
			eye.color = textures[0].Get();
			eye.depth = textures[1].Get();
			eye.motion = textures[2].Get();
			eye.output = textures[3].Get();
			batch.eyes.push_back(eye);
		}
		void Offer() { OfferBatch(device.Get(), context.Get(), batch); }
		Json Complete()
		{
			// A test fixture explicitly submits its own work; capture never flushes.
			context->Flush();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			Json status;
			do {
				Offer();
				status = Status();
				if (status.at("state") != "capturing")
					return status;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} while (std::chrono::steady_clock::now() < deadline);
			Require(false);
			return status;
		}
	};
	void WaitWriter()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (Status().at("writerActive").get<bool>() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		Require(!Status().at("writerActive").get<bool>());
	}
}

int main()
{
	const auto scratch = std::filesystem::current_path() / ("nr-replay-capture-test-" + std::to_string(GetCurrentProcessId()));
	Require(std::filesystem::create_directory(scratch));
	std::filesystem::current_path(scratch);
	Require(!Request(0).at("ok").get<bool>());
	Require(!Request(33).at("ok").get<bool>());
	Require(!Request(1, 30001).at("ok").get<bool>());
	auto request = Request();
	Require(request.at("ok").get<bool>() && IsArmed());
	Require(!Request().at("ok").get<bool>());
	Require(!Cancel("not-owner").at("ok").get<bool>() && IsArmed());
	Require(Cancel(request.at("requestId").get<std::string>()).at("state") == "cancelled");
	Require(!IsArmed());

	Fixture fixture;
	Require(Request().at("ok").get<bool>());
	fixture.Offer();
	const auto complete = fixture.Complete();
	Require(complete.at("state") == "complete" && !IsArmed());
	std::ifstream manifestFile(complete.at("manifest").get<std::string>());
	const auto manifest = Json::parse(manifestFile);
	Require(manifest.at("complete").get<bool>());
	Require(manifest.at("frames").size() == 1);
	Require(manifest.at("captureTimingIsPerformanceEvidence") == false);
	const auto& frame = manifest.at("frames").at(0);
	for (const auto* role : { "color", "depth", "motion", "output" }) {
		const auto& image = frame.at("eyes").at(0).at(role);
		Require(image.at("rowBytes") == 32 && image.at("height") == 9);
		Require(image.at("sha256") == Util::CryptoHash::ToHex(Util::CryptoHash::Sha256Bytes(fixture.expected)));
		const auto path = std::filesystem::path(complete.at("directory").get<std::string>()) / image.at("file").get<std::string>();
		std::ifstream payload(path, std::ios::binary);
		std::vector<std::byte> bytes(fixture.expected.size());
		payload.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Require(payload.good() && bytes == fixture.expected);
	}

	Require(Request(2).at("ok").get<bool>());
	fixture.Offer();
	fixture.Offer();
	Require(Status().at("capturedFrames") == 1);
	fixture.batch.metadata["sourceWorldFrame"] = 12;
	fixture.Offer();
	Require(Status().at("state") == "failed");
	fixture.context->Flush();
	WaitWriter();
	fixture.batch.metadata["sourceWorldFrame"] = 10;

	Require(Request(2).at("ok").get<bool>());
	fixture.Offer();
	fixture.batch.metadata["sourceWorldFrame"] = 11;
	fixture.batch.metadata["colorRevision"] = 2;
	fixture.Offer();
	Require(Status().at("state") == "failed");
	fixture.context->Flush();
	WaitWriter();
	fixture.batch.metadata["colorRevision"] = 1;

	Require(Request().at("ok").get<bool>());
	fixture.batch.eyes[0].featureUpscaling = true;
	fixture.Offer();
	Require(fixture.Complete().at("state") == "complete");
	fixture.batch.eyes[0].featureUpscaling = false;

	D3D11_TEXTURE2D_DESC guideDesc{};
	guideDesc.Width = 4;
	guideDesc.Height = 5;
	guideDesc.MipLevels = guideDesc.ArraySize = guideDesc.SampleDesc.Count = 1;
	guideDesc.Format = DXGI_FORMAT_R32_FLOAT;
	guideDesc.Usage = D3D11_USAGE_DEFAULT;
	ComPtr<ID3D11Texture2D> guide;
	D3D11_SUBRESOURCE_DATA guideData{ fixture.expected.data(), 4 * 4, 0 };
	Require(SUCCEEDED(fixture.device->CreateTexture2D(&guideDesc, &guideData, &guide)));
	Util::SetResourceName(guide.Get(), "ReplayCaptureTest::ScaledGuide");
	fixture.batch.eyes[0].depth = fixture.batch.eyes[0].motion = guide.Get();
	fixture.batch.eyes[0].featureUpscaling = true;
	Require(Request().at("ok").get<bool>());
	fixture.Offer();
	Require(fixture.Complete().at("state") == "complete");
	fixture.batch.eyes[0].output = guide.Get();
	Require(Request().at("ok").get<bool>());
	fixture.Offer();
	Require(Status().at("state") == "failed");
	fixture.batch.eyes[0].output = fixture.textures[3].Get();

	fixture.batch.eyes[0].outputSubrect = { 1, 1, 5, 7 };
	Require(Request().at("ok").get<bool>());
	fixture.Offer();
	Require(Status().at("state") == "failed");
	fixture.batch.eyes[0].outputSubrect = { 0, 0, 8, 9 };

	Require(Request(2).at("ok").get<bool>());
	fixture.batch.metadata["sourceWorldFrame"] = 20;
	fixture.Offer();
	fixture.batch.metadata["sourceWorldFrame"] = 21;
	fixture.Offer();
	const auto sequence = fixture.Complete();
	Require(sequence.at("state") == "complete" && sequence.at("savedFrames") == 2);

	Require(Request().at("ok").get<bool>());
	fixture.batch.supported = false;
	fixture.batch.unsupportedReason = "unproven initialized full canvas";
	fixture.Offer();
	Require(Status().at("state") == "failed");
	fixture.batch.supported = true;

	Require(Request().at("ok").get<bool>());
	fixture.batch.eyes.push_back(fixture.batch.eyes[0]);
	fixture.batch.eyes[1].slot = 1;
	fixture.Offer();
	Require(fixture.Complete().at("state") == "complete");

	D3D11_TEXTURE2D_DESC larger{};
	larger.Width = larger.Height = 1024;
	larger.MipLevels = larger.ArraySize = larger.SampleDesc.Count = 1;
	larger.Format = DXGI_FORMAT_R32_FLOAT;
	larger.Usage = D3D11_USAGE_DEFAULT;
	ComPtr<ID3D11Texture2D> large;
	Require(SUCCEEDED(fixture.device->CreateTexture2D(&larger, nullptr, &large)));
	Util::SetResourceName(large.Get(), "ReplayCaptureTest::BudgetBoundary");
	fixture.batch.eyes.resize(1);
	auto& largeEye = fixture.batch.eyes[0];
	largeEye.color = largeEye.depth = largeEye.motion = largeEye.output = large.Get();
	largeEye.outputSubrect = { 0, 0, 1024, 1024 };
	Require(Request(32).at("ok").get<bool>());
	fixture.Offer();
	Require(Status().at("state") == "failed");
	Require(Status().at("reason").get<std::string>().find("budget") != std::string::npos);

	Require(Request(1, 1).at("ok").get<bool>());
	std::this_thread::sleep_for(std::chrono::milliseconds(3));
	Require(Status().at("state") == "failed" && !IsArmed());
	Require(Request().at("ok").get<bool>());
	Fail("caller metadata failed");
	Require(Status().at("state") == "failed");
	std::printf("%u native replay capture checks passed; WARP only, no NR performance measurements\n", checks);
}
