# Source environment variables
if [ -f .env ]; then
    set -a
    . ./.env
    set +a
fi
if [ -f .env.override ]; then
    set -a
    . ./.env.override
    set +a
fi

cd src
OS_NAME="$(uname -s)"

if [ "$OS_NAME" = "Darwin" ]; then
    NATIVE_LIB_NAME="monohook.dylib"
    PRELOAD_HINT='DYLD_INSERT_LIBRARIES=./monohook.dylib DYLD_FORCE_FLAT_NAMESPACE=1 ./Terraria'
    echo "macOS detected. If Terraria is running under Rosetta (x86_64), build with: make DARWIN_ARCHS=\"x86_64\""
    echo "For a universal dylib, build with: make DARWIN_ARCHS=\"arm64 x86_64\""

    LIB_PATH="$TERRARIA_PATH/MacOS/"
    PLUGINS_PATH="$TERRARIA_PATH/Resources/monohook/plugins"
else
    NATIVE_LIB_NAME="monohook.so"
    PRELOAD_HINT='LD_PRELOAD=./monohook.so ./Terraria'

    LIB_PATH="$TERRARIA_PATH/"
    PLUGINS_PATH="$TERRARIA_PATH/monohook/plugins"
fi

make && rm -f "$TERRARIA_PATH/$NATIVE_LIB_NAME" && cp "$NATIVE_LIB_NAME" "$LIB_PATH"
cd ..

cd plugins/demo
dotnet build -c Release && rm -f "$PLUGINS_PATH/MonoHook.Demo.dll" && cp ./bin/Release/net40/MonoHook.Demo.dll "$PLUGINS_PATH/MonoHook.Demo.dll"
cd ../..
# cd plugins/MonoMod
# dotnet build -c Release && rm -f "$PLUGINS_PATH/MonoHook.MonoMod.dll" && cp ./bin/Release/net40/MonoHook.MonoMod.dll "$PLUGINS_PATH/MonoHook.MonoMod.dll"
# cd ../..

echo "Build complete! Remember to set the environment variable MONOHOOK_VERBOSE=1 if you want verbose logging."
echo "Run using $PRELOAD_HINT"