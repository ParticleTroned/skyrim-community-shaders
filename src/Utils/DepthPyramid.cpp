#include "DepthPyramid.h"
#include "DepthPyramidPolicy.h"

#include "Globals.h"
#include "Util.h"
#include "Utils/ComputeState.h"

#include <bit>
#include <cstring>

void DepthPyramid::SetupResources()
{
	constants = std::make_unique<Buffer>(ConstantBufferDesc(32), nullptr, "DepthPyramid::Parameters");
	spdConstants = std::make_unique<Buffer>(ConstantBufferDesc(16), nullptr, "DepthPyramid::SPDParameters");
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = 16;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	counter = std::make_unique<Buffer>(desc, nullptr, "DepthPyramid::SPDCounter");
	D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
	view.Format = DXGI_FORMAT_R32_TYPELESS;
	view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	view.Buffer.NumElements = 4;
	view.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	counter->CreateUAV(view);
}

void DepthPyramid::Reset()
{
	valid = false;
	baseShader.Reset();
	spdShader.Reset();
}

void DepthPyramid::Allocate(uint32_t nextWidth, uint32_t nextHeight, uint32_t eyes)
{
	valid = false;
	if ((eyes != 1 && eyes != 2) || !nextWidth || !nextHeight || nextWidth % eyes ||
		!std::has_single_bit(nextWidth / eyes) || !std::has_single_bit(nextHeight) ||
		nextWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || nextHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
		throw std::invalid_argument("Invalid depth pyramid dimensions");
	const UINT levels = UINT(std::min(std::bit_width(nextWidth / eyes), std::bit_width(nextHeight)));
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = nextWidth;
	desc.Height = nextHeight;
	desc.MipLevels = levels;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	winrt::com_ptr<ID3D11Texture2D> next;
	DX::ThrowIfFailed(globals::d3d::device->CreateTexture2D(&desc, nullptr, next.put()));
	Util::SetResourceName(next.get(), "DepthPyramid::Texture");
	winrt::com_ptr<ID3D11ShaderResourceView> nextSRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = desc.Format;
	view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	view.Texture2D.MipLevels = levels;
	DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(next.get(), &view, nextSRV.put()));
	Util::SetResourceName(nextSRV.get(), "DepthPyramid::Texture SRV");
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> nextSRVs(levels);
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> nextUAVs(levels);
	for (UINT mip = 0; mip < levels; ++mip) {
		view.Texture2D.MostDetailedMip = mip;
		view.Texture2D.MipLevels = 1;
		DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(next.get(), &view, nextSRVs[mip].put()));
		Util::SetResourceName(nextSRVs[mip].get(), "DepthPyramid::Mip%u SRV", mip);
		D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
		output.Format = desc.Format;
		output.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		output.Texture2D.MipSlice = mip;
		DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(next.get(), &output, nextUAVs[mip].put()));
		Util::SetResourceName(nextUAVs[mip].get(), "DepthPyramid::Mip%u UAV", mip);
	}
	texture = std::move(next);
	srv = std::move(nextSRV);
	mipSRVs = std::move(nextSRVs);
	mipUAVs = std::move(nextUAVs);
	width = nextWidth;
	height = nextHeight;
	mipCount = levels;
	eyeCount = eyes;
}

bool DepthPyramid::Build(ID3D11DeviceContext1* context, ID3D11ShaderResourceView* source,
	uint32_t sourceWidth, uint32_t sourceHeight, uint32_t activeWidth, uint32_t activeHeight, uint32_t eyes)
{
	valid = false;
	if (!context || !source || !texture || !constants || !spdConstants || !counter ||
		eyes != eyeCount || !sourceWidth || !sourceHeight || !activeWidth || !activeHeight || activeWidth % eyes || sourceWidth % eyes ||
		sourceWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || sourceHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		activeWidth > sourceWidth || activeHeight > sourceHeight || ((activeWidth / eyes + 1) / 2) > width / eyes || (activeHeight + 1) / 2 > height)
		return false;
	auto base = baseShader.Get(L"Data\\Shaders\\Common\\DepthPyramidBaseCS.hlsl", {}, "cs_5_0", "main", "DepthPyramid::BaseCS");
	if (!base)
		return false;
	ID3D11ComputeShader* spd = nullptr;
	if (globals::d3d::device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 && mipCount > 1)
		spd = spdShader.Get(L"Data\\Shaders\\Common\\DepthPyramidSPD.hlsl", {}, "cs_5_0", "main", "DepthPyramid::SPDCS");
	D3DState::ComputeState restore(context, spd ? 14u : 4u);
	const auto reduce = [&](UINT mip) {
		const std::array<UINT, 8> values{ std::max(width >> mip, eyes), std::max(height >> mip, 1u),
			mip ? std::max(width >> (mip - 1), eyes) : sourceWidth, mip ? std::max(height >> (mip - 1), 1u) : sourceHeight,
			eyes, mip == 0 ? 1u : 0u, activeWidth, activeHeight };
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(constants->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		std::memcpy(mapped.pData, values.data(), sizeof(values));
		context->Unmap(constants->resource.get(), 0);
		auto cb = constants->resource.get();
		auto input = mip ? mipSRVs[mip - 1].get() : source;
		auto output = mipUAVs[mip].get();
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetShaderResources(0, 1, &input);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->CSSetShader(base, nullptr, 0);
		context->Dispatch((values[0] + 7) / 8, (values[1] + 7) / 8, 1);
		input = nullptr;
		output = nullptr;
		context->CSSetShaderResources(0, 1, &input);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
	};
	reduce(0);
	if (!spd) {
		for (UINT mip = 1; mip < mipCount; ++mip)
			reduce(mip);
		valid = true;
		return true;
	}
	const UINT groupsX = (width + 63) / 64, groupsY = (height + 63) / 64;
	const UINT spdMips = DepthPyramidPolicy::SinglePassMipCount(width, height, mipCount);
	const std::array<UINT, 4> values{ width, height, spdMips, groupsX * groupsY };
	D3D11_MAPPED_SUBRESOURCE mapped{};
	DX::ThrowIfFailed(context->Map(spdConstants->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	std::memcpy(mapped.pData, values.data(), sizeof(values));
	context->Unmap(spdConstants->resource.get(), 0);
	constexpr std::array<UINT, 4> zero{};
	context->ClearUnorderedAccessViewUint(counter->uav.get(), zero.data());
	std::array<ID3D11UnorderedAccessView*, 14> outputs{};
	for (UINT mip = 1; mip <= spdMips; ++mip)
		outputs[mip - 1] = mipUAVs[mip].get();
	outputs[12] = counter->uav.get();
	outputs[13] = mipUAVs[0].get();
	auto cb = spdConstants->resource.get();
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetUnorderedAccessViews(0, UINT(outputs.size()), outputs.data(), nullptr);
	context->CSSetShader(spd, nullptr, 0);
	context->Dispatch(groupsX, groupsY, 1);
	outputs.fill(nullptr);
	context->CSSetUnorderedAccessViews(0, UINT(outputs.size()), outputs.data(), nullptr);
	for (UINT mip = spdMips + 1; mip < mipCount; ++mip)
		reduce(mip);
	valid = true;
	return true;
}
