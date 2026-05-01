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
make && rm -f "$TERRARIA_PATH/monohook.so" && cp monohook.so "$TERRARIA_PATH"
cd ..

cd plugins/demo
dotnet build -c Release && rm -f "$TERRARIA_PATH/monohook/plugins/MonoHook.Demo.dll" && cp ./bin/Release/net40/MonoHook.Demo.dll "$TERRARIA_PATH/monohook/plugins/MonoHook.Demo.dll"
cd ../..
# cd plugins/MonoMod
# dotnet build -c Release && rm -f "$TERRARIA_PATH/monohook/plugins/MonoHook.MonoMod.dll" && cp ./bin/Release/net40/MonoHook.MonoMod.dll "$TERRARIA_PATH/monohook/plugins/MonoHook.MonoMod.dll"
# cd ../..

echo "Build complete! Remember to set the environment variable MONOHOOK_VERBOSE=1 if you want verbose logging."
echo "Run using LD_PRELOAD=./monohook.so ./Terraria"