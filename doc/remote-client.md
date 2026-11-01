# Native remote frontend — first preview

This experimental frontend targets a qBittorrent **5.2.x** Web API server
(tested protocol fixture: 5.2.4). It uses the unchanged upstream
`src/gui/mainwindow.ui` and `src/gui/properties/propertieswidget.ui`, together
with the actual upstream widget classes and theme system. It does not embed
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

Use **Tools > Connection profiles** to add a named server, its Web UI URL
(including a reverse-proxy prefix), username/password or API key, and folder
mappings. **Save** stores changes; **Save and connect** connects the selected
profile. **File > Connect to Server** selects a saved profile and remembers the
last selection. The last selected profile connects automatically on launch; if it no
longer exists, the profile chooser opens. Earlier previews' address/username migrate
into an editable profile; their secrets were never stored and must be entered once.

Profiles and mappings are kept in the client's local QSettings configuration
(the current user's registry on Windows), separate from the local qBittorrent
client. Passwords/API keys are stored only in an encrypted Windows DPAPI blob.
They are readable by the Windows account that encrypted them. An unreadable blob
is retained during unrelated edits, and the editor requests new credentials.
The non-Windows development target does not fall back to plaintext secret storage.
Cancel discards the editor's drafts. Invalid edits do not replace saved profiles.

Each profile has its own **remote folder → local folder** table, for example:

| Remote folder | Local folder |
| --- | --- |
| `/srv/downloads` | `D:\Downloads` |
| `/srv/downloads/video` | `\\nas\video` |

The longest matching remote folder wins; matches respect path-component
boundaries. Linux remote paths are case-sensitive; Windows drive/UNC remote paths
are case-insensitive. Use an absolute local drive path or accessible UNC share.
Mappings change only local open operations, never paths sent to the server.

Double-click a file/folder in **Content**, or press Enter, to open its mapped local
counterpart with the default Windows application. The API content path is used
to account for a separate incomplete-download storage directory. The torrent context menu's
original **Open destination folder** action opens the mapped content directory
(or a file's parent). Missing mappings or local files produce an explanatory
message. Editing an active profile's mappings applies immediately. Changing its
connection details disconnects until reconnect; deleting the active profile also
disconnects it. Files are accessed through the local filesystem/share.

## Implemented

- Remote controller inherits the real `MainWindow`; the form, toolbar icons,
  filter `LineEdit` and transfer splitter layout are initialized in `src/gui` by
  the same methods used by the local client.
- The actual `TransferListWidget` and `TransferListDelegate` render the torrent
  list. Its original column defaults, size hints, header menu and progress painter
  are used. API values are adapted to native model roles (including `TorrentState`
  and percentage values); state strings, icons and colors come from the original
  `TransferListModel` presentation helpers, not a duplicate implementation.
- The actual `PropertiesWidget`, `PeerListWidget`, `TrackerListWidget` and
  `TorrentContentWidget` are instantiated. The original property form is compiled
  directly without replacing promoted widgets or generating a modified UI file.
- Original `TransferListFiltersWidget`, `StatusFilterWidget` (all fourteen rows),
  category/tag filter widgets and `BaseFilterWidget` provide sidebar presentation.
  Remote code binds filter models and counts; there is no custom filter-list view.
- The original `StatusBar` formats a backend-independent session snapshot.
  `UIThemeManager` and its native theme sources provide icons/colors/styles.
  The platform style default is shared with local preferences: **Fusion on Windows**.
  Free-space display follows the original default (hidden).
- Russian localization reuses the upstream catalog when Qt LinguistTools is
  installed (included by the Windows workflow). The system language is used by
  default; Options > Interface selects System/English/Russian for the next launch.
- All 38 upstream torrent-list columns, populated from Web API data when present.
- Incremental `/sync/maindata` polling, preserving existing row indexes and
  selections; full snapshots remove obsolete fields and rows on reconnect.
- Independent status, category, tag and tracker filters with counts, plus
  case-insensitive torrent-name/server-path search.
- Start, stop, force start, recheck, reannounce, queue-position commands.
- Add local `.torrent` files or magnet/HTTP(S) URLs. URL entry uses the original
  `DownloadFromURLDialog`, including clipboard handling and duplicate removal.
  Both routes then use the actual `AddNewTorrentDialog` and unchanged upstream
  `addnewtorrentdialog.ui`, with server defaults loaded through `/app/preferences`.
  Save/incomplete paths are literal **server** paths; local browsing, validation and
  filesystem completion are disabled for these fields. There is no second path prompt.
  Category, tags, start/stop condition, content layout, skip checking, queue-top,
  sequential and first/last-piece choices are sent to the server. Local torrent
  metadata (v1/v2) provides the file list; file priorities are applied after a
  successful upload because multipart `torrents/add` does not accept priorities.
  Such uploads remain stopped until all priority updates succeed, then start only
  if requested. A failed update leaves the torrent stopped without replaying writes.
  Magnet/HTTP URL metadata is obtained by the server after acceptance.
  Options > Adding torrents controls whether this dialog is shown. There is no
  menu checkbox or “never show again” checkbox in the addition dialog.
- Remove torrents; server content deletion is unchecked by default and requires
  the original `DeletionConfirmationDialog`.
- Original General property layout with the actual upstream downloaded-pieces
  and availability bars, stretched across the original layout.
- Shared native Trackers/Peers/Content presentation: original column order,
  numeric/natural sorting, tracker endpoints, content progress delegate and
  file icons. Rows, selections and expanded folders survive polling. These
  tables use the same context-menu factories as the local backend: tracker
  add/edit/remove/reannounce, peer add/copy/ban, HTTP source add/edit/remove/copy,
  and content opening/renaming/priorities. Main-table menus include native
  categories/tags, management mode, queue, copy and export submenus; sidebar
  menus manage categories/tags/trackers and visible torrents. Tracker editing
  uses the real `TrackerEntriesDialog`. Its API limitation is displayed in the
  dialog: existing tiers are preserved, and tier reordering is unavailable.
- Original SpeedWidget/SpeedPlotView with all seven time ranges. History starts
  at connection and survives torrent selection. The API exposes payload upload/
  download rates; unavailable overhead/DHT/tracker series are disabled. Switching
  servers clears history.
- Tools > Server Web UI opens the connected server's Web UI in the
  default browser, preserving any reverse-proxy prefix. Use the gear icon there
  for server settings: the stock 5.2.4 Web UI has no standalone settings URL.
  The browser uses its own login/session; client passwords, API keys and cookies
  are not passed in the URL. The action is disabled until connected.
- Options (toolbar/menu/Alt+O) opens client-local settings, available offline:
  Interface (language, Qt widget style, system/light/dark palette on Qt 6.8+,
  optional .qbtheme), Adding torrents, Transfer list (double-click action,
  alternating rows, removal confirmation) and Desktop (last-profile auto-connect,
  minimized startup, always-on-top and notification-area behavior).
  Appearance/language changes require a restart; behavior changes apply on Save.
  Cancel discards drafts. Closing to the notification area requires an available,
  visible tray icon; Exit always exits. Disabling removal confirmation never
  deletes the downloaded server files.
- Column widths, order, sorting and visible-column choices persist for the main
  table and property tables. The internal peers IP column is never exposed,
  including when restoring older settings. Content widths survive torrent changes.
- Clicking the active lower-panel tab collapses the panel to its tab bar using
  the original splitter logic; clicking a tab restores its previous height.
  The collapsed state survives restart.
- Connection state, session transfer rates and server free space.
- API-key/password authentication, reverse proxy prefixes, request timeouts,
  manual reconnect and exponential retry for interrupted reads. Redirects are
  rejected, TLS certificate verification uses Qt defaults.
- Mutations are never replayed automatically. If a response is lost, inspect
  refreshed server state before retrying the action.

## Explicit first-preview limitations

UI policy: reuse the canonical widgets and their shared initialization in
`src/gui`. Do not add remote copies of forms, painters, style sheets, sizing rules
or visual widget implementations. `src/remote` is the API/data/command adapter;
its profile editor and client-only settings dialog are remote-specific additions.
Native session-controller sections are excluded from the API-only target; shared
view code is still compiled from the same source files. The local torrent engine
is never instantiated. Full functionality parity is not claimed.

Server settings, RSS, search plugins, torrent creation, session pause and statistics
remain unported. Torrent options opens the server Web UI.
Renaming files before adding a torrent and fetching magnet metadata before
acceptance are unavailable. Rename and priority commands are available in
Content after addition; tracker tier reordering and server-directory browsing
are not exposed by this adapter. Connect to Server is in the File menu;
Tools > Server Web UI opens server settings via the browser Web UI.
The remote profile does not import the local client's appearance preferences.
The original theme loader is available through the separate remote profile
(`remote/theme` for a qbtheme path, `remote/style` for a Qt style) and through
Options > Interface. New remote-specific prompts include Russian translations.
Mapped local opening is supported; server directory browsing and file transfer
through the Web API remain unported.
No remote server shutdown command is sent when closing the client.

The automated tests use an injected `QNetworkAccessManager` with scripted Qt
network replies. They cover protocol requests and asynchronous behavior without
requiring access to a live server. Live-server verification and native Windows
verification must be reported separately from these tests.
