#include "onow/i18n.h"
#include "onow/i18n/TranslationData.h"

#include "onow/Log.h"

#include <algorithm>
#include <cstring>

namespace onow {

namespace i18n_data {
struct LocaleTable;
struct TranslationEntry;
extern const LocaleTable kLocales[];
extern const int kLocaleCount;
} // namespace i18n_data

I18n& I18n::instance() {
  static I18n i18n;
  return i18n;
}

I18n::I18n() {
  for (int i = 0; i < i18n_data::kLocaleCount; ++i) {
    available_.push_back(i18n_data::kLocales[i].code);
  }
  active_ = &i18n_data::kLocales[0];
}

void I18n::set_locale(const std::string& code) {
  for (int i = 0; i < i18n_data::kLocaleCount; ++i) {
    if (code == i18n_data::kLocales[i].code) {
      locale_ = code;
      active_ = &i18n_data::kLocales[i];
      active_entries_ = i18n_data::kLocales[i].entry_count;
      return;
    }
  }
  ONOW_WARN("i18n", "unknown locale '%s'; keeping '%s'", code.c_str(), locale_.c_str());
}

const std::string& I18n::display_name(const std::string& code) const {
  static const std::string kEmpty;
  for (int i = 0; i < i18n_data::kLocaleCount; ++i) {
    if (code == i18n_data::kLocales[i].code) {
      static thread_local std::string cached;
      cached = i18n_data::kLocales[i].display_name;
      return cached;
    }
  }
  return kEmpty;
}

const char* I18n::lookup(const std::string& key) const {
  if (!active_) return nullptr;
  // The tables are sorted by key, so binary search applies.
  const i18n_data::TranslationEntry* entries = active_->entries;
  const int count = active_->entry_count;
  int low = 0;
  int high = count - 1;
  while (low <= high) {
    const int mid = low + (high - low) / 2;
    const int cmp = key.compare(entries[mid].key);
    if (cmp == 0) return entries[mid].value;
    if (cmp < 0) high = mid - 1;
    else low = mid + 1;
  }
  // Fall back to the English base table.
  if (active_->code != std::string("en")) {
    const i18n_data::TranslationEntry* base = i18n_data::kLocales[0].entries;
    const int base_count = i18n_data::kLocales[0].entry_count;
    int lo = 0;
    int hi = base_count - 1;
    while (lo <= hi) {
      const int mid = lo + (hi - lo) / 2;
      const int cmp = key.compare(base[mid].key);
      if (cmp == 0) return base[mid].value;
      if (cmp < 0) hi = mid - 1;
      else lo = mid + 1;
    }
  }
  return nullptr;
}

std::string I18n::interpolate(const char* template_text,
                              const std::vector<std::pair<std::string, std::string>>& params) {
  if (!template_text) return std::string();
  if (params.empty()) return template_text;
  std::string out;
  out.reserve(std::strlen(template_text) + 32);
  const std::string text(template_text);
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t open = text.find("{{", pos);
    if (open == std::string::npos) {
      out.append(text, pos, std::string::npos);
      break;
    }
    out.append(text, pos, open - pos);
    const size_t close = text.find("}}", open + 2);
    if (close == std::string::npos) {
      out.append(text, open, std::string::npos);
      break;
    }
    const std::string name = text.substr(open + 2, close - open - 2);
    bool replaced = false;
    for (const auto& param : params) {
      if (param.first == name) {
        out += param.second;
        replaced = true;
        break;
      }
    }
    if (!replaced) out.append(text, open, close + 2 - open);
    pos = close + 2;
  }
  return out;
}

std::string I18n::t(const std::string& key) const {
  const char* value = lookup(key);
  if (value) return value;
  ONOW_TRACE("i18n", "missing key %s", key.c_str());
  return key;
}

std::string I18n::t(const std::string& key,
                    const std::vector<std::pair<std::string, std::string>>& params) const {
  return interpolate(lookup(key), params);
}

std::string I18n::t_count(const std::string& key, long long count) const {
  return t_count(key, count, {});
}

std::string I18n::t_count(const std::string& key, long long count,
                          const std::vector<std::pair<std::string, std::string>>& params) const {
  std::string effective = key;
  if (count != 1) {
    if (lookup(key + "_plural")) effective = key + "_plural";
  }
  std::vector<std::pair<std::string, std::string>> merged = params;
  merged.emplace_back("count", std::to_string(count));
  return interpolate(lookup(effective), merged);
}

std::string tr(const std::string& key) { return I18n::instance().t(key); }

std::string tr(const std::string& key,
               const std::vector<std::pair<std::string, std::string>>& params) {
  return I18n::instance().t(key, params);
}

std::string tr_count(const std::string& key, long long count) {
  return I18n::instance().t_count(key, count);
}

} // namespace onow
