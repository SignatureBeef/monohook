## What is this?
ClrHook is a shared library (`clrhook.so/dylib`) designed to be injected into mono/clr-based applications at runtime. It hooks into the runtime, allowing you to dynamically load and execute managed .NET plugins at runtime, without modifying the target application's binaries.

## What can it do?
- Load and execute arbitrary managed .NET plugins into any mono/coreclr application at runtime.
- Enable advanced modding, debugging, or instrumentation scenarios for Mono applications.
- No need to recompile or patch the target application.

## Requirements
- Linux x64
- Mono runtime (target application must use Mono)

## Usage
1. Build `coreclr.so` (see `build.sh` or your preferred build method).
3. Launch your Mono application with `coreclr.so` injected, e.g.:
   ```sh
   CORECLR_VERBOSE=1 LD_PRELOAD=/path/to/coreclr.so ./TheGame
   ```
   Initial run will create the directory structures, so close the application and proceed.
2. Place your plugin DLLs in the `coreclr/plugins/` directory. Each plugin should have a class `StartupHook` with a static `Initialize` method ([see clr docs](https://github.com/dotnet/runtime/blob/main/docs/design/features/host-startup-hook.md)).
5. Run again and check the output for plugin load status and logs.

### Plugin Examples
- [Demo Plugin](./plugins/demo/)
- [Terraria](https://github.com/SignatureBeef/ClrHook.Terraria)


## Notes
- Mainly tested on Linux x64 on a Terraria steam install (using a mono embedded via MonoKickstart)
- OSX Apple Silicon (ARM64) is also working, albiet fragile
- Plugins must be compatible with the target application's Mono version and runtime configuration.
- This tool is is provided without warranty. Use at your own risk, as with anything...

