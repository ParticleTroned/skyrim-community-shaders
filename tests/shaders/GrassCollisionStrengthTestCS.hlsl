#include "Common/Math.hlsli"
struct VS_INPUT
{
	float4 Color, InstanceData1, InstanceData2, InstanceData3, InstanceData4, Position;
};
cbuffer NativeWorld : register(b0)
{
	row_major float4x4 World[1];
	row_major float4x4 PreviousWorld[1];
	float3 ScaleMask;
};
#include "GrassCollision/GrassCollision.hlsli"
struct Case
{
	float4 color, root, position, distance;
};
struct Result
{
	float4 current, previous;
};
StructuredBuffer<Case> Cases : register(t0);
RWStructuredBuffer<Result> Results : register(u0);
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
	VS_INPUT input = (VS_INPUT)0;
	input.Color = Cases[id.x].color;
	input.InstanceData1 = Cases[id.x].root;
	input.InstanceData2.z = Cases[id.x].distance.y;
	input.InstanceData3.zw = Cases[id.x].distance.zw;
	input.Position = Cases[id.x].position - Cases[id.x].root;
	float3 initialPosition = Cases[id.x].root.xyz + float3(input.InstanceData2.z, input.InstanceData3.zw) * input.Position.z;
	float3 current = initialPosition, previous = current;
	float3 axis;
	float angle, compression;
	GrassCollision::ApplyDeformation(input, current, previous, Cases[id.x].distance.x, axis, angle, compression);
	Result output;
	output.current = float4(current - initialPosition, GrassCollision::DeformNormal(float3(0, 0, 1), axis, angle, compression).z);
	output.previous = float4(previous - initialPosition, 0);
	Results[id.x] = output;
}
