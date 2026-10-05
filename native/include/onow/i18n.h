// Localisation. The web client's i18n.ts resolves `t("home.deck.jumpBackIn")`
// through a locale bundle with `{{placeholder}}` interpolation and `_plural`
// siblings. This is the same lookup with the same rules:
//
//   * `en` is the base bundle; every other locale only carries overrides.
//   * A missing key falls back to English, then to the key itself.
//   * `t("library.gameCount", {{"count", 3}})` picks `library.gameCount_plural`
//     when count != 1 and the plural sibling exists.
#pragma once

#include <string>
#include <vector>

namespace onow {

namespace i18n_data {
struct LocaleTable;
}

class I18n {
public:
  static I18n& instance();

  // Two-letter locale code, e.g. "en". Falls back to "en" when unknown.
  const std::string& locale() const { return locale_; }
  void set_locale(const std::string& code);
  const std::vector<std::string>& available_locales() const { return available_; }
  const std::string& display_name(const std::string& code) const;

  // Translates `key`, interpolating `{{name}}` placeholders from `params`.
  std::string t(const std::string& key) const;
  std::string t(const std::string& key, const std::vector<std::pair<std::string, std::string>>& params) const;
  // Count-aware form: picks the `_plural` sibling when count != 1.
  std::string t_count(const std::string& key, long long count) const;
  std::string t_count(const std::string& key, long long count,
                      const std::vector<std::pair<std::string, std::string>>& params) const;

  // Number of keys resolved from the active locale's own table (diagnostics).
  int active_entries() const { return active_entries_; }

private:
  I18n();
  const char* lookup(const std::string& key) const;
  static std::string interpolate(const char* template_text,
                                 const std::vector<std::pair<std::string, std::string>>& params);

  std::string locale_ = "en";
  std::vector<std::string> available_;
  const i18n_data::LocaleTable* active_ = nullptr;
  int active_entries_ = 0;
};

// Free-function shorthand.
std::string tr(const std::string& key);
std::string tr(const std::string& key, const std::vector<std::pair<std::string, std::string>>& params);
std::string tr_count(const std::string& key, long long count);

} // namespace onow
