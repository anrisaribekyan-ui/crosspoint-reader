#pragma once
// Ядро лаунчера X3: только стандартный C++17, без Arduino. Проверяется на хосте.
// Содержит: время, модель плиток главного экрана, навигацию кнопками, очередь отправки.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace x3 {

constexpr int kW = 528;
constexpr int kH = 792;

enum class Screen : uint8_t {
  Hub,
  Reader,
  Sleep,
  English,
  Library,
  Apps,
  Settings,
  Wifi,
  Prayer,
  Bible,
  Notes,
  Calendar,
  QR,
  Focus,
  Update,
  Offline
};
enum class Key : uint8_t { Back, Ok, OkLong, Left, Right };
enum class Act : uint8_t { None, Redraw, Sync, GoSleep };

// ---------- данные пакета ----------
struct Pack {
  bool valid = false;
  int64_t updatedUnix = 0;
  std::string weekday, dateText, fast;
  std::string readTitle, readAuthor;
  int readChapter = 1, readPct = 0;
  int englishDue = 0;
  std::string wordW, wordTr, wordIpa, wordPos;
  int todayDone = 0, todayTotal = 0;
  std::vector<std::pair<std::string, bool>> todayItems;
  std::string verseRef, verseText;
  int wbOrders = 0;
  std::string vpn = "unknown";  // online | offline | unknown
};

// ---------- время ----------
inline int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

inline int64_t daysFromCivil(int64_t y, int m, int d) {
  y -= (m <= 2);
  int64_t era = floorDiv(y, 400);
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline void civilFromUnix(int64_t unixSec, int offsetSec, int& y, int& m, int& d, int& hh, int& mm) {
  int64_t t = unixSec + offsetSec;
  int64_t days = floorDiv(t, 86400);
  int64_t sod = t - days * 86400;
  hh = static_cast<int>(sod / 3600);
  mm = static_cast<int>((sod % 3600) / 60);
  int64_t z = days + 719468;
  int64_t era = floorDiv(z, 146097);
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yy + (m <= 2));
}

// "2026-10-28T10:00:00+03:00" -> unix. 0 при ошибке.
inline int64_t parseIso(const std::string& s) {
  int y, mo, d, h, mi, se;
  if (s.size() < 19) return 0;
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return 0;
  int64_t t = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
  // хвост: Z | +HH:MM | -HH:MM (после секунд могут идти доли)
  size_t i = 19;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
  }
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    int oh = 0, om = 0;
    if (std::sscanf(s.c_str() + i + 1, "%2d:%2d", &oh, &om) < 1) return 0;
    int off = oh * 3600 + om * 60;
    t += (s[i] == '+') ? -off : off;
  }
  return t;
}

inline std::string staleLabel(int64_t now, int64_t updated, int offsetSec) {
  if (updated <= 0) return "нет данных";
  int y1, m1, d1, h1, i1, y2, m2, d2, h2, i2;
  civilFromUnix(now, offsetSec, y1, m1, d1, h1, i1);
  civilFromUnix(updated, offsetSec, y2, m2, d2, h2, i2);
  int64_t dayDiff = daysFromCivil(y1, m1, d1) - daysFromCivil(y2, m2, d2);
  char buf[48];
  if (dayDiff == 0)
    std::snprintf(buf, sizeof buf, "обновлено %02d:%02d", h2, i2);
  else if (dayDiff == 1)
    std::snprintf(buf, sizeof buf, "вчера %02d:%02d", h2, i2);
  else
    std::snprintf(buf, sizeof buf, "%02d.%02d %02d:%02d", d2, m2, h2, i2);
  return buf;
}

inline bool isStale(int64_t now, int64_t updated, int64_t maxAgeSec = 3 * 3600) {
  return updated <= 0 || now - updated > maxAgeSec;
}

// ---------- текст ----------
inline std::string truncCp(const std::string& s, size_t maxCp) {
  size_t cp = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
      if (cp == maxCp) return s.substr(0, i) + "…";
      ++cp;
    }
  }
  return s;
}

// ---------- плитки главного экрана ----------
struct Tile {
  std::string tag, big, sub;
  bool bigIsText = false;
  Screen target = Screen::Hub;
};

constexpr int kHubTiles = 6;

