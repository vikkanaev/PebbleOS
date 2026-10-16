# iOS allow-list (`@allow` marker) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** В прошивке PebbleOS правило-маркер `@allow` переключает iOS filter rules приложения из блок-листа в белый список, чтобы на Pebble Time 2 с iPhone приходили только звонки и Telegram-сообщения от выбранных людей.

**Architecture:** Вся логика в одном модуле прошивки `fw/services/notifications/ancs/ancs_filtering.c`: разбор сериализованных правил ищет маркер и матчит остальные правила; новая публичная функция `ancs_filtering_should_ignore_by_rules()` отвечает «отбросить или нет». Единственный вызывающий код `ancs_notifications.c` переключается на неё. Формат правил, BlobDB, протокол и мобильное приложение не меняются.

**Tech Stack:** C (PebbleOS, CMake, `pbl` CLI), clar unit tests (`pbl test`), toolchain PebbleOS-SDK, плата `obelix` (Pebble Time 2), sideload `.pbz` через iOS-приложение Pebble.

**Spec:** `docs/superpowers/specs/2026-10-17-ios-allowlist-design.md` (копируется в форк в Task 1).

## Global Constraints

- Репозиторий: форк `coredevices/PebbleOS` под `vikkanaev`, ветка `ios-allowlist` от тега `v4.41.2`.
- Рабочая копия: `~/projects/Peble/PebbleOS`. Перед любой командой `pbl` активировать venv: `source ~/projects/Peble/PebbleOS/.venv/bin/activate`.
- Маркер: pattern после обрезки пробелов по краям равен `@allow` без учёта регистра (только ASCII-фолдинг). Поле и флаг регистра маркера игнорируются.
- Семантика (из спеки, раздел 3.2): нет правил → пропустить; правила без маркера → отбросить, если совпало хотя бы одно; маркер + правила → пропустить, если совпало хотя бы одно не-маркерное, иначе отбросить; только маркер → отбросить всё.
- Матчинг содержимого не меняется: байтовый UTF-8 `contains`, ASCII-only case folding, Title-правила проверяют title и subtitle, regex-правила никогда не совпадают.
- Не трогать: формат сериализации правил, `ios_notif_pref_db`, `ancs_phone_call.c`, Quiet Time, Mute, мобильное приложение.
- Стиль: clang-format для C, комментарии короткие, логи DBG для рутинных событий (AGENTS.md репозитория). Коммиты с DCO: `git commit -s`. Каждый коммит заканчивается строками:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01Y11EmBbQNcgaGn3gLoBBcv
  ```
- Тесты: `pbl test -R ancs_filtering` должен быть зелёным после каждой задачи; `pbl test -R ancs` перед финальной сборкой.

## Review Focus

1. Пустой pattern (`""`) в allow-режиме: `prv_match_contains` с пустой иглой возвращает true, то есть пустое правило пропускает всё. Это согласуется с блок-режимом (пустое правило блокирует всё), поведение пиним тестом в Task 2.
2. Повреждённый список правил без нуль-терминатора: цикл прерывается на первом битом правиле; в allow-режиме маркер учитывается, только если успел распарситься раньше обрыва. Пиним тестом в Task 2: битый список без маркера пропускает, как сейчас.
3. Уведомление без title (`title == nullptr`) при allow-режиме с Title-правилами должно отбрасываться, а не падать. Пиним тестом в Task 2.
4. Маркер не первым в списке (после обычных правил) обязан включать allow-режим, значит цикл не должен прерываться на первом совпадении. Пиним тестом в Task 2.
5. Кириллица: `Мама` совпадает с `Мама`, но `мама` не совпадает с `Мама` даже при case-insensitive, потому что фолдинг только ASCII. Пользователь должен знать, что имена пишутся как в Контактах. Пиним тестом в Task 2.

---

### Task 1: Форк, окружение, базовый прогон тестов, документы в форке

**Files:**
- Create: `~/projects/Peble/PebbleOS/` (клон форка)
- Create: `~/projects/Peble/PebbleOS/docs/superpowers/specs/2026-10-17-ios-allowlist-design.md` (копия)
- Create: `~/projects/Peble/PebbleOS/docs/superpowers/plans/2026-10-17-ios-allowlist.md` (копия)

**Interfaces:**
- Consumes: ничего.
- Produces: рабочая копия с venv, тулчейном и зелёным `pbl test -R ancs_filtering`; ветка `ios-allowlist`.

- [ ] **Step 1: Форк и клон от тега v4.41.2**

```bash
cd ~/projects/Peble
gh repo fork coredevices/PebbleOS --clone=true --remote=true --default-branch-only -- PebbleOS
cd PebbleOS
git fetch upstream --tags
git checkout -b ios-allowlist v4.41.2
git submodule update --init --recursive
```

Ожидаемо: `git status` показывает ветку `ios-allowlist`, `git remote -v` содержит `origin` (vikkanaev/PebbleOS) и `upstream` (coredevices/PebbleOS).

- [ ] **Step 2: Системные зависимости macOS**

```bash
brew install python git openocd
brew link --overwrite python@3 || true
brew install $(cat requirements-brew.txt)
```

Ожидаемо: завершается без ошибок. Если пакет уже стоит, brew так и напишет, это нормально.

- [ ] **Step 3: Python-окружение и тулчейн**

```bash
cd ~/projects/Peble/PebbleOS
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
curl -LsSf https://github.com/coredevices/PebbleOS-SDK/releases/latest/download/pebbleos-sdk-installer.sh | sh
pbl --help | head -5
```

Ожидаемо: `pbl --help` печатает список команд (configure, build, bundle, test, qemu). Если installer просит перезапустить shell или добавить путь в PATH, сделать это и повторить `pbl --help`.

- [ ] **Step 4: Базовый прогон тестов фильтра**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl test -R ancs_filtering --no-images
```

