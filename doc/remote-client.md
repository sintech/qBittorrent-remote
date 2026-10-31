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

- Original main-window menus/toolbar, Transfers layout, native sortable table,
  progress bars, rearrangeable/hideable columns, multi-selection and shortcuts.
- All 38 upstream torrent-list columns, populated from Web API data when present.
- Incremental `/sync/maindata` polling, preserving existing row indexes and
  selections; full snapshots remove obsolete fields and rows on reconnect.
- Status, category and tag filters, plus case-insensitive torrent-name search.
- Start, stop, force start, recheck, reannounce, queue-position commands.
- Add local `.torrent` files or magnet/HTTP(S) URLs. The optional download path
  is a literal path on the **server**.
- Remove torrents; server content deletion is unchecked by default and requires
  a confirmation in the application.
- General properties and read-only Trackers, Peers and Content tabs.
- Connection state, session transfer rates and server free space.
- API-key/password authentication, reverse proxy prefixes, request timeouts,
  manual reconnect and exponential retry for interrupted reads. Redirects are
  rejected, TLS certificate verification uses Qt defaults.
- Mutations are never replayed automatically. If a response is lost, inspect
  refreshed server state before retrying the action.

## Explicit first-preview limitations

This reuses the upstream **window form and resources**, not the full existing
`MainWindow` implementation or its session-bound widgets. The remote model and
controller are new. The original desktop executable and local mode are unchanged.
Implementing `Session`/`Torrent` adapters and converging the widget implementations
remains a possible later refactor; this preview does not claim complete parity.

The details tables are currently read-only and differ from the original property
panels. Settings, RSS, search plugins, torrent creation, session pause, statistics,
tray integration, full add-torrent options and other unported actions are disabled.
Existing translations do not yet cover the new remote frontend.
Path translation, opening server files, and browsing server directories are deferred.
No remote server shutdown command is sent when closing the client.

The automated tests use an injected `QNetworkAccessManager` with scripted Qt
network replies. They cover protocol requests and asynchronous behavior without
requiring access to a live server. Live-server verification and native Windows
verification must be reported separately from these tests.
