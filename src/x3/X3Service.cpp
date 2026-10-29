#include "X3Service.h"

#include <ArduinoJson.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>

#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"
#include "network/WifiPowerSaveGuard.h"
#include "x3_pack.h"
#include "x3crypto.h"

namespace x3svc {
namespace {

constexpr const char* kDir = "/x3";
constexpr const char* kPackPath = "/x3/pack.json";
constexpr const char* kQueuePath = "/x3/queue.jsonl";
constexpr size_t kPackMax = 16 * 1024;
constexpr size_t kQueueMax = 64 * 1024;
constexpr size_t kReplyMax = 8 * 1024;
constexpr uint32_t kJoinTimeoutMs = 12000;
constexpr int kMinBattery = 15;
constexpr int kMinBatteryFlash = 40;
constexpr const char* kFwPath = "/x3/firmware.bin";
constexpr const char* kFwShaPath = "/x3/fw.sha";
constexpr size_t kFwMin = 200 * 1024;
constexpr size_t kFwMax = 6553600;

struct Config {
  std::string server, token, device = "x3";
  int utcOffsetMin = 180;
  bool valid() const { return server.size() > 8 && token.size() >= 16; }
};

bool g_begun = false;
Config g_cfg;
x3::Pack g_pack;
x3::Queue g_queue;
Result g_last = Result::NoConfig;
uint32_t g_lastAttemptMs = 0;
bool g_attempted = false;

bool loadConfig() {
  std::string raw;
  if (!Storage.readFileToString("X3", "/config.json", 2048, raw)) return false;
  JsonDocument d;
  if (deserializeJson(d, raw) != DeserializationError::Ok) return false;
  g_cfg.server = d["server"] | "";
  g_cfg.token = d["token"] | "";
  g_cfg.device = d["device"] | "x3";
  g_cfg.utcOffsetMin = d["utc_offset_min"] | 180;
  while (!g_cfg.server.empty() && g_cfg.server.back() == '/') g_cfg.server.pop_back();
  return g_cfg.valid();
}

bool writeAtomic(const char* path, const std::string& data) {
  const std::string tmp = std::string(path) + ".tmp";
  if (!Storage.writeFile(tmp.c_str(), String(data.c_str()))) return false;
  return Storage.replaceFile(tmp.c_str(), path);
}

void saveQueue() { writeAtomic(kQueuePath, g_queue.serialize()); }

// /wifi.json on the SD root: {"networks":[{"ssid":"..","pass":".."}]}. New networks
// are copied into the firmware's own credential store once; known ones are skipped.
void importWifiJson() {
  std::string raw;
  if (!Storage.readFileToString("X3", "/wifi.json", 4096, raw)) return;
  JsonDocument d;
  if (deserializeJson(d, raw) != DeserializationError::Ok) return;
  bool changed = false;
  for (JsonVariantConst n : d["networks"].as<JsonArrayConst>()) {
    const std::string ssid = n["ssid"] | "";
    const std::string pass = n["pass"] | "";
    if (ssid.empty() || WIFI_STORE.hasSavedCredential(ssid)) continue;
    changed |= WIFI_STORE.addCredential(ssid, pass);
  }
  if (changed) WIFI_STORE.saveToFile();
}

// Joins the strongest saved network in range. Returns false when none connects.
bool joinWifi() {
  WIFI_STORE.loadFromFile();
  importWifiJson();
  WiFi.mode(WIFI_STA);
  const int found = WiFi.scanNetworks();
  int best = -1;
  int bestRssi = -1000;
  for (int i = 0; i < found; i++) {
    if (WiFi.RSSI(i) > bestRssi && WIFI_STORE.hasSavedCredential(WiFi.SSID(i).c_str())) {
      best = i;
      bestRssi = WiFi.RSSI(i);
    }
  }
  if (best < 0) {
    WiFi.scanDelete();
    return false;
  }
  const std::string ssid = WiFi.SSID(best).c_str();
  WiFi.scanDelete();
  const auto cred = WIFI_STORE.findCredential(ssid);
  if (!cred) return false;
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  const unsigned long deadline = millis() + kJoinTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

bool fetchPack(std::string& body) {
  HttpDownloader::FetchOptions options;
  options.bearer = g_cfg.token;
  int status = 0;
  options.statusOut = &status;
  body.clear();
  const bool ok = HttpDownloader::fetchUrl(
      g_cfg.server + "/api/pack",
      [&body](const uint8_t* data, size_t len) {
        if (body.size() + len > kPackMax) return false;
        body.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      options);
  return ok && status == 200;
}

void uploadQueue() {
  if (g_queue.size() == 0) return;
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient http;
  if (!http.begin(g_cfg.server + "/api/sync")) return;
  http.setTimeout(20000);
  http.setInsecure();
  http.addHeader("Authorization", "Bearer " + g_cfg.token);
  http.addHeader("Content-Type", "application/json");
  const std::string body = g_queue.body(g_cfg.device);
  std::string reply;
  const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                                      [&reply](const uint8_t* data, size_t len) {
                                        const size_t room = kReplyMax - std::min(reply.size(), kReplyMax);
                                        reply.append(reinterpret_cast<const char*>(data), std::min(len, room));
                                        return true;
                                      });
  if (status != 200) return;
  // Rejected events leave the queue too, otherwise a bad event would be re-sent forever.
  {
    RenderLock lock;
    g_queue.ack(x3::parseIdList(reply, "accepted"));
    g_queue.ack(x3::parseIdList(reply, "rejected", "id"));
  }
  saveQueue();
}

void fwNoProgress(size_t, size_t, void*) {}

// SHA-256 of a file on the SD card, as lowercase hex. Empty on read error.
std::string fileSha256(const char* path) {
  HalFile f;
  if (!Storage.openFileForRead("X3", path, f)) return "";
  x3c::Sha256 sha;
  uint8_t buf[1024];
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) sha.update(buf, static_cast<size_t>(n));
  f.close();
  uint8_t out[32];
  sha.final(out);
  return x3c::toHex(out, 32);
}

}  // namespace

void begin() {
  if (g_begun) return;
  g_begun = true;
  Storage.ensureDirectoryExists(kDir);
  loadConfig();
  std::string raw;
  if (Storage.readFileToString("X3", kPackPath, kPackMax, raw)) x3::parsePack(raw, g_pack);
  if (Storage.readFileToString("X3", kQueuePath, kQueueMax, raw)) g_queue = x3::Queue::parse(raw);
}

const x3::Pack& pack() { return g_pack; }
bool hasConfig() { return g_cfg.valid(); }
const std::string& serverUrl() { return g_cfg.server; }
Result lastResult() { return g_last; }
size_t queued() { return g_queue.size(); }

uint32_t lastAttemptSec() { return g_attempted ? std::max<uint32_t>(1, g_lastAttemptMs / 1000) : 0; }

bool syncDue(uint32_t intervalSec) {
  if (!g_cfg.valid()) return false;
  if (!g_attempted) return true;
  return (millis() - g_lastAttemptMs) / 1000 >= intervalSec;
}

void pushEvent(const char* type, const std::string& payloadJson) {
  begin();
  const time_t now = time(nullptr);
  const int64_t ts = now > 1700000000 ? now : 0;
  {
    RenderLock lock;
    if (g_queue.push(type, payloadJson, ts).empty()) return;
  }
  saveQueue();
}

Result syncNow() {
  begin();
  g_attempted = true;
  g_lastAttemptMs = millis();
  if (!g_cfg.valid()) return g_last = Result::NoConfig;
  if (powerManager.getBatteryPercentage() < kMinBattery) return g_last = Result::LowBattery;

  const bool wasConnected = WiFi.status() == WL_CONNECTED;
  if (!wasConnected && !joinWifi()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return g_last = Result::NoWifi;
  }
  trustedtime::startSync();

  Result result = Result::ServerError;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    result = Result::NoMemory;
  } else {
    std::string body;
    x3::Pack fresh;
    if (fetchPack(body) && x3::parsePack(body, fresh)) {
      {
        RenderLock lock;
        g_pack = std::move(fresh);
      }
      writeAtomic(kPackPath, body);
      result = Result::Ok;
      uploadQueue();
    }
  }