Ожидаемо: сборка `build-test/` и `100% tests passed` для `test_ancs_filtering`. Если падает на окружении (не на коде), починить окружение прежде чем идти дальше, это блокер для всех следующих задач.

- [ ] **Step 5: Скопировать спеку и план в форк и закоммитить**

```bash
cd ~/projects/Peble/PebbleOS
mkdir -p docs/superpowers/specs docs/superpowers/plans
cp ~/projects/Peble/docs/superpowers/specs/2026-10-17-ios-allowlist-design.md docs/superpowers/specs/
cp ~/projects/Peble/docs/superpowers/plans/2026-10-17-ios-allowlist.md docs/superpowers/plans/
git add docs/superpowers
git commit -s -m "docs: add iOS allow-list design spec and plan

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Y11EmBbQNcgaGn3gLoBBcv"
```

---

### Task 2: Allow-маркер в `ancs_filtering.c` (TDD)

**Files:**
- Modify: `fw/services/notifications/ancs/ancs_filtering.c` (функции `prv_match_rule`, `ancs_filtering_matches_rules`, строки ~89-175)
- Modify: `include/pbl/services/notifications/ancs/ancs_filtering.h`
- Test: `tests/fw/services/notifications/test_ancs_filtering.c`

**Interfaces:**
- Consumes: существующие `prv_ascii_to_lower`, `prv_match_contains`, `prv_attr_to_cstring`, `prv_match_rule`, типы `FilteringRulesSerialized`, `FilteringRuleSerialized`, `iOSNotifPrefs`, `ANCSAttribute`, `attribute_get_string_list`, `AttributeIdNotificationFilteringRules`.
- Produces: `bool ancs_filtering_should_ignore_by_rules(const iOSNotifPrefs *app_notif_prefs, const ANCSAttribute *title_attr, const ANCSAttribute *subtitle_attr, const ANCSAttribute *body_attr);` возвращает true, если уведомление надо отбросить. Функция `ancs_filtering_matches_rules` удаляется из публичного API (Task 3 переключает единственного вызывающего).

