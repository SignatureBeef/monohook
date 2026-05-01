using System;
using System.IO;
using System.Reflection;

namespace MonoHook
{
    public class Startup
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

        public static void Run(AppDomain domain, Assembly assembly, string[] args)
        {
            if (IsVerbose)
            {
                Log($"[MonoHook.NET] Startup.Run() invoked on Domain: {domain.FriendlyName}, Assembly: {assembly.GetName().Name}");
                foreach (var arg in args)
                {
                    Log($" - {arg}");
                }

                Log("[MonoHook.NET] Startup.Run() completed!");
            }
            Console.WriteLine("[MonoHook.NET] Demo Plugin Loaded!");
        }
    }
}
