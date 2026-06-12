#pragma once

#include <stdint.h>

/** PN532-based RFID/NFC (ported from NullTag-style flows). Install "Adafruit PN532". */
namespace RfidNfc {

/** Probe PN532 on SPI (shared bus). On failure, remounts SD; on success, hands SPI to SD pins until session exit. */
bool begin();

/** True after successful begin(). */
bool hardwareOk();

/** Clear cached clone source (optional). */
void resetCloneBuffer();

/** Clear session-retry flag before entering a feature loop. */
void clearSessionRetry();

/** @return true once if user pressed Ready on a failure result screen. */
bool consumeSessionRetry();

void sessionCardReader();
void sessionClone();
void sessionErase();
void sessionDump();
void sessionDecodeAccess();
void sessionJamReader();
void sessionDisruptEmulate();
void sessionTagDisrupt();

} // namespace RfidNfc
