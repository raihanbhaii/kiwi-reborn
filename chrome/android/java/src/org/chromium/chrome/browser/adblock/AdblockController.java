// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.adblock;

import android.content.Context;
import android.content.SharedPreferences;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.library_loader.LibraryLoader;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Owns adblock preferences, filter-list caching / downloading and pushes the
 * current configuration into the native engine. All public methods are safe
 * to call from the UI thread; heavy work happens on a background executor.
 */
public class AdblockController {
    private static final String TAG = "KiwiAdblock";

    private static final String PREF_PREFIX = "kiwi_adblock_";
    private static final String PREF_ENABLED = PREF_PREFIX + "enabled";
    private static final String PREF_CUSTOM_FILTERS = PREF_PREFIX + "custom_filters";
    private static final String PREF_ALLOWLIST = PREF_PREFIX + "allowlist";
    private static final String PREF_CUSTOM_URL = PREF_PREFIX + "custom_url";
    private static final String LIST_ENABLED_SUFFIX = "_enabled";
    private static final String LIST_UPDATED_SUFFIX = "_updated";

    /** Re-check remote lists at most once a day. */
    private static final long UPDATE_INTERVAL_MS = 24 * 60 * 60 * 1000L;

    /** Built-in filter lists (id must be stable: it keys the native side). */
    public static final int LIST_EASYLIST = 0;
    public static final int LIST_EASYPRIVACY = 1;
    public static final int LIST_FANBOY_ANNOYANCE = 2;
    public static final int LIST_EASYLIST_COOKIES = 3;
    public static final int LIST_CUSTOM_URL = 100;

    private static final int CONNECT_TIMEOUT_MS = 10_000;
    private static final int READ_TIMEOUT_MS = 20_000;

    private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean mInitialized = new AtomicBoolean();
    private final AtomicBoolean mUpdateInFlight = new AtomicBoolean();

    private static class Holder {
        static final AdblockController INSTANCE = new AdblockController();
    }

    public static AdblockController get() {
        return Holder.INSTANCE;
    }

    private AdblockController() {}

    private SharedPreferences prefs() {
        return ContextUtils.getAppSharedPreferences();
    }

    public boolean isEnabled() {
        return prefs().getBoolean(PREF_ENABLED, true);
    }

    public void setEnabled(boolean enabled) {
        prefs().edit().putBoolean(PREF_ENABLED, enabled).apply();
        pushStateToNative();
    }

    public boolean isListEnabled(int listId) {
        return prefs().getBoolean(PREF_PREFIX + listId + LIST_ENABLED_SUFFIX,
                listId == LIST_EASYLIST || listId == LIST_EASYPRIVACY);
    }

    public void setListEnabled(int listId, boolean enabled) {
        prefs().edit().putBoolean(PREF_PREFIX + listId + LIST_ENABLED_SUFFIX, enabled).apply();
        if (enabled) {
            startUpdateIfStale();
        } else {
            mExecutor.execute(() -> {
                AdblockBridgeJni.get().setListContent(listId, "");
                AdblockBridgeJni.get().rebuild();
            });
        }
    }

    public long getListUpdatedMs(int listId) {
        return prefs().getLong(PREF_PREFIX + listId + LIST_UPDATED_SUFFIX, 0L);
    }

    public String getCustomFilters() {
        return prefs().getString(PREF_CUSTOM_FILTERS, "");
    }

    public void setCustomFilters(String filters) {
        prefs().edit().putString(PREF_CUSTOM_FILTERS, filters == null ? "" : filters).apply();
        pushStateToNative();
    }

    public String getAllowlist() {
        return prefs().getString(PREF_ALLOWLIST, "");
    }

    public void setAllowlist(String allowlist) {
        prefs().edit().putString(PREF_ALLOWLIST, allowlist == null ? "" : allowlist).apply();
        pushStateToNative();
    }

    public String getCustomListUrl() {
        return prefs().getString(PREF_CUSTOM_URL, "");
    }

    public void setCustomListUrl(String url) {
        prefs().edit().putString(PREF_CUSTOM_URL, url == null ? "" : url.trim()).apply();
        if (isListEnabled(LIST_CUSTOM_URL)) {
            startUpdateIfStale(true);
        } else {
            setListEnabled(LIST_CUSTOM_URL, true);
        }
    }

    /** Idempotent; safe to call repeatedly (deferred startup, settings). */
    public void ensureInitialized() {
        if (!mInitialized.compareAndSet(false, true)) return;
        pushStateToNative();
        startUpdateIfStale();
    }

