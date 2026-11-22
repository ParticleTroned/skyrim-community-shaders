# Rebuilding the bundled RenderDoc DLL

Extract the adjacent `renderdoc-*.tar.gz` and copy `Directory.Build.props`
into the extracted source root. The properties retain the fork distribution
identity. The archive includes all source and third-party notices.

The packaged DLL was built with Visual Studio 2026's v145 toolset, x64,
Release. From a Visual Studio developer PowerShell in the source root:

```powershell
msbuild renderdoc.sln '/t:DLL\renderdoc' /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
```

The output is `x64/Release/renderdoc.dll`. Building from the source archive
retains the distribution version in the supplied properties; the optional
Git-commit export requires a Git checkout at the commit named in the parent
README. To obtain that checkout, clone
`https://github.com/ParticleTroned/renderdoc.git` with history and tags,
check out that commit, then copy the supplied properties into its root.

For matching replay tools, follow `docs/CONTRIBUTING/Compiling.md` in this
source tree. The AIO includes the capture DLL, not the standalone Qt GUI.
