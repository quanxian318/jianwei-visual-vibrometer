#include "log.h"

#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
SemaphoreHandle_t g_logMutex = nullptr;
LogLevel g_level = LogLevel::kInfo;

void writeNonBlocking(const char* text, size_t len) {
  if (!Serial) return;  // no host has the port open
  if (static_cast<size_t>(Serial.availableForWrite()) < len) return;
  Serial.write(reinterpret_cast<const uint8_t*>(text), len);
}
}  // namespace

void logBegin() {
  if (g_logMutex == nullptr) g_logMutex = xSemaphoreCreateMutex();
}

void logSetLevel(LogLevel level) { g_level = level; }

LogLevel logGetLevel() { return g_level; }

void logPrintf(LogLevel level, const char* tag, const char* fmt, ...) {
  if (static_cast<uint8_t>(level) > static_cast<uint8_t>(g_level)) return;
  static const char kLetters[] = {'E', 'W', 'I', 'D'};
  char line[256];
  int n = snprintf(line, sizeof(line), "[%10lu] %c %s: ", static_cast<unsigned long>(millis()),
                   kLetters[static_cast<uint8_t>(level)], tag);
  if (n < 0) return;
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  size_t len = static_cast<size_t>(n + m);
  if (len > sizeof(line) - 3) len = sizeof(line) - 3;
  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;
  if (g_logMutex) xSemaphoreTake(g_logMutex, portMAX_DELAY);
  writeNonBlocking(line, len);
  if (g_logMutex) xSemaphoreGive(g_logMutex);
}

void logRaw(const char* text) {
  if (g_logMutex) xSemaphoreTake(g_logMutex, portMAX_DELAY);
  writeNonBlocking(text, strlen(text));
  if (g_logMutex) xSemaphoreGive(g_logMutex);
}
