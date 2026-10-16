/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/services/blob_db/ios_notif_pref_db.h>

#include <comm/ble/kernel_le_client/ancs/ancs_types.h>

/**
 * @defgroup services_notifications_ancs_ancs_filtering ANCS filtering
 * @ingroup services_notifications_ancs
 * @brief Per-app mute schedules and filtering rules for ANCS notifications.
 * @{
 */

/**
 * @brief Record an app in the notification preferences database.
 *
 * Adds or updates the app name, default mute, mute expiration and filtering rules attributes and
 * the last seen timestamp (at most once a day). When anything changes, the entry is written, which
 * syncs it to the phone, and @p app_notif_prefs is replaced with the updated copy.
 *
 * @param[in,out] app_notif_prefs Existing prefs for the app, or NULL. Replaced (and the old copy
 *                                freed) when updated.
 * @param app_id App identifier.
 * @param display_name App display name.
 * @param title Notification title, used as the name when there is no display name (e.g. Apple
 *              Pay).
 */
void ancs_filtering_record_app(iOSNotifPrefs **app_notif_prefs, const ANCSAttribute *app_id,
                               const ANCSAttribute *display_name, const ANCSAttribute *title);

/**
 * @brief Check whether an app is muted now.
 *
 * @param app_notif_prefs Prefs of the app, may be NULL.
 * @return true if today is a muted day of the week or the mute expiration is in the future.
 */
bool ancs_filtering_is_muted(const iOSNotifPrefs *app_notif_prefs);

/**
 * @brief Get the mute days of an app.
 *
 * @param app_notif_prefs Prefs of the app, may be NULL.
 * @return MuteBitfield of muted days, MuteBitfield_None if unset.
 */
uint8_t ancs_filtering_get_mute_type(const iOSNotifPrefs *app_notif_prefs);

//! Evaluates the app's iOS notification filter rules.
//! Rules are a block-list unless one of them is the "@allow" marker, in which case the
//! remaining rules form an allow-list. Returns true if the notification should be dropped.
bool ancs_filtering_should_ignore_by_rules(const iOSNotifPrefs *app_notif_prefs,
                                           const ANCSAttribute *title_attr,
                                           const ANCSAttribute *subtitle_attr,
                                           const ANCSAttribute *body_attr);

/** @} */
