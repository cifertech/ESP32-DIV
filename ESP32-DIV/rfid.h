#pragma once

#include <stdint.h>

namespace RfidNfc {

bool begin();

bool hardwareOk();

void resetCloneBuffer();

void sessionCardReader();
void sessionClone();
void sessionErase();
void sessionDump();
void sessionDecodeAccess();
void sessionJamReader();
void sessionDisruptEmulate();
void sessionTagDisrupt();

} 
