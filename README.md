# mh3g-saveEdit

## Windows build

Install a Qt 5 MinGW kit first, then run from the repository root. For MSYS2 MINGW64, the Qt bin path is usually `C:\msys64\mingw64\bin`; MSYS2 may provide the tools as `qmake-qt5.exe` and `windeployqt-qt5.exe`, which this script also supports.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-windows.ps1 -QtBin C:\Qt\5.15.2\mingw81_64\bin
```

If the script can find Qt under `C:\Qt`, `-QtBin` can be omitted:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The packaged Windows build is written to `release/windows/`.
