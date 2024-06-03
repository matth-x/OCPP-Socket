// Derivate of the PN532 driver of the OpenEVSE project:
// see https://github.com/OpenEVSE/openevse_esp32_firmware
// GNU General Public License (GPL) V3

#ifndef PN532_H
#define PN532_H

#include <functional>

#ifndef PN532_SDA
#define PN532_SDA 5
#endif

#ifndef PN532_CLK
#define PN532_CLK 4
#endif

//uncomment to enable debug output to Serial port
//#define PN532_ENABLE_DEBUG

void pn532_init(std::function<void(const char*)> onReadIdTag);

void pn532_loop();

#endif
