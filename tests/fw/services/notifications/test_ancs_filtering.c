/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/services/blob_db/ios_notif_pref_db.h>
#include <pbl/services/notifications/alerts_preferences.h>
#include <pbl/services/notifications/ancs/ancs_filtering.h>
#include <pbl/services/timeline/attributes_actions.h>

#include <clar.h>

// Stubs
////////////////////////////////////////////////////////////////
#include <pbl/util/units.h>

#include <stubs_logging.h>
#include <stubs_passert.h>
#include <stubs_pbl_malloc.h>

// Fakes
////////////////////////////////////////////////////////////////

static time_t s_now;
time_t rtc_get_time(void) {
  return s_now;
}

RtcTicks rtc_get_ticks(void) {
  return 0;
}

bool s_performed_store = false;
AttributeList *s_expected_attributes = nullptr;
TimelineItemActionGroup *s_expected_actions = nullptr;

static uint8_t app_id_data[] = {
  0x00,                 // id
  0x04, 0x00,           // length
  'P',  'h',  'i', 'l', // Value
};
static ANCSAttribute *s_app_id_attr = (ANCSAttribute *)&app_id_data;

static uint8_t display_name_data[] = {
  0x00,                                // id
  0x07, 0x00,                          // length
  'A',  'w',  'e', 's', 'o', 'm', 'e', // Value
};
static ANCSAttribute *s_display_name_attr = (ANCSAttribute *)&display_name_data;

static uint8_t title_data[] = {
  0x01,                                                                  // id
  0x0E, 0x00,                                                            // length
  'A',  'p',  'p', 'l', 'e', ' ', 'P', 'a', 'y', ' ', '=', ' ', ':', '(' // Value
};
static ANCSAttribute *s_title_attr = (ANCSAttribute *)&title_data;

static uint8_t cyrillic_title_data[] = {
  0x01,                                           // id
  0x08, 0x00,                                     // length
  0xD0, 0x9C, 0xD0, 0xB0, 0xD0, 0xBC, 0xD0, 0xB0, // "Мама"
};
static ANCSAttribute *s_cyrillic_title_attr = (ANCSAttribute *)&cyrillic_title_data;

typedef union {
  struct pbl_string_list list;
  uint8_t storage[PBL_STRING_LIST_SIZE(4, 32)];
} FilteringRules;

static void prv_set_rules(FilteringRules *rules, const char *data, uint16_t length) {
  rules->list.serialized_byte_length = length;
  memcpy(rules->list.data, data, length);
}

static iOSNotifPrefs prv_prefs_with_rules(FilteringRules *rules, Attribute *storage) {
  *storage = (Attribute){.id = AttributeIdNotificationFilteringRules, .string_list = &rules->list};
  return (iOSNotifPrefs){
    .attr_list = {
      .num_attributes = 1,
      .attributes = storage,
    },
  };
}

static FilteringRules s_empty_filtering_rules;

status_t ios_notif_pref_db_store_prefs(const uint8_t *app_id, int length, AttributeList *attr_list,
                                       TimelineItemActionGroup *action_group) {
  s_performed_store = true;
  if (s_expected_attributes) {
    const int buf_size = 256;
    uint8_t expected_buf[buf_size];
    uint8_t actual_buf[buf_size];
    attributes_actions_serialize_payload(s_expected_attributes, s_expected_actions, expected_buf,
                                         buf_size);
    attributes_actions_serialize_payload(attr_list, action_group, actual_buf, buf_size);
    cl_assert_equal_m(
        expected_buf, actual_buf,
        attributes_actions_get_serialized_payload_size(s_expected_attributes, s_expected_actions));
  }
  return S_SUCCESS;
}

void ios_notif_pref_db_free_prefs(iOSNotifPrefs *prefs) {
  return;
}

static void prv_compare_notif_prefs(iOSNotifPrefs *prefs1, iOSNotifPrefs *prefs2) {
  const int buf_size = 256;
  uint8_t buf1[buf_size];
  uint8_t buf2[buf_size];
  attributes_actions_serialize_payload(&prefs1->attr_list, &prefs1->action_group, buf1, buf_size);
  attributes_actions_serialize_payload(&prefs2->attr_list, &prefs2->action_group, buf2, buf_size);
  cl_assert_equal_m(
      buf1, buf2,
      attributes_actions_get_serialized_payload_size(&prefs2->attr_list, &prefs2->action_group));
}

