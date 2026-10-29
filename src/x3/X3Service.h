#pragma once
#include <cstdint>
#include <string>

#include "x3core.h"

// Gateway sync for the launcher: Wi-Fi join, GET /api/pack, POST /api/sync.
// All files live on the SD card: /config.json, /wifi.json, /x3/pack.json, /x3/queue.jsonl.
namespace x3svc {

enum class Result : uint8_t { Ok, NoConfig, LowBattery, NoWifi, NoMemory, ServerError };

// Loads the cached pack and event queue once per boot. Safe to call repeatedly.
void begin();

const x3::Pack& pack();
bool hasConfig();
const std::string& serverUrl();

// Blocking: joins a known network, fetches the pack, uploads queued events,
// then drops Wi-Fi again if this call raised it.
Result syncNow();
Result lastResult();
// Seconds since boot of the last finished attempt (0 = none yet).
uint32_t lastAttemptSec();
bool syncDue(uint32_t intervalSec);

// Event types accepted by the gateway: note, progress, card, task_done.
// payloadJson must be a JSON object.
void pushEvent(const char* type, const std::string& payloadJson);
size_t queued();

// Firmware update over the air from the gateway (/api/firmware/meta, /api/firmware).
// The image is accepted only when its SHA-256 matches a meta record signed with the
// gateway token (HMAC-SHA256), and it also passes the normal image checks.
enum class FwResult : uint8_t {
  Updated,  // flashed; caller restarts
  UpToDate,
  NoConfig,
  LowBattery,
  NoWifi,
  NoMemory,
  ServerError,
  BadSignature,
  BadHash,
  BadImage,
  FlashFail
};
// Blocking, takes a minute or two. Does not restart.
FwResult updateFirmware();
const char* fwResultText(FwResult r);

// Text for "updated HH:MM" style labels; falls back to "нет данных".
std::string freshnessLabel();
const char* resultText(Result r);

}  // namespace x3svc
