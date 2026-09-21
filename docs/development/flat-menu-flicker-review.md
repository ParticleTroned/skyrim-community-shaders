# Flat-screen menu stability

The first-time setup dialog and theme application both interact with live
ImGui font state. ImGui retains a window's font scale between frames, while
saved theme geometry can contain stale base-font and DPI values. Reusing
either value makes otherwise fixed menu content alternate size or inherit an
unrelated scale.

The setup dialog now normalizes its window font scale immediately after
`Begin()`, before measuring or positioning content. Theme application copies
the saved geometry and colors while retaining ImGui's live `FontSizeBase`,
`FontScaleDpi`, and pending next-frame base size.

The SE/AE Present hook already forwards `DXGI_PRESENT_TEST` before rendering,
frame accounting, UI, screenshots, or startup-blur state. This branch does
not contain the separate top-status displacement layout from the source
change, so no equivalent placement correction is required.

Live validation should exercise the welcome dialog for several consecutive
frames, switch themes at multiple DPI scales, and confirm that test presents
do not change frame counters or startup blur state.
