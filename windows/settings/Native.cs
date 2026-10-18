using System.Runtime.InteropServices;

namespace Pixwall.Settings;

internal static partial class Native
{
    [LibraryImport("user32.dll", EntryPoint = "GetDpiForWindow")]
    public static partial uint ForWindow(nint hwnd);

    /// 題名で窓を探して前に出す
    public static void BringToFront(string title)
    {
        var hwnd = FindWindowW(null, title);
        if (hwnd == 0) return;
        ShowWindow(hwnd, 9);  // SW_RESTORE
        SetForegroundWindow(hwnd);
    }

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindowW(string? className, string? windowName);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool ShowWindow(nint hwnd, int cmd);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetForegroundWindow(nint hwnd);
}
