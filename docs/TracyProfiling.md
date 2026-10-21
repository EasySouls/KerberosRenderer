# Tracy profiling

The engine keeps its existing Chrome-trace JSON profiler and can additionally
send live data to Tracy when configured with `-DKBR_ENABLE_TRACY=ON`.

## Build the client

Tracy is tracked as the `KerberosEngine/ThirdParty/tracy` submodule. Initialize
all submodules before configuring the project:

```powershell
git submodule update --init --recursive
cmake -S . -B out\build\tracy-client -DKBR_ENABLE_TRACY=ON
cmake --build out\build\tracy-client --config Debug
```

The application is the Tracy client. It does not require the viewer to be
running; when the viewer is available, Tracy discovers it on the local machine
and streams the instrumented zones and frame marks.

## Build and run the viewer

Build the separate Tracy Profiler application from the submodule:

```powershell
.\Scripts\BuildTracyProfiler.ps1
.\Scripts\RunTracyProfiler.ps1
```

Start the viewer before the instrumented `KerberosEditor` for a live capture.
Use the viewer to save a capture after reproducing the workload. The Tracy
viewer is not part of the KerberosRenderer application target and is not
required for normal builds.

## Instrumentation

Use `KBR_ENABLE_TRACY` only for profiling builds. `KBR_TRACY_SCOPE` and
`KBR_TRACY_FRAME_MARK` become no-ops when Tracy is disabled, while the existing
`KBR_PROFILE_*` macros continue to produce the current JSON profiles.
