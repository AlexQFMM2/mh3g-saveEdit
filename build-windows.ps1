param(
    [string]$QtBin = "",
    [ValidateSet("release", "debug")]
    [string]$Configuration = "release",
    [string]$OutDir = ".\release\windows"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectFile = Join-Path $Root "MH3USaveEditorGUI.pro"
$TargetName = "MH3USaveEditorGUI"

function Find-QtBin {
    param([string]$RequestedQtBin)

    $candidates = @()
    if ($RequestedQtBin) {
        $candidates += $RequestedQtBin
    }
    if ($env:QtBin) {
        $candidates += $env:QtBin
    }
    if ($env:QTDIR) {
        $candidates += (Join-Path $env:QTDIR "bin")
    }

    foreach ($candidate in $candidates) {
        if (-not $candidate) {
            continue
        }
        $qmake = Join-Path $candidate "qmake.exe"
        $deploy = Join-Path $candidate "windeployqt.exe"
        if ((Test-Path $qmake) -and (Test-Path $deploy)) {
            return (Resolve-Path $candidate).Path
        }
    }

    if (Test-Path "C:\Qt") {
        $qmake = Get-ChildItem -Path "C:\Qt" -Filter "qmake.exe" -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\mingw[^\\]*\\bin\\qmake\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($qmake) {
            return $qmake.DirectoryName
        }
    }

    throw "Qt MinGW bin directory not found. Pass -QtBin, for example: -QtBin C:\Qt\5.15.2\mingw81_64\bin"
}

function Find-Make {
    param([string]$ResolvedQtBin)

    $qtMake = Join-Path $ResolvedQtBin "mingw32-make.exe"
    if (Test-Path $qtMake) {
        return $qtMake
    }

    $command = Get-Command "mingw32-make.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "mingw32-make.exe not found. Make sure the Qt MinGW toolchain is installed and available."
}

$ResolvedQtBin = Find-QtBin $QtBin
$Qmake = Join-Path $ResolvedQtBin "qmake.exe"
$WinDeployQt = Join-Path $ResolvedQtBin "windeployqt.exe"
$Make = Find-Make $ResolvedQtBin

Write-Host "Qt bin: $ResolvedQtBin"
Write-Host "qmake: $Qmake"
Write-Host "make: $Make"
Write-Host "configuration: $Configuration"

Set-Location $Root

& $Qmake $ProjectFile "CONFIG+=$Configuration" "CONFIG-=debug_and_release"
& $Make "-j$([Environment]::ProcessorCount)"

$BuiltExe = Join-Path $Root "bin\$TargetName.exe"
if (-not (Test-Path $BuiltExe)) {
    throw "Build finished but executable was not found: $BuiltExe"
}

$PackageDir = Join-Path $Root $OutDir
if (Test-Path $PackageDir) {
    Remove-Item $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

Copy-Item $BuiltExe (Join-Path $PackageDir "$TargetName.exe")
Copy-Item (Join-Path $Root "data") (Join-Path $PackageDir "data") -Recurse

& $WinDeployQt (Join-Path $PackageDir "$TargetName.exe") "--$Configuration"

$RunBat = Join-Path $PackageDir "run-windows.bat"
Set-Content -Path $RunBat -Encoding ASCII -Value @"
@echo off
cd /d "%~dp0"
start "" "$TargetName.exe"
"@

Write-Host ""
Write-Host "Windows package created:"
Write-Host $PackageDir
