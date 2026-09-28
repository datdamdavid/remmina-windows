param(
    [string]$DistDir = "$PSScriptRoot\dist\remmina-win64",
    [string]$UcrtBin = "C:\msys64\ucrt64\bin"
)

$ErrorActionPreference = "Stop"

Write-Host "=== Packaging Remmina for Windows ===" -ForegroundColor Cyan
Write-Host "Target output: $DistDir"

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
New-Item -ItemType Directory -Force -Path "$DistDir\plugins" | Out-Null
New-Item -ItemType Directory -Force -Path "$DistDir\share" | Out-Null
New-Item -ItemType Directory -Force -Path "$DistDir\lib" | Out-Null

# 1. Copy remmina.exe and RDP plugin
$srcExe = "$PSScriptRoot\build\src\remmina.exe"
$srcPlugin = "$PSScriptRoot\build\plugins\rdp\remmina-plugin-rdp.dll"

if (!(Test-Path $srcExe)) {
    throw "Source executable not found: $srcExe"
}
if (!(Test-Path $srcPlugin)) {
    throw "Source plugin not found: $srcPlugin"
}

Copy-Item $srcExe -Destination "$DistDir\remmina.exe" -Force
Copy-Item $srcPlugin -Destination "$DistDir\plugins\remmina-plugin-rdp.dll" -Force

# 2. Copy GDK Pixbuf loaders
$srcPixbufDir = "C:\msys64\ucrt64\lib\gdk-pixbuf-2.0\2.10.0"
$destPixbufDir = "$DistDir\lib\gdk-pixbuf-2.0\2.10.0"
New-Item -ItemType Directory -Force -Path "$destPixbufDir\loaders" | Out-Null

Get-ChildItem -Path "$srcPixbufDir\loaders\*.dll" | ForEach-Object {
    Copy-Item $_.FullName -Destination "$destPixbufDir\loaders\" -Force
}
if (Test-Path "$srcPixbufDir\loaders.cache") {
    Copy-Item "$srcPixbufDir\loaders.cache" -Destination "$destPixbufDir\loaders.cache" -Force
}

# 3. Recursively collect and copy all DLL dependencies
$objdump = "C:\msys64\ucrt64\bin\objdump.exe"
$systemDlls = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    "kernel32.dll", "kernelbase.dll", "user32.dll", "gdi32.dll", "gdi32full.dll", "shell32.dll",
    "ole32.dll", "oleaut32.dll", "comctl32.dll", "comdlg32.dll", "advapi32.dll", "ws2_32.dll",
    "shlwapi.dll", "imm32.dll", "msvcrt.dll", "ucrtbase.dll", "ntdll.dll", "bcrypt.dll",
    "crypt32.dll", "secur32.dll", "winmm.dll", "setupapi.dll", "iphlpapi.dll", "dnsapi.dll",
    "wldap32.dll", "d3d11.dll", "dxgi.dll", "d2d1.dll", "dwrite.dll", "usp10.dll",
    "rpcrt4.dll", "version.dll", "normaliz.dll", "uxtheme.dll", "dwmapi.dll", "mpr.dll",
    "winspool.drv", "cfgmgr32.dll", "gdiplus.dll", "userenv.dll", "bcryptprimitives.dll",
    "msimg32.dll", "hid.dll", "opengl32.dll", "credui.dll", "ncrypt.dll", "dbghelp.dll"
) | ForEach-Object { [void]$systemDlls.Add($_) }

$processedFiles = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$dllQueue = [System.Collections.Generic.Queue[string]]::new()

# Seed queue
$dllQueue.Enqueue("$DistDir\remmina.exe")
$dllQueue.Enqueue("$DistDir\plugins\remmina-plugin-rdp.dll")
Get-ChildItem -Path "$destPixbufDir\loaders\*.dll" | ForEach-Object {
    $dllQueue.Enqueue($_.FullName)
}

