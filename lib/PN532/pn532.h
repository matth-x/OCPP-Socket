// Copyright (c) 2022 ESP-PN532

#ifndef SONOFF_PN532_H
#define SONOFF_PN532_H

#include <Arduino.h>
#include <functional>

#ifndef PN532_SDA
#define PN532_SDA 5
#endif

#ifndef PN532_CLK
#define PN532_CLK 4
#endif

//uncomment to enable debug output to Serial port
//#define PN532_ENABLE_DEBUG

void pn532_init(std::function<void(String)> onReadIdTag);

void pn532_loop();

#endif
