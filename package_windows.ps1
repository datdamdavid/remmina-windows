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
New-Item -ItemType Directory -Force -Path "$DistDir\etc\gtk-3.0" | Out-Null

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
if (Test-Path "$PSScriptRoot\src\remmina.ico") {
    Copy-Item "$PSScriptRoot\src\remmina.ico" -Destination "$DistDir\remmina.ico" -Force
}

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

# 3. Copy OpenSSL modules (legacy.dll for MD4/RC4/NTLM support in FreeRDP)
$srcOsslDir = "C:\msys64\ucrt64\lib\ossl-modules"
$destOsslDir = "$DistDir\lib\ossl-modules"
if (Test-Path $srcOsslDir) {
    New-Item -ItemType Directory -Force -Path $destOsslDir | Out-Null
    Copy-Item "$srcOsslDir\*.dll" -Destination "$destOsslDir\" -Force
    Write-Host "Copied OpenSSL legacy providers."
}

# 4. Recursively collect and copy all DLL dependencies
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
Get-ChildItem -Path "$destOsslDir\*.dll" | ForEach-Object {
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
                }
            }
        }
    }
}

# 5. Copy GLib schemas
Write-Host "Copying and compiling GLib schemas..."
$destSchemaDir = "$DistDir\share\glib-2.0\schemas"
New-Item -ItemType Directory -Force -Path $destSchemaDir | Out-Null
Copy-Item "C:\msys64\ucrt64\share\glib-2.0\schemas\*" -Destination $destSchemaDir -Force
& "C:\msys64\ucrt64\bin\glib-compile-schemas.exe" $destSchemaDir

# 6. Copy Icons (Adwaita, hicolor, remmina actions/emblems/apps/plugin icons)
Write-Host "Copying icon themes and Remmina icons..."
$destIcons = "$DistDir\share\icons"
New-Item -ItemType Directory -Force -Path $destIcons | Out-Null

if (Test-Path "C:\msys64\ucrt64\share\icons\Adwaita") {
    Copy-Item "C:\msys64\ucrt64\share\icons\Adwaita" -Destination "$destIcons\Adwaita" -Recurse -Force
}
if (Test-Path "C:\msys64\ucrt64\share\icons\hicolor") {
    Copy-Item "C:\msys64\ucrt64\share\icons\hicolor" -Destination "$destIcons\hicolor" -Recurse -Force
}

# Actions
$destActions = "$destIcons\hicolor\scalable\actions"
New-Item -ItemType Directory -Force -Path $destActions | Out-Null
Get-ChildItem -Path "$PSScriptRoot\data\icons\scalable\actions\*.svg" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName -Destination $destActions -Force
    $unprefixed = $_.Name -replace '^org\.remmina\.Remmina-', ''
    if ($unprefixed -ne $_.Name) {
        Copy-Item $_.FullName -Destination "$destActions\$unprefixed" -Force
    }
}

# Emblems
$destEmblems = "$destIcons\hicolor\scalable\emblems"
New-Item -ItemType Directory -Force -Path $destEmblems | Out-Null
Get-ChildItem -Path "$PSScriptRoot\data\icons\scalable\emblems\*.svg" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName -Destination $destEmblems -Force
    $unprefixed = $_.Name -replace '^org\.remmina\.Remmina-', ''
    if ($unprefixed -ne $_.Name) {
        Copy-Item $_.FullName -Destination "$destEmblems\$unprefixed" -Force
    }
}

# Plugin icons (RDP, VNC, etc.)
Get-ChildItem -Path "$PSScriptRoot\plugins\*\scalable\emblems\*.svg" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName -Destination $destEmblems -Force
    $unprefixed = $_.Name -replace '^org\.remmina\.Remmina-', ''
    if ($unprefixed -ne $_.Name) {
        Copy-Item $_.FullName -Destination "$destEmblems\$unprefixed" -Force
    }
    if ($_.Name -match 'rdp') {
        Copy-Item $_.FullName -Destination "$destEmblems\remmina-rdp.svg" -Force
        Copy-Item $_.FullName -Destination "$destEmblems\remmina-rdp-symbolic.svg" -Force
    }
}

# Desktop resolution icons (16x16 ... scalable)
$desktopDirs = Get-ChildItem -Path "$PSScriptRoot\data\desktop" -Directory
foreach ($d in $desktopDirs) {
    if (Test-Path "$($d.FullName)\apps") {
        $destAppDir = "$destIcons\hicolor\$($d.Name)\apps"
        New-Item -ItemType Directory -Force -Path $destAppDir | Out-Null
        Copy-Item "$($d.FullName)\apps\*" -Destination $destAppDir -Force
    }
}

# Main app icons in apps
$destApps = "$destIcons\hicolor\scalable\apps"
New-Item -ItemType Directory -Force -Path $destApps | Out-Null
if (Test-Path "$destApps\org.remmina.Remmina.svg") {
    Copy-Item "$destApps\org.remmina.Remmina.svg" -Destination "$destApps\remmina.svg" -Force
}

# Update icon caches
& "C:\msys64\ucrt64\bin\gtk-update-icon-cache.exe" -f -t "$destIcons\hicolor"
& "C:\msys64\ucrt64\bin\gtk-update-icon-cache.exe" -f -t "$destIcons\Adwaita"

# 7. Configure GTK3 settings.ini for default dark theme and Segoe UI font
$etcGtk = "$DistDir\etc\gtk-3.0"
$gtkSettings = @"
[Settings]
gtk-theme-name = Adwaita
gtk-application-prefer-dark-theme = true
gtk-icon-theme-name = hicolor
gtk-fallback-icon-theme = Adwaita
gtk-font-name = Segoe UI 10
"@
Set-Content -Path "$etcGtk\settings.ini" -Value $gtkSettings -Encoding ASCII

# 8. Create remmina.bat launcher
$launcherContent = @"
@echo off
setlocal
set "DIR=%~dp0"
set "PATH=%DIR%;%PATH%"
set "GSETTINGS_SCHEMA_DIR=%DIR%share\glib-2.0\schemas"
set "GDK_PIXBUF_MODULE_FILE=%DIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "XDG_DATA_DIRS=%DIR%share"
set "OPENSSL_MODULES=%DIR%lib\ossl-modules"
start "" "%DIR%remmina.exe" %*
"@
Set-Content -Path "$DistDir\remmina.bat" -Value $launcherContent -Encoding ASCII

# 9. Create portable ZIP archive
$zipPath = "$PSScriptRoot\dist\remmina-win64-portable.zip"
Write-Host "Creating portable ZIP archive: $zipPath..."
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

if (Get-Command "7z.exe" -ErrorAction SilentlyContinue) {
    & 7z.exe a -tzip -mx=5 $zipPath "$DistDir\*" | Out-Null
} else {
    Compress-Archive -Path "$DistDir\*" -DestinationPath $zipPath -Force
}

# 10. Summary
$dllCount = (Get-ChildItem -Path "$DistDir\*.dll").Count
Write-Host "`n=== Packaging Complete ===" -ForegroundColor Green
Write-Host "Copied $dllCount DLL dependencies."
Write-Host "Portable folder: $DistDir"
Write-Host "Portable ZIP:    $zipPath"