inline std::vector<Tile> hubTiles(const Pack& p) {
  std::vector<Tile> t(kHubTiles);
  t[0] = {"[01] ЧТЕНИЕ", std::to_string(p.readPct) + "%",
          truncCp(p.readTitle, 16) + " · гл. " + std::to_string(p.readChapter), false, Screen::Reader};
  t[1] = {"[02] ENGLISH", std::to_string(p.englishDue), "карточек к повтору", false, Screen::English};
  t[2] = {"[03] СЕГОДНЯ", std::to_string(p.todayDone) + "/" + std::to_string(p.todayTotal), "задач из Распорядка",
          false, Screen::Calendar};
  t[3] = {"[04] ВЕРА", "Молитвослов", "стих дня · " + p.verseRef, true, Screen::Prayer};
  t[4] = {"[05] БИБЛИЯ", "Синодальный", "читать · папка /Bible", true, Screen::Bible};
  t[5] = {"[06] ПРИЛОЖЕНИЯ", "Игры · QR", "Заметки · скрипты с SD", true, Screen::Apps};
  return t;
}

// ---------- навигация ----------
inline Screen parentOf(Screen s) {
  switch (s) {
    case Screen::Wifi:
    case Screen::Update:
      return Screen::Settings;
    case Screen::Reader:
      return Screen::Library;
    case Screen::Focus:
    case Screen::QR:
    case Screen::Notes:
      return Screen::Apps;
    default:
      return Screen::Hub;
  }
}

inline int defaultItemCount(Screen s) {
  switch (s) {
    case Screen::Settings:
      return 11;
    case Screen::Apps:
      return 10;
    case Screen::Library:
      return 8;
    case Screen::Wifi:
      return 4;
    case Screen::Prayer:
      return 8;
    case Screen::Bible:
      return 6;
    case Screen::Notes:
      return 6;
    case Screen::QR:
      return 3;
    case Screen::Update:
      return 6;
    default:
      return 0;
  }
}

struct Nav {
  Screen cur = Screen::Hub;
  int sel = 0;   // плитка на главном экране
  int item = 0;  // пункт списка на других экранах
  int itemCount = 0;
  bool offline = false;

  void open(Screen s) {
    cur = s;
    item = 0;
    itemCount = defaultItemCount(s);
  }

  // Что открывает «Ок» на пункте списка (Hub, если ничего).
  Screen okTarget() const {
    switch (cur) {
      case Screen::Settings:
        return item <= 1 ? Screen::Wifi : item == 9 ? Screen::Update : Screen::Hub;
      case Screen::Apps:
        return item == 6 ? Screen::QR : item == 7 ? Screen::Notes : item == 9 ? Screen::Focus : Screen::Hub;
      case Screen::Library:
        return item == 0 ? Screen::Bible : Screen::Reader;
      default:
        return Screen::Hub;
    }
  }

  Act handle(Key k) {
    if (cur == Screen::Hub) {
      switch (k) {
        case Key::Left:
          sel = (sel + kHubTiles - 1) % kHubTiles;
          return Act::Redraw;
        case Key::Right:
          sel = (sel + 1) % kHubTiles;
          return Act::Redraw;
        case Key::Ok: {
          static const Screen targets[kHubTiles] = {Screen::Reader, Screen::English,  Screen::Calendar,
                                                    Screen::Prayer, Screen::Settings, Screen::Apps};
          open(targets[sel]);
          return Act::Redraw;
        }
        case Key::OkLong:
          return Act::Sync;
        case Key::Back:
          cur = Screen::Sleep;
          return Act::GoSleep;
      }
    }
    if (cur == Screen::Sleep) {
      if (k == Key::OkLong) return Act::Sync;
      cur = Screen::Hub;
      return Act::Redraw;
    }
    switch (k) {
      case Key::Back:
        open(parentOf(cur));
        return Act::Redraw;
      case Key::Ok: {
        Screen t = okTarget();
        if (t != Screen::Hub) open(t);
        return Act::Redraw;
      }
      case Key::Left:
        if (itemCount > 0) item = (item + itemCount - 1) % itemCount;
        return Act::Redraw;
      case Key::Right:
        if (itemCount > 0) item = (item + 1) % itemCount;
        return Act::Redraw;
      case Key::OkLong:
        return Act::Sync;
    }
    return Act::None;
  }

  // Итог синхронизации. Экран «Нет сети» только если её запустил сам пользователь.
  Act onSyncResult(bool ok, bool userInitiated) {
    if (ok) {
      offline = false;
      if (cur == Screen::Offline) open(Screen::Hub);
      return Act::Redraw;
    }
    offline = true;
    if (userInitiated && cur != Screen::Sleep) {
      open(Screen::Offline);
      return Act::Redraw;
    }
    return Act::None;
  }
};

