# Builds Bina and packages it into "dist\Bina" (Qt + FFmpeg DLLs next to the exe).
# Usage:  powershell -ExecutionPolicy Bypass -File deploy.ps1 [-Launch]
param([switch]$Launch)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$qt = "C:\Qt\6.10.1\mingw_64"
$ffmpeg = if ($env:FFMPEG_ROOT) { $env:FFMPEG_ROOT } else { "C:\ffmpeg" }
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$qt\bin;" + $env:PATH

# The exe can't be replaced while it runs
Get-Process "Bina", "Music App", "Music_App" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

Push-Location "$root\build"
ninja
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Build failed" }
Pop-Location

$dist = "$root\dist\Bina"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$root\build\Bina.exe" "$dist\Bina.exe"
windeployqt6.exe --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$dist\Bina.exe" | Out-Null
foreach ($dll in "avcodec-63", "avformat-63", "avutil-61", "swresample-7") {
    Copy-Item "$ffmpeg\bin\$dll.dll" $dist
}
Copy-Item "$root\src\resources\fonts\*-OFL.txt" $dist

Write-Host "Packaged into $dist"
if ($Launch) {
    Start-Process -FilePath "$dist\Bina.exe" -WorkingDirectory $dist
}
