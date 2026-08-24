# CSX lossless capture API

CSX revision 5 exposes `ICSCaptureInterface001` through
`ICSInterface001::GetCaptureInterface001()`. The capture interface is deliberately
codec-neutral: CSX owns screenshots, lossless frame sets, and their manifest. It
does not compose video, select codecs, or capture audio.

## Output locations

Relative paths use Windows Known Folders rather than the Skyrim installation:

- Screenshots: `Pictures\Community Shaders\Screenshots`
- Frame sets: `Videos\Community Shaders\Frame Captures`

An absolute configured path remains absolute. If Windows cannot resolve the
appropriate Known Folder, the request fails with `kOutputUnavailable`; CSX does
not fall back to the game directory.

## Calls

`RequestScreenshot` schedules one lossless still. `StartFrameSequence` creates a
new session and begins capturing one frame at a time without blocking the render
thread on image encoding. `StopFrameSequence` changes the session to `kFlushing`;
clients should poll `GetCaptureStatus` until it becomes `kComplete` or `kFailed`.

In Skyrim VR, `kLeft` and `kRight` save the selected accepted eye submission.
`kBoth` saves a synchronized pair beneath `left` and `right`. Flat Skyrim treats
all eye selections as its single desktop image.

The interface is owned by CSX and must not be deleted. Calls may originate from
an SKSE plugin thread; capture itself is marshalled onto the render and worker
paths. A client should nevertheless serialize start/stop decisions and avoid
starting another session while the current session is capturing or flushing.

## Frame-set contract

Each session directory contains lossless numbered images and a JSON manifest:

```text
CS_sequence_<date>_<session>/
  sequence.json
  left/frame_000000001.bmp
  right/frame_000000001.bmp
```

BMP is the default for frame sequences because it trades large files for much
higher capture throughput. PNG remains available as a compact lossless option,
and ordinary screenshots keep their own independent format setting. Flat
sessions use `frames/`. During capture, `sequence.json.partial` is updated
periodically. The final `sequence.json` uses schema `csx.frame-sequence/1` and
contains the selected eye mode, monotonic timestamps in microseconds, written
and dropped counts, and relative paths for every scheduled frame. `audio` is
always `false` in version 1.

Companion mods should call `CopySequencePath` after retaining the session ID.
First call with a null buffer to obtain the required UTF-8 byte count, including
the NUL terminator, then call again with a suitably sized buffer.

## Capture indicator

While a sequence is recording, CSX draws one red dot after it has queued that
frame's lossless copy. The desktop indicator is therefore excluded from saved
frames. In VR, CSX uses a compositor overlay where the runtime exposes
`IVROverlay`, keeping the indicator outside the submitted eye textures as well.
