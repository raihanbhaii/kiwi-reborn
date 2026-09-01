<p align="center">
  <img src="kiwi_logo_circle.svg" alt="Kiwi Reborn Ultimate" width="160" height="160"/>
</p>

<h1 align="center">Kiwi Reborn Ultimate</h1>

<p align="center">
  <b>Chromium 105 · Android · Brave-style native adblock · universal background playback · desktop-class extension support</b>
</p>

<p align="center">
  <a href="https://github.com/shslab-org/kiwi-reborn/actions/workflows/build_and_sign_release_apk.yml"><img src="https://github.com/shslab-org/kiwi-reborn/actions/workflows/build_and_sign_release_apk.yml/badge.svg" alt="Build APK"/></a>
  <img src="https://img.shields.io/badge/Engine-Chromium_105.0.5195.24-blue" alt="Engine"/>
  <img src="https://img.shields.io/badge/Android-6%2B_(API_23)-3ddc84" alt="Android"/>
  <img src="https://img.shields.io/badge/APKs-arm64__v8a_·_armeabi__v7a-orange" alt="ABIs"/>
  <img src="https://img.shields.io/badge/Adblock-Native_C%2B%2B_network--level-EA4AAA" alt="Adblock"/>
  <img src="https://img.shields.io/badge/Extensions-Chrome_Web_Store-4285F4" alt="Extensions"/>
</p>

---

