# 開いているファイルを選ぶ画面（#32770）のファイル名の欄にパスを入れ、「開く」（IDOK）を押す
param([string]$Path)
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
[DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
[DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
// ファイル名の欄（コントロール ID 1148 の ComboBox の中の Edit）を、入れ子をたどって探す
public static IntPtr FileNameEdit(IntPtr dlg) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(dlg, (h, l) => {
        var sb = new System.Text.StringBuilder(64); GetClassNameW(h, sb, 64);
        if (sb.ToString() == "Edit" && GetDlgCtrlID(GetParent(h)) == 1148) { found = h; return false; }
        return true;
    }, IntPtr.Zero);
    return found;
}
'@
$dlg = [W.U]::FindWindowW('#32770', 'Open')
$edit = [W.U]::FileNameEdit($dlg)
"dlg=$dlg edit=$edit"
[W.U]::SendMessageW($edit, 0x000C, 0, $Path) | Out-Null   # WM_SETTEXT
[W.U]::PostMessageW($dlg, 0x0111, 1, 0) | Out-Null        # WM_COMMAND IDOK
Start-Sleep 2
Get-Content $env:APPDATA\pixwall\config.json