Формат сериализованных правил (напоминание для тестов): `count:u8`, затем на каждое правило `match_type:u8 (0 text, 1 regex)`, `match_field:u8 (0 any, 1 title, 2 body)`, `case_sensitive:u8`, `pattern` с завершающим `\0`.

- [ ] **Step 1: Переписать существующие три теста на новую функцию и добавить новые тесты**

В `tests/fw/services/notifications/test_ancs_filtering.c` увеличить буфер правил (строки 54-57):

```c
typedef union {
  struct pbl_string_list list;
  uint8_t storage[PBL_STRING_LIST_SIZE(4, 32)];
} FilteringRules;
```

Добавить после `s_title_attr` (строка ~52) кириллический title `Мама` (UTF-8: D0 9C D0 B0 D0 BC D0 B0):

```c
static uint8_t cyrillic_title_data[] = {
  0x01,                                           // id
  0x08, 0x00,                                     // length
  0xD0, 0x9C, 0xD0, 0xB0, 0xD0, 0xBC, 0xD0, 0xB0, // "Мама"
};
static ANCSAttribute *s_cyrillic_title_attr = (ANCSAttribute *)&cyrillic_title_data;
```

Добавить helper после `prv_set_rules`:

```c
static iOSNotifPrefs prv_prefs_with_rules(FilteringRules *rules) {
  return (iOSNotifPrefs){
    .attr_list = {
      .num_attributes = 1,
      .attributes = (Attribute[]){
        {.id = AttributeIdNotificationFilteringRules, .string_list = &rules->list},
      },
    },
  };
}
```

В трёх существующих тестах (`matches_text_rule_body_case_insensitive`, `matches_text_rule_title_case_sensitive`, `does_not_match_regex_rule`) заменить `ancs_filtering_matches_rules(` на `ancs_filtering_should_ignore_by_rules(`. Ожидания не меняются: первые два `cl_assert(...)`, третий `cl_assert(!...)`.

Добавить в конец файла новые тесты:

