# taa-renderdoc-ab.py - runtime A/B check for TAA shader refactors.
#
# This script runs inside RenderDoc's embedded Python via the renderdoc MCP Eval
# tool. The global ctx HandlerContext must be in scope; this is not a standalone
# command-line script.
#
# Purpose: validate behavior-preserving shader refactors whose compiled DXBC is
# expected to differ. It swaps the ISTemporalAA pixel shader on a single captured
# frame and diffs the output render target. Because the source textures and
# constants are frozen by the capture, the live shader, baseline DXBC, and
# candidate DXBC all run on byte-identical inputs.

import math
import os
import struct

import renderdoc as rd


# eid -> (original PS ResourceId, replacement ResourceId). Keep real resource
# objects so restore() does not round-trip them through strings.
_replacements = {}

# Above this baseline-vs-live mean_abs, the floor is implausible for ordinary
# runtime-vs-fxc residue. Treat it as a bad baseline/permutation instead of
# allowing an inflated floor to rubber-stamp a candidate.
FLOOR_SANITY_LIMIT = 1e-2


def _walk(actions):
    for action in actions:
        yield action
        for child in _walk(action.children):
            yield child


def _desc_res(desc):
    for attr in ("resource", "resourceId"):
        if hasattr(desc, attr):
            return getattr(desc, attr)
    return rd.ResourceId.Null


def _as_resid(shader):
    # GetShader returns a ResourceId on current RenderDoc; guard older
    # (id, reflection) tuples.
    return shader[0] if isinstance(shader, tuple) else shader


# ISTemporalAA's pixel shader binds these t0..t5 textures. Output-slot counting
# is unreliable on D3D11 because empty slots can show up as ResourceId::0.
_TAA_TEX = {
    "currentframetex",
    "historytex",
    "velocitytex",
    "depthtex",
    "masktex",
    "alphatex",
}


def find_candidates(srv_names, reverse=False, max_scan_drawcalls=0, stop_after=0):
    """Find draws whose pixel shader binds all requested SRV names.

    Match by resource fingerprint rather than draw index, because draw indices
    shift frame to frame. For large captures, use reverse=True and stop_after=1
    because post-process passes are near the end of the frame.
    """
    wanted = {name.lower() for name in srv_names}

    def work(ctrl):
        sdfile = ctrl.GetStructuredFile()
        draws = [
            action
            for action in _walk(ctrl.GetRootActions())
            if (action.flags & rd.ActionFlags.Drawcall)
        ]
        if reverse:
            draws = draws[::-1]
        if max_scan_drawcalls > 0:
            draws = draws[:max_scan_drawcalls]

        results = []
        for action in draws:
            ctrl.SetFrameEvent(action.eventId, True)
            reflection = ctrl.GetPipelineState().GetShaderReflection(rd.ShaderStage.Pixel)
            if not reflection:
                continue

            names = {res.name.lower() for res in reflection.readOnlyResources}
            if wanted.issubset(names):
                results.append({"eventId": action.eventId, "name": action.GetName(sdfile)})
                if stop_after > 0 and len(results) >= stop_after:
                    break
        return results

    return ctx.replay(work)


def taa_candidates(reverse=False, max_scan_drawcalls=0, stop_after=0):
    """Find ISTemporalAA candidates using the known t0..t5 SRV fingerprint."""
    return find_candidates(
        _TAA_TEX,
        reverse=reverse,
        max_scan_drawcalls=max_scan_drawcalls,
        stop_after=stop_after,
    )


def _tex_desc(ctrl, rid):
    for tex in ctrl.GetTextures():
        if tex.resourceId == rid:
            return tex
    return None


def grab_rt(eid, target_index=0):
    """Return (resourceId_str, raw_bytes, (width, height, format_name))."""

    def work(ctrl):
        ctrl.SetFrameEvent(eid, True)
        outputs = ctrl.GetPipelineState().GetOutputTargets()
        if target_index >= len(outputs):
            return (None, b"", (0, 0, "none"))

        rid = _desc_res(outputs[target_index])
        if str(rid) == "ResourceId::0":
            return (str(rid), b"", (0, 0, "none"))

        data = bytes(ctrl.GetTextureData(rid, rd.Subresource(0, 0, 0)))
        tex = _tex_desc(ctrl, rid)
        meta = (tex.width, tex.height, str(tex.format.Name())) if tex else (0, 0, "?")
        return (str(rid), data, meta)

    return ctx.replay(work)


def replace_ps_with_dxbc(eid, dxbc_path, entry="main"):
    """Replace the event's pixel shader with precompiled DXBC."""
    path = os.path.expandvars(os.path.expanduser(dxbc_path))
    if not os.path.isfile(path):
        return {"ok": False, "errors": "DXBC not found: " + path}

    try:
        with open(path, "rb") as handle:
            blob = handle.read()
    except OSError as err:
        return {"ok": False, "errors": "cannot read %s: %r" % (path, err)}

    if eid in _replacements:
        restore(eid)

    def work(ctrl):
        ctrl.SetFrameEvent(eid, True)
        original = _as_resid(ctrl.GetPipelineState().GetShader(rd.ShaderStage.Pixel))
        new_id, errors = ctrl.BuildTargetShader(
            entry,
            rd.ShaderEncoding.DXBC,
            blob,
            rd.ShaderCompileFlags(),
            rd.ShaderStage.Pixel,
        )
        ok = new_id != rd.ResourceId.Null
        if ok:
            ctrl.ReplaceResource(original, new_id)
            ctrl.SetFrameEvent(eid, True)
        return ok, original, new_id, str(errors)

    ok, original, new_id, errors = ctx.replay(work)
    if ok:
        _replacements[eid] = (original, new_id)
    return {"ok": ok, "errors": errors}


