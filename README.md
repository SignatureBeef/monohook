## What is this?
ClrHook is a lightweight native library that enables drop-in managed .NET plugins to load at runtime without modifying the target application.

It provides a unified plugin model across runtimes by wrapping CoreCLR’s built-in [Host Startup Hook](https://github.com/dotnet/runtime/blob/main/docs/design/features/host-startup-hook.md) feature, and backporting equivalent behavior to Mono so that plugins work transparently on both runtimes.

## Requirements
- Linux x64 or OSX Arm64
- Mono or CoreCLR runtime based application

## Usage
1. Build `coreclr` (see `build.sh` or your preferred build method).
3. Launch your Mono application with `coreclr` injected, e.g.:
   ```sh
   # Linux
   LD_PRELOAD=/path/to/coreclr.so ./TheGame
   # OSX
   DYLD_INSERT_LIBRARIES=./clrhook.dylib ./TheGame
   ```
   Initial run will create the directory structures, so close the application and proceed.
2. Place your plugin DLLs in the `coreclr/plugins/` directory. Each plugin should have a class `StartupHook` with a static `Initialize` method ([see clr docs](https://github.com/dotnet/runtime/blob/main/docs/design/features/host-startup-hook.md)).
5. Run again and check the output for plugin load status and logs.

### Plugin Examples
- [Demo Plugin](./plugins/demo/)
- [Terraria](https://github.com/SignatureBeef/ClrHook.Terraria)


## Notes
- Tested on Linux x64 & OSX Arm64 (Apple Silicon) on games:
  - Terraria via Steam - Mono based runtime embedded (MonoKickstart/FNA)
  - Stardew Valley via Steam - Coreclr based runtime (MonoGame)
- Plugins must be compatible with the target application's Mono version and runtime configuration.
- This tool is is provided without warranty. Use at your own risk, as with anything...

