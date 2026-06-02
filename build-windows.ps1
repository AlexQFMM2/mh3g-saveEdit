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

function Copy-ImportedDlls {
    param(
        [string]$PackageDir,
        [string]$DependencyDir,
        [string]$Objdump
    )

    if (-not (Test-Path $Objdump)) {
        Write-Warning "objdump.exe not found; skipping MinGW runtime dependency scan."
        return
    }

    $systemDlls = @{
        "advapi32.dll" = $true
        "bcrypt.dll" = $true
        "comdlg32.dll" = $true
        "crypt32.dll" = $true
        "dwmapi.dll" = $true
        "gdi32.dll" = $true
        "imm32.dll" = $true
        "iphlpapi.dll" = $true
        "kernel32.dll" = $true
        "mpr.dll" = $true
        "netapi32.dll" = $true
        "ole32.dll" = $true
        "oleaut32.dll" = $true
        "opengl32.dll" = $true
        "rpcrt4.dll" = $true
        "secur32.dll" = $true
        "setupapi.dll" = $true
        "shell32.dll" = $true
        "shlwapi.dll" = $true
        "user32.dll" = $true
        "userenv.dll" = $true
        "uxtheme.dll" = $true
        "version.dll" = $true
        "winmm.dll" = $true
        "winspool.drv" = $true
        "ws2_32.dll" = $true
    }

    $queue = New-Object System.Collections.Queue
    Get-ChildItem -Path $PackageDir -Filter "*.exe" -File -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }
    Get-ChildItem -Path $PackageDir -Filter "*.dll" -File -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }

    $scanned = @{}
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        $binaryKey = $binary.ToLowerInvariant()
        if ($scanned.ContainsKey($binaryKey)) {
            continue
        }
        $scanned[$binaryKey] = $true

        $imports = & $Objdump -p $binary 2>$null |
            Select-String "DLL Name:" |
            ForEach-Object { ($_.Line -replace ".*DLL Name:\s*", "").Trim() } |
            Where-Object { $_ }

        foreach ($dll in $imports) {
            $dllKey = $dll.ToLowerInvariant()
            if ($systemDlls.ContainsKey($dllKey)) {
                continue
            }

            $target = Join-Path $PackageDir $dll
            if (Test-Path $target) {
                continue
            }

            $source = Join-Path $DependencyDir $dll
            if (Test-Path $source) {
                Copy-Item $source $target
                Write-Host "copied dependency: $dll"
                $queue.Enqueue($target)
            }
            else {
                Write-Warning "dependency not found in ${DependencyDir}: $dll"
            }
        }
    }
}

function Copy-QtPlatformPlugin {
    param(
        [string]$PackageDir,
        [string]$QtRoot
    )

    $platformsDir = Join-Path $PackageDir "platforms"
    New-Item -ItemType Directory -Force -Path $platformsDir | Out-Null

    $candidates = @(
        (Join-Path $QtRoot "share\qt5\plugins\platforms\qwindows.dll"),
        (Join-Path $QtRoot "plugins\platforms\qwindows.dll")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            Copy-Item $candidate (Join-Path $platformsDir "qwindows.dll") -Force
            Write-Host "copied Qt platform plugin: qwindows.dll"
            return
        }
    }

    Write-Warning "qwindows.dll platform plugin not found. Checked: $($candidates -join ', ')"
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
$Objdump = Resolve-Tool @("objdump.exe") @($ResolvedQtBin)

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
Write-Host "objdump: $Objdump"
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
Copy-QtPlatformPlugin $PackageDir $QtRoot
Copy-ImportedDlls $PackageDir $ResolvedQtBin $Objdump

$RunBat = Join-Path $PackageDir "run-windows.bat"
Set-Content -Path $RunBat -Encoding ASCII -Value @"
@echo off
cd /d "%~dp0"
start "" "$TargetName.exe"
"@

Write-Host ""
Write-Host "Windows package created:"
Write-Host $PackageDir