// ---------- очередь отправки ----------
inline std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else
          o += c;
    }
  }
  return o;
}

class Queue {
 public:
  static constexpr size_t kMax = 500;      // при переполнении теряются самые старые
  static constexpr size_t kPerSync = 200;  // как на сервере

  // Возвращает id события или "" при неверных данных.
  std::string push(const std::string& type, const std::string& payloadJson, int64_t ts) {
    if (type != "note" && type != "progress" && type != "card" && type != "task_done") return "";
    if (payloadJson.size() < 2 || payloadJson.front() != '{' || payloadJson.back() != '}') return "";
    std::string id = "e" + std::to_string(ts) + "-" + std::to_string(++seq_);
    lines_.push_back("{\"id\":\"" + id + "\",\"type\":\"" + type + "\",\"ts\":" + std::to_string(ts) +
                     ",\"payload\":" + payloadJson + "}");
    while (lines_.size() > kMax) lines_.erase(lines_.begin());
    return id;
  }

  size_t size() const { return lines_.size(); }

  std::vector<std::string> ids(size_t max = kPerSync) const {
    std::vector<std::string> out;
    for (size_t i = 0; i < lines_.size() && i < max; ++i) out.push_back(idOf(lines_[i]));
    return out;
  }

  std::string body(const std::string& device, size_t max = kPerSync) const {
    std::string b = "{\"device\":\"" + jsonEscape(device) + "\",\"queue\":[";
    for (size_t i = 0; i < lines_.size() && i < max; ++i) {
      if (i) b += ',';
      b += lines_[i];
    }
    return b + "]}";
  }

  void ack(const std::vector<std::string>& accepted) {
    for (const auto& id : accepted) {
      for (size_t i = 0; i < lines_.size(); ++i) {
        if (idOf(lines_[i]) == id) {
          lines_.erase(lines_.begin() + static_cast<long>(i));
          break;
        }
      }
    }
  }

  std::string serialize() const {
    std::string s;
    for (const auto& l : lines_) {
      s += l;
      s += '\n';
    }
    return s;
  }

  static Queue parse(const std::string& text) {
    Queue q;
    size_t start = 0;
    while (start < text.size()) {
      size_t nl = text.find('\n', start);
      if (nl == std::string::npos) nl = text.size();
      std::string line = text.substr(start, nl - start);
      const std::string id = idOf(line);
      if (line.size() > 2 && line.front() == '{' && line.back() == '}' && !id.empty()) {
        q.lines_.push_back(line);
        const size_t dash = id.rfind('-');
        if (dash != std::string::npos) {
          q.seq_ = std::max<uint32_t>(q.seq_, static_cast<uint32_t>(std::strtoul(id.c_str() + dash + 1, nullptr, 10)));
        }
      }
      start = nl + 1;
    }
    return q;
  }

 private:
  static std::string idOf(const std::string& line) {
    const std::string key = "\"id\":\"";
    size_t p = line.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    size_t e = line.find('"', p);
    return e == std::string::npos ? "" : line.substr(p, e - p);
  }
  std::vector<std::string> lines_;
  uint32_t seq_ = 0;
};

// Достаёт id из {"accepted":["a","b"],"rejected":[...]}. Отклонённые ридер тоже снимает с очереди
// (иначе неверное событие будет уходить вечно): их id берём из "rejected".
inline std::vector<std::string> parseIdList(const std::string& json, const std::string& arrayKey,
                                            const std::string& idKey = "") {
  std::vector<std::string> out;
  size_t p = json.find("\"" + arrayKey + "\"");
  if (p == std::string::npos) return out;
  p = json.find('[', p);
  if (p == std::string::npos) return out;
  size_t end = json.find(']', p);
  if (end == std::string::npos) return out;
  std::string seg = json.substr(p + 1, end - p - 1);
  if (idKey.empty()) {
    size_t i = 0;
    while ((i = seg.find('"', i)) != std::string::npos) {
      size_t e = seg.find('"', i + 1);
      if (e == std::string::npos) break;
      out.push_back(seg.substr(i + 1, e - i - 1));
      i = e + 1;
    }
  } else {
    const std::string key = "\"" + idKey + "\":\"";
    size_t i = 0;
    while ((i = seg.find(key, i)) != std::string::npos) {
      i += key.size();
      size_t e = seg.find('"', i);
      if (e == std::string::npos) break;
      out.push_back(seg.substr(i, e - i));
      i = e + 1;
    }
  }
  return out;
}

}  // namespace x3