void test_ancs_filtering__initialize(void) {
  s_now = 1;
  s_performed_store = false;
  s_expected_attributes = nullptr;
  s_expected_actions = nullptr;
  prv_set_rules(&s_empty_filtering_rules, "", 1);
}

void test_ancs_filtering__cleanup(void) {
}

void test_ancs_filtering__record_app_no_action_needed(void) {
  // We have some existing prefs which includes all the defaults
  // We don't need to insert anything
  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 7,
      .attributes = (Attribute[]){
        {.id = AttributeIdTitle, .cstring = "Title"},
        {.id = AttributeIdBody, .cstring = "Body"},
        {.id = AttributeIdAppName, .cstring = "Awesome"},
        {.id = AttributeIdLastUpdated, .uint32 = s_now},
        {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_Always},
        {.id = AttributeIdMuteExpiration, .uint32 = 0},
        {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      },
    },
  };
  iOSNotifPrefs *existing_prefs = &prefs;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(!s_performed_store);
}

void test_ancs_filtering__record_app_no_prefs_yet(void) {
  // No existing prefs yet, we should insert all the defaults
  iOSNotifPrefs *existing_prefs = nullptr;

  AttributeList attr_list = {
    .num_attributes = 5,
    .attributes = (Attribute[]){
      {.id = AttributeIdAppName, .cstring = "Awesome"},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
      {.id = AttributeIdMuteExpiration, .uint32 = 0},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &attr_list;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = attr_list,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__record_app_existing_mute(void) {
  // We already have a mute attribute and 2 unrelated attributes, make sure mute doesn't change
  // and the remaining default attributes are added
  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 3,
      .attributes = (Attribute[]){
        {.id = AttributeIdTitle, .cstring = "Title"},
        {.id = AttributeIdBody, .cstring = "Body"},
        {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_Always},
      },
    },
  };
  iOSNotifPrefs *existing_prefs = &prefs;

  AttributeList expected_attributes = {
    .num_attributes = 7,
    .attributes = (Attribute[]){
      {.id = AttributeIdTitle, .cstring = "Title"},
      {.id = AttributeIdBody, .cstring = "Body"},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_Always},
      {.id = AttributeIdAppName, .cstring = "Awesome"},
      {.id = AttributeIdMuteExpiration, .uint32 = 0},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &expected_attributes;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = expected_attributes,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__record_app_existing_display_name(void) {
  // We got a new app name, make sure it gets updated
  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdAppName, .cstring = "Phil was here"},
      },
    },
  };
  iOSNotifPrefs *existing_prefs = &prefs;

  AttributeList expected_attributes = {
    .num_attributes = 5,
    .attributes = (Attribute[]){
      {.id = AttributeIdAppName, .cstring = "Awesome"},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
      {.id = AttributeIdMuteExpiration, .uint32 = 0},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &expected_attributes;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = expected_attributes,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__record_app_update_timestamp(void) {
  // We already have all the default attributes, but it has been a while since we got our last
  // notification from this source
  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 5,
      .attributes = (Attribute[]){
        {.id = AttributeIdAppName, .cstring = "Awesome"},
        {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
        {.id = AttributeIdMuteExpiration, .uint32 = 0},
        {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
        {.id = AttributeIdLastUpdated, .uint32 = s_now},
      },
    },
  };
  iOSNotifPrefs *existing_prefs = &prefs;

  s_now += PBL_SEC_PER_DAY - 1;
  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(!s_performed_store);

  s_now += 2;
  AttributeList expected_attributes = {
    .num_attributes = 5,
    .attributes = (Attribute[]){
      {.id = AttributeIdAppName, .cstring = "Awesome"},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
      {.id = AttributeIdMuteExpiration, .uint32 = 0},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &expected_attributes;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = expected_attributes,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__should_ignore_because_muted(void) {
  iOSNotifPrefs mute_always = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdMuteDayOfWeek, .uint8 = 0x7F},
      }
    }
  };

  iOSNotifPrefs mute_weekends = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdMuteDayOfWeek, .uint8 = 0x41},
      }
    }
  };

  iOSNotifPrefs mute_weekdays = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdMuteDayOfWeek, .uint8 = 0x3E},
      }
    }
  };

  s_now = 1451606400; // Friday Jan 1, 2016
  cl_assert(ancs_filtering_is_muted(&mute_always));
  cl_assert(!ancs_filtering_is_muted(&mute_weekends));
  cl_assert(ancs_filtering_is_muted(&mute_weekdays));

  s_now += PBL_SEC_PER_DAY; // Saturday Jan 2, 2016
  cl_assert(ancs_filtering_is_muted(&mute_always));
  cl_assert(ancs_filtering_is_muted(&mute_weekends));
  cl_assert(!ancs_filtering_is_muted(&mute_weekdays));

  s_now += PBL_SEC_PER_DAY; // Sunday Jan 3, 2016
  cl_assert(ancs_filtering_is_muted(&mute_always));
  cl_assert(ancs_filtering_is_muted(&mute_weekends));
  cl_assert(!ancs_filtering_is_muted(&mute_weekdays));

  s_now += PBL_SEC_PER_DAY; // Monday Jan 4, 2016
  cl_assert(ancs_filtering_is_muted(&mute_always));
  cl_assert(!ancs_filtering_is_muted(&mute_weekends));
  cl_assert(ancs_filtering_is_muted(&mute_weekdays));
}

