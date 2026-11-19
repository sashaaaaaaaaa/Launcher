# Porting MultiMC/DevLauncher (`Launcher`) to Qt 6 — Research + Plan (Nov 2026)

> **Status:** Research + review notes; implementation is in progress on `qt6-linux`. See §1a for the implementation log.
> **Repo:** `/home/sasha/Projects/Launcher` (branch `qt6-linux`; research began from `develop` commit `a0979f76`).
> **Scope decisions (from discussion):** **Qt 6 only** (no dual Qt5/Qt6). **Linux-first** (get a local Linux build running; defer full Windows/macOS packaging/toolchain work beyond notes).

---

## 0) Corrections / notes from review (important)

- **CMP0020 is Windows-only in this tree:** in `CMakeLists.txt:3–6` it is inside `if(WIN32)`. On **Linux it is not executed**, so it is **not** the first Linux configure blocker. Removing it is still good hygiene (and required for CMake 4 when configuring for Windows), but do not treat it as the primary Linux failure.
- **`QPalette::Foreground` mapping:** `QPalette::Foreground` is an alias of `QPalette::WindowText` in Qt5; the 1:1 replacement is **`QPalette::WindowText`** (not `QPalette::Text`). See `launcher/ui/widgets/VersionListView.cpp:139`.
- **`foreach` / `Q_FOREACH` are not removed in Qt 6:** they remain available unless `QT_NO_FOREACH` is defined. Replacing ~13 uses is **cleanup**, not a build blocker.
- **Silent/runtime break already in the tree (missed earlier):** `launcher/ui/pages/global/ProxyPage.cpp:32` uses `SIGNAL(buttonClicked(int))` on `QButtonGroup`. In **Qt 6 this signal is removed** (use `idClicked(int)` / `buttonClicked(QAbstractButton*)` carefully); this will silently stop working if not migrated.
- **Removed API: `QSortFilterProxyModel::filterRegExp()` / `setFilterRegExp()`:** replaced by `filterRegularExpression()` / `setFilterRegularExpression()`. Seen in `launcher/ui/pages/instance/ModFolderPage.cpp` (3 uses) and `launcher/ui/pagedialog/PageContainer.cpp:47` (call site context) — must be updated.
- **Deprecated in Qt 6 (still compiles in many cases):** `QVariant::type()` / comparing to `QVariant::Type::*` is deprecated; `launcher/ui/MainWindow.cpp:1255` does `sAction->data().type() != QVariant::Type::Int` — prefer `typeId()` (or `metaType()` patterns).
- **Redirect handling note (amplified):** Qt 6 defaults to automatic redirect following in many cases depending on policy; `Net::Download` implements manual `handleRedirect()` logic. Treat explicit redirect policy (`ManualRedirectPolicy`) as part of the **silent-behavior** work, not just “small cleanup”.
- **String-based connects:** there are **~130** `SIGNAL(`/`SLOT(` occurrences in the tree (broader than the ~12 `error` cases). Qt prints `QObject::connect: No such signal...` at runtime for broken connects. Add **runtime stderr/Qt warning greps during smoke tests** to the definition of done.

---

## 1) Executive summary

(See **§0** for reviewer corrections: CMP0020 is Windows-only in-tree, `QPalette::Foreground`→`WindowText`, `foreach` still present, plus missed Qt6 breaks like `QButtonGroup::buttonClicked(int)`, `filterRegExp`, and redirect-default behavior.)

- This codebase is **firmly Qt5**: `find_package(Qt5Core/Widgets/Concurrent/Network/Test/Xml)` and `Qt5::*` targets across root, `launcher/`, and vendored libs; `qt5_wrap_ui`/`qt5_add_resources`; qmake-derived install paths; Qt5-era CMake idioms.
- **Local environment (observed earlier in this effort):** CMake **4.5.0**, Qt **6.11.3** installed under system paths. The repo does **not** configure/build cleanly on a Qt6/CMake4 toolchain today (notably C++11, old CMake minimums in many places, Qt5 modules/APIs).
- **Immediate blockers (CMake/bootstrap) — Linux-first context:**
  - `CMakeLists.txt:3–6` sets `cmake_policy(SET CMP0020 OLD)` **only inside `if(WIN32)`**; on Linux this is not the first configure failure. Still remove for cleanliness/CMake4+Windows.
  - `cmake_minimum_required(VERSION 3.10)` in root and many subdirs; Qt6/CMake4 workflows expect newer CMake (often ≥3.16 in practice). Raise consistently.
  - C++ baseline is **C++11** with forced **`-D_GLIBCXX_USE_CXX11_ABI=0`** (`CMakeLists.txt:53–72`). Qt6 + typical distro Qt packages expect **C++17** and the **new** libstdc++ ABI — keeping ABI=0 is high risk.
- **Largest dependency/code risks:**
  - **`libraries/iconfix`** — vendored Qt5 private `QIconLoader` code (`internal/qiconloader*.cpp/.h`, `QIconEnginePlugin`, removed/changed hooks, `QLatin1Literal`, platform bits). No in-tree Qt6 replacement; historically treated as hardest — **consider a time-boxed spike first** (see §5/§6) because call sites are few (~9) and may be replaceable by `QIcon::fromTheme`/theme APIs.
  - **`libraries/quazip`** (pinned MultiMC fork) — public API exposes `QTextCodec*`, plus uses `QString::SkipEmptyParts`, `qSort`, `trUtf8` in sources/tests. Upstream QuaZip 1.x direction exists; **verify upstream Qt6 deps** (reports suggest upstream 1.x may still pull Core5Compat in some Qt6 configurations — do not assume “upstream = no Core5Compat”). Also `MMCZip.cpp` does not touch codec APIs, so codec pain may be localized to QuaZip headers/tests/fork surface.
  - **Silent runtime breaks** (expanded): string-based `SIGNAL(error(...))` for `QNetworkReply`/`QProcess` → `errorOccurred`; `QMimeData::retrieveData` (`QVariant::Type` → `QMetaType`); Qt6 redirect default vs manual `Download::handleRedirect()`; `QButtonGroup::buttonClicked(int)` in `ProxyPage.cpp:32`; `filterRegExp()` in mod list/page container → `filterRegularExpression()`; deprecated `QVariant::type()` comparisons; manual HiDPI/DPR math + obsolete attributes.