    /** Pushes prefs + cached list contents into the native engine. */
    private void pushStateToNative() {
        if (!LibraryLoader.getInstance().isInitialized()) return;
        mExecutor.execute(() -> {
            try {
                AdblockBridgeJni.get().setEnabled(isEnabled());
                AdblockBridgeJni.get().setCustomFilters(getCustomFilters());
                AdblockBridgeJni.get().setAllowlistContent(getAllowlist());
                for (int id : new int[] {LIST_EASYLIST, LIST_EASYPRIVACY,
                             LIST_FANBOY_ANNOYANCE, LIST_EASYLIST_COOKIES,
                             LIST_CUSTOM_URL}) {
                    AdblockBridgeJni.get().setListContent(
                            id, isListEnabled(id) ? readCache(id) : "");
                }
                AdblockBridgeJni.get().rebuild();
            } catch (Exception e) {
                Log.w(TAG, "pushStateToNative failed: %s", e.getMessage());
            }
        });
    }

    /** Downloads enabled lists whose cache is missing or older than a day. */
    public void startUpdateIfStale() {
        startUpdateIfStale(false);
    }

    public void startUpdateIfStale(boolean force) {
        if (!mUpdateInFlight.compareAndSet(false, true)) return;
        mExecutor.execute(() -> {
            try {
                if (!LibraryLoader.getInstance().isInitialized()) return;
                long now = System.currentTimeMillis();
                boolean anyChanged = false;
                for (int id : new int[] {LIST_EASYLIST, LIST_EASYPRIVACY,
                             LIST_FANBOY_ANNOYANCE, LIST_EASYLIST_COOKIES,
                             LIST_CUSTOM_URL}) {
                    if (!isListEnabled(id)) continue;
                    String url = listUrl(id);
                    if (url.isEmpty()) continue;
                    if (!force && getListUpdatedMs(id) > 0
                            && now - getListUpdatedMs(id) < UPDATE_INTERVAL_MS
                            && !readCache(id).isEmpty()) {
                        continue;
                    }
                    try {
                        String content = download(url);
                        if (!content.isEmpty()) {
                            writeCache(id, content);
                            prefs().edit()
                                    .putLong(PREF_PREFIX + id + LIST_UPDATED_SUFFIX, now)
                                    .apply();
                            AdblockBridgeJni.get().setListContent(id, content);
                            anyChanged = true;
                        }
                    } catch (Exception e) {
                        Log.w(TAG, "list %d update failed: %s", id, e.getMessage());
                    }
                }
                if (anyChanged) {
                    AdblockBridgeJni.get().rebuild();
                }
            } finally {
                mUpdateInFlight.set(false);
            }
        });
    }

    private String listUrl(int id) {
        switch (id) {
            case LIST_EASYLIST:
                return "https://easylist.to/easylist/easylist.txt";
            case LIST_EASYPRIVACY:
                return "https://easylist.to/easylist/easyprivacy.txt";
            case LIST_FANBOY_ANNOYANCE:
                return "https://easylist.to/easylist/fanboy-annoyance.txt";
            case LIST_EASYLIST_COOKIES:
                return "https://easylist.to/easylist/easylist_cookie.txt";
            case LIST_CUSTOM_URL:
                return getCustomListUrl();
            default:
                return "";
        }
    }

    private static String download(String url) throws Exception {
        HttpURLConnection connection =
                (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setRequestProperty("Accept-Encoding", "gzip");
        connection.setInstanceFollowRedirects(true);
        int responseCode = connection.getResponseCode();
        if (responseCode != HttpURLConnection.HTTP_OK) {
            throw new Exception("HTTP " + responseCode);
        }
        StringBuilder builder = new StringBuilder(1 << 20);
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                     connection.getInputStream(), StandardCharsets.UTF_8))) {
            char[] buffer = new char[16 * 1024];
            int read;
            while ((read = reader.read(buffer)) != -1) {
                builder.append(buffer, 0, read);
                if (builder.length() > 32 * 1024 * 1024) {
                    throw new Exception("filter list too large");
                }
            }
        } finally {
            connection.disconnect();
        }
        return builder.toString();
    }

    private File cacheFile(int listId) {
        Context context = ContextUtils.getApplicationContext();
        File dir = new File(context.getFilesDir(), "kiwi_adblock");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, "list_" + listId + ".txt");
    }

    private String readCache(int listId) {
        File file = cacheFile(listId);
        if (!file.exists()) return "";
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] data = new byte[(int) file.length()];
            int pos = 0, read;
            while (pos < data.length
                    && (read = in.read(data, pos, data.length - pos)) != -1) {
                pos += read;
            }
            return new String(data, 0, pos, StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "";
        }
    }

    private void writeCache(int listId, String content) {
        File file = cacheFile(listId);
        File tmp = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(content.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
            if (!tmp.renameTo(file)) {
                // Fall back to a direct write if the atomic rename fails.
                try (FileOutputStream direct = new FileOutputStream(file)) {
                    direct.write(content.getBytes(StandardCharsets.UTF_8));
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "cache write failed: %s", e.getMessage());
        }
    }
}
