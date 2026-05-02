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
    echo "macOS detected. If Terraria is running under Rosetta (x86_64), build with: make DARWIN_ARCHS=\"x86_64\""
    echo "For a universal dylib, build with: make DARWIN_ARCHS=\"arm64 x86_64\""

    NATIVE_LIB_NAME="clrhook.dylib"
    PRELOAD_HINT='DYLD_INSERT_LIBRARIES=./clrhook.dylib DYLD_FORCE_FLAT_NAMESPACE=1 ./TheGame'

    LIB_PATH="$DESTINATION_PATH/MacOS/"
    PLUGINS_PATH="$DESTINATION_PATH/Resources/clrhook/plugins"
else
    NATIVE_LIB_NAME="clrhook.so"
    PRELOAD_HINT='LD_PRELOAD=./clrhook.so ./TheGame'

    LIB_PATH="$DESTINATION_PATH/"
    PLUGINS_PATH="$DESTINATION_PATH/clrhook/plugins"
fi

make && rm -f "$DESTINATION_PATH/$NATIVE_LIB_NAME" && cp "$NATIVE_LIB_NAME" "$LIB_PATH"
cd ..

cd plugins/demo
dotnet build -c Release && rm -f "$PLUGINS_PATH/ClrHook.Demo.dll" && cp ./bin/Release/$TARGET_RUNTIME/ClrHook.Demo.dll "$PLUGINS_PATH/ClrHook.Demo.dll"
cd ../..

echo "Build complete!"
echo "Run using $PRELOAD_HINT"
