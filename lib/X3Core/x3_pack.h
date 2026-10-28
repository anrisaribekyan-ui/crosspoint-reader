#pragma once
// Разбор пакета /api/pack в x3::Pack. Нужен только ArduinoJson, поэтому собирается и на хосте.
#include <ArduinoJson.h>

#include <string>

#include "x3core.h"

namespace x3 {

inline std::string jstr(JsonVariantConst v, const char* def = "") {
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  return s ? std::string(s) : std::string(def);
}

inline int jint(JsonVariantConst v, int def, int lo, int hi) {
  if (!v.is<long>()) return def;
  long x = v.as<long>();
  return static_cast<int>(x < lo ? lo : x > hi ? hi : x);
}

// true, если JSON разобран и версия пакета поддерживается. При ошибке out не меняется.
inline bool parsePack(const std::string& json, Pack& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  if (jint(doc["v"], 0, 0, 1000) != 1) return false;

  Pack p;
  p.updatedUnix = parseIso(jstr(doc["updated"]));
  p.weekday = jstr(doc["date"]["weekday"]);
  p.dateText = jstr(doc["date"]["text"]);
  p.fast = jstr(doc["date"]["fast"]);

  p.readTitle = jstr(doc["reading"]["title"]);
  p.readAuthor = jstr(doc["reading"]["author"]);
  p.readChapter = jint(doc["reading"]["chapter"], 1, 0, 9999);
  p.readPct = jint(doc["reading"]["pct"], 0, 0, 100);

  p.englishDue = jint(doc["english"]["due"], 0, 0, 99999);
  p.wordW = jstr(doc["english"]["word"]["w"]);
  p.wordTr = jstr(doc["english"]["word"]["tr"]);
  p.wordIpa = jstr(doc["english"]["word"]["ipa"]);
  p.wordPos = jstr(doc["english"]["word"]["pos"]);

  p.todayDone = jint(doc["today"]["done"], 0, 0, 999);
  p.todayTotal = jint(doc["today"]["total"], 0, 0, 999);
  int n = 0;
  for (JsonVariantConst it : doc["today"]["items"].as<JsonArrayConst>()) {
    if (++n > 6) break;
    p.todayItems.emplace_back(jstr(it["t"]), it["done"] | false);
  }

  p.verseRef = jstr(doc["faith"]["verse_ref"]);
  p.verseText = jstr(doc["faith"]["verse_text"]);
  p.wbOrders = jint(doc["services"]["wb_orders"], 0, 0, 999999);
  std::string vpn = jstr(doc["services"]["vpn"], "unknown");
  p.vpn = (vpn == "online" || vpn == "offline") ? vpn : "unknown";

  p.valid = true;
  out = p;
  return true;
}

}  // namespace x3
