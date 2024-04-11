// Copyright (c) 2022 ESP-PN532

#include "pn532.h"

#include <Wire.h>

#define PN532_I2C_ADDRESS  (0x48 >> 1)
#define PN532_RDY 0x01
#define PN532_FRAME_HEADER_LEN 4
#define PN532_FRAME_IDENTIFIER_LEN 1
#define PN532_CMD_SAMCONFIGURATION_RESPONSE 0x15
#define PN532_CMD_INAUTOPOLL_RESPONSE 0x61

#define I2C_READ_BUFFSIZE 35 //might increase later

#define  PN532_SCAN_DELAY            1000
#define  PN532_ACK_DELAY              30

#define MAXIMUM_UNRESPONSIVE_TIME  60000UL //after this period the pn532 is considered offline
#define AUTO_REFRESH_CONNECTION         30 //after this number of polls, the connection to the PN532 will be refreshed

std::function<void(String)> onCardDetected = [] (String) {};

bool pn532_listen = false;
bool pn532_listenAck = false;
bool pn532_hasContact = false;

ulong pn532_lastResponse = 0;
uint pn532_pollCount = 0;

void pn532_configure();
void pn532_poll();
void pn532_read();
bool pn532_enabled();

enum class PN532Status {
    ACTIVE,
    NOT_ACTIVE,
    FAILED
};

PN532Status pn532_status = PN532Status::NOT_ACTIVE;

ulong tPoll = 0;
ulong tDelay = 0;

#ifdef PN532_ENABLE_DEBUG
#define PRINT(...) Serial.print(__VA_ARGS__)
#define PRINTF(...) Serial.printf(__VA_ARGS__)
#define PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define PRINT(...)
#define PRINTF(...)
#define PRINTLN(...)
#endif

void pn532_init(std::function<void(String)> onReadIdTag) {
    onCardDetected = onReadIdTag;

    Wire.begin(PN532_SDA, PN532_SCL);

    pn532_configure();

    Serial.println("[PN532] initialized");
}

void pn532_loop() {

    if (millis() - tPoll < tDelay) {
        return;
    }
    tPoll = millis();

    pn532_read();

    if (pn532_listenAck) {
        pn532_listenAck = false; //ack has been read, now wait for the RFID-scanning period to end
        tDelay = PN532_SCAN_DELAY;
        return;
    }

    if (!pn532_enabled()) {
        pn532_status = PN532Status::NOT_ACTIVE;
        tDelay = PN532_SCAN_DELAY;
        return;
    }

    if (pn532_status == PN532Status::ACTIVE && millis() - pn532_lastResponse > MAXIMUM_UNRESPONSIVE_TIME) {
        PRINTLN(F("[rfid] connection to PN532 lost"));
        pn532_status = PN532Status::FAILED;
    }
    
    if (pn532_status != PN532Status::ACTIVE) {
        pn532_configure();
        pn532_listenAck = true;
        tDelay = PN532_ACK_DELAY;
        return;
    }

    if (pn532_pollCount >= AUTO_REFRESH_CONNECTION) {
        pn532_pollCount = 0;
        pn532_configure();
        pn532_listenAck = true;
    } else {
        pn532_pollCount++;
        pn532_poll();
        pn532_listenAck = true;
    }

    tDelay = PN532_ACK_DELAY;
    return;
}

void pn532_configure() {
    uint8_t SAMConfiguration [] = {0x00, 0xFF, 0x05, 0xFB, 
                                   0xD4, 0x14, 0x01, 0x00, 0x00,
                                   0x17};
    Wire.beginTransmission(PN532_I2C_ADDRESS);
    Wire.write(SAMConfiguration, sizeof(SAMConfiguration) / sizeof(*SAMConfiguration));
    Wire.endTransmission();

    pn532_listen = true;
}

void pn532_poll() {
    uint8_t InAutoPoll [] = {0x00, 0xFF, 0x06, 0xFA, 
                             0xD4, 0x60, 0x01, 0x01, 0x10, 0x20,
                             0x9A};
    Wire.beginTransmission(PN532_I2C_ADDRESS);
    Wire.write(InAutoPoll, sizeof(InAutoPoll) / sizeof(*InAutoPoll));
    Wire.endTransmission();

    pn532_listen = true;
}

