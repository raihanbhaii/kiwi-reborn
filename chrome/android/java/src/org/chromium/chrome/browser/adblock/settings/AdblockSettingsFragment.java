// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.adblock.settings;

import android.app.Activity;
import android.content.DialogInterface;
import android.text.InputType;
import android.widget.EditText;

import androidx.appcompat.app.AlertDialog;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.adblock.AdblockBridgeJni;
import org.chromium.chrome.browser.adblock.AdblockController;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.SettingsUtils;

/**
 * Kiwi Reborn Ultimate — adblock settings: engine toggle, per-list toggles,
 * custom filters, site allowlist and live block statistics.
 */
public class AdblockSettingsFragment extends PreferenceFragmentCompat {
    public static final String PREF_ADBLOCK_SWITCH = "adblock_switch";
    public static final String PREF_LIST_PREFIX = "adblock_list_";
    public static final String PREF_CUSTOM_FILTERS = "adblock_custom_filters";
    public static final String PREF_ALLOWLIST = "adblock_allowlist";
    public static final String PREF_CUSTOM_URL = "adblock_custom_url";
    public static final String PREF_STATS = "adblock_stats";

    private static final int[] LIST_IDS = new int[] {
            AdblockController.LIST_EASYLIST,
            AdblockController.LIST_EASYPRIVACY,
            AdblockController.LIST_FANBOY_ANNOYANCE,
            AdblockController.LIST_EASYLIST_COOKIES,
            AdblockController.LIST_CUSTOM_URL};

    @Override
    public void onCreatePreferences(android.os.Bundle savedInstanceState, String rootKey) {
        getActivity().setTitle(R.string.preferences_adblock);
        SettingsUtils.addPreferencesFromResource(this, R.xml.adblock_preferences);

        ChromeSwitchPreference master =
                (ChromeSwitchPreference) findPreference(PREF_ADBLOCK_SWITCH);
        master.setChecked(AdblockController.get().isEnabled());
        master.setOnPreferenceChangeListener((preference, newValue) -> {
            AdblockController.get().setEnabled((Boolean) newValue);
            refreshListsAndStats();
            return true;
        });

        Preference customFilters = findPreference(PREF_CUSTOM_FILTERS);
        customFilters.setSummary(AdblockController.get().getCustomFilters());
        customFilters.setOnPreferenceClickListener(preference -> {
            showTextDialog(R.string.preferences_adblock_custom_filters_title,
                    AdblockController.get().getCustomFilters(),
                    text -> {
                        AdblockController.get().setCustomFilters(text);
                        customFilters.setSummary(text);
                    });
            return true;
        });

        Preference allowlist = findPreference(PREF_ALLOWLIST);
        allowlist.setSummary(AdblockController.get().getAllowlist());
        allowlist.setOnPreferenceClickListener(preference -> {
            showTextDialog(R.string.preferences_adblock_allowlist_title,
                    AdblockController.get().getAllowlist(),
                    text -> {
                        AdblockController.get().setAllowlist(text);
                        allowlist.setSummary(text);
                    });
            return true;
        });

        Preference customUrl = findPreference(PREF_CUSTOM_URL);
        customUrl.setSummary(AdblockController.get().getCustomListUrl());
        customUrl.setOnPreferenceClickListener(preference -> {
            showTextDialog(R.string.preferences_adblock_custom_url_title,
                    AdblockController.get().getCustomListUrl(),
                    text -> {
                        AdblockController.get().setCustomListUrl(text);
                        customUrl.setSummary(text);
                    });
            return true;
        });

        Preference updateNow = findPreference("adblock_update_now");
        updateNow.setOnPreferenceClickListener(preference -> {
            AdblockController.get().startUpdateIfStale(true);
            return true;
        });
    }

    @Override
    public void onResume() {
        super.onResume();
        refreshListsAndStats();
    }

    private void refreshListsAndStats() {
        boolean engineEnabled = AdblockController.get().isEnabled();
        for (int id : LIST_IDS) {
            ChromeSwitchPreference listPref = (ChromeSwitchPreference) findPreference(
                    PREF_LIST_PREFIX + id);
            if (listPref == null) continue;
            listPref.setEnabled(engineEnabled);
            listPref.setChecked(AdblockController.get().isListEnabled(id));
            listPref.setOnPreferenceChangeListener((preference, newValue) -> {
                AdblockController.get().setListEnabled(id, (Boolean) newValue);
                return true;
            });
            if (id == AdblockController.LIST_CUSTOM_URL) {
                String url = AdblockController.get().getCustomListUrl();
                listPref.setEnabled(engineEnabled && !url.isEmpty());
            }
        }

        Preference stats = findPreference(PREF_STATS);
        if (stats != null) {
            try {
                long blocked = AdblockBridgeJni.get().getBlockedCount();
                long network = AdblockBridgeJni.get().getNetworkRuleCount();
                long cosmetic = AdblockBridgeJni.get().getCosmeticRuleCount();
                stats.setSummary(getString(R.string.preferences_adblock_stats_summary,
                        blocked, network, cosmetic));
            } catch (Exception e) {
                // Native not ready yet; leave the default summary.
            }
        }
    }

    private interface TextCallback { void onText(String value); }

    private void showTextDialog(int titleRes, String initial, TextCallback callback) {
        Activity activity = getActivity();
        if (activity == null) return;
        EditText input = new EditText(activity);
        input.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        input.setMinLines(3);
        input.setText(initial);
        new AlertDialog.Builder(activity)
                .setTitle(titleRes)
                .setView(input)
                .setPositiveButton(android.R.string.ok,
                        (DialogInterface dialog, int which) -> callback.onText(
                                input.getText().toString().trim()))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }
}
