# 保存する画面（Save As）で、ファイル名はそのままに「保存」（IDOK）を押す
Add-Type -Namespace W -Name S -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
[DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
'@
$dlg = [W.S]::FindWindowW('#32770', 'Save As')
"dlg=$dlg"
if ($dlg -ne [IntPtr]::Zero) { [W.S]::PostMessageW($dlg, 0x0111, 1, 0) | Out-Null }