void test_ancs_filtering__record_app_existing_mute_expiration(void) {
  // We already have a mute expiration attribute, make sure it doesn't change
  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 4,
      .attributes = (Attribute[]){
        {.id = AttributeIdTitle, .cstring = "Title"},
        {.id = AttributeIdBody, .cstring = "Body"},
        {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
        {.id = AttributeIdMuteExpiration, .uint32 = 12345678},
      },
    },
  };
  iOSNotifPrefs *existing_prefs = &prefs;

  AttributeList expected_attributes = {
    .num_attributes = 7,
    .attributes = (Attribute[]){
      {.id = AttributeIdTitle, .cstring = "Title"},
      {.id = AttributeIdBody, .cstring = "Body"},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
      {.id = AttributeIdMuteExpiration, .uint32 = 12345678},
      {.id = AttributeIdAppName, .cstring = "Awesome"},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &expected_attributes;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, s_display_name_attr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = expected_attributes,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__should_ignore_because_timed_muted(void) {
  iOSNotifPrefs muted = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdMuteExpiration, .uint32 = 2000},
      }
    }
  };

  s_now = 1000;
  cl_assert(ancs_filtering_is_muted(&muted));

  s_now = 2000;
  cl_assert(!ancs_filtering_is_muted(&muted));

  s_now = 3000;
  cl_assert(!ancs_filtering_is_muted(&muted));
}

void test_ancs_filtering__record_app_no_display_name(void) {
  iOSNotifPrefs *existing_prefs = nullptr;

  // No display name so we expect the app name to be the title
  AttributeList expected_attributes = {
    .num_attributes = 5,
    .attributes = (Attribute[]){
      {.id = AttributeIdAppName, .cstring = "Apple Pay = :("},
      {.id = AttributeIdMuteDayOfWeek, .uint8 = MuteBitfield_None},
      {.id = AttributeIdMuteExpiration, .uint32 = 0},
      {.id = AttributeIdNotificationFilteringRules, .string_list = &s_empty_filtering_rules.list},
      {.id = AttributeIdLastUpdated, .uint32 = s_now},
    }
  };
  s_expected_attributes = &expected_attributes;

  ancs_filtering_record_app(&existing_prefs, s_app_id_attr, nullptr, s_title_attr);
  cl_assert(s_performed_store);

  // Make sure the our existing prefs got updated
  iOSNotifPrefs expected_prefs = {
    .attr_list = expected_attributes,
  };
  prv_compare_notif_prefs(existing_prefs, &expected_prefs);
}

