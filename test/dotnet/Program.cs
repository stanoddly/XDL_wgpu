using System;
using System.Runtime.InteropServices;

namespace XDL_wgpu.LinkTest;

// Each call binds one archive's P/Invoke module; the GPU driver list shows whose SDL_GPU* definitions the link kept.
internal static partial class Program
{
    private static int Main()
    {
        Console.WriteLine($"SDL {SDL_GetVersion()}, SDL_image {IMG_Version()}, SDL_mixer {MIX_Version()}, SDL_ttf {TTF_Version()}");
        int driverCount = SDL_GetNumGPUDrivers();
        string? firstDriver = driverCount > 0 ? Marshal.PtrToStringUTF8(SDL_GetGPUDriver(0)) : null;
        Console.WriteLine($"GPU drivers: {driverCount}, first: {firstDriver}");
        bool ttf = TTF_Init();
        bool mixer = MIX_Init();
        Console.WriteLine($"TTF_Init: {ttf}, MIX_Init: {mixer}");

        bool passed = driverCount == 1 && firstDriver == "webgpu" && ttf && mixer;
        Console.WriteLine(passed ? "XDL_wgpu link test PASSED" : "XDL_wgpu link test FAILED");
        return passed ? 0 : 1;
    }

    [LibraryImport("SDL3")]
    private static partial int SDL_GetVersion();

    [LibraryImport("SDL3")]
    private static partial int SDL_GetNumGPUDrivers();

    [LibraryImport("SDL3")]
    private static partial IntPtr SDL_GetGPUDriver(int index);

    [LibraryImport("SDL3_image")]
    private static partial int IMG_Version();

    [LibraryImport("SDL3_mixer")]
    private static partial int MIX_Version();

    [LibraryImport("SDL3_mixer")]
    [return: MarshalAs(UnmanagedType.U1)]
    private static partial bool MIX_Init();

    [LibraryImport("SDL3_ttf")]
    private static partial int TTF_Version();

    [LibraryImport("SDL3_ttf")]
    [return: MarshalAs(UnmanagedType.U1)]
    private static partial bool TTF_Init();
}
