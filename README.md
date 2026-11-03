# qBittorrent Remote

<img src="doc/assets/remote-icon.svg" width="96" alt="qBittorrent Remote icon">

qBittorrent Remote is a native desktop client for a standard qBittorrent server. It connects to the server through the built-in Web UI API at `/api/v2/`. An unmodified installation of qBittorrent or `qbittorrent-nox` with Web UI enabled is sufficient. The server handles torrent transfers, search plugins, and logs.

## Server compatibility

The client checks both the qBittorrent version and the Web API version on connection. It supports qBittorrent 5.2.x and 5.3.x with Web API 2.x, starting at 2.15.1.

| Server family | Reference server | Web API |
| --- | --- | --- |
| qBittorrent 5.2.x | 5.2.4 | 2.15.1 |
| qBittorrent 5.3.x | 5.3.0 | 2.16.2 |

The reference versions were checked against upstream source and automated API fixtures.

## Getting started

1. Enable Web UI on your qBittorrent server.
2. Open **Tools → Connection profiles** in qBittorrent Remote and enter the server's Web UI address and credentials.
3. Select **Save and connect**.

Manage transfers, add torrent files or magnet links, search with server-side plugins, and view the server's execution log. Optional folder mappings let you open downloaded content through a local path or network share.

See the [setup guide, build instructions, and limitations](doc/remote-client.md).

## Upstream and license

The desktop interface is based on qBittorrent 5.2.4 and reuses its Qt widgets and forms. Upstream project and source code are available at [qbittorrent/qBittorrent](https://github.com/qbittorrent/qBittorrent).

See [AUTHORS](AUTHORS), [COPYING](COPYING), and [third-party notices](doc/remote-third-party.txt). The source is licensed under GPL-2.0-or-later. Binary distributions are licensed under GPL-3.0-or-later because they include GPL-3.0-or-later assets.