def restore(eid):
    """Undo a replacement made at eid."""
    pair = _replacements.pop(eid, None)
    if not pair:
        return False

    original, new_id = pair

    def work(ctrl):
        ctrl.RemoveReplacement(original)
        try:
            ctrl.FreeTargetResource(new_id)
        except Exception:
            pass
        ctrl.SetFrameEvent(eid, True)
        return True

    return ctx.replay(work)


def _diff(a_bytes, b_bytes, meta):
    """Byte fast path plus sampled float-magnitude estimate."""
    out = {
        "size_a": len(a_bytes),
        "size_b": len(b_bytes),
        "format": meta[2],
        "dims": [meta[0], meta[1]],
        "mean_abs": None,
        "max_abs": None,
    }

    if not a_bytes or not b_bytes:
        out["verdict"] = "NO-DATA"
        return out

    if a_bytes == b_bytes:
        out["verdict"] = "IDENTICAL"
        out["bytes_differing"] = 0
        out["mean_abs"] = 0.0
        out["max_abs"] = 0.0
        out["sample_frac_differing"] = 0.0
        return out

    if len(a_bytes) != len(b_bytes):
        out["verdict"] = "DIFFERS"
        out["note"] = "size mismatch"
        return out

    fmt = meta[2].lower()
    packed_10bit = "10g10b10a2" in fmt
    if packed_10bit:
        element_size, unpack_code = 4, None
    elif "16_float" in fmt:
        element_size, unpack_code = 2, "<e"
    elif "32_float" in fmt:
        element_size, unpack_code = 4, "<f"
    elif "8_unorm" in fmt:
        element_size, unpack_code = 1, None
    else:
        out["verdict"] = "NOT-COMPARABLE"
        out["note"] = "unrecognized format: " + meta[2]
        return out

    total = len(a_bytes) // element_size
    stride = max(1, total // 100000)
    max_abs = 0.0
    abs_sum = 0.0
    count = 0
    diff_count = 0
    packed_channels = (
        (0, 1023, 1023.0),
        (10, 1023, 1023.0),
        (20, 1023, 1023.0),
        (30, 3, 3.0),
    )

    for i in range(0, total, stride):
        offset = i * element_size
        if packed_10bit:
            px = struct.unpack_from("<I", a_bytes, offset)[0]
            py = struct.unpack_from("<I", b_bytes, offset)[0]
            delta = max(
                abs(((px >> shift) & mask) / denom - ((py >> shift) & mask) / denom)
                for (shift, mask, denom) in packed_channels
            )
        elif unpack_code:
            x = struct.unpack_from(unpack_code, a_bytes, offset)[0]
            y = struct.unpack_from(unpack_code, b_bytes, offset)[0]
            delta = abs(x - y)
            if math.isnan(delta):
                delta = 0.0 if (x == y or (math.isnan(x) and math.isnan(y))) else float("inf")
        else:
            delta = abs(a_bytes[offset] - b_bytes[offset]) / 255.0

        if delta > 0:
            diff_count += 1
        if delta > max_abs:
            max_abs = delta
        abs_sum += delta
        count += 1

    out["sampled_elems"] = count
    out["sample_frac_differing"] = (diff_count / count) if count else 0.0
    out["max_abs"] = max_abs
    out["mean_abs"] = (abs_sum / count) if count else 0.0
    out["verdict"] = "COMPARED"
    return out


def ab(eid, candidate_dxbc, baseline_dxbc=None, entry="main"):
    """Run a full same-frame A/B on one event."""
    rid, live_bytes, meta = grab_rt(eid)
    report = {
        "eventId": eid,
        "rt": rid,
        "rt_meta": {"dims": [meta[0], meta[1]], "format": meta[2]},
    }

    if baseline_dxbc:
        result = replace_ps_with_dxbc(eid, baseline_dxbc, entry)
        if not result["ok"]:
            report["baseline_vs_live"] = {
                "verdict": "BUILD-FAILED",
                "errors": result["errors"],
            }
        else:
            try:
                _, baseline_bytes, _ = grab_rt(eid)
                report["baseline_vs_live"] = _diff(live_bytes, baseline_bytes, meta)
            finally:
                restore(eid)

    result = replace_ps_with_dxbc(eid, candidate_dxbc, entry)
    if not result["ok"]:
        report["candidate_vs_live"] = {
            "verdict": "BUILD-FAILED",
            "errors": result["errors"],
        }
        report["verdict"] = "BUILD-FAILED"
        return report

    try:
        _, candidate_bytes, _ = grab_rt(eid)
        report["candidate_vs_live"] = _diff(live_bytes, candidate_bytes, meta)
    finally:
        restore(eid)

    base = report.get("baseline_vs_live") or {}
    floor = base.get("mean_abs")
    cand_mean = report["candidate_vs_live"].get("mean_abs")

    if cand_mean is None:
        report["verdict"] = "NOT-COMPARABLE"
    elif floor is not None:
        report["noise_floor_mean"] = floor
        if floor > FLOOR_SANITY_LIMIT:
            report["verdict"] = "UNVERIFIED-BASELINE"
            report["note"] = (
                "baseline noise floor %.4g exceeds %.4g; verify defines, "
                "permutation, and baseline ref"
                % (floor, FLOOR_SANITY_LIMIT)
            )
        else:
            report["verdict"] = (
                "EQUIVALENT" if cand_mean <= max(floor * 3.0, 1e-4) else "DIFFERS"
            )
    elif baseline_dxbc:
        report["verdict"] = "UNVERIFIED-BASELINE"
    else:
        report["verdict"] = "EQUIVALENT" if cand_mean <= 1e-3 else "DIFFERS"

    return report
