# Builds the plugin (zig c++ as mingw-w64 compiler) into build\MarkdownLive, ready to copy to plugins\.
# Zig is downloaded on first run (SHA-256 verified). Keep this file ASCII: Windows PowerShell 5.1 reads it as ANSI.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$zigVersion = '0.15.2'
$zigSha256 = '3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
$zig = "$root\_tools\zig\zig.exe"
$out = "$root\build\MarkdownLive"

if (-not (Test-Path $zig)) {
    $zip = "$env:TEMP\zig-$zigVersion.zip"
    Write-Host "Downloading zig $zigVersion..."
    Invoke-WebRequest "https://ziglang.org/download/$zigVersion/zig-x86_64-windows-$zigVersion.zip" -OutFile $zip -UseBasicParsing
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $zigSha256) { throw 'zig archive checksum mismatch.' }
    New-Item -ItemType Directory -Force "$root\_tools" | Out-Null
    Expand-Archive $zip "$root\_tools" -Force
    Rename-Item "$root\_tools\zig-x86_64-windows-$zigVersion" 'zig'
    Remove-Item $zip
}

if (Test-Path $out) { Remove-Item -LiteralPath $out -Recurse -Force }   # no leftovers from older builds
New-Item -ItemType Directory -Force "$out\assets" | Out-Null
$ErrorActionPreference = 'Continue'
& $zig c++ -target x86_64-windows-gnu -O2 -shared -std=c++20 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN `
    -I"$root\third_party\npp" -I"$root\third_party\webview2" `
    "$root\src\main.cpp" "$root\src\panel.cpp" "$root\src\layout.cpp" "$root\src\detect.cpp" "$root\src\edit.cpp" "$root\src\i18n.cpp" "$root\src\tools.cpp" "$root\src\sysprint.cpp" `
    -o "$out\MarkdownLive.dll" `
    -lole32 -loleaut32 -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -luser32 -lgdi32 -ladvapi32 -lgdiplus -ld3d11 -ld2d1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item "$root\assets\*" "$out\assets" -Recurse -Force
Move-Item "$out\assets\WebView2Loader.dll" "$out\WebView2Loader.dll" -Force
Remove-Item "$out\*.lib", "$out\*.pdb" -ErrorAction SilentlyContinue
Write-Host "Done: $out"
