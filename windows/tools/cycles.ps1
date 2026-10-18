# pixwall の 10 秒のサイクル数（百万/秒）
Add-Type -Namespace W -Name C -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool QueryProcessCycleTime(IntPtr h, out ulong c);'
$p = Get-Process pixwall
$a = 0; [W.C]::QueryProcessCycleTime($p.Handle, [ref]$a) | Out-Null
Start-Sleep 10
$b = 0; [W.C]::QueryProcessCycleTime($p.Handle, [ref]$b) | Out-Null
"{0:N1} Mcyc/s" -f (($b - $a) / 10 / 1e6)