void pn532_read() {
    if (!pn532_listen)
        return;

    Wire.requestFrom(PN532_I2C_ADDRESS, I2C_READ_BUFFSIZE + 1, 1);
    if (Wire.read() != PN532_RDY) {
        //no msg available
        return;
    }

    uint8_t response [I2C_READ_BUFFSIZE] = {0};
    uint8_t frame_len = 0;
    while (Wire.available() && frame_len < I2C_READ_BUFFSIZE) {
        response[frame_len] = Wire.read();
        frame_len++;
    }
    Wire.flush();

    /*
     * For frame layout, see NXP manual, page 28
     */

    if (frame_len < PN532_FRAME_HEADER_LEN) {
        //frame corrupt
        pn532_listen = false;
        return;
    }

    uint8_t *header = nullptr;
    uint8_t header_offs = 0;
    while (header_offs < frame_len - PN532_FRAME_HEADER_LEN) {
        if (response[header_offs] == 0x00 && response[header_offs + 1] == 0xFF) { //find frame preamble
            header = response + header_offs; //header preamble found
            break;
        }
        header_offs++;
    }

    if (!header) {
        //invalid packet
        pn532_listen = false;
        return;
    }

    //check for ack frame
    if (header[2] == 0x00 && header[3] == 0xFF) {
        //ack detected
        return;
    }

    if (header[2] == 0x00 || header[2] == 0xFF) {
        //ignore frame type
        pn532_listen = false;
        return;
    }

    uint8_t data_len = header[2];
    uint8_t data_lcs = header[3];
    data_lcs += data_len;

    if (data_lcs) {
        //checksum wrong
        pn532_listen = false;
        return;
    }

    if (data_len <= PN532_FRAME_IDENTIFIER_LEN) {
        //ingore frame type
        pn532_listen = false;
        return;
    }

    //frame layout: <header><frame_identifier><data><data_checksum>
    uint8_t *frame_identifier = header + PN532_FRAME_HEADER_LEN;
    uint8_t *data = frame_identifier + PN532_FRAME_IDENTIFIER_LEN;
    uint8_t *data_dcs = frame_identifier + data_len;

    if (data_dcs - response >= frame_len) {
        PRINTLN(F("[rfid] Did not read sufficient bytes. Abort"));
        pn532_listen = false;
        return;
    }

    uint8_t data_checksum = *data_dcs;
    for (uint8_t *i = frame_identifier; i != data_dcs; i++) {
        data_checksum += *i;
    }

    if (data_checksum) {
        //wrong checksum
        pn532_listen = false;
        return;
    }

    if (frame_identifier[0] != 0xD5) {
        //ignore frame type
        pn532_listen = false;
        return;
    }

    uint8_t cmd_code = data[0];

    if (cmd_code == PN532_CMD_SAMCONFIGURATION_RESPONSE) {
        //success
        PRINTLN(F("[rfid] connection to PN532 active"));
        pn532_status = PN532Status::ACTIVE;
        pn532_lastResponse = millis();

    } else if (cmd_code == PN532_CMD_INAUTOPOLL_RESPONSE) {

        /*
         * see NXP manual, page 145
         */

        if (data_len < 3 || data[1] == 0) {
            pn532_hasContact = false;
            pn532_listen = false;
            return;
        }

        if (data_len < 10) {
            pn532_listen = false;
            return;
        }

        uint8_t card_type = data[2];
        uint8_t targetDataLen = data[3];
        uint8_t uidLen = data[8];

        if (data_len - 5 < targetDataLen || targetDataLen - 5 < uidLen) {
            PRINTLN(F("[rfid] INAUTOPOLL format err"));
            pn532_listen = false;
            return;
        }

        pn532_lastResponse = millis(); //successfully read

        if (pn532_hasContact) {
            //valid card already scanned the last time; nothing to do
            pn532_listen = false;
            return;
        }

        String uid = String('\0');
        uid.reserve(2*uidLen);

        for (uint8_t i = 0; i < uidLen; i++) {
            uint8_t uid_i = data[9 + i];
            uint8_t lhw = uid_i / 0x10;
            uint8_t rhw = uid_i % 0x10;
            uid += (char) (lhw <= 9 ? lhw + '0' : lhw%10 + 'a');
            uid += (char) (rhw <= 9 ? rhw + '0' : rhw%10 + 'a');
        }

        uid.toUpperCase();

        PRINT(F("[rfid] found card! type = "));
        PRINT(card_type);
        PRINT(F(", uid = "));
        PRINT(uid);
        PRINTLN(F(" end"));
        (void) card_type;

        onCardDetected(uid);
        pn532_hasContact = true;

    } else {
        PRINT(F("[rfid] unknown response; cmd_code = "));
        PRINTLN(cmd_code);
    }

    pn532_listen = false;
    return;
}

bool pn532_enabled() {
    return true;
}