  if (!wasConnected) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  LOG_DBG("X3", "sync result %d", static_cast<int>(result));
  return g_last = result;
}

FwResult updateFirmware() {
  begin();
  if (!g_cfg.valid() || g_cfg.server.compare(0, 8, "https://") != 0) return FwResult::NoConfig;
  if (powerManager.getBatteryPercentage() < kMinBatteryFlash) return FwResult::LowBattery;
  const bool wasConnected = WiFi.status() == WL_CONNECTED;
  if (!wasConnected && !joinWifi()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return FwResult::NoWifi;
  }

  FwResult result = FwResult::ServerError;
  std::string sha;
  size_t size = 0;
  do {
    if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
        ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
      result = FwResult::NoMemory;
      break;
    }
    HttpDownloader::FetchOptions options;
    options.bearer = g_cfg.token;
    int status = 0;
    options.statusOut = &status;
    std::string meta;
    const bool got = HttpDownloader::fetchUrl(
        g_cfg.server + "/api/firmware/meta",
        [&meta](const uint8_t* data, size_t len) {
          if (meta.size() + len > 1024) return false;
          meta.append(reinterpret_cast<const char*>(data), len);
          return true;
        },
        options);
    JsonDocument d;
    if (!got || status != 200 || deserializeJson(d, meta) != DeserializationError::Ok) break;
    sha = d["sha256"] | "";
    size = d["size"] | 0;
    const std::string sig = d["sig"] | "";
    if (sha.size() != 64 || size < kFwMin || size >= kFwMax) break;
    if (!x3c::sameHex(sig, x3c::hmacSha256Hex(g_cfg.token, sha + ":" + std::to_string(size)))) {
      result = FwResult::BadSignature;
      break;
    }
    std::string known;
    Storage.readFileToString("X3", kFwShaPath, 80, known);
    if (known.compare(0, 64, sha) == 0 && known.size() >= 64) {
      result = FwResult::UpToDate;
      break;
    }
    Storage.ensureDirectoryExists(kDir);
    Storage.remove(kFwPath);
    const auto err = HttpDownloader::downloadToFile(g_cfg.server + "/api/firmware", kFwPath, nullptr, nullptr, "", "",
                                                    {{"Authorization", "Bearer " + g_cfg.token}});
    if (err != HttpDownloader::OK) break;
    result = FwResult::Updated;  // downloaded; checked below
  } while (false);

  if (!wasConnected) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  if (result != FwResult::Updated) return result;

  if (fileSha256(kFwPath) != sha) {
    Storage.remove(kFwPath);
    return FwResult::BadHash;
  }
  if (firmware_flash::validateImageFile(kFwPath, 0) != firmware_flash::Result::OK) {
    Storage.remove(kFwPath);
    return FwResult::BadImage;
  }
  const auto flashed = firmware_flash::flashFromSdPath(kFwPath, fwNoProgress, nullptr, true);
  Storage.remove(kFwPath);
  if (flashed != firmware_flash::Result::OK) return FwResult::FlashFail;
  Storage.writeFile(kFwShaPath, String(sha.c_str()));
  return FwResult::Updated;
}