```c
void test_ancs_filtering__no_rules_passes(void) {
  iOSNotifPrefs prefs = prv_prefs_with_rules(&s_empty_filtering_rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__block_mode_non_matching_rule_passes(void) {
  FilteringRules rules;
  // count=1, text, title, insensitive, "google\0"
  prv_set_rules(&rules,
                (const char[]){0x01, 0x00, 0x01, 0x00, 'g', 'o', 'o', 'g', 'l', 'e', '\0'}, 11);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_matching_title_passes(void) {
  FilteringRules rules;
  // count=2: "@allow\0" (any, insensitive), then text/title/sensitive "Apple\0"
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0'},
                20);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_non_matching_title_is_ignored(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/insensitive "google\0"
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x00, 'g', 'o', 'o', 'g', 'l', 'e', '\0'},
                21);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_marker_only_ignores_everything(void) {
  FilteringRules rules;
  // count=1: "@allow\0"
  prv_set_rules(&rules, (const char[]){0x01, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0'},
                11);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_tolerates_case_spaces_field_and_flags(void) {
  FilteringRules rules;
  // count=2: " @ALLOW \0" stored as body + case-sensitive (both ignored for a marker),
  // then text/title/insensitive "google\0" which does not match the title
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x02, 0x01, ' ', '@', 'A', 'L', 'L', 'O', 'W', ' ', '\0',
                               0x00, 0x01, 0x00, 'g', 'o', 'o', 'g', 'l', 'e', '\0'},
                23);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  // Without the marker this list would pass; the marker turns it into an empty-match allow-list.
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_marker_after_other_rules_still_enables_allow_mode(void) {
  FilteringRules rules;
  // count=2: text/title/sensitive "Apple\0" (matches), then "@allow\0"
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0',
                               0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0'},
                20);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  // In block mode "Apple" would be ignored; the trailing marker flips it to allow mode => passes.
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_regex_rule_never_matches(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then regex/any/insensitive ".*\0"
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x01, 0x00, 0x00, '.', '*', '\0'},
                17);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_empty_pattern_matches_everything(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/insensitive "\0" (empty pattern)
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x00, '\0'},
                15);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__truncated_rules_without_marker_pass(void) {
  FilteringRules rules;
  // count=1, text/title/insensitive, pattern "goo" with NO terminator
  prv_set_rules(&rules, (const char[]){0x01, 0x00, 0x01, 0x00, 'g', 'o', 'o'}, 7);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&prefs, s_title_attr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_null_title_is_ignored_not_crashing(void) {
  FilteringRules rules;
  // count=2: "@allow\0", then text/title/sensitive "Apple\0"
  prv_set_rules(&rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x01, 'A', 'p', 'p', 'l', 'e', '\0'},
                20);
  iOSNotifPrefs prefs = prv_prefs_with_rules(&rules);
  cl_assert(ancs_filtering_should_ignore_by_rules(&prefs, nullptr, nullptr, nullptr));
}

void test_ancs_filtering__allow_mode_cyrillic_exact_case_only(void) {
  FilteringRules exact_rules;
  // count=2: "@allow\0", then text/title/insensitive "Мама\0" (D0 9C D0 B0 D0 BC D0 B0)
  prv_set_rules(&exact_rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x00, (char)0xD0, (char)0x9C, (char)0xD0, (char)0xB0,
                               (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0, '\0'},
                23);
  iOSNotifPrefs exact_prefs = prv_prefs_with_rules(&exact_rules);
  cl_assert(!ancs_filtering_should_ignore_by_rules(&exact_prefs, s_cyrillic_title_attr, nullptr,
                                                   nullptr));

  FilteringRules lower_rules;
  // Same but pattern "мама\0" (D0 BC D0 B0 D0 BC D0 B0): ASCII-only folding => no match
  prv_set_rules(&lower_rules,
                (const char[]){0x02, 0x00, 0x00, 0x00, '@', 'a', 'l', 'l', 'o', 'w', '\0',
                               0x00, 0x01, 0x00, (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0,
                               (char)0xD0, (char)0xBC, (char)0xD0, (char)0xB0, '\0'},
                23);
  iOSNotifPrefs lower_prefs = prv_prefs_with_rules(&lower_rules);
  cl_assert(ancs_filtering_should_ignore_by_rules(&lower_prefs, s_cyrillic_title_attr, nullptr,
                                                  nullptr));
}
```

- [ ] **Step 2: Убедиться, что тесты не собираются (функции ещё нет)**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl test -R ancs_filtering --no-images
```

Ожидаемо: ошибка компиляции `implicit declaration of function 'ancs_filtering_should_ignore_by_rules'` (или аналогичная ошибка линковки). Это и есть «красный» этап.

- [ ] **Step 3: Реализация в `ancs_filtering.c`**

Заменить функцию `ancs_filtering_matches_rules` (строки ~111-174) на следующий код. `prv_match_rule`, `prv_attr_to_cstring`, `prv_match_contains`, `prv_ascii_to_lower` остаются как есть.

```c
// A rule whose pattern is "@allow" (trimmed, ASCII case-insensitive) switches the app's
// rules from a block-list to an allow-list. Its field and case flags are ignored.
static const char s_allow_marker[] = "@allow";

static bool prv_is_allow_marker(const char *pattern, size_t pattern_len) {
  while ((pattern_len > 0) && (pattern[0] == ' ')) {
    pattern++;
    pattern_len--;
  }
  while ((pattern_len > 0) && (pattern[pattern_len - 1] == ' ')) {
    pattern_len--;
  }
  const size_t marker_len = sizeof(s_allow_marker) - 1;
  if (pattern_len != marker_len) {
    return false;
  }
  for (size_t i = 0; i < marker_len; i++) {
    if (prv_ascii_to_lower(pattern[i]) != s_allow_marker[i]) {
      return false;
    }
  }
  return true;
}