**Kiwi Reborn Ultimate** is a feature fork of [raihanbhaii/kiwi-reborn](https://github.com/raihanbhaii/kiwi-reborn) — itself part of the [Kiwi Browser](https://github.com/kiwibrowser/src.next) lineage — built on **Chromium 105.0.5195.24** — the version the overlay's build files actually target (see [Why Chromium 105](#-why-chromium-105-the-honest-story)). This fork keeps every Kiwi Reborn capability (Chrome Web Store extension support, the full AMOLED/night-mode engine, per-site ads toggle) and adds three things the base did not have:

1. a **Brave-inspired native adblock engine** (real C++ engine, network-level blocking + cosmetic filtering),
2. **universal background playback** for HTML5 media (generic, not site-specific),
3. a **honest, key-based Google sync story** and a modernized **two-ABI CI**.

> [!IMPORTANT]
> **Nothing from the existing Kiwi Reborn feature set was removed.** Every addition is additive and individually toggleable. See [What came from where](#-what-came-from-where-transparency).

## 🔍 Why Chromium 105 (the honest story)

During this fork's first real CI build we hit `gn` errors that pointed somewhere uncomfortable. The investigation:

* The upstream repo's `CHROMIUM_VERSION` was bumped **105 → 112 → 148** in two commits dated May 2026 that each changed **only that one file** (3 lines) — no source imports, no build-file updates.
* The overlay tree itself is the Kiwi Browser (`kiwibrowser/src.next`) state, whose build files target **Chromium 105.0.5195.24** — the last Kiwi-pinned version. `KIWI_VERSION` in the tree still reads `105.0.5195.33`.
* Building against a real 148 checkout fails immediately: the 105-era build files import 60+ files that no longer exist in 148 (NaCl, Feed, PPAPI build flags, Cast config, Assistant, old Polymer toolchain — all removed or moved upstream after 105). Upstream's CI could never have run (its workflow was `workflow_dispatch`-only and checked out a fixed ref).

Per this project's own ground rules — *don't break the project for the sake of version numbers* — this fork **realigned the pin to the overlay's true engine, Chromium 105.0.5195.24**, adapted all new feature code to the 105 API surface, and documented the modern-Chromium rebase as the top roadmap item. That trade-off is stated here plainly rather than papered over with a number the build can't back.


## 📖 Contents

- [Features at a glance](#-features-at-a-glance)
- [Native adblock](#-native-adblock-brave-inspired-c-engine)
- [Universal background playback](#%EF%B8%8F-universal-background-playback)
- [Chrome extension support](#-chrome-extension-support-preserved)
- [Dark / AMOLED themes](#-dark--amoled-themes-preserved--verified)
- [Privacy](#-privacy)
- [Google Account sync](#-google-account-sync-honest-enablement)
- [Releases & APKs](#-releases--apks)
- [Building](#-building)
- [Architecture](#-architecture)
- [Development notes](#-development-notes)
- [Testing](#-testing)
- [Roadmap](#-roadmap)
- [Known limitations](#%EF%B8%8F-known-limitations-honest-list)
- [What came from where](#-what-came-from-where-transparency)
- [Credits](#-credits)

## ✨ Features at a glance

| Feature | What you get | Where |
|---|---|---|
| 🛡 **Native adblock** | Network-level blocking (pre-connection, `ERR_BLOCKED_BY_CLIENT`) + cosmetic filtering, EasyList-family lists, custom lists & filters, allowlist, live stats | Settings → **Adblock** |
| ▶️ **Background playback** | HTML5 audio/video keeps playing with screen off / app backgrounded; lock-screen & notification media session controls; Bluetooth headset buttons | Settings → **Background playback** (default on) |
| 🧩 **Extensions** | Install desktop Chrome extensions from the Chrome Web Store, full `chrome://extensions` management | inherited Kiwi feature |
| 🌙 **Themes** | 6 web-contents night modes incl. pure-black **AMOLED** + system-following dark UI | Settings → **Night mode** |
| 🔒 **Privacy** | No new telemetry; adblock fully local; standard Chromium metrics only with explicit consent | — |
| 🔑 **Sync-ready builds** | Optional Google API/OAuth keys via build-time secrets (see [sync](#-google-account-sync-honest-enablement)) | build config |
| 📦 **Two-ABI CI** | Parallel `arm64-v8a` + `armeabi-v7a` APK artifacts on every manual run | [Actions](https://github.com/shslab-org/kiwi-reborn/actions/workflows/build_and_sign_release_apk.yml) |

## 🛡 Native adblock (Brave-inspired, C++ engine)

This is **not** a URL-blocker toy and not a page-script hack. It is the same architectural position Brave takes: filter *inside* the browser, before the network stack ever dials out.

### How it works

```
                     ┌────────────────────────────────────────────────┐
 request ──────────► │ AdblockURLLoaderThrottle (browser process)     │
 (every URL)         │  • checks URL against engine snapshot          │
                     │  • blocked? → ERR_BLOCKED_BY_CLIENT, no socket │
                     └────────────────────────────────────────────────┘
                     ┌────────────────────────────────────────────────┐
 page load ────────► │ AdblockTabHelper (per frame)                   │
                     │  • injects cosmetic CSS in an isolated world   │
                     │  • page JS cannot see or remove it             │
                     └────────────────────────────────────────────────┘
                     ┌────────────────────────────────────────────────┐
 lists ────────────► │ components/adblock engine (C++)                │
                     │  • ABP parser → hash-indexed matcher           │
                     │  • immutable snapshots, lock-free matching     │
                     │  • updates parse off-thread, swap atomically   │
                     └────────────────────────────────────────────────┘
```

* **Engine core** (`components/adblock/`, self-contained C++, no `content/` dependency): parses ABP/EasyList syntax and builds hash-indexed structures so candidate lookup is O(host-length) hash probes instead of a linear scan over 100k+ rules. Matching runs on immutable `EngineData` snapshots — list updates never stall requests, requests never block updates.
* **Network blocking** (`chrome/browser/adblock/AdblockURLLoaderThrottle`): every request flowing through Chromium's network service is checked *before any connection is made* and cancelled with `ERR_BLOCKED_BY_CLIENT`.
* **Cosmetic injection** (`AdblockTabHelper`): element-hiding CSS is injected per frame in an isolated world the page cannot inspect or fight.
* **List management**: EasyList + EasyPrivacy (default **on**), Fanboy's Annoyance, EasyList Cookies, plus a **custom list URL**. Lists are cached with atomic writes, refreshed daily in the background, and parsed off a worker thread.
* **UI** (Settings → Adblock): master switch, per-list toggles, "update now", custom filters (one per line), per-site allowlist, and live statistics (blocked requests / network rules / cosmetic rules).
* The pre-existing app-menu per-site ads toggle (Chromium `ContentSettingsType.ADS`) continues to work alongside the engine.

### Filter syntax support

| Syntax | Example | Status |
|---|---|---|
| `\|\|host^` host anchor | `\|\|ads.example.com^` | ✅ |
| `^` separator | `/ads^` | ✅ |
| `*` wildcard | `\|\|ads.*.example^` | ✅ |
| `@@` exception rule | `@@\|\|cdn.good.com^` | ✅ |
| `$important` | `\|\|x.com^$important` | ✅ |
| `$domain=` | `\|\|x.com^$domain=a.com\|b.org` | ✅ |
| `$third-party` / `$first-party` | `\|\|tracker.io^$third-party` | ✅ |
| resource types (`$script`, `$image`, `$stylesheet`, `$subdocument`, `$xmlhttprequest`, `$media`, `$websocket`, `$popup`, `$font`, `$ping`, …) | `\|\|ad.js^$script` | ✅ |
| cosmetic `##` selectors | `##.ad-banner` | ✅ |
| cosmetic exceptions `#@#` | `#@#.allowed-ad` | ✅ |
| `$generichide` / `$elemhide` | `\|\|site.com^$generichide` | ✅ |
| `$redirect=` | treated as block | ⚠️ approximated |
| `$csp=` | — | ❌ dropped |
| scriptlets `#$#` / procedural `#?#` | — | ❌ ignored (see [roadmap](#-roadmap)) |

## ▶️ Universal background playback

Generic HTML5 media background play — **no YouTube hardcode**; any site benefits:

* `WebMediaPlayerImpl::OnFrameHidden` (blink, patched at the pinned tag) keeps playback alive when the app loses focus or the screen turns off — it skips the gesture-to-resume rule and the idle-pause timer.
* `Document::IsPageVisible` (blink) reports hidden pages as visible while the feature is on, so **site scripts** (e.g. YouTube's `visibilitychange` pause handler) cannot stop media through the Page Visibility API. Unload semantics and Chromium's timer/memory throttling are untouched.
* The flag is a persisted user toggle (Settings → **Background playback**, default **on**), propagated from the browser process (`KiwiFlagsService`) into each renderer via `AppendExtraCommandLineSwitches`. It takes effect for newly spawned renderers — do a full relaunch after toggling.
* **Lock-screen / notification controls, audio focus, Bluetooth & headset buttons** come from upstream Chromium's MediaSession integration, wherever the site exposes one.
* Multiple media tabs: each plays through its own media session; Android's media notification switches between active sessions.

## 🧩 Chrome extension support (preserved)

Kiwi's flagship feature — desktop-class extension support on Android — is preserved untouched: Chrome Web Store install flow (`webstorePrivate` wiring), `chrome://extensions` management, extensions keep running across restarts. This fork's changes (adblock throttle, playback flags) do not alter the extensions pipeline.

## 🌙 Dark / AMOLED themes (preserved & verified)

The complete Kiwi Reborn night-mode engine is intact and verified on this branch:

| Mode | Effect |
|---|---|
| Default | no transformation |
| **AMOLED** | pure-black forced dark (battery-friendly on OLED panels) |
| AMOLED grayscale | pure black + desaturated |
| Gray | classic forced dark |
| Gray grayscale | classic dark + desaturated |
| High contrast | maximum-contrast dark |

Configure in **Settings → Night mode**. System-following dark UI is available independently.

## 🔒 Privacy

* **Audit result**: the overlay adds **no new telemetry** to upstream Chromium. UMA histograms are the standard local instrumentation that only uploads with the user's explicit consent (Settings → Google services → usage & diagnostics).
* The adblock engine runs **fully local**: filter lists are fetched directly from their publishers (easylist.to or your custom URL) — **no proxy, no MITM, no account, no telemetry**.
* No analytics, crash-report ingestion change, or data-collection code was introduced by this branch.
* The architecture is inspectable: every new file is in this repository, nothing is obfuscated or remotely configurable.

## 🔑 Google Account Sync (honest enablement)

Chromium's native Google services (sign-in, bookmark/history/password/tab sync) require **per-developer API keys** under Google's ToS — a browser build cannot ship someone else's keys. This fork wires the keys cleanly at build time:

1. Get your own key triple from Google (API Console: OAuth client + API key with Chrome/Sign-in scopes).
2. Add them as GitHub Actions secrets in your fork: `GOOGLE_API_KEY`, `GOOGLE_DEFAULT_CLIENT_ID`, `GOOGLE_DEFAULT_CLIENT_SECRET`.
3. The workflow passes them into `gn gen` (`google_api_key`, `google_default_client_id`, `google_default_client_secret`).

Builds **without** secrets stay key-free exactly as before — sign-in UI is present, sync will not connect. With keys, the full Chromium sign-in/sync stack activates per Google's ToS. Everything else about sign-in/sync plumbing in the Kiwi Reborn base is preserved.

## 📦 Releases & APKs

CI produces **two APKs** — Chromium's build system outputs one APK per ABI (a single "universal" APK with both ABIs is not a supported Chromium configuration):

| Artifact | Devices |
|---|---|
| `kiwi-reborn-arm64-v8a` | modern phones (2016+, the vast majority) — **install this one** |
| `kiwi-reborn-armeabi-v7a` | legacy 32-bit devices |

Runs are manual (`workflow_dispatch`); each artifact keeps 14 days. Requires **Android 6.0+** (Chromium 105 default, API 23). APKs are signed with Chromium's auto-generated debug keystore — installable directly; re-sign with your own release key if you plan to distribute.

## 🛠 Building

**Requirements (local):** Linux, ~250 GB free disk, 32 GB RAM recommended, depot_tools.

### Via GitHub Actions (recommended)

Run the **Build Kiwi Reborn APK** workflow ([Actions → Build Kiwi Reborn APK → Run workflow](https://github.com/shslab-org/kiwi-reborn/actions/workflows/build_and_sign_release_apk.yml)). It:

1. syncs Chromium at the version pinned in `CHROMIUM_VERSION` (105.0.5195.24),
2. overlays this tree on top (the overlay model — see [architecture](#-architecture)),
3. runs `gn gen` + `autoninja chrome_public_apk` **for both ABIs in parallel**,
4. uploads `kiwi-reborn-arm64-v8a` and `kiwi-reborn-armeabi-v7a`.

The checkout step uses the **triggering ref**, so dispatching on any branch builds that branch's code. Full build takes up to ~6 h per ABI (parallel jobs).

### Locally (Linux)

```bash
# 1. depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PWD/depot_tools:$PATH"

# 2. Fetch Chromium at the pinned version (mirror of CHROMIUM_VERSION)
mkdir chromium && cd chromium
gclient config --name=src https://chromium.googlesource.com/chromium/src.git --unmanaged
echo "target_os = ['android']" >> .gclient
gclient sync --no-history --nohooks --revision=src@105.0.5195.24 -D

# 3. Overlay this tree
git clone https://github.com/shslab-org/kiwi-reborn.git kiwi-patches
cd src
for d in base chrome components content extensions net remoting services third_party ui; do
  if [ -d "../kiwi-patches/$d" ]; then
    cp -rf "../kiwi-patches/$d/." "./$d/"
  fi
done
gclient runhooks

# 4. Dependencies + build (arm64-v8a; repeat with target_cpu = "arm" for 32-bit)
./build/install-build-deps.sh --android --no-prompt
gn gen out/arm64 --args='
  target_os = "android"
  target_cpu = "arm64"
  is_debug = false
  is_component_build = false
  is_official_build = false
  ffmpeg_branding = "Chrome"
  proprietary_codecs = true
  android_channel = "stable"
  chrome_pgo_phase = 0
  symbol_level = 0
  blink_symbol_level = 0
'
autoninja -C out/arm64 chrome_public_apk
# → out/arm64/apks/ChromePublic.apk
```

Optional Google keys: append `google_api_key = "..."`, `google_default_client_id = "..."`, `google_default_client_secret = "..."` to the `gn gen` args.

## 🏗 Architecture

This repository is an **overlay tree**: it contains only the files that differ from (or are added to) upstream Chromium at the pinned tag. CI assembles the full tree:

```
 pinned Chromium 105.0.5195.24  (gclient sync)
        +
 this repo's overlay           (cp -rf over the checkout)
        ↓
 gn gen ──► autoninja ──► ChromePublic.apk (per ABI)
```

Consequence: every modified upstream file (e.g. `chrome/browser/BUILD.gn`, `third_party/blink/renderer/core/dom/document.cc`) is a **full replacement** of the pinned tag's file — edits must always be based on the exact `CHROMIUM_VERSION`.

### Where the new code lives

| Path | Role |
|---|---|
| `components/adblock/` | filter engine: ABP parser, hash-indexed matcher, engine-data snapshots, cosmetic rules (C++, unit-testable, no `content/` deps) |
| `chrome/browser/adblock/` | `AdblockURLLoaderThrottle`, `AdblockTabHelper` (isolated-world CSS injection), thread-safe `AdblockService`, JNI bridge |
| `chrome/android/java/…` | Settings UI: Adblock (lists/custom filters/allowlist/stats), Background playback switch |
| `chrome/browser/kiwi/` | `KiwiFlagsService` + `KiwiFlagsBridge` — browser-side feature flags |
| `third_party/blink/…/document.cc`, `…/web_media_player_impl.cc` | background-playback visibility patches |
| `chrome/browser/BUILD.gn`, `chrome/android/chrome_java_sources.gni`, `chrome/android/BUILD.gn` | registration points (C++ sources, Java sources, resources, JNI) |

## 🧭 Development notes

* Java sources go in `kiwi_java_sources` (`chrome/android/chrome_java_sources.gni`), resources in `chrome_java_resources.gni`, JNI classes in `generate_jni("chrome_jni_headers")` inside `chrome/android/BUILD.gn`.
* New C++ integrates in `chrome/browser/BUILD.gn` (`static_library("browser")`) and must keep the `components/adblock` ↔ `chrome/browser/adblock` dependency direction (engine stays `content/`-free).
* Adblock UI strings live in `chrome/android/java/res/values/strings.xml` (keys prefixed `preferences_adblock_`).
* CI checks out the **triggering ref** — feature branches build themselves; the base `kiwi` branch always builds the upstream state.

## 🧪 Testing

* **Static**: GN source/deps registration checks, `kiwi_java_sources`/resources/`generate_jni` registrations, JNI include paths, macro signatures verified against the Chromium 105 tag (`WEB_CONTENTS_USER_DATA_KEY_IMPL(Type)`, `URLLoaderThrottle` interface, `ExecuteJavaScriptInIsolatedWorld`, isolated-world id bounds).
* **CI**: two-ABI matrix build validates compilation of all C++/Java (this is what the badge reflects).
* **On-device manual matrix** (post-build): browsing/tabs/downloads/bookmarks/private browsing; CWS install/enable/disable/persist/restart; ad blocking on test pages, custom filters, allowlist, stats; YouTube + generic HTML5 background play, screen lock, notification & Bluetooth controls, multiple media tabs; light/dark/AMOLED themes; cold/warm start, rotation, low memory, long sessions.

## 📋 Roadmap

- [ ] Rebase the overlay onto a modern Chromium (148+) — see [the honest story](#-why-chromium-105-the-honest-story)
- [ ] Scriptlet (`#$#`) and procedural (`#?#`) filter support
- [ ] True `$redirect=` resource substitution (currently blocks)
- [ ] Per-site visibility-spoof scoping for background playback
- [ ] Release-keystore signing option in CI
- [ ] Sync sign-in validation with user-provided keys

## ⚠️ Known limitations (honest list)

1. **Filter syntax subset**: scriptlet injection (`#$#`) and procedural selectors (`#?#`) are ignored; `csp=` rules are dropped; `redirect=` rules block instead of substituting.
2. **Background playback scope**: the visibility spoof is global while enabled; renderer flag changes apply to newly spawned renderers (relaunch after toggling).
3. **YouTube**: works via the generic Page-Visibility approach; YouTube can change its scripts anytime, and in-video ad blocking depends on the filter lists, not on special-casing YouTube.
4. **Sync**: requires user-provided Google API keys (Google's ToS applies); without keys the sign-in UI is present but sync will not function.
5. **APK signing**: CI APKs use Chromium's debug keystore — fine for personal use; re-sign for distribution.

## 📬 What came from where (transparency)

| Area | Source |
|---|---|
| Chromium 105 engine, extension support, AMOLED/night modes, per-site ads toggle, Kiwi feature set | **raihanbhaii/kiwi-reborn** (preserved) |
| Native adblock engine, network throttle, cosmetic injection, Adblock settings UI | **new in this fork** (Brave architecture as inspiration, own implementation) |
| Universal background playback (blink patches + browser plumbing + UI) | **new in this fork** |
| Optional Google API key plumbing | **new in this fork** (build config only) |
| Two-ABI CI matrix + triggering-ref checkout | **new in this fork** |

> [!NOTE]
> [Relixor/Relixor](https://github.com/Relixor/Relixor) was inspected in depth as part of this fork's development. Its `kiwi` branch tree is byte-identical to the Kiwi Reborn base apart from README/logo assets, and its other branches are the upstream Kiwi Chromium-bump history — i.e. its advertised features are the *inherited Kiwi feature set* (extension support, AMOLED modes, per-site ads toggle), all of which are present and preserved here. There were no new feature implementations to port, so the requested feature gaps were implemented directly.

## 🙏 Credits

* [Chromium](https://www.chromium.org/) — the engine this whole project stands on.
* [Kiwi Browser](https://github.com/kiwibrowser/src.next) (Geometry Ou & contributors) — the original extension-capable Android browser.
* [raihanbhaii/kiwi-reborn](https://github.com/raihanbhaii/kiwi-reborn) — the maintained base this fork builds on.
* [Brave](https://github.com/brave/brave-core) — architectural inspiration for native adblocking.
* EasyList/EasyPrivacy/Fanboy maintainers — the filter lists that power the adblock engine.

---

<p align="center">
  <i>Kiwi Reborn Ultimate — maintained with ❤️ on top of the Kiwi lineage and the Chromium project.</i>
</p>
