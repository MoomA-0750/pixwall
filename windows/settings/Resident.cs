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
        ExportStill = 6,
        RenderPreview = 7,
        FrameCount = 8,
        AutoColor = 9,
    }

    /// config.json を読み直してもらう。常駐側が動いていなければ起動する（起動すると config.json を読む）
    public static void ReloadConfig()
    {
        if (Send(Request.ReloadConfig) == 0) Start();
    }

    public static void Refresh() => Send(Request.Refresh);

    public static void SetPaused(bool paused) => Send(Request.SetPaused, [paused ? (byte)1 : (byte)0]);

    public static void Quit() => Send(Request.Quit);

    public static bool IsPaused => Send(Request.IsPaused) == 2;

    /// 静止画を書き出してもらう。主の画面は path に、ほかの画面は同じフォルダに書く
    public static bool ExportStill(int frame, string path) => Send(Request.ExportStill, Text($"{frame}\n{path}")) == 1;

    /// frame 枚目を、長い辺が maxSize に収まる整数倍で PNG に描いてもらう（プレビュー用）
    public static bool RenderPreview(int frame, int maxSize, string path) =>
        Send(Request.RenderPreview, Text($"{frame}\n{maxSize}\n{path}")) == 1;

    /// 動かしている画像のフレームの数。動いていない・画像が無ければ 0
    public static int FrameCount => (int)Send(Request.FrameCount);

    /// 自動の余白の色（画像の左上の1ドット、0xRRGGBB）。動いていない・画像が無ければ null
    public static int? AutoColor => Send(Request.AutoColor) is var v && v >= 0x1000000 ? (int)(v & 0xFFFFFF) : null;

    private static byte[] Text(string s) => System.Text.Encoding.Unicode.GetBytes(s);

    public static bool IsRunning => FindWindowW("pixwall-message", null) != 0;

    /// 設定画面は pixwall.exe の隣の settings フォルダに置く
    private static void Start()
    {
        var exe = Path.Combine(AppContext.BaseDirectory, "..", "pixwall.exe");
        if (File.Exists(exe)) Process.Start(new ProcessStartInfo(Path.GetFullPath(exe)) { UseShellExecute = true });
    }

    private static nint Send(Request request, byte[]? payload = null)
    {
        var hwnd = FindWindowW("pixwall-message", null);
        if (hwnd == 0) return 0;
        var length = payload?.Length ?? 0;
        var buffer = Marshal.AllocHGlobal(Math.Max(1, length));
        try
        {
            if (payload is not null) Marshal.Copy(payload, 0, buffer, length);
            var data = new CopyDataStruct
            {
                dwData = (nuint)request,
                cbData = length,
                lpData = length == 0 ? 0 : buffer,
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
