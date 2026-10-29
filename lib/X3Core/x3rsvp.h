#pragma once
// One-word-at-a-time reading: word splitting and timing. Pure C++17, tested on the host.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace x3r {

constexpr int kMinWpm = 60;
constexpr int kMaxWpm = 600;
constexpr int kWpmStep = 20;

inline int clampWpm(int wpm) { return std::max(kMinWpm, std::min(kMaxWpm, wpm)); }

// Number of UTF-8 characters.
inline size_t charCount(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n++;
  return n;
}

// Time to show one word. The panel needs refreshMs to draw it, so the result is never shorter than that.
inline int delayMs(const std::string& word, int wpm, int refreshMs) {
  double ms = 60000.0 / clampWpm(wpm);
  if (charCount(word) > 8) ms *= 1.3;
  if (!word.empty()) {
    const char last = word.back();
    if (last == '.' || last == '!' || last == '?') ms *= 1.8;
    else if (last == ',' || last == ';' || last == ':') ms *= 1.4;
  }
  return std::max(static_cast<int>(ms), refreshMs + 30);
}

// The fastest speed the panel can follow with the given refresh time.
inline int maxWpmFor(int refreshMs) { return std::min(kMaxWpm, std::max(kMinWpm, 60000 / (refreshMs + 30))); }

// Splits a byte stream fed in chunks into words (ASCII whitespace separates words).
class WordSplitter {
 public:
  void feed(const char* data, size_t n) { buf_.append(data, n); }

  // Returns the next complete word; at end of input also returns the last partial word.
  bool next(std::string& word, bool atEof, size_t* endOffset = nullptr) {
    size_t i = pos_;
    while (i < buf_.size() && isSpace(buf_[i])) i++;
    size_t j = i;
    while (j < buf_.size() && !isSpace(buf_[j])) j++;
    if (i == buf_.size() || (j == buf_.size() && !atEof)) {
      compact(i);
      return false;
    }
    word.assign(buf_, i, j - i);
    pos_ = j;
    if (endOffset) *endOffset = base_ + j;
    if (pos_ > 4096) compact(pos_);
    return true;
  }

  void reset(size_t absoluteOffset) {
    buf_.clear();
    pos_ = 0;
    base_ = absoluteOffset;
  }

 private:
  static bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
  void compact(size_t upTo) {
    buf_.erase(0, upTo);
    base_ += upTo;
    pos_ = pos_ >= upTo ? pos_ - upTo : 0;
  }
  std::string buf_;
  size_t pos_ = 0;
  size_t base_ = 0;
};

}  // namespace x3r
