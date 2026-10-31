# Native remote frontend — first preview

This experimental frontend targets a qBittorrent **5.2.x** Web API server
(tested protocol fixture: 5.2.4). It uses the unchanged upstream
`src/gui/mainwindow.ui`, upstream icons and native Qt widgets. It does not embed
a browser and does not start a local BitTorrent session.

## Build and run

Install CMake, a C++20 compiler and Qt 6.6+ with Core, Network, Widgets and Svg.
No libtorrent, Boost or local qBittorrent profile is required for this target.

```sh
cmake -S src/remote -B build-remote -DREMOTE_TESTING=ON
cmake --build build-remote --config Release
ctest --test-dir build-remote -C Release --output-on-failure
```

Run `build-remote/qbittorrent-remote` on Linux, or
`build-remote/Release/qbittorrent-remote.exe` with a Visual Studio generator.
The root build also accepts `-DREMOTE_ONLY=ON`.
The **Remote frontend preview** Actions workflow builds and tests only Windows x64,
then uploads a portable preview with its Qt DLLs. It runs once per push, caches
the required Qt archives and cancels obsolete CI runs on the work branch.
Inherited CI pipelines are limited to master; they do not run for this feature
branch or its pull request against base/5.2.4.

Enter the server's Web UI URL (including a reverse proxy prefix, if applicable).
Use an API key from the server's Web UI settings, or its username and password.
URL, username, authentication type, window geometry and table layout are stored
in a separate `qBittorrent-remote` settings profile. Passwords and API keys are
kept in process memory only and must be entered on the next launch.

## Implemented

- Original main-window menus/toolbar, a native QTreeView torrent list, upstream
  progress-bar painter, original default state colors/icons, rearrangeable/hideable
  columns, multi-selection and shortcuts. The filter is in the toolbar.
- Upstream PropertiesWidget form with the original Transfer/Information groups,
  upstream property-tab button bar at the bottom, and upstream collapsible sidebar
  section headers. The sidebar spans the full height of the transfers area.
- Russian localization reuses the upstream catalog when Qt LinguistTools is
  installed (included by the Windows workflow). The system language is used by
  default; View > Language selects English/Russian for the next launch.
- All 38 upstream torrent-list columns, populated from Web API data when present.
- Incremental `/sync/maindata` polling, preserving existing row indexes and
  selections; full snapshots remove obsolete fields and rows on reconnect.
- Independent status, category, tag and tracker filters with counts, plus
  case-insensitive torrent-name/server-path search.
- Start, stop, force start, recheck, reannounce, queue-position commands.
- Add local `.torrent` files or magnet/HTTP(S) URLs. The optional download path
  is a literal path on the **server**.
- Remove torrents; server content deletion is unchecked by default and requires
  a confirmation in the application.
- Original General property layout with the actual upstream downloaded-pieces
  and availability bars, stretched across the original layout.
- Shared native Trackers/Peers/Content presentation: original column order,
  numeric/natural sorting, tracker endpoints, content progress delegate and
  file icons. Rows, selections and expanded folders survive polling. These
  tables and HTTP Sources remain read-only.
- Original SpeedWidget/SpeedPlotView with all seven time ranges. History starts
  at connection and survives torrent selection. The API exposes payload upload/
  download rates; unavailable overhead/DHT/tracker series are disabled. Switching
  servers clears history.
- Connection state, session transfer rates and server free space.
- API-key/password authentication, reverse proxy prefixes, request timeouts,
  manual reconnect and exponential retry for interrupted reads. Redirects are
  rejected, TLS certificate verification uses Qt defaults.
- Mutations are never replayed automatically. If a response is lost, inspect
  refreshed server state before retrying the action.

## Explicit first-preview limitations

This reuses upstream **main-window/property forms, button bars, sidebar headers,
property-table presentation, piece bars, speed graph, progress painting and
resources**, with remote data binding. It does not yet reuse
all session-bound widget controllers or the full `MainWindow` implementation.
The remote data model and controller are new; complete GUI parity is not claimed.

Trackers, peers and content are still read-only. Settings, RSS, search plugins,
torrent creation, session pause, statistics, tray integration and
full add-torrent options remain unported. Connect to Server is in the File menu;
the original Options action keeps its label and stays disabled until ported.
Custom qbtheme files and the local client's appearance preferences are not yet
imported. Some new remote-specific dialogs still use English strings.
Path translation, opening server files, and browsing server directories are deferred.
No remote server shutdown command is sent when closing the client.

The automated tests use an injected `QNetworkAccessManager` with scripted Qt
network replies. They cover protocol requests and asynchronous behavior without
requiring access to a live server. Live-server verification and native Windows
verification must be reported separately from these tests.
