using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace Pixwall.Settings;

/// 常駐側（pixwall.exe）とのやり取り。隠れた窓（pixwall-message）へ WM_COPYDATA で頼む。
/// 頼みごとの値は常駐側の main.cpp の Request とそろえる
public static partial class Resident
{
    private enum Request : ulong
    {
        ReloadConfig = 1,
        Refresh = 2,
        SetPaused = 3,
        Quit = 4,
        IsPaused = 5,
    }

    /// config.json を読み直してもらう。常駐側が動いていなければ起動する（起動すると config.json を読む）
    public static void ReloadConfig()
    {
        if (Send(Request.ReloadConfig) == 0) Start();
    }

    public static void Refresh() => Send(Request.Refresh);

    public static void SetPaused(bool paused) => Send(Request.SetPaused, paused ? (byte)1 : (byte)0);

    public static void Quit() => Send(Request.Quit);

    public static bool IsPaused => Send(Request.IsPaused) == 2;

    public static bool IsRunning => FindWindowW("pixwall-message", null) != 0;

    /// 設定画面は pixwall.exe の隣の settings フォルダに置く
    private static void Start()
    {
        var exe = Path.Combine(AppContext.BaseDirectory, "..", "pixwall.exe");
        if (File.Exists(exe)) Process.Start(new ProcessStartInfo(Path.GetFullPath(exe)) { UseShellExecute = true });
    }

    private static nint Send(Request request, byte? value = null)
    {
        var hwnd = FindWindowW("pixwall-message", null);
        if (hwnd == 0) return 0;
        var buffer = Marshal.AllocHGlobal(1);
        try
        {
            Marshal.WriteByte(buffer, value ?? 0);
            var data = new CopyDataStruct
            {
                dwData = (nuint)request,
                cbData = value is null ? 0 : 1,
                lpData = value is null ? 0 : buffer,
            };
            return SendMessageW(hwnd, WM_COPYDATA, 0, ref data);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private const uint WM_COPYDATA = 0x004A;

    [StructLayout(LayoutKind.Sequential)]
    private struct CopyDataStruct
    {
        public nuint dwData;
        public int cbData;
        public nint lpData;
    }

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindowW(string? className, string? windowName);

    [LibraryImport("user32.dll")]
    private static partial nint SendMessageW(nint hwnd, uint msg, nint wParam, ref CopyDataStruct lParam);
}
