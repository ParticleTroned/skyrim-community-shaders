# devbench API overlay port

This overlay pins the MIT-licensed devbench cross-plugin API used by the
optional `DEVBENCH_BRIDGE` build, fetched from `ParticleTroned/devbench` at
an immutable revision. The API registers CSX tools in
the external devbench SKSE host; it does not embed an MCP server in this DLL.

To update the API, bump the pinned devbench commit and archive hash in
`portfile.cmake`, then update the port version. The external devbench plugin is
a separate runtime dependency and the bridge remains inert when it is absent.

The interface retains its original vtable slots. The appended time-scale
methods require host build 12000 or newer; consumers must check
`GetBuildNumber()` before calling them. Updating this API port does not
update an installed host or its MCP transport. Install the matching fork
host separately to use its tool-discovery and transport improvements.