Write-Host "Resolving DLL dependencies..."
while ($dllQueue.Count -gt 0) {
    $currentBinary = $dllQueue.Dequeue()
    if ($processedFiles.Contains($currentBinary)) {
        continue
    }
    [void]$processedFiles.Add($currentBinary)

    $output = & $objdump -p $currentBinary 2>$null
    foreach ($line in $output) {
        if ($line -match 'DLL Name:\s*(.*)') {
            $dllName = $matches[1].Trim()
            if ($dllName -like "api-ms-win-*" -or $dllName -like "ext-ms-*") {
                continue
            }
            if ($systemDlls.Contains($dllName)) {
                continue
            }

            $destDll = "$DistDir\$dllName"
            if (!(Test-Path $destDll)) {
                $srcDll = "$UcrtBin\$dllName"
                if (Test-Path $srcDll) {
                    Write-Host "  Copying $dllName"
                    Copy-Item $srcDll -Destination $destDll -Force
                    $dllQueue.Enqueue($destDll)
                } else {
                    Write-Warning "Could not find $dllName in $UcrtBin"
                }
            }
        }
    }
}

# 4. Copy GLib schemas
Write-Host "Copying and compiling GLib schemas..."
$destSchemaDir = "$DistDir\share\glib-2.0\schemas"
New-Item -ItemType Directory -Force -Path $destSchemaDir | Out-Null
Copy-Item "C:\msys64\ucrt64\share\glib-2.0\schemas\*" -Destination $destSchemaDir -Force
& "C:\msys64\ucrt64\bin\glib-compile-schemas.exe" $destSchemaDir

# 5. Copy Icons (Adwaita, hicolor, remmina icons)
Write-Host "Copying icon themes..."
$destIcons = "$DistDir\share\icons"
New-Item -ItemType Directory -Force -Path $destIcons | Out-Null

if (Test-Path "C:\msys64\ucrt64\share\icons\Adwaita") {
    Copy-Item "C:\msys64\ucrt64\share\icons\Adwaita" -Destination "$destIcons\Adwaita" -Recurse -Force
}
if (Test-Path "C:\msys64\ucrt64\share\icons\hicolor") {
    Copy-Item "C:\msys64\ucrt64\share\icons\hicolor" -Destination "$destIcons\hicolor" -Recurse -Force
}

$destRemminaIcons = "$destIcons\hicolor\scalable\apps"
New-Item -ItemType Directory -Force -Path $destRemminaIcons | Out-Null
Get-ChildItem -Path "$PSScriptRoot\data\icons\scalable\apps\*.svg" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName -Destination $destRemminaIcons -Force
}
if (Test-Path "$destRemminaIcons\remmina.svg") {
    Copy-Item "$destRemminaIcons\remmina.svg" -Destination "$destRemminaIcons\org.remmina.Remmina.svg" -Force
}

# 6. Create remmina.bat launcher
$launcherContent = @"
@echo off
setlocal
set "DIR=%~dp0"
set "PATH=%DIR%;%PATH%"
set "GSETTINGS_SCHEMA_DIR=%DIR%share\glib-2.0\schemas"
set "GDK_PIXBUF_MODULE_FILE=%DIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "XDG_DATA_DIRS=%DIR%share"
start "" "%DIR%remmina.exe" %*
"@
Set-Content -Path "$DistDir\remmina.bat" -Value $launcherContent -Encoding ASCII

# 7. Create ZIP archive
$zipPath = "$PSScriptRoot\dist\remmina-win64-portable.zip"
Write-Host "Creating portable ZIP archive: $zipPath..."
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
Compress-Archive -Path "$DistDir\*" -DestinationPath $zipPath -Force

# 8. Summary
$dllCount = (Get-ChildItem -Path "$DistDir\*.dll").Count
Write-Host "`n=== Packaging Complete ===" -ForegroundColor Green
Write-Host "Copied $dllCount DLL dependencies."
Write-Host "Portable folder: $DistDir"
Write-Host "Portable ZIP:    $zipPath"