void test_ancs_filtering__matches_text_rule_body_case_insensitive(void) {
  static uint8_t body_data[] = {0x03,       // id
                                0x12, 0x00, // length
                                'M',  'e',  'e', 't', 'i', 'n', 'g', ' ', 'i',
                                's',  ' ',  'S', 'P', 'A', 'M', ' ', 'n', 'o'};
  ANCSAttribute *body_attr = (ANCSAttribute *)&body_data;

  FilteringRules filtering_rules;
  // count=1, type=text, field=body, case=insensitive, pattern="spam\0"
  prv_set_rules(&filtering_rules, (const char[]){0x01, 0x00, 0x02, 0x00, 's', 'p', 'a', 'm', '\0'},
                9);

  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdNotificationFilteringRules, .string_list = &filtering_rules.list},
      },
    },
  };

  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, body_attr));
}

void test_ancs_filtering__matches_text_rule_title_case_sensitive(void) {
  FilteringRules filtering_rules;
  // count=1, type=text, field=title, case=sensitive, pattern="Apple\0"
  prv_set_rules(&filtering_rules, (const char[]){0x01, 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e'},
                9);
  filtering_rules.list.data[8] = '\0';

  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdNotificationFilteringRules, .string_list = &filtering_rules.list},
      },
    },
  };

  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__does_not_match_regex_rule(void) {
  FilteringRules filtering_rules;
  // count=1, type=regex, field=any, case=insensitive, pattern=".*\0"
  prv_set_rules(&filtering_rules,
                (const char[]){0x01, 0x01, 0x00, 0x00, '.', '*', '\0', 0x00, 0x00}, 9);
  filtering_rules.list.serialized_byte_length = 7;

  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdNotificationFilteringRules, .string_list = &filtering_rules.list},
      },
    },
  };

  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__matches_text_rule_title_via_subtitle(void) {
  static uint8_t subtitle_data[] = {
    0x02,                                                   // id
    0x0B, 0x00,                                             // length
    'S',  'o',  'm', 'e', ' ', 'S', 'e', 'r', 'v', 'e', 'r' // Value
  };
  ANCSAttribute *subtitle_attr = (ANCSAttribute *)&subtitle_data;

  FilteringRules filtering_rules;
  // count=1, type=text, field=title, case=insensitive, pattern="server\0"
  prv_set_rules(&filtering_rules,
                (const char[]){0x01, 0x00, 0x01, 0x00, 's', 'e', 'r', 'v', 'e', 'r', '\0'}, 11);

  iOSNotifPrefs prefs = {
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdNotificationFilteringRules, .string_list = &filtering_rules.list},
      },
    },
  };

  // "server" is in the subtitle, not the title: a title rule must match it.
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, subtitle_attr, nullptr));
  // A body rule must not consider the subtitle.
  filtering_rules.list.data[2] = 0x02;
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, subtitle_attr, nullptr));
}