typedef struct {
  bool has_allow_marker;
  bool matched;
} FilteringRulesResult;

static FilteringRulesResult prv_evaluate_rules(const iOSNotifPrefs *app_notif_prefs,
                                               const ANCSAttribute *title_attr,
                                               const ANCSAttribute *subtitle_attr,
                                               const ANCSAttribute *body_attr) {
  FilteringRulesResult result = {};
  if (!app_notif_prefs) {
    return result;
  }
  struct pbl_string_list *rules =
      attribute_get_string_list(&app_notif_prefs->attr_list, AttributeIdNotificationFilteringRules);
  if (!rules || (rules->serialized_byte_length == 0)) {
    return result;
  }

  size_t title_len = 0;
  char *allocated_title = prv_attr_to_cstring(title_attr, &title_len);
  const char *title = allocated_title ? allocated_title : "";
  size_t subtitle_len = 0;
  char *allocated_subtitle = prv_attr_to_cstring(subtitle_attr, &subtitle_len);
  const char *subtitle = allocated_subtitle ? allocated_subtitle : "";
  size_t body_len = 0;
  char *allocated_body = prv_attr_to_cstring(body_attr, &body_len);
  const char *body = allocated_body ? allocated_body : "";

  const FilteringRulesSerialized *serialized_rules = (const FilteringRulesSerialized *)rules->data;
  size_t remaining = rules->serialized_byte_length - 1;
  const uint8_t *cursor = serialized_rules->data;
  // No early exit: a marker may follow a matching rule.
  for (uint8_t i = 0; i < serialized_rules->count; i++) {
    if (remaining < sizeof(FilteringRuleSerialized)) {
      break;
    }
    const FilteringRuleSerialized *rule = (const FilteringRuleSerialized *)cursor;
    cursor += sizeof(FilteringRuleSerialized);
    remaining -= sizeof(FilteringRuleSerialized);
    const char *pattern = (const char *)cursor;
    const char *terminator = memchr(cursor, '\0', remaining);
    if (!terminator) {
      break;
    }
    const size_t pattern_len = (size_t)(terminator - pattern);
    if (prv_is_allow_marker(pattern, pattern_len)) {
      result.has_allow_marker = true;
    } else if (!result.matched) {
      result.matched =
          prv_match_rule(rule->match_type, rule->match_field, (rule->case_sensitive != 0),
                         pattern, pattern_len, title, title_len, subtitle, subtitle_len, body,
                         body_len);
    }
    cursor = (const uint8_t *)(terminator + 1);
    remaining -= (pattern_len + 1);
  }

  kernel_free(allocated_body);
  kernel_free(allocated_subtitle);
  kernel_free(allocated_title);
  return result;
}

bool ancs_filtering_should_ignore_by_rules(const iOSNotifPrefs *app_notif_prefs,
                                           const ANCSAttribute *title_attr,
                                           const ANCSAttribute *subtitle_attr,
                                           const ANCSAttribute *body_attr) {
  const FilteringRulesResult result =
      prv_evaluate_rules(app_notif_prefs, title_attr, subtitle_attr, body_attr);
  if (result.has_allow_marker) {
    // Allow-list: only notifications matching some rule get through.
    return !result.matched;
  }
  // Block-list: matching a rule drops the notification.
  return result.matched;
}
```

В `include/pbl/services/notifications/ancs/ancs_filtering.h` заменить объявление `ancs_filtering_matches_rules` на:

```c
//! Evaluates the app's iOS notification filter rules.
//! Rules are a block-list unless one of them is the "@allow" marker, in which case the
//! remaining rules form an allow-list. Returns true if the notification should be dropped.
bool ancs_filtering_should_ignore_by_rules(const iOSNotifPrefs *app_notif_prefs,
                                           const ANCSAttribute *title_attr,
                                           const ANCSAttribute *subtitle_attr,
                                           const ANCSAttribute *body_attr);
