## What is this?
MonoHook is a shared library (`monohook.so/dylib`) designed to be injected into Mono-based applications at runtime. It hooks into the Mono runtime, allowing you to dynamically load and execute managed .NET plugins at runtime, without modifying the target application's binaries.

## How does it work?
- **Dynamic Library Injection:** `monohook.so` is injected into a running Mono process (e.g., via `LD_PRELOAD`).
- **Mono Runtime Hooking:** It patches the `mono_jit_exec` function to intercept the Mono application's startup.
- **Plugin Discovery:** On startup, it scans the `monohook/plugins/` directory for all `.dll` files.
- **Plugin Loading:** Each plugin DLL is loaded into the Mono AppDomain using `mono_domain_assembly_open`.
- **Plugin Execution:** For each loaded plugin, it looks for the class `MonoHook.Loader` and calls its static `Run` method (with or without arguments, depending on the method signature).
- **Logging:** Verbose logging can be enabled by setting the environment variable `MONOHOOK_VERBOSE=1`.

## What can it do?
- Load and execute arbitrary managed .NET plugins into any Mono-based application at runtime.
- Enable advanced modding, debugging, or instrumentation scenarios for Mono applications.
- No need to recompile or patch the target application.

## Requirements
- Linux x64
- Mono runtime (target application must use Mono)

## Usage
1. Build `monohook.so` (see `build.sh` or your preferred build method).
2. Place your plugin DLLs in the `monohook/plugins/` directory. Each plugin should have a class `MonoHook.Loader` with a static `Run` method.
3. Launch your Mono application with `monohook.so` injected, e.g.:
   ```sh
   MONOHOOK_VERBOSE=1 LD_PRELOAD=/path/to/monohook.so ./TheGame
   ```
4. Check the output for plugin load status and logs.

### Plugin Examples
- [Demo Plugin](./plugins/demo/)
- [Terraria](https://github.com/SignatureBeef/MonoHook.Terraria)

## Expected Output

When everything is working correctly, you should see console output similar to the following when launching your Mono application with MonoHook:

```
[monohook] intercepted mono_jit_exec successfully
[MonoHook.NET] Demo Plugin Loaded!
SDL v3004003.SDL-3.4.3--128-NOTFOUND (Github Workflow)
FNA3D v26.3.0
FAudio v26.3.0
Error Logging Enabled.
SDL Video Driver: wayland
```

This indicates that MonoHook has successfully hooked into the Mono runtime, loaded your plugins, and the application is running as expected.

## Notes
- Mainly tested on Linux x64 on a Terraria steam install (using a mono embedded via MonoKickstart)
- OSX Apple Silicon (ARM64) is also working, albiet fragile
- Plugins must be compatible with the target application's Mono version and runtime configuration.
- This tool is is provided without warranty. Use at your own risk, as with anything...

