# Remmina for Windows (Native Win64 Fork)

This repository is a native Windows fork of [Remmina](https://gitlab.com/Remmina/Remmina), the open-source remote desktop client. It brings the full GTK3 Remmina client and FreeRDP 3 protocol plugin natively to Windows without requiring WSL, Cygwin, or MSYS2 at runtime.

---

## Highlights & Features

- **100% Native Win32 / Universal C Runtime (UCRT64):**
  Targets Microsoft's `ucrtbase.dll`, ensuring zero POSIX emulation overhead and seamless compatibility with modern Windows 10 and Windows 11 systems.
- **Modern FreeRDP 3.32+ Support:**
  Built with FreeRDP 3, featuring full H.264/AVC hardware-accelerated remote desktop streaming, NLA (Network Level Authentication), TLS 1.3, smart sizing, multi-monitor support, and dynamic resolution renegotiation.
- **Zero-Dependency Portable Distribution:**
  The packaged bundle is fully self-contained. It includes `remmina.exe`, `plugins/remmina-plugin-rdp.dll`, all 100+ runtime DLLs, compiled GLib schemas, GDK Pixbuf image loaders, and icon themes. Copy it to any machine or USB drive and run immediately.
- **Automatic Plugin & Asset Discovery:**
  `remmina.exe` dynamically discovers plugins located in adjacent directories (`.\plugins\`, `.\`), user configuration paths (`%APPDATA%\remmina\plugins`), or custom locations specified via `REMMINA_PLUGIN_PATH`. It also initializes GTK schema and pixbuf environment paths automatically.

---

## Quick Start (Prebuilt Portable Package)

1. Extract `dist/remmina-win64-portable.zip` (or navigate to `dist/remmina-win64/`).
2. Double-click `remmina.exe` or `remmina.bat`.
3. Create an RDP connection profile or enter an RDP address (`rdp://hostname` or `rdp://username@hostname`) and connect.

### Portable Folder Layout

```text
remmina-win64/
├── remmina.exe                   # Main Remmina executable
├── remmina.bat                   # Optional convenience launcher
├── *.dll                         # Bundled runtime DLLs (GTK3, FreeRDP, Cairo, OpenSSL, etc.)
├── plugins/
│   └── remmina-plugin-rdp.dll    # FreeRDP 3 Remote Desktop Protocol plugin
├── lib/
│   └── gdk-pixbuf-2.0/           # Image loaders (PNG, SVG, BMP, ICO) and loaders.cache
└── share/
    ├── glib-2.0/schemas/         # Compiled GLib schemas (gschemas.compiled)
    └── icons/                    # Adwaita, hicolor, and Remmina scalable icons
```

---

## Building from Source

### Prerequisites

- Windows 10/11 (x64)
- [MSYS2](https://www.msys2.org/) (installed at `C:\msys64` or custom path)

### 1. Install UCRT64 Toolchain and Dependencies

Open the **MSYS2 UCRT64** terminal (or run from PowerShell via `C:\msys64\usr\bin\bash.exe -lc`):

```bash
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-gtk3 \
    mingw-w64-ucrt-x86_64-freerdp \
    mingw-w64-ucrt-x86_64-libssh \
    mingw-w64-ucrt-x86_64-libsodium \
    mingw-w64-ucrt-x86_64-json-glib \
    mingw-w64-ucrt-x86_64-libgcrypt \
    mingw-w64-ucrt-x86_64-curl \
    mingw-w64-ucrt-x86_64-pcre2 \
    mingw-w64-ucrt-x86_64-glib2
```

### 2. Configure with CMake

From the repository root:

```bash
cmake -B build -G Ninja \
    -DWITH_FREERDP3=ON \
    -DWITH_CUPS=OFF \
    -DWITH_LIBVNCSERVER=OFF \
    -DWITH_WEBKIT2GTK=OFF \
    -DWITH_SPICE=OFF \
    -DWITH_LIBSECRET=OFF \
    -DWITH_WWW=OFF \
    -DWITH_PYTHONLIBS=OFF \
    -DWITH_NEWS=OFF \
    -DWITH_STATS=OFF \
    -DWITH_MANPAGES=OFF \
    -DWITH_ICON_CACHE=OFF \
    -DWITH_UPDATE_DESKTOP_DB=OFF \
    -DWITH_VTE=OFF \
    -DHAVE_LIBAPPINDICATOR=OFF \
    -DWITH_TRANSLATIONS=OFF \
    -DCMAKE_BUILD_TYPE=Release
```

### 3. Build

```bash
ninja -C build
```

This compiles:
- `build/src/remmina.exe`
- `build/plugins/rdp/remmina-plugin-rdp.dll`

### 4. Create Standalone Portable Package

Run the included PowerShell packaging script:

```powershell
powershell -ExecutionPolicy Bypass -File .\package_windows.ps1
```

The script will automatically:
- Trace all DLL imports recursively for `remmina.exe`, `remmina-plugin-rdp.dll`, and image loaders.
- Copy all required runtime DLLs from UCRT64.
- Bundle compiled GLib schemas and GDK Pixbuf loaders.
- Copy application icons and themes.
- Produce `dist/remmina-win64/` and `dist/remmina-win64-portable.zip`.

---

## Technical Porting Notes

1. **POSIX to Win32 Translation:**
   - Sockets: Initialized via Winsock2 (`WSAStartup`), replaced POSIX non-blocking `fcntl(O_NONBLOCK)` with `ioctlsocket(FIONBIO)`.
   - FreeRDP Integration: Replaced POSIX pipes and file descriptor signaling in `rdp_event.c` with native Win32 `CreateEvent`, `SetEvent`, and `ResetEvent` bridged into FreeRDP 3 WinPR event handlers.
   - Sockets / Pipes: Guarded POSIX domain sockets (`AF_UNIX`) and X11 display handles to return `-1` on Windows.
   - System Info: Guarded `<sys/utsname.h>` and `/etc/machine-id`, replacing with static platform info and machine identifiers.
   - Utilities: Added portable `getline()` and `strcasestr()` implementations.
2. **GTK Backends:**
   - Configured `gdk_set_allowed_backends("win32")` under Windows to avoid restricting GTK backend selection to non-existent X11/Wayland backends.
3. **Dynamic Plugin Architecture:**
   - Replaced compile-time hardcoded Linux plugin directories with dynamic Win32 module resolution (`GetModuleFileNameW`), checking relative directories and `%APPDATA%\remmina\plugins`.

---

## License

Remmina is free software released under the GNU General Public License (GPL) version 2 or later, with OpenSSL exception clauses. See `LICENSE` for details.