```

Примечание: после этого шага `ancs_notifications.c` перестанет собираться в прошивку (он ещё зовёт старую функцию). Это чинит Task 3; юнит-тест `test_ancs_filtering` на него не линкуется, поэтому здесь это не мешает. Также проверить `tests/stubs/default/ancs_filtering.c`: если там есть стаб `ancs_filtering_matches_rules`, переименовать его в `ancs_filtering_should_ignore_by_rules` с той же сигнатурой и телом `return false;`.

- [ ] **Step 4: Прогнать тесты фильтра**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl test -R ancs_filtering --no-images
```

Ожидаемо: `100% tests passed`. Если упал `allow_mode_cyrillic_exact_case_only`, проверить, что байты в `cyrillic_title_data` и в правилах совпадают (`Мама` = D0 9C D0 B0 D0 BC D0 B0, `мама` = D0 BC D0 B0 D0 BC D0 B0).

- [ ] **Step 5: clang-format и коммит**

```bash
cd ~/projects/Peble/PebbleOS
clang-format -i fw/services/notifications/ancs/ancs_filtering.c \
  include/pbl/services/notifications/ancs/ancs_filtering.h \
  tests/fw/services/notifications/test_ancs_filtering.c
pbl test -R ancs_filtering --no-images
git add fw/services/notifications/ancs/ancs_filtering.c \
  include/pbl/services/notifications/ancs/ancs_filtering.h \
  tests/fw/services/notifications/test_ancs_filtering.c tests/stubs/default/ancs_filtering.c
git commit -s -m "fw/services/notifications/ancs: add @allow marker for iOS filter rules

A rule whose pattern is \"@allow\" switches an app's iOS notification
filter rules from a block-list to an allow-list: notifications pass only
when another rule matches. Without the marker behaviour is unchanged.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Y11EmBbQNcgaGn3gLoBBcv"
```

Если `git add` ругается, что `tests/stubs/default/ancs_filtering.c` не менялся, убрать его из команды.

---

### Task 3: Подключить новую функцию в `ancs_notifications.c`

**Files:**
- Modify: `fw/services/notifications/ancs/ancs_notifications.c` (функция `prv_should_ignore_notification`, блок с `ancs_filtering_matches_rules`, строки ~246-252)
- Test: `tests/fw/services/notifications/test_ancs_notifications.c` (существующие тесты должны остаться зелёными; новых не добавляем, модуль фильтра там застаблен)

**Interfaces:**
- Consumes: `ancs_filtering_should_ignore_by_rules(prefs, title, subtitle, body)` из Task 2.
- Produces: прошивка, в которой путь уведомлений и путь входящего звонка (он ниже по той же функции) используют allow-режим.

- [ ] **Step 1: Заменить вызов**

В `prv_should_ignore_notification` заменить блок

```c
  if (ancs_filtering_matches_rules(app_notif_prefs, title, subtitle, message)) {
    char app_id_buffer[app_id->length + 1];
    pbl_pstring16_to_cstring(&app_id->pstr, app_id_buffer);

    PBL_LOG_DBG("Ignoring notification from <%s>: Matched filtering rule", app_id_buffer);
    return true;
  }
```

на

```c
  if (ancs_filtering_should_ignore_by_rules(app_notif_prefs, title, subtitle, message)) {
    char app_id_buffer[app_id->length + 1];
    pbl_pstring16_to_cstring(&app_id->pstr, app_id_buffer);

    PBL_LOG_DBG("Ignoring notification from <%s>: Filtering rules", app_id_buffer);
    return true;
  }
```