const char* fwResultText(FwResult r) {
  switch (r) {
    case FwResult::Updated:
      return "Прошивка обновлена";
    case FwResult::UpToDate:
      return "Уже последняя версия";
    case FwResult::NoConfig:
      return "Нет config.json";
    case FwResult::LowBattery:
      return "Мало заряда (нужно 40%)";
    case FwResult::NoWifi:
      return "Нет известной сети Wi-Fi";
    case FwResult::NoMemory:
      return "Мало памяти, перезагрузи";
    case FwResult::ServerError:
      return "Сервер не отвечает";
    case FwResult::BadSignature:
      return "Подпись не сошлась";
    case FwResult::BadHash:
      return "Файл повреждён";
    case FwResult::BadImage:
      return "Образ не подходит";
    case FwResult::FlashFail:
      return "Ошибка записи";
  }
  return "";
}

std::string freshnessLabel() {
  if (!g_pack.valid) return "нет данных";
  const time_t now = time(nullptr);
  const int offset = g_cfg.utcOffsetMin * 60;
  if (now < 1700000000) return x3::staleLabel(g_pack.updatedUnix, g_pack.updatedUnix, offset);
  return x3::staleLabel(now, g_pack.updatedUnix, offset);
}

const char* resultText(Result r) {
  switch (r) {
    case Result::Ok:
      return "Обновлено";
    case Result::NoConfig:
      return "Нет config.json на карте";
    case Result::LowBattery:
      return "Мало заряда для синхронизации";
    case Result::NoWifi:
      return "Нет известной сети Wi-Fi";
    case Result::NoMemory:
      return "Не хватает памяти";
    case Result::ServerError:
      return "Сервер не ответил";
  }
  return "";
}

}  // namespace x3svc
