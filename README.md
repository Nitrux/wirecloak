# Wirecloak | [![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

Wirecloak is a modern, native WireGuard VPN client for Nitrux, built with **[MauiKit](https://mauikit.org/)**.

It provides a user-friendly interface for managing VPN tunnels while securely integrating with the system's immutable root filesystem.

![Cinderward](https://nxos.org/wp-content/uploads/2025/12/screenshot-20251214-013530.png)
> Wirecloak, a GUI for using WireGuard.

> [!WARNING]
> Wirecloak is intended to work exclusively in Nitrux OS, and using this utility in other distributions is not supported. Please do not open issues regarding this use case; they will be closed.

## Features

- **Profile Management**: Easily manage WireGuard configuration files (`.conf`).
- **Tunnel Control**: Toggle VPN connections on or off with a simple switch.
- **Live Monitoring**: View real-time connection statistics, including data received (RX) and transmitted (TX).

## Requirements

- Nitrux 5.0.0 and newer.

### Runtime Requirements

```
mauikit (>= 4.0.2)
mauikit-filebrowsing (>= 4.0.2)
qt6 (>= 6.8.2)
kf6-windowsystem (>= 6.13.0)
kf6-i18n (>= 6.13.0)
kf6-coreaddons (>= 6.13.0)
wireguard-tools
```

# Usage

To use Wirecloak, launch it from the applications menu.

1. Click the + button in the header to import a .conf file.
2. Select a tunnel from the dropdown menu.
3. Use the VPN Status switch to connect or disconnect.

# Licensing

The license for this repository and its contents is **BSD-3-Clause**.

# Issues

If you find problems with the contents of this repository, please create an issue and use the **🐞 Bug report** template.

## Submitting a bug report

Before submitting a bug, you should look at the [existing bug reports]([url](https://github.com/Nitrux/wirecloak/issues)) to verify that no one has reported the bug already.

©2025 Nitrux Latinoamericana S.C.
