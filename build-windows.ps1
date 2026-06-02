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

function Resolve-Tool {
    param(
        [string[]]$Names,
        [string[]]$Directories
    )

    foreach ($directory in $Directories) {
        if (-not $directory) {
            continue
        }
        foreach ($name in $Names) {
            $path = Join-Path $directory $name
            if (Test-Path $path) {
                return (Resolve-Path $path).Path
            }
        }
    }

    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    return ""
}

function Find-QtRoot {
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
        $qmake = Resolve-Tool @("qmake.exe", "qmake-qt5.exe") @($candidate)
        if ($qmake) {
            return (Resolve-Path $candidate).Path
        }
    }

    if (Test-Path "C:\Qt") {
        $qmake = Get-ChildItem -Path "C:\Qt" -Filter "qmake*.exe" -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\mingw[^\\]*\\bin\\qmake(-qt5)?\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($qmake) {
            return $qmake.DirectoryName
        }
    }

    throw "Qt MinGW directory not found. Pass -QtBin, for example: -QtBin C:\msys64\mingw64\bin"
}

function Find-Make {
    param([string]$ResolvedQtBin)

    $make = Resolve-Tool @("mingw32-make.exe") @($ResolvedQtBin)
    if ($make) {
        return $make
    }

    throw "mingw32-make.exe not found. Make sure the Qt MinGW toolchain is installed and available."
}

$ResolvedQtBin = Find-QtRoot $QtBin
$QtRoot = Split-Path -Parent $ResolvedQtBin
$QtToolDirs = @(
    $ResolvedQtBin,
    (Join-Path $QtRoot "share\qt5\bin"),
    (Join-Path $QtRoot "lib\qt5\bin")
)

$Qmake = Resolve-Tool @("qmake.exe", "qmake-qt5.exe") $QtToolDirs
$WinDeployQt = Resolve-Tool @("windeployqt.exe", "windeployqt-qt5.exe") $QtToolDirs
$Make = Find-Make $ResolvedQtBin

if (-not $Qmake) {
    throw "qmake.exe/qmake-qt5.exe not found under: $($QtToolDirs -join ', ')"
}
if (-not $WinDeployQt) {
    throw "windeployqt.exe/windeployqt-qt5.exe not found under: $($QtToolDirs -join ', ')"
}

Write-Host "Qt bin: $ResolvedQtBin"
Write-Host "qmake: $Qmake"
Write-Host "windeployqt: $WinDeployQt"
Write-Host "make: $Make"
Write-Host "configuration: $Configuration"

Set-Location $Root

# qmake-generated Makefiles call tools such as g++ by name. Put the selected
# Qt/MinGW bin first so another MinGW installation earlier in PATH is not used.
$env:PATH = "$ResolvedQtBin;$env:PATH"
Write-Host "compiler: $(& g++.exe --version | Select-Object -First 1)"

foreach ($path in @("Makefile", "Makefile.Debug", "Makefile.Release", "build", "bin")) {
    $fullPath = Join-Path $Root $path
    if (Test-Path $fullPath) {
        Remove-Item $fullPath -Recurse -Force
    }
}

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
