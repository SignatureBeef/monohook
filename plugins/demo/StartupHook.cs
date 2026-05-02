using System;

internal class StartupHook
{
    static readonly bool IsVerbose = Environment.GetEnvironmentVariable("MONOHOOK_VERBOSE") == "1";

    static void Log(string message)
    {
        if (IsVerbose)
        {
            Console.WriteLine($"[MonoHook.NET] {message}");
        }
    }
    static void Error(string message)
    {
        Console.Error.WriteLine($"[MonoHook.NET] ERROR: {message}");
    }

    public static void Initialize()
    {
        Console.WriteLine("[MonoHook.NET] Demo Plugin Loaded!");
    }
}
