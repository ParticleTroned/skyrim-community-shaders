// CSX-owned colour processing. No NVIDIA NR colour-space ABI is assumed.
// Every read is inside the evaluated rectangle; other output texels are undefined.
cbuffer ColorConstants : register(b0)
{
	uint2 Base;
	uint2 Size;
	uint Mode;
	uint SourceTransfer;
	uint ModelCodec;
	uint Radius;
	float WhitePoint;
	float DetailStrength;
	float AppearanceMix;
	float MaxDetailStops;
	float3 MaximumOutput;
	float Padding;
};
Texture2D<float4> Baseline : register(t0);
Texture2D<float4> Prepared : register(t1);
Texture2D<float4> Model : register(t2);
RWTexture2D<float4> Result : register(u0);

bool Finite3(float3 x) { return all((asuint(x) & 0x7f800000u) != 0x7f800000u); }
float3 EncodeSRGB(float3 x)
{
	float3 positive = max(x, 0.0);
	return float3(x.x <= 0.0031308 ? 12.92 * x.x : 1.055 * pow(positive.x, 1.0 / 2.4) - 0.055,
		x.y <= 0.0031308 ? 12.92 * x.y : 1.055 * pow(positive.y, 1.0 / 2.4) - 0.055,
		x.z <= 0.0031308 ? 12.92 * x.z : 1.055 * pow(positive.z, 1.0 / 2.4) - 0.055);
}
float3 DecodeSRGB(float3 x)
{
	float3 positive = max((x + 0.055) / 1.055, 0.0);
	return float3(x.x <= 0.04045 ? x.x / 12.92 : pow(positive.x, 2.4),
		x.y <= 0.04045 ? x.y / 12.92 : pow(positive.y, 2.4),
		x.z <= 0.04045 ? x.z / 12.92 : pow(positive.z, 2.4));
}
float3 ToWorking(float3 x) { return SourceTransfer == 2 ? DecodeSRGB(x) : x; }
float3 FromWorking(float3 x) { return SourceTransfer == 2 ? EncodeSRGB(x) : x; }

float3 EncodeModel(float3 x)
{
	if (ModelCodec == 0) return x;
	// Non-identity codecs require explicit linear input. Negative values are
	// excluded from this experimental proxy and retained from Baseline on resolve.
	x = max(x, 0.0);
	if (ModelCodec == 2) x /= WhitePoint + max(x.x, max(x.y, x.z));
	return EncodeSRGB(x);
}
bool DecodeModel(float3 x, out float3 result)
{
	result = x;
	if (!Finite3(x)) return false;
	if (ModelCodec == 0) {
		result = ToWorking(x);
	} else {
		if (any(x < 0.0)) return false;
		result = DecodeSRGB(x);
		if (ModelCodec == 2) {
			float peak = max(result.x, max(result.y, result.z));
			// Finite-precision proxy is not lossless. Reject a non-invertible or
			// ill-conditioned model result instead of manufacturing a highlight.
			if (peak >= 0.9999) return false;
			result *= WhitePoint / (1.0 - peak);
		}
	}
	return Finite3(result);
}

// Difference against the ACTUAL quantized model input, not a freshly encoded
// reference. This prevents a zero neural residual from becoming codec drift.
float3 Candidate(uint2 p, float3 b)
{
	float3 encodedReference = Prepared.Load(int3(p, 0)).rgb;
	float3 encodedModel = Model.Load(int3(p, 0)).rgb;
	if (all(encodedReference == encodedModel)) return b;
	if (ModelCodec != 0 && any(Baseline.Load(int3(p, 0)).rgb < 0.0)) return b;
	float3 reference, model;
	if (!DecodeModel(encodedReference, reference) || !DecodeModel(encodedModel, model)) return b;
	float3 candidate = b + (model - reference);
	return Finite3(candidate) ? candidate : b;
}
float Luma(float3 x) { return dot(max(x, 0.0), float3(0.2126, 0.7152, 0.0722)); }
float LogDelta(float3 b, float3 n)
{
	return clamp(log2(max(Luma(n), 1e-6)) - log2(max(Luma(b), 1e-6)), -8.0, 8.0);
}

[numthreads(8, 8, 1)] void Prepare(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Size)) return;
	uint2 p = Base + id.xy;
	float4 b = Baseline.Load(int3(p, 0));
	float3 encoded = EncodeModel(b.rgb);
	Result[p] = float4(Finite3(encoded) ? encoded : 0.0, b.a);
}

[numthreads(8, 8, 1)] void Resolve(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Size)) return;
	uint2 p = Base + id.xy;
	float4 original = Baseline.Load(int3(p, 0));
	float3 b = ToWorking(original.rgb);
	if (!Finite3(b) ||
		all(Prepared.Load(int3(p, 0)).rgb == Model.Load(int3(p, 0)).rgb) ||
		(Mode == 2 && DetailStrength == 0.0 && AppearanceMix == 0.0)) {
		Result[p] = original;
		return;
	}
	float3 n = Candidate(p, b);
	float3 result = n;
	if (Mode == 2 && AppearanceMix < 1.0) {
		float3 detail = b;
		if (DetailStrength > 0.0 && MaxDetailStops > 0.0 && Luma(b) > 1e-6 && all(b >= 0.0)) {
			float centreLog = log2(max(Luma(b), 1e-6));
			float sum = 0.0, weightSum = 0.0;
			int radius = int(Radius);
			[loop] for (int y = -radius; y <= radius; ++y) {
				[loop] for (int x = -radius; x <= radius; ++x) {
					uint2 q = uint2(clamp(int2(p) + int2(x, y), int2(Base), int2(Base + Size - 1)));
					float3 qb = ToWorking(Baseline.Load(int3(q, 0)).rgb);
					if (!Finite3(qb) || any(qb < 0.0)) continue;
					float3 qn = Candidate(q, qb);
					float weight = exp2(-abs(log2(max(Luma(qb), 1e-6)) - centreLog)) /
						(1.0 + float(x * x + y * y));
					sum += weight * LogDelta(qb, qn);
					weightSum += weight;
				}
			}
			float residual = LogDelta(b, n) - sum / max(weightSum, 1e-6);
			float stops = clamp(DetailStrength * residual, -MaxDetailStops, MaxDetailStops);
			// Smoothly suppress gain on near-black baseline pixels.
			detail = b * exp2(stops * saturate(Luma(b) / 1e-4));
		}
		result = lerp(detail, n, AppearanceMix);
	}
	float3 encoded = FromWorking(result);
	// Preserve the baseline for finite-precision overflow and unsupported
	// negatives. Do not clamp valid HDR highlights to display white here.
	if (!Finite3(encoded) || any(encoded < 0.0) || any(encoded > MaximumOutput)) encoded = original.rgb;
	Result[p] = float4(encoded, original.a);
}