void test_ancs_filtering__no_rules_passes(void) {
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&s_empty_filtering_rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__block_mode_non_matching_rule_passes(void) {
  FilteringRules rules;
  // count=1, text, title, insensitive, "google\0"
  prv_set_rules(&rules, (const char[]){0x01, 0x00, 0x01, 0x00, 'g', 'o', 'o', 'g', 'l', 'e', '\0'},
                11);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_matching_title_passes(void) {
  FilteringRules rules;
  // count=2: "@allow\0" (any, insensitive), then text/title/sensitive "Apple\0"
  prv_set_rules(&rules, (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w',
                                       '\0', 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0'},
                20);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_non_matching_title_is_ignored(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/insensitive "google\0"
  prv_set_rules(&rules, (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                                       0x00, 0x01, 0x00, 'g',  'o', 'o', 'g', 'l', 'e', '\0'},
                21);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_marker_only_ignores_everything(void) {
  FilteringRules rules;
  // count=1: "@allow\0"
  prv_set_rules(&rules, (const char[]){0x01, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0'},
                11);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_tolerates_case_spaces_field_and_flags(void) {
  FilteringRules rules;
  // count=2: " @ALLOW \0" stored as body + case-sensitive (both ignored for a marker),
  // then text/title/insensitive "google\0" which does not match the title
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x02, 0x01, ' ', '@', 'A', 'L', 'L', 'O', 'W', ' ',
                               '\0', 0x00, 0x01, 0x00, 'g', 'o', 'o', 'g', 'l', 'e', '\0'},
                23);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  // Without the marker this list would pass; the marker turns it into an empty-match allow-list.
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_after_other_rules_still_enables_allow_mode(void) {
  FilteringRules rules;
  // count=2: text/title/sensitive "Apple\0" (matches), then "@allow\0"
  prv_set_rules(&rules, (const char[]){0x02, 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0',
                                       0x00, 0x00, 0x00, '@',  'a', 'l', 'l', 'o', 'w', '\0'},
                20);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  // In block mode "Apple" would be ignored; the trailing marker flips it to allow mode => passes.
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_regex_rule_never_matches(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then regex/any/insensitive ".*\0"
  prv_set_rules(
      &rules,
      (const char[]){
        0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0', 0x01, 0x00, 0x00, '.', '*', '\0'
      },
      17);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_empty_pattern_matches_everything(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/insensitive "\0" (empty pattern)
  prv_set_rules(&rules,
                (const char[]){
                  0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0', 0x00, 0x01, 0x00, '\0'
                },
                15);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__truncated_rules_without_marker_pass(void) {
  FilteringRules rules;
  // count=1, text/title/insensitive, pattern "goo" with NO terminator
  prv_set_rules(&rules, (const char[]){0x01, 0x00, 0x01, 0x00, 'g', 'o', 'o'}, 7);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_null_title_is_ignored_not_crashing(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/sensitive "Apple\0"
  prv_set_rules(&rules, (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w',
                                       '\0', 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0'},
                20);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, nullptr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_cyrillic_exact_case_only(void) {
  FilteringRules exact_rules;
  // count=2: "@allow\0", then text/title/insensitive "Мама\0" (D0 9C D0 B0 D0 BC D0 B0)
  prv_set_rules(
      &exact_rules,
      (const char[]){0x02,       0x00,       0x00,       0x00,       '@',        'a',
                     'l',        'l',        'o',        'w',        '\0',       0x00,
                     0x01,       0x00,       (char)0xD0, (char)0x9C, (char)0xD0, (char)0xB0,
                     (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0, '\0'},
      23);
  Attribute attr_exact;
  iOSNotifPrefs exact_prefs = prv_prefs_with_rules(&exact_rules, &attr_exact);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&exact_prefs, s_cyrillic_title_attr, nullptr,
                                                   nullptr));

  FilteringRules lower_rules;
  // Same but pattern "мама\0" (D0 BC D0 B0 D0 BC D0 B0): ASCII-only folding => no match
  prv_set_rules(
      &lower_rules,
      (const char[]){0x02,       0x00,       0x00,       0x00,       '@',        'a',
                     'l',        'l',        'o',        'w',        '\0',       0x00,
                     0x01,       0x00,       (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0,
                     (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0, '\0'},
      23);
  Attribute attr_lower;
  iOSNotifPrefs lower_prefs = prv_prefs_with_rules(&lower_rules, &attr_lower);
  cl_assert(
      ancs_filtering_should_ignore_by_rules(&lower_prefs, s_cyrillic_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_then_truncated_rule_stays_allow_mode(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/insensitive "Apple" with NO terminator
  prv_set_rules(&rules,
                (const char[]){
                  0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0', 0x00, 0x01, 0x00, 'A',
                  'p', 'p', 'l', 'e'
                },
                19);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__truncated_rule_before_marker_is_block_mode(void) {
  FilteringRules rules;
  // count=2: text/title/sensitive "Apple" with NO terminator; the marker is unreachable
  prv_set_rules(&rules, (const char[]){0x02, 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e'}, 9);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_tolerates_tab_and_newline(void) {
  FilteringRules rules;
  // count=1: "\t@allow\n\0" (any, insensitive)
  prv_set_rules(
      &rules,
      (const char[]){0x01, 0x00, 0x00, 0x00, '\t', '@', 'a', 'l', 'l', 'o', 'w', '\n', '\0'}, 13);
  Attribute attr;
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules, &attr);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}