- **Upstream context (informational):** MultiMC issue [#4429 “Use Qt6”](https://github.com/MultiMC/Launcher/issues/4429) was reopened later; maintainer comments indicated Qt6 is considered but staged behind other work (updater/toolchain). No Qt6 branch is active in this repo history beyond abandoned experiments noted in commit `4aa2b3ce`. Community PRs like `#5655` (rejected) and `#5656` (still open, QuaZip direction) exist.

### 1a) Implementation log (qt6-linux)
- **Phase 0/1 (done):** branch `qt6-linux`; CMake 3.16 floor; Qt6 targets; C++17; old libstdc++ ABI flag removed; `qt_wrap_ui`/`qt_add_resources`; qmake fallback; `Qt6::OpenGL`; Java helper `--release 8`. Configure with Qt 6.11.3 / CMake 4.5 succeeds and the Java jars build.
- **Vendored dependencies:** ported `systeminfo` and `LocalPeer` off `QRegExp`; applied mechanical Qt6 fixes in the QuaZip fork and enabled `Qt6::Core5Compat` for its `QTextCodec` API. This is scaffolding, not a final QuaZip strategy decision.
- **iconfix spike (done):** replaced the vendored private `QIconLoader` engine with a small `XdgIcon` shim over `QIcon::fromTheme`, Qt6 theme search paths and explicit bundled-theme fallback. The old private-loader sources remain in-tree but are no longer compiled / no longer a build blocker.
- **Launcher source port (in progress):** mechanical API fixes applied for regex/validators, variants, model filters, containers, codecs, fonts, palettes, process/network signals, MIME type and explicit manual redirect policy. Additional removals handled: `QList::toSet/toList/swap`, `QPixmapCache::find` single-argument form, `QString::sprintf`, `QStringRef`, `QJsonDocument::{to,from}BinaryData`, `QPalette::resolve()`, `QIconEngine::availableSizes() const`, `Qt::TextColorRole`, `QAction`/`QShortcut` header locations, QtConcurrent object/member-function calls, `Qt::CTRL + Qt::Key_*` (now `|`), `QModelIndex::child()`, `QFileDialog::DirectoryOnly`, `QApplication::globalStrut()`, `QLayout::setMargin()`, `QAbstractItemView::viewOptions()` (reimplemented via `initViewItemOption()` shim for friend `VisualGroup`), and `qMakePair` forwarding-reference capture of enum flags. Qt 5 binary JSON compatibility is temporarily provided by `Qt6::Core5Compat` (`QBinaryJson`) until the on-disk format migration is decided.
- **Full build (done):** `cmake --build build-qt6 -j2` completes with status 0. All warnings are benign (`QFile::open` `nodiscard` in test helpers, GCC `-Wsfinae-incomplete`).
- **Smoke test (done):** launcher starts on Qt 6.11.3, loads translations/news/icons/themes/instances, detects Java 27, and shows the main window. **No `QObject::connect: No such signal` warnings** — all `SIGNAL()`/`SLOT()` connections resolve. Runtime-only Qt 6 model warnings found and fixed: `TranslationsModel::endInsertRows` invalid index (early `beginInsertRows` first-row bug), unbalanced `beginResetModel`/`endResetModel` in `VersionProxyModel::setSourceModel` (missing `endResetModel` on early return) and `JavaInstallList::sortVersions` (nested reset). Remaining two `connectSlotsByName` warnings are pre-existing dead slots for commented-out actions.
- **Test suite (done):** 3 initial failures all traced to a single Qt 6 behavior change: `QRegularExpressionMatch::capturedTexts()` omits non-participating optional groups, so `GradleSpecifier::operator=` indexed past the list (confirmed with a standalone probe). Fixed by using `match.captured(n)`. **Final result: 32/32 tests pass** (`ctest --output-on-failure -j2`).
- **Not yet done:** iconfix custom-theme switching validation, redirect/updater/auth runtime checks, HiDPI/DPR math, Fusion/theme rendering, QHash/QSet ordering, packaging/doc updates, and final QuaZip upstream-vs-fork decision.
- **SlackBuild patches still unapplied (packaging, not needed for local build):** `0001-Readd-lin-system-and-LAUNCHER_LINUX_DATADIR.patch`, `fix-jars.patch`, `mmc-brand.patch`, `fix-updater-test-root.patch` (only needed when tests run as root; local uid is 1000). `modern-java.patch` was applied earlier.

---

## 2) Repo facts (from this checkout)

- **Top-level build:** `CMakeLists.txt` (CMake 3.10, C++11, `AUTOMOC ON`, custom compile flags including `-D_GLIBCXX_USE_CXX11_ABI=0`, `QT_NO_DEPRECATED_WARNINGS=Y` in practice via flags).
- **Qt5 modules found in CMake:** Core, Widgets, Concurrent, Network, Test, Xml (root), plus DBus in `launcher/CMakeLists.txt`, Svg via `libraries/qrcode`.
- **Submodules** (`.gitmodules`): `libraries/libnbtplusplus` (pinned), `libraries/quazip` (pinned to MultiMC fork).
- **CI:** `.github/` contains only issue templates (no workflows). No in-repo Qt6 CI.
- **Notable launcher areas:** `launcher/ui/*`, `launcher/net/*`, `launcher/minecraft/auth/*`, `launcher/updater/*`, `launcher/skins/*` (OpenGL), themes/styles, accessibility (`ui/instanceview/AccessibleInstanceView*`).

---

## 3) Build system / dependency work (detailed)

### 3.1 CMake / toolchain
- Remove `if(WIN32) set(CMP0020 OLD) ...` in `CMakeLists.txt`.
- Bump `cmake_minimum_required` (root + vendored projects that pin 3.10). Target a floor compatible with **Qt 6.11.3** and **CMake 4.5** (practically ≥3.16; verify exact Qt6 package requirements on your system).
- **C++:** move to **C++17** (root and all subdirs that set standards: `libraries/katabasis`, `libraries/qrcode`, `libraries/libnbtplusplus`, etc.). Remove `-D_GLIBCXX_USE_CXX11_ABI=0` for Qt6/Linux-first plan (document the ABI break/tradeoff).
- Replace `find_package(Qt5 ...)` with `find_package(Qt6 REQUIRED COMPONENTS ...)` as appropriate (likely at minimum: `Core Widgets Concurrent Network Test Xml` plus `DBus` on Linux, `Svg` for qrcode). Add **`OpenGL`** (and/or `OpenGLWidgets` as needed for Qt6) for `QOpenGLWindow`/`QOpenGLFunctions` usage in `launcher/skins/*`.
- Replace `Qt5::*` with `Qt6::*`.
- Remove `Qt5_POSITION_INDEPENDENT_CODE` assumption; set PIC explicitly if still needed.
- Replace `qt5_wrap_ui` → `qt_wrap_ui` (or explicit Qt6 command) in `launcher/CMakeLists.txt` (~45 `.ui`). Replace `qt5_add_resources` → `qt_add_resources` for `.qrc` files.
- Replace `Qt5::Test` in tests/CMake (`launcher/CMakeLists.txt`, `cmake/UnitTest.cmake`).
- **qmake queries:** `cmake/QMakeQuery.cmake` uses `Qt5::qmake`. Remove or replace with Qt6 CMake install-path variables (e.g. `QT6_INSTALL_PLUGINS`, `QT6_INSTALL_LIBS`, `QT6_INSTALL_LIBEXECS`) where still needed. For **Linux-first local dev**, you may temporarily simplify plugin handling, but note deployment implications.
- **`QT_NO_DEPRECATED_WARNINGS=Y`:** stop masking; fix deprecations/removals. (Debug builds may still use `-Werror` in places — decide policy.)

### 3.2 Vendored libraries (Qt coupling)

| Library | Qt5 usage (high level) | Qt6 notes / risks | Effort (rough) |
|--------|------------------------|-------------------|----------------|
| `libraries/quazip` (fork, pinned) | Qt5Core (+ tests Qt5Network/Test in CMake), public headers use `QTextCodec*`, sources use `QString::SkipEmptyParts`, `qSort`, `trUtf8`; tests use `QTextCodec` | **Hard API blocker:** `QTextCodec` removed from Qt6 Core public story in this form. Options: (A) migrate to **upstream QuaZip 1.x** (recommend for maintainability) — update `MMCZip.*`, CMake target names (e.g. `QuaZip::QuaZip`), handle encoding API changes; (B) maintain fork port (incomplete/dirty branch history observed earlier) + consider temporary `Core5Compat` only as last resort (public headers complicate this). Also fix `SkipEmptyParts`, `qSort`→`std::sort`, `trUtf8`→`tr`. | **Medium** (upstream) / **Large** (fork maintenance) |
| `libraries/iconfix` | Vendored Qt5 private `QIconLoader` (`QIconEnginePlugin`, `QIconLoader` internals), `QLatin1Literal`, mac scale helpers, relies on `QIconEngine` hooks (`AvailableSizesHook`, `IconNameHook`) and old patterns | **Largest blocker.** Qt6 `QIconEngine` API changed (virtuals like `availableSizes()`, `iconName()`, `scaledPixmap()` direction differs from old hooks). Private Qt headers unstable. No local Qt6 replacement. May require replacing with modern `QIcon`/theme behavior or a substantial rewrite. Also `foreach` in `xdgicon.cpp`. | **Large** |
| `libraries/LocalPeer` | Qt5Core+Network, `QRegExp` in `LocalPeer.cpp`, Windows: `qt_windows.h`, `Qt::HANDLE`, `foreach` in `LockedFile_win.cpp` | Small–medium: replace `QRegExp` with `QRegularExpression` (or Core5Compat temporarily), adjust Windows includes/macros for Qt6 patterns, replace `foreach`. | **Small–Medium** |
| `libraries/systeminfo` | Qt5Core/Gui/Network (CMake), `QRegExp` in public `include/sys.h` and impl, `QString::SkipEmptyParts` | Public header uses `QRegExp` → affects API/consumers; prefer `QRegularExpression`. `SkipEmptyParts` → `Qt::SkipEmptyParts`. | **Small–Medium** |
| `libraries/katabasis` | Qt5Core/Network, C++11, `foreach` in sources | CMake + `foreach` → `range-for`; API usage generally small. | **Small** |
| `libraries/qrcode` | Qt5Core/Gui/Svg, C++11 | Mostly CMake + ensure Qt6 Svg/Core/Gui; inspect for any removed APIs (low expected risk). | **Small** |
| `libraries/classparser` | Qt5Core, links against `Launcher_quazip`, stale `Qt5Base_INCLUDE_DIRS` | CMake cleanup + follow QuaZip/Qt6 target changes. | **Small** |
| `libraries/libnbtplusplus` | No Qt | C++17/ABI alignment only. | **Small–Medium** |
| Others (`hoedown`, `launcher` Java tools helpers in-tree C++, `xz-embedded`, `tomlc99`, `monocypher`, `optional-bare`, etc.) | Generally no Qt | No direct Qt6 Qt-API port work expected from surveys (verify if any stray Qt includes). | **Small/None** |

### 3.3 Deployment / packaging (awareness; Linux-first focus)
- `CMakeLists.txt` install layout logic (`Launcher_LAYOUT` auto/win-bundle/lin-nodeps/mac-bundle) and manual plugin copying in `launcher/CMakeLists.txt` are Qt5-oriented (plugin dirs `imageformats/iconengines/platforms/styles`, qmake path queries).
- `launcher/install_prereqs.cmake.in` greps for `/lib/libQt...` style paths and misses common `lib64` layouts; uses old `BundleUtilities`.
- `launcher/Launcher.in` sets `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QT_FONTPATH` and does `ldd` checks on plugins — may need adjustment for Qt6 packaging.
- No `windeployqt`/`macdeployqt` usage in repo. **Linux-first:** you can often run from a build tree with system Qt6, but document gaps for shipping bundles later.

### 3.4 Docs / CI / metadata
- `BUILD.md` is Qt5.6/JDK8/OpenSSL1.0.2g/MinGW32-era — must be rewritten for Qt6 toolchains when you formalize build instructions.
- No GitHub Actions workflows; no automated Qt6 builds. Consider adding at least a Linux Qt6 configure+build smoke job later.
- Updater variant IDs currently mention `*-qt5*` in comments/tests context (`CMakeLists.txt` notes, `launcher/updater/v2/*` tests/data) — plan to align variant naming strategy if/when shipping Qt6 builds (out of scope for Linux-first compile goal except awareness).

---

## 4) Source/API incompatibilities (launcher + common patterns)

### 4.1 Compile-time breaks (mechanical)
- **`QString::SkipEmptyParts`** → `Qt::SkipEmptyParts` (many sites: `launcher/modplatform/legacy_ftb/PrivatePackManager.cpp`, `launcher/java/JavaChecker.cpp`, `launcher/minecraft/MinecraftInstance.cpp`, `launcher/ui/MainWindow.cpp`, `launcher/ui/dialogs/AboutDialog.cpp`, `launcher/ui/dialogs/ExportInstanceDialog.cpp`, `launcher/updater/v2/Checker_test.cpp`, plus vendored `systeminfo`, `quazip/quazipdir.cpp`).
- **`QRegExp` / `QRegExpValidator`** → `QRegularExpression` / `QRegularExpressionValidator`. Examples: `launcher/ui/dialogs/AccentColorDialog.cpp`, `launcher/ui/dialogs/AccountsDialog.cpp`, `launcher/BaseInstance.cpp`, `launcher/minecraft/MinecraftInstance.cpp`, `launcher/minecraft/VersionFile.cpp`, `launcher/minecraft/GradleSpecifier.h`, `launcher/JavaCommon.cpp`, `launcher/minecraft/auth/MinecraftAccount.cpp`, `launcher/modplatform/atlauncher/ATLPackInstallTask.cpp`, `launcher/ui/pages/instance/ScreenshotsPage.cpp`, `launcher/ui/dialogs/UpdateDialog.cpp`, plus vendored `LocalPeer`, `systeminfo`.
- **`QTextCodec` / `QTextStream::setCodec(...)`** → `QStringConverter` / `QTextStream::setEncoding(...)`. Launcher: `launcher/settings/INIFile.cpp`, `launcher/ui/dialogs/AboutDialog.cpp`, `launcher/ui/themes/ReuseInfo.cpp`. QuaZip also embeds `QTextCodec` in API/tests.
- **`QFontMetrics::width()`** → `horizontalAdvance()`: `launcher/ui/widgets/ColorEdit.cpp`, `launcher/ui/widgets/ElidedLabel.cpp`.
- **`QPalette::Foreground`** → `QPalette::WindowText`: `launcher/ui/widgets/VersionListView.cpp`.
- **`QList::toSet()` / `QSet::fromList()` patterns** removed in Qt6 usage → use `QSet` range/iterator constructors or equivalents (14 launcher sites found, e.g. `InstanceList.cpp`, `IconList.cpp`, `skins/SkinsModel.cpp`, `minecraft/mod/ModFolderModel.cpp`, `net/NetJob.cpp`, `updater/v2/*`, `ui/dialogs/*`).
- **`Qt::ItemIsTristate`** → `Qt::ItemIsAutoTristate`: `launcher/ui/dialogs/ExportInstanceDialog.cpp`.
- **`QLibraryInfo::location(...)`** → `QLibraryInfo::path(...)`: `launcher/translations/TranslationsModel.cpp`.
- **`QLatin1Literal`** removed: `libraries/iconfix/internal/qiconloader.cpp` (also check `launcher/translations/TranslationsModel.cpp` if present).
- **`foreach` (keyword)** not available in Qt6 in the same way → replace with range-for: seen in `katabasis`, `iconfix`, `LocalPeer/LockedFile_win.cpp`, and others (audit). `Q_FOREACH` macro still exists but prefer range-for.
- **`qSort`** → `std::sort` (QuaZip/tests). **`trUtf8`** → `tr` (QuaZip).
- **High-DPI app attributes** in `launcher/main.cpp`: `Qt::AA_EnableHighDpiScaling`, `Qt::AA_UseHighDpiPixmaps` are obsolete/no-op in Qt6 behavior — remove guards/usage and rely on Qt6 defaults (then validate).

### 4.2 Silent / behavioral risks (test carefully)
- **String-based connects with renamed signals (compile OK, runtime broken):**
  - `QNetworkReply::error(QNetworkReply::NetworkError)` → `errorOccurred` (many: `net/Download.cpp`, `net/PasteUpload.cpp`, `screenshots/*`, `modplatform/modrinth/ModrinthHashLookupRequest.cpp`, `minecraft/auth/AuthRequest.cpp`, etc.).
  - `QProcess::error(QProcess::ProcessError)` → `errorOccurred` (`LoggedProcess.cpp`, `java/JavaChecker.cpp`, and related connects).  
  **Action:** convert these to function-pointer (typed) connects during port; consider a broader pass to reduce future drift.
- **`QMimeData::retrieveData` override**: `launcher/minecraft/WorldList.cpp` uses `QVariant::Type` parameter in Qt5 style — Qt6 uses `QMetaType preferredType`. Wrong signature becomes an overload, drag-out can silently produce bad MIME data. Fix to new signature + `override`.
- **High-DPI / DPR math:** manual scaling in `icons/CappedIconEngine.cpp`, `ui/instanceview/InstanceDelegate.cpp`, `ui/instanceview/InstanceView.cpp`, `skins/SkinWidget.cpp`, `ui/widgets/LabeledToolButton.cpp`. Qt6 changes icon pixmap/DPR semantics in subtle ways; validate on 1x/1.5x/2x displays.
- **Themes/styles:** `FusionStyle`/`ThemeManager` Qt5 workarounds (palette/font/style sheet ordering, cache clears) may be unnecessary or wrong in Qt6; visual QA across Bright/Dark/System/custom themes.
- **Accessibility:** custom `QAccessibleTableInterface` usage in `ui/instanceview/AccessibleInstanceView*` — interface still exists in Qt6.11 headers (per check), but behavior should be sanity-checked (focus, screen reader basics).
- **Containers/hashing:** `QHash`/`QSet` iteration/order and hashing changes in Qt6 — audit any logic that implicitly relied on order (serialization, deterministic UI order). Grep suggested no obvious save-order bugs, but verify lists/groups/mod ordering.
- **`QListView::setResizeMode(QListView::Adjust)`**, old-style `foreach` in some Qt APIs: deprecated-but-present in Qt6 in some cases? Prefer updating; note `foreach` keyword is the bigger issue (removed). `setResizeMode(Adjust)` was deprecated in Qt5 but still exists in Qt6 headers in practice — low priority unless warned.
- **QtConcurrent / QFutureWatcher:** usage patterns seen (`QtConcurrent::run`, `QFutureWatcher::finished/canceled`) appear broadly compatible; still compile-test.

### 4.3 Networking / auth / updater notes
- `QNetworkAccessManager` usage is mostly direct (no custom subclass overriding `createRequest` observed) — lower Qt6 risk there.
- Common `QNetworkRequest` attributes used (`ContentTypeHeader`, `UserAgentHeader`, `HttpStatusCodeAttribute`, `ContentDispositionHeader`, `LocationHeader`, `HttpReasonPhraseAttribute`) still present.
- SSL error handling exists (`net/Download`, `minecraft/auth/AuthRequest`) — test failure paths carefully (Qt6/OpenSSL backend differences possible on some distros, but Linux-first with system Qt6 is the focus).
- No widespread `QNetworkRequest::setRedirectPolicy` patterns found; verify redirect behavior if auth flows misbehave.

### 4.4 OpenGL (skins)
- `launcher/skins/SkinWidget.h/.cpp` and `SkinRenderer.*` use `QOpenGLWindow`, `QOpenGLFunctions`. Qt6 may require the appropriate Qt6 OpenGL module(s) in CMake (`OpenGL`, `OpenGLWidgets`) depending on API form — verify at configure time.

---

## 5) Phased plan (recommended)

### Phase 0 — Preparation (branch + decisions)
1. Create a branch, e.g. `qt6-linux`.
2. **Decide (record):** Qt 6 only; C++17; **drop `-D_GLIBCXX_USE_CXX11_ABI=0`** (Linux). Accept rebuild implications for any bundled assumptions.
3. **Decide:** QuaZip strategy — audit fork vs upstream 1.x (note `MMCZip.cpp` does not use codec API; verify upstream may still require `Qt6::Core5Compat` in some setups). Avoid public `Core5Compat` if feasible, but confirm before committing.
4. (Optional) Note target Qt6 version for Linux-first dev (6.11.3 locally).

### Phase 1 — CMake/bootstrap to a configurable state
1. Remove `CMP0020 OLD` from `CMakeLists.txt` (Windows-only block; not the Linux blocker, but cleanup). Bump `cmake_minimum_required` in root and vendored CMake files.
2. Switch all Qt5 packages/targets to Qt6 across the tree (`find_package(Qt6 ...)`, `Qt6::*`).
3. Migrate `qt5_wrap_ui` → `qt_wrap_ui`, `qt5_add_resources` → `qt_add_resources`.
4. Replace qmake-based install path queries with Qt6 CMake variables or simplify for dev builds (document temporary shortcuts).
5. Configure a Qt6 build directory and iterate until configure succeeds or only source errors remain (example):  
   `cmake -S . -B build-qt6 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/usr/lib64/cmake` (adjust to your distro).

### Phase 2 — Dependency ports (in order)
1. **`iconfix` (spike first, ~1 day):** try replacing `XdgIcon` with `QIcon::fromTheme`/theme APIs in Qt6. If viable, delete vendored private loader; else document gaps and do a focused port.
2. **`quazip`:** audit fork vs upstream (patch delta + Core5Compat reality), migrate per choice, update `MMCZip.*`/CMake targets.
3. **Small vendored libs:** `LocalPeer`, `systeminfo`, `katabasis`, `qrcode`, `classparser`, `libnbtplusplus` (C++/ABI). Fix `foreach`, Windows headers/macros as needed.
4. **Launcher mechanical API fixes:** `SkipEmptyParts`→`Qt::SkipEmptyParts`, `QLatin1Literal`, `QFontMetrics::width`→`horizontalAdvance()`, `QPalette::Foreground`→`QPalette::WindowText`, `ItemIsTristate`→`ItemIsAutoTristate`, `toSet/fromList` patterns, `QRegExp`/validators→`QRegularExpression*`, `filterRegExp()`→`filterRegularExpression()`, `QVariant::type()`→`typeId()` where applicable, `QTextCodec`/`setCodec`→`QStringConverter`/`setEncoding`, `QLibraryInfo::location`→`path`, remove obsolete HiDPI attributes.
5. **Silent-break fixes:** `QNetworkReply::errorOccurred`, `QProcess::errorOccurred` (typed), `WorldMimeData::retrieveData` (`QMetaType` + `override`), `ProxyPage.cpp:32` (`QButtonGroup::buttonClicked(int)`), explicit redirect policy for `Net::Download` (`ManualRedirectPolicy`) to keep `handleRedirect()`, then fix other string connects found via runtime warnings.

### Phase 3 — Build + functional verification (Linux)
1. Compile (expect to fix warnings; note `-Werror` in some debug configs). Resolve OpenGL module linkage for skins.
2. **Smoke tests (manual):** launch app, preferences, instance create/import basics, start/stop Java/launch path (exercises `QProcess`), network/download and auth flows (exercises `errorOccurred`), theme switching, instance list/groups, world drag/export behavior, screenshots/mod UI touches involving regex changes, high-DPI at your scale.
3. **Unit tests:** build and run applicable `add_unit_test` targets in `launcher/CMakeLists.txt` (and vendored test targets you care about), updating tests for Qt6 API changes (QuaZip, regex, `SkipEmptyParts`, etc.).

### Phase 4 — Linux packaging/deploy hygiene (lightweight)
1. Document or fix the worst deployment footguns for local use: `install_prereqs.cmake.in` (`lib64`), manual plugin copy lists, `Launcher.in` environment variables. Defer full multi-distro bundle redesign unless needed.
2. Update `BUILD.md` draft notes for Qt6 Linux (Qt packages, CMake version, compiler, any required distro packages) — can be iterative.

---

## 6) Risks and unknowns (explicit)
- **`iconfix` schedule risk:** a day-long spike may show `QIcon::fromTheme` + theme APIs are sufficient (preferred) or insufficient; either outcome should drive the plan (avoid committing to XL port before the spike).
- **QuaZip choice:** upstream 1.x may still depend on `Qt6::Core5Compat` in some Qt6 setups (verify). `MMCZip.cpp` is codec-agnostic, so minimizing fork churn is feasible if only QuaZip internals/tests are affected; still requires diffing the MultiMC fork.
- **Visual/UX regressions:** HiDPI, Fusion/theme, custom delegates, skin preview — require human testing; automated coverage is limited.
- **Silent signal/connect bugs:** ~120 `SIGNAL(`/`SLOT(` uses remain in the tree (verify by grep). Qt logs `QObject::connect: No such signal...` at runtime when broken; add stderr/Qt warning scraping to smoke tests (and fix `ProxyPage.cpp:32` `QButtonGroup::buttonClicked(int)`, plus all `error`→`errorOccurred` cases).
- **Distribution differences:** Qt6 plugin backends (Wayland/XCB), OpenSSL/TLS backend behavior, and theme availability vary by distro — Linux-first means “works on your dev box” first.
- **Upstream alignment:** if you intend to contribute upstream later, coordinate on QuaZip direction, variant IDs, and whether they want `Core5Compat`-free code immediately.

---

## 7) Suggested “definition of done” for Linux-first milestone
- Configures with **Qt 6.11.3** and **CMake 4.5** on Linux using documented commands (after Phase 1 CMake work).
- Builds successfully with **C++17**, **no `-D_GLIBCXX_USE_CXX11_ABI=0`**, and `CMP0020 OLD` removed (note: it was only set on Windows in-tree).
- App launches; core flows work in manual smoke tests listed in Phase 3.
- **No `QObject::connect` runtime warnings** about missing signals in a normal run of smoke tests (grep application stderr/log for `No such signal`). Fix known cases: `ProxyPage.cpp:32`, all `QNetworkReply::error`/`QProcess::error` string connects.
- At least the existing non-broken unit tests pass (or are updated with clear rationale). 

---

## 8) Reference paths (quick index)
- Root build: `CMakeLists.txt`, `cmake/QMakeQuery.cmake`, `cmake/UnitTest.cmake`
- Launcher build/tests: `launcher/CMakeLists.txt`
- Key source hotspots: `launcher/main.cpp`, `launcher/InstanceList.cpp`, `launcher/minecraft/WorldList.cpp`, `launcher/net/*`, `launcher/minecraft/auth/*`, `launcher/ui/dialogs/*`, `launcher/ui/widgets/*`, `launcher/skins/*`, `launcher/translations/TranslationsModel.cpp`
- Vendored: `libraries/iconfix/*`, `libraries/quazip/*`, `libraries/LocalPeer/*`, `libraries/systeminfo/*`, `libraries/katabasis/*`

---

## 9) Ticket-sized checklist (copy/paste into issues)

### Phase 0 — Prep
- **[P0-Prep-01] Create Qt6 branch and document decisions**  
  - Scope: create branch (e.g. `qt6-linux`), record decisions (Qt6-only, C++17, drop `-D_GLIBCXX_USE_CXX11_ABI=0`, QuaZip strategy: upstream vs fork; note `MMCZip.cpp` does not use QuaZip codec API).  
  - Effort: **S**

### Phase 1 — CMake/bootstrap
- **[P1-CMake-01] Remove `CMP0020 OLD` (cleanup; CMake 4 + Windows)**  
  - Files: `CMakeLists.txt:3–6` (remove Windows policy block).  
  - Change: delete obsolete policy. Note: on Linux this block is not executed, so not the primary Linux configure blocker.  
  - Effort: **S** | Risk: **Low**

- **[P1-CMake-02] Bump `cmake_minimum_required` (root + vendored)**  
  - Files (examples): `CMakeLists.txt:1`, `libraries/katabasis/CMakeLists.txt:1`, `libraries/qrcode/CMakeLists.txt:1`, `libraries/quazip/CMakeLists.txt:1`, `libraries/LocalPeer/CMakeLists.txt:1`, `libraries/iconfix/CMakeLists.txt:1`, `libraries/libnbtplusplus/CMakeLists.txt`, plus any other subdirs pinned to `3.10`.  
  - Change: set to a Qt6/CMake4-compatible floor (e.g. `>=3.16`) consistently.  
  - Effort: **M** | Risk: **Medium** (many files; CI/tooling assumptions)

- **[P1-CMake-03] C++ standard to C++17, remove old libstdc++ ABI flag**  
  - Files: `CMakeLists.txt:53–72` (set `CMAKE_CXX_STANDARD 17`, drop `-D_GLIBCXX_USE_CXX11_ABI=0`, re-evaluate flags like `QT_NO_DEPRECATED_WARNINGS` policy). Also update `libraries/katabasis/CMakeLists.txt:22–25`, `libraries/qrcode/CMakeLists.txt:9–12`, `libraries/libnbtplusplus/CMakeLists.txt` (C++11 → 17).  
  - Change: align all subprojects to C++17; document ABI change.  
  - Effort: **M** | Risk: **High** (ABI/linking surprises with system libs/bundles)

- **[P1-CMake-04] Migrate `find_package(Qt5*)` → `find_package(Qt6 ...)` and targets**  
  - Files: `CMakeLists.txt` (Core/Widgets/Concurrent/Network/Test/Xml), `buildconfig/CMakeLists.txt`, `launcher/CMakeLists.txt` (add `DBus` on Linux, link `Qt6::*`), `cmake/UnitTest.cmake`, and all `libraries/*/CMakeLists.txt` that use Qt5 (katabasis, LocalPeer, iconfix, systeminfo, quazip, qrcode, classparser).  
  - Add modules as needed: `Qt6::Svg` (qrcode), `Qt6::OpenGL`/`Qt6::OpenGLWidgets` if required for skins (`launcher/skins/*`).  
  - Effort: **L** | Risk: **High** (touches many CMake files; transitive deps)

- **[P1-CMake-05] Replace `qt5_wrap_ui`/`qt5_add_resources` with Qt6 equivalents**  
  - Files: `launcher/CMakeLists.txt` (all `qt5_wrap_ui`, `qt5_add_resources` calls).  
  - Effort: **S–M** | Risk: **Low–Medium** (mechanical, but easy to miss entries)

- **[P1-CMake-06] Replace qmake path queries with Qt6 CMake paths (or simplify for dev)**  
  - Files: `cmake/QMakeQuery.cmake`, `CMakeLists.txt` usage of `QT_INSTALL_PLUGINS/LIBS/LIBEXECS` derived from qmake.  
  - Change: use `Qt6`-provided install paths or remove manual plugin copy logic for Linux-first dev build (document follow-up).  
  - Effort: **M** | Risk: **Medium** (deployment behavior)

### Phase 2 — Dependency ports
- **[P2-Dep-01] `libraries/iconfix` — Qt6 strategy (replace or port)**  
  - Files: `libraries/iconfix/CMakeLists.txt`, `libraries/iconfix/internal/qiconloader*.{h,cpp}`, `libraries/iconfix/xdgicon.cpp`, consumers (`launcher/Application.cpp`, `launcher/icons/MMCIcon.cpp`, `launcher/ui/instanceview/InstanceDelegate.cpp`, `launcher/ui/themes/IconThemeList.cpp`, linked in `launcher/CMakeLists.txt`).  
  - Tasks: decide replace-vs-port; remove private API dependence (`QIconEnginePlugin`, old hooks, `QLatin1Literal`, QtGui private headers patterns), fix `foreach`, validate icon themes on Linux.  
  - Effort: **XL** | Risk: **Very High**

- **[P2-Dep-02] `libraries/quazip` — evaluate upstream 1.x vs fork (with caveats)**  
  - Files: `libraries/quazip/*` (submodule), `launcher/MMCZip.cpp/h`, `launcher/CMakeLists.txt` link targets.  
  - Tasks: **First diff the MultiMC fork** against upstream for custom patches (do not blindly switch). Note: `launcher/MMCZip.cpp` does **not** use QuaZip codec APIs, so `QTextCodec` pain is largely inside the QuaZip header/tests/fork surface. **Verify upstream QuaZip 1.x Qt6 dependencies** (community reports indicate upstream 1.x on Qt6 may still require `Qt6::Core5Compat` in some cases) — if true, “upstream + no Core5Compat” is contradictory. If upstream is viable and patch delta is small, migrate (update targets e.g. `QuaZip::QuaZip`, encoding API). If not, a **minimal fork port** (remove/fix `QTextCodec` surface used only internally/tests as feasible) may be cheaper than a full fork rewrite.  
  - Effort: **M (audit + minimal fork fix) / L (upstream migration) / XL (full fork port)** | Risk: **High** (API + archive behavior)

- **[P2-Dep-03] `libraries/LocalPeer` Qt6 port**  
  - Files: `libraries/LocalPeer/src/LocalPeer.cpp`, `libraries/LocalPeer/src/LockedFile_win.cpp`, `libraries/LocalPeer/CMakeLists.txt`, tests `LocalPeer_test.cpp`.  
  - Tasks: `QRegExp`→`QRegularExpression`, `qChecksum` new form, `QCryptographicHash::Algorithm::Sha1`→`QCryptographicHash::Sha1`, replace `foreach`, adjust Windows includes/macros (`qt_windows.h`, `Qt::HANDLE` still exists but verify).  
  - Effort: **M** | Risk: **Medium** (single-instance identity/socket naming)

- **[P2-Dep-04] `libraries/systeminfo` Qt6 port**  
  - Files: `libraries/systeminfo/include/sys.h`, `libraries/systeminfo/src/distroutils.cpp`, `libraries/systeminfo/CMakeLists.txt`.  
  - Tasks: `QRegExp`→`QRegularExpression` (public header impact), `QString::SkipEmptyParts`→`Qt::SkipEmptyParts`.  
  - Effort: **S–M** | Risk: **Medium** (public API)

- **[P2-Dep-05] `libraries/katabasis` Qt6 port**  
  - Files: `libraries/katabasis/CMakeLists.txt`, `libraries/katabasis/src/*.cpp` (`foreach`, verify `QJsonObject::toVariantMap()`/JSON usage patterns).  
  - Tasks: CMake to Qt6, replace `foreach`, test device/refresh flows; do **not** enable SSL error ignoring.  
  - Effort: **S–M** | Risk: **Medium** (auth/security flows)

- **[P2-Dep-06] `libraries/qrcode`, `libraries/classparser`, `libraries/libnbtplusplus` alignment**  
  - Files: `libraries/qrcode/CMakeLists.txt`, `libraries/classparser/CMakeLists.txt`, `libraries/libnbtplusplus/CMakeLists.txt` (+ sources if compile errors).  
  - Tasks: Qt6 CMake/targets; `classparser` follows quazip/Qt6; `libnbtplusplus` C++17/ABI alignment.  
  - Effort: **S** each | Risk: **Low–Medium**

### Phase 2 — Launcher mechanical + silent fixes
- **[P2-L-01] `QString::SkipEmptyParts` → `Qt::SkipEmptyParts` (launcher + touched vendored)**  
  - Files (examples): `launcher/modplatform/legacy_ftb/PrivatePackManager.cpp`, `launcher/java/JavaChecker.cpp`, `launcher/minecraft/MinecraftInstance.cpp`, `launcher/ui/MainWindow.cpp`, `launcher/ui/dialogs/AboutDialog.cpp`, `launcher/ui/dialogs/ExportInstanceDialog.cpp`, `launcher/updater/v2/Checker_test.cpp`, plus any remaining from grep.  
  - Effort: **S** | Risk: **Low**

- **[P2-L-02] `QLatin1Literal` removal**  
  - Files: `libraries/iconfix/internal/qiconloader.cpp`, `launcher/translations/TranslationsModel.cpp` (`const static QLatin1Literal ...` → `QLatin1String`/equivalent).  
  - Effort: **S** | Risk: **Low**

- **[P2-L-03] `QFontMetrics::width()` → `horizontalAdvance()`**  
  - Files: `launcher/ui/widgets/ColorEdit.cpp`, `launcher/ui/widgets/ElidedLabel.cpp`.  
  - Effort: **S** | Risk: **Low**

- **[P2-L-04] `QPalette::Foreground` → `QPalette::WindowText`** (alias fix)  
  - File: `launcher/ui/widgets/VersionListView.cpp:139`.  
  - Change: use `QPalette::WindowText` (do **not** use `QPalette::Text` as a blind replacement).  
  - Effort: **S** | Risk: **Low**

- **[P2-L-05] `Qt::ItemIsTristate` → `Qt::ItemIsAutoTristate`**  
  - File: `launcher/ui/dialogs/ExportInstanceDialog.cpp`.  
  - Effort: **S** | Risk: **Low**

- **[P2-L-06] `QList::toSet()` / `QSet` conversion patterns**  
  - Files (non-exhaustive): `launcher/InstanceList.cpp`, `launcher/icons/IconList.cpp`, `launcher/skins/SkinsModel.cpp`, `launcher/minecraft/mod/ModFolderModel.cpp`, `launcher/net/NetJob.cpp`, `launcher/updater/v2/Formats.cpp`, `launcher/updater/v2/State.cpp`, `launcher/ui/dialogs/*Copy/New*Dialog.cpp`, `launcher/modplatform/legacy_ftb/PrivatePackManager.cpp`.  
  - Change: use Qt6-appropriate `QSet` construction/from-list forms.  
  - Effort: **M** | Risk: **Medium** (logic correctness)

- **[P2-L-07] `QRegExp` / `QRegExpValidator` → `QRegularExpression*`**  
  - Files (launcher): `launcher/ui/dialogs/AccentColorDialog.cpp`, `launcher/ui/dialogs/AccountsDialog.cpp`, `launcher/BaseInstance.cpp`, `launcher/minecraft/MinecraftInstance.cpp`, `launcher/minecraft/VersionFile.cpp`, `launcher/minecraft/GradleSpecifier.h` (wildcard semantics), `launcher/JavaCommon.cpp`, `launcher/minecraft/auth/MinecraftAccount.cpp`, `launcher/modplatform/atlauncher/ATLPackInstallTask.cpp`, `launcher/ui/pages/instance/ScreenshotsPage.cpp`, `launcher/ui/dialogs/UpdateDialog.cpp`, plus vendored per P2-Dep-03/04.  
  - Effort: **M** | Risk: **Medium–High** (semantics: wildcard/version matching, captures, validator behavior)

- **[P2-L-07b] `QSortFilterProxyModel::filterRegExp()` → `filterRegularExpression()`**  
  - Files: `launcher/ui/pages/instance/ModFolderPage.cpp` (3 uses), `launcher/ui/pagedialog/PageContainer.cpp:47` (verify call site).  
  - Effort: **S** | Risk: **Low–Medium** (behavior parity; test filtering UI)

- **[P2-L-07c] `QVariant::type()` deprecated comparisons**  
  - File: `launcher/ui/MainWindow.cpp:1255` (`.type() != QVariant::Type::Int`) — migrate to `typeId()` / `metaType()` per Qt6 guidance.  
  - Effort: **S** | Risk: **Low**

- **[P2-L-08] `QTextCodec` / `QTextStream::setCodec()` → `QStringConverter` / `setEncoding()`**  
  - Files: `launcher/settings/INIFile.cpp`, `launcher/ui/dialogs/AboutDialog.cpp`, `launcher/ui/themes/ReuseInfo.cpp`. (QuaZip handled in P2-Dep-02.)  
  - Effort: **S** | Risk: **Low**

- **[P2-L-09] `QLibraryInfo::location()` → `QLibraryInfo::path()`**  
  - File: `launcher/translations/TranslationsModel.cpp`.  
  - Effort: **S** | Risk: **Low**

- **[P2-L-10] Remove obsolete HiDPI attributes in `main.cpp`**  
  - File: `launcher/main.cpp` (remove `AA_EnableHighDpiScaling`, `AA_UseHighDpiPixmaps` usage/guards).  
  - Effort: **S** | Risk: **Medium** (visual behavior — test scales)

- **[P2-L-11] Silent break: `QNetworkReply::error` → `errorOccurred` (typed connects)**  
  - Files: `launcher/net/Download.cpp`, `launcher/minecraft/auth/AuthRequest.cpp`, `launcher/net/PasteUpload.cpp`, `launcher/screenshots/ImgurUpload.cpp`, `launcher/screenshots/ImgurAlbumCreation.cpp`, `launcher/modplatform/modrinth/ModrinthHashLookupRequest.cpp` (and audit others).  
  - Effort: **S–M** | Risk: **High** (auth/download failures if missed)

- **[P2-L-11b] `QButtonGroup::buttonClicked(int)` removed in Qt6**  
  - File: `launcher/ui/pages/global/ProxyPage.cpp:32` (`SIGNAL(buttonClicked(int))`) — migrate to `idClicked(int)` (or connect by button object as appropriate).  
  - Effort: **S** | Risk: **Medium** (proxy UI silently broken)

- **[P2-L-12] Silent break: `QProcess::error` → `errorOccurred`**  
  - Files: `launcher/LoggedProcess.cpp`, `launcher/java/JavaChecker.cpp` (and related).  
  - Effort: **S** | Risk: **Medium**

- **[P2-L-13] Fix `QMimeData::retrieveData` signature (world drag/drop)**  
  - File: `launcher/minecraft/WorldList.cpp` (`WorldMimeData::retrieveData(const QString&, QMetaType preferredType)`, add `override`).  
  - Effort: **S** | Risk: **High** if missed (silent wrong drag data)

- **[P2-L-14] Redirect policy for `Net::Download` (explicit)**  
  - Files: `launcher/net/Download.*` (set `QNetworkRequest::RedirectPolicyAttribute = ManualRedirectPolicy` to preserve `handleRedirect()`), review `updater/v2/*` usage.  
  - Effort: **S** | Risk: **High** (updater/mirror correctness)

- **[P2-L-15] Audit JSON/date/URL edge cases (security-sensitive paths)**  
  - Files: `launcher/Json.cpp`, `launcher/minecraft/auth/Parsers.cpp`, `launcher/updater/v2/Checker.cpp` (ISO dates/expiry), `launcher/modplatform/*` JSON parsing as needed.  
  - Effort: **M** | Risk: **High** for auth/updater

- **[P2-L-16] QDom/XML parsing sanity (news/FTB)**  
  - Files: `launcher/news/NewsChecker.cpp`, `launcher/news/NewsEntry.cpp`, `launcher/modplatform/legacy_ftb/PackFetchTask.cpp`; consider `QXmlStreamReader` alternatives if stricter Qt6 behavior breaks feeds.  
  - Effort: **M** | Risk: **Medium**

- **[P2-L-17] SSL/TLS behavior review (no ignores, test OpenSSL 3/system Qt6)**  
  - Files: `launcher/net/Download.cpp` (`sslErrors`), `launcher/minecraft/auth/AuthRequest.cpp` (`onSslErrors`), Katabasis `Reply.cpp`/`DeviceFlow.cpp` (ensure no ignore).  
  - Effort: **M** | Risk: **High** (transport security; connectivity)

### Phase 3 — Build, run, verify (Linux)
- **[P3-01] Configure + compile on Linux with Qt 6.11.3**  
  - Effort: **M–L** (depends on Phase 2) | Risk: **High**

- **[P3-02] Manual smoke tests (checklist)**  
  - Launch, prefs, instance create/import, Java/QProcess launch, downloads/auth (error paths), themes, instance list/groups, world drag, screenshots/mod UI regex areas, HiDPI at local scale.  
  - Effort: **M** | Risk: **Medium–High**

- **[P3-03] Unit tests**  
  - Build/run relevant `add_unit_test` targets and vendored tests; update for API changes.  
  - Effort: **M** | Risk: **Medium**

### Phase 4 — Linux deploy/docs hygiene
- **[P4-01] Triage install/deploy scripts for Qt6 realities (dev notes acceptable)**  
  - Files: `launcher/install_prereqs.cmake.in`, `launcher/Launcher.in`, `launcher/CMakeLists.txt` plugin copy logic.  
  - Effort: **M** | Risk: **Medium**

- **[P4-02] Update `BUILD.md` for Qt6 Linux (draft)**  
  - Effort: **S–M** | Risk: **Low**
