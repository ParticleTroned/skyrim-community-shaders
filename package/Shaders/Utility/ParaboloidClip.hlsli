#if defined(GEOMETRYSHADER) && defined(RENDER_SHADOWMAP_PB) && SHADOWFILTER != 2

// Shared-edge intersections use the same arithmetic from either triangle.
VS_OUTPUT ParaboloidMix(VS_OUTPUT a, VS_OUTPUT b, float t)
{
	VS_OUTPUT o = a;
#	if defined(ALPHA_TEST) || defined(RENDER_NORMAL) || defined(DEBUG_SHADOWSPLIT) || defined(RENDER_BASE_TEXTURE)
	o.TexCoord0 = lerp(a.TexCoord0, b.TexCoord0, t);
#	endif
#	if defined(RENDER_NORMAL)
	o.Normal = lerp(a.Normal, b.Normal, t);
#	endif
#	if defined(LOCALMAP_FOGOFWAR) || (defined(ALPHA_TEST) && defined(VC) && !defined(TREE_ANIM))
	o.Alpha = lerp(a.Alpha, b.Alpha, t);
#	endif
	o.ParaboloidViewPos = lerp(a.ParaboloidViewPos, b.ParaboloidViewPos, t);
	o.PositionCS.w = lerp(a.PositionCS.w, b.PositionCS.w, t);
	o.TexCoord1 = lerp(a.TexCoord1, b.TexCoord1, t);
	return o;
}

VS_OUTPUT ParaboloidProject(VS_OUTPUT o)
{
	const float radius = length(o.ParaboloidViewPos);
	const float denominator = o.ParaboloidViewPos.z + ParabolaParam.y * radius;
	// At the light origin the direction is undefined; keep the degenerate vertex finite.
	o.PositionCS.xy = denominator != 0 ? o.ParaboloidViewPos.xy / denominator : float2(0, 0);
	o.PositionCS.z = ParabolaParam.x * radius;
	o.TexCoord1.xy = o.PositionCS.zw;
#	if defined(OFFSET_DEPTH)
	o.PositionCS = FrameBuffer::OffsetClipDepth(o.PositionCS, 5.0);
#	endif
#	if defined(VR)
	Stereo::VR_OUTPUT projected = Stereo::GetVRVSOutput(o.PositionCS, o.EyeIndex);
	o.PositionCS = projected.VRPosition;
	o.ClipDistance = projected.ClipDistance;
	o.CullDistance = projected.CullDistance;
#	endif
	return o;
}

VS_OUTPUT ParaboloidIntersection(VS_OUTPUT a, VS_OUTPUT b)
{
	if (a.ParaboloidViewPos.z > b.ParaboloidViewPos.z) {
		VS_OUTPUT swap = a;
		a = b;
		b = swap;
	}
	const float t = a.ParaboloidViewPos.z / (a.ParaboloidViewPos.z - b.ParaboloidViewPos.z);
	VS_OUTPUT o = ParaboloidMix(a, b, t);
	o.ParaboloidViewPos.z = 0;
	o.TexCoord1.z = 0.5;
	return ParaboloidProject(o);
}

void EmitParaboloidTriangle(VS_OUTPUT a, VS_OUTPUT b, VS_OUTPUT c, inout TriangleStream<VS_OUTPUT> stream)
{
	stream.Append(a);
	stream.Append(b);
	stream.Append(c);
	stream.RestartStrip();
}

void EmitParaboloidQuad(VS_OUTPUT a, VS_OUTPUT b, VS_OUTPUT c, VS_OUTPUT d, inout TriangleStream<VS_OUTPUT> stream)
{
	stream.Append(b);
	stream.Append(c);
	stream.Append(a);
	stream.Append(d);
	stream.RestartStrip();
}

[maxvertexcount(4)] void main(triangle VS_OUTPUT input[3], inout TriangleStream<VS_OUTPUT> stream) {
	if (!all(isfinite(ParabolaParam)) || ParabolaParam.x <= 0 || abs(ParabolaParam.y) != 1)
		return;
	// Explicit local copies preserve defined system values when repairing origin vertices.
	VS_OUTPUT vertices[3] = { input[0], input[1], input[2] };
	float side[3] = { 0, 0, 0 };
	[unroll] for (uint i = 0; i < 3; ++i)
	{
		const float radiusSquared = dot(vertices[i].ParaboloidViewPos, vertices[i].ParaboloidViewPos);
		if (!isfinite(radiusSquared) || !isfinite(vertices[i].PositionCS.w))
			return;
		if (radiusSquared == 0)
			vertices[i] = ParaboloidProject(vertices[i]);
		side[i] = ParabolaParam.y * vertices[i].ParaboloidViewPos.z;
	}
	if (side[0] < 0 && side[1] < 0 && side[2] < 0)
		return;
	if (side[0] >= 0 && side[1] >= 0 && side[2] >= 0) {
		EmitParaboloidTriangle(vertices[0], vertices[1], vertices[2], stream);
		return;
	}
	const bool front0 = side[0] >= 0, front1 = side[1] >= 0, front2 = side[2] >= 0;
	const uint lone = front0 != front1 && front0 != front2 ? 0 : front1 != front0 && front1 != front2 ? 1 :
	                                                                                                    2;
	const VS_OUTPUT a = vertices[lone], b = vertices[(lone + 1) % 3], c = vertices[(lone + 2) % 3];
	const VS_OUTPUT ab = ParaboloidIntersection(a, b);
	const VS_OUTPUT ac = ParaboloidIntersection(a, c);
	if (side[lone] >= 0)
		EmitParaboloidTriangle(a, ab, ac, stream);
	else
		EmitParaboloidQuad(ab, b, c, ac, stream);
}
#endif
