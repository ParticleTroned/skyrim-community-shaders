# devbench API overlay port

This overlay pins the MIT-licensed devbench cross-plugin API used by the
optional `DEVBENCH_BRIDGE` build. The API registers Community Shaders tools in
the external devbench SKSE host; it does not embed an MCP server in this DLL.

To update the API, bump the pinned devbench commit and archive hash in
`portfile.cmake`, then update the port version. The external devbench plugin is
a separate runtime dependency and the bridge remains inert when it is absent.
