# build-windows.ps1
# Downloads pre-built FFmpeg shared libraries for Windows from BtbN/FFmpeg-Builds
# Usage: .\scripts\build-windows.ps1 [-Version 7.1]

param(
    [string]$Version = "7.1"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$FFmpegDir = Join-Path $ProjectRoot "windows\FFmpeg"
$BuildDir = Join-Path $ProjectRoot "ffmpeg\build"

Write-Host "==========================================" -ForegroundColor Green
Write-Host "FFmpeg Windows Setup (v$Version)" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green

# Determine download URL
if ($Version -eq "master") {
    $FileName = "ffmpeg-master-latest-win64-lgpl-shared.zip"
} else {
    $FileName = "ffmpeg-n$Version-latest-win64-lgpl-shared-$Version.zip"
}
$Url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$FileName"

Write-Host "URL: $Url"
Write-Host "Output: $FFmpegDir"
Write-Host ""

# Create build directory
New-Item -Path $BuildDir -ItemType Directory -Force | Out-Null

$ZipPath = Join-Path $BuildDir "ffmpeg-win64.zip"

# Download
Write-Host "Downloading pre-built FFmpeg $Version for Windows..." -ForegroundColor Yellow
# Use -L to follow redirects
Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UseBasicParsing
Write-Host "Downloaded: $([math]::Round((Get-Item $ZipPath).Length / 1MB, 1)) MB" -ForegroundColor Green

# Extract
Write-Host "Extracting..." -ForegroundColor Yellow
$ExtractDir = Join-Path $BuildDir "ffmpeg-extracted"
if (Test-Path $ExtractDir) { Remove-Item $ExtractDir -Recurse -Force }
Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force

# Find the extracted FFmpeg directory (e.g., ffmpeg-n7.1-latest-win64-lgpl-shared-7.1)
$Extracted = Get-ChildItem $ExtractDir -Directory | Select-Object -First 1
if (-not $Extracted) {
    Write-Error "Could not find extracted FFmpeg directory"
    exit 1
}
Write-Host "Found: $($Extracted.Name)" -ForegroundColor Green

# Copy to plugin directory
New-Item -Path "$FFmpegDir\bin" -ItemType Directory -Force | Out-Null
New-Item -Path "$FFmpegDir\lib" -ItemType Directory -Force | Out-Null
New-Item -Path "$FFmpegDir\include" -ItemType Directory -Force | Out-Null

$SrcDir = $Extracted.FullName

# Copy DLLs
Write-Host "Copying DLLs..." -ForegroundColor Yellow
Get-ChildItem "$SrcDir\bin\*.dll" | ForEach-Object {
    Copy-Item $_.FullName "$FFmpegDir\bin\" -Force
    Write-Host "  $($_.Name)"
}

# Copy import libraries (.lib or .dll.a)
Write-Host "Copying import libraries..." -ForegroundColor Yellow
$LibFiles = Get-ChildItem "$SrcDir\lib\*" -Include "*.lib","*.dll.a","*.def" -ErrorAction SilentlyContinue
if ($LibFiles) {
    $LibFiles | ForEach-Object {
        Copy-Item $_.FullName "$FFmpegDir\lib\" -Force
        Write-Host "  $($_.Name)"
    }
} else {
    Write-Host "  (no .lib files found - may need to generate from .def)" -ForegroundColor Yellow
}

# Copy headers
Write-Host "Copying headers..." -ForegroundColor Yellow
Copy-Item "$SrcDir\include\*" "$FFmpegDir\include\" -Recurse -Force
$HeaderDirs = Get-ChildItem "$FFmpegDir\include" -Directory | Select-Object -ExpandProperty Name
Write-Host "  Directories: $($HeaderDirs -join ', ')"

# Summary
Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "FFmpeg $Version installed to: $FFmpegDir" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green

# List DLLs with their sizes
Write-Host ""
Write-Host "DLLs:" -ForegroundColor Yellow
Get-ChildItem "$FFmpegDir\bin\*.dll" | ForEach-Object {
    Write-Host ("  {0,-30} {1,8} KB" -f $_.Name, [math]::Round($_.Length/1KB))
}

# Check for import libs
$ImportLibs = Get-ChildItem "$FFmpegDir\lib\*" -Include "*.lib","*.dll.a" -ErrorAction SilentlyContinue
if (-not $ImportLibs) {
    Write-Host ""
    Write-Host "WARNING: No .lib import libraries found!" -ForegroundColor Red
    Write-Host "BtbN builds use MinGW and provide .dll.a instead of .lib files." -ForegroundColor Red
    Write-Host "You may need to generate .lib files using:" -ForegroundColor Yellow
    Write-Host '  foreach ($dll in Get-ChildItem "windows\FFmpeg\bin\*.dll") {' -ForegroundColor Yellow
    Write-Host '    $name = $dll.BaseName -replace "-\d+$"' -ForegroundColor Yellow
    Write-Host '    lib /machine:x64 /def:... /out:"windows\FFmpeg\lib\$name.lib"' -ForegroundColor Yellow
    Write-Host '  }' -ForegroundColor Yellow
}