Отклонение от спеки: одна лог-строка вместо двух раздельных причин. Разделять причины потребовало бы второй публичной функции ради DBG-лога, это не стоит того.

- [ ] **Step 2: Убедиться, что других вызовов старой функции нет**

```bash
cd ~/projects/Peble/PebbleOS
grep -rn "ancs_filtering_matches_rules" fw include tests
```

Ожидаемо: вывод пустой. Если что-то нашлось, заменить на новую функцию с той же семантикой «true = отбросить».

- [ ] **Step 3: Прогнать оба ANCS-набора тестов**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl test -R ancs --no-images
```

Ожидаемо: `100% tests passed` для `test_ancs_filtering`, `test_ancs_notifications`, `test_ancs_notifications_util`.

- [ ] **Step 4: Коммит**

```bash
cd ~/projects/Peble/PebbleOS
clang-format -i fw/services/notifications/ancs/ancs_notifications.c
git add fw/services/notifications/ancs/ancs_notifications.c
git commit -s -m "fw/services/notifications/ancs: use allow-aware filter rules check

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Y11EmBbQNcgaGn3gLoBBcv"
```

---

### Task 4: Сборка прошивки obelix, бандл, проверка на часах

**Files:**
- Create: `build/*.pbz` (артефакт, не коммитится)

**Interfaces:**
- Consumes: ветку `ios-allowlist` с Task 2 и Task 3.
- Produces: `.pbz` для sideload и подтверждённый сценарий на часах.

- [ ] **Step 1: Полный прогон юнит-тестов**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl test --no-images
```

Ожидаемо: `100% tests passed`. Если падают тесты, не связанные с ANCS, проверить, падают ли они и на чистом `v4.41.2` (`git stash; pbl test -R <имя>; git stash pop`); если да, это не наш регресс, зафиксировать в отчёте и идти дальше.

- [ ] **Step 2: Сборка release-прошивки для obelix и бандл**

```bash
cd ~/projects/Peble/PebbleOS && source .venv/bin/activate
pbl configure --board obelix -DCONFIG_RELEASE=y
pbl build
pbl bundle
ls -la build/*.pbz
```

Ожидаемо: один `.pbz` в `build/`. Если `pbl bundle` требует другой путь или флаг, смотреть `pbl bundle --help` и `docs/pbl/`.

- [ ] **Step 3: Push ветки в форк**

```bash
cd ~/projects/Peble/PebbleOS
git push -u origin ios-allowlist
```

- [ ] **Step 4: Ручной шаг (Виктор): sideload и проверка**

Это делает человек, сабагент здесь только готовит чеклист и сообщает путь к `.pbz`.

1. Переслать `.pbz` на iPhone (AirDrop или Files).
2. Приложение Pebble: Settings → Show debug options → Devices → часы → Firmware Update Debug → Sideload FW → выбрать файл. Дождаться перезагрузки часов.
3. Pebble → Notifications: всем приложениям, кроме Telegram и Phone, поставить Mute.
4. Telegram → Filter rules: добавить `@allow`, затем правило на имя контакта из списка, поле Title.
5. Phone появится в списке после первого входящего звонка. Добавить `@allow` и те же имена.
6. Проверки:
   - сообщение в Telegram от человека из списка: пришло на часы;
   - сообщение от человека не из списка: на часах тишина;
   - звонок с контакта из списка: часы звонят и показывают имя;
   - звонок не из списка: часы молчат;
   - удалить `@allow` у Telegram: правила снова работают как блок-лист (сообщение от человека из списка перестаёт приходить, остальные приходят).
7. Откат при проблемах: Back+Up+Select переводит часы в PRF, из приложения ставится стоковая прошивка.

- [ ] **Step 5: Зафиксировать результат**

Записать в `~/projects/Peble/Handoff.md` (создать, если нет): версия upstream-тега, SHA коммитов ветки, путь к `.pbz`, результаты проверок из Step 4 и открытые вопросы.
