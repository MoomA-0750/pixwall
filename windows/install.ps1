# pixwall を入れる。Windows PowerShell 5.1 が日本語を読めるよう、このファイルは BOM 付きの UTF-8 で置く。
# ビルドした build\pixwall.exe を %LOCALAPPDATA%\Programs\pixwall に置き、
# スタートメニューに「pixwall」を作る。管理者の権限は要らない（自分のユーザーにだけ入れる）
#   powershell -ExecutionPolicy Bypass -File install.ps1            入れる・入れ直す
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Image <画像のパス>  入れて、その画像で始める
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall 外す（設定の config.json は残す）
param([string]$Image, [switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$dir = Join-Path $env:LOCALAPPDATA 'Programs\pixwall'
$exe = Join-Path $dir 'pixwall.exe'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'pixwall.lnk'

# 動いている pixwall は、片付けてから終わるよう /f を付けずに止める
Get-Process pixwall -ErrorAction SilentlyContinue | ForEach-Object {
    taskkill /pid $_.Id | Out-Null
    $_.WaitForExit(5000) | Out-Null
}

if ($Uninstall) {
    Remove-Item $shortcut -ErrorAction SilentlyContinue
    Remove-Item $dir -Recurse -ErrorAction SilentlyContinue
    Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name pixwall -ErrorAction SilentlyContinue
    "外しました"
    return
}

$built = Join-Path $PSScriptRoot 'build\pixwall.exe'
if (-not (Test-Path $built)) { throw "先にビルドしてください: $built が無い" }
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item $built $exe -Force

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = $exe
$link.WorkingDirectory = $dir
$link.Description = 'ドット絵の GIF を壁紙として動かす'
$link.Save()

# 入れたものから始める。ログイン時の起動（Run）も、ここに入れた exe を指すよう書き直される
if ($Image) { Start-Process $exe -ArgumentList ('"' + (Resolve-Path $Image) + '"') } else { Start-Process $exe }
"入れました: $exe"
