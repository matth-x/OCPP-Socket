// matth-x/OCPP-Socket
// Copyright Matthias Akstaller 2024
// GPL-3.0 License

#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti WiFiMulti;
#elif defined(ESP32)
#include <WiFi.h>
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

#include <CSE7766.h>
#include "pn532.h"

#include <MicroOcpp.h>

#include <MicroOcpp/Core/Context.h> //for showing connectiviy info on LED

/*
 * Network configuration
 */

#define STASSID "YOUR_WIFI_SSID"
#define STAPSK  "YOUR_WIFI_PW"

#define OCPP_BACKEND_URL   "ws://echo.websocket.events/"  //e.g. "ws://192.168.178.100:8180/steve/websocket/CentralSystemService"
#define OCPP_CHARGE_BOX_ID "sonoff_powr3"                 //OCPP identifier of charge point
#define OCPP_AUTH_KEY      ""                             //WebSocket Basic Auth token (can probably remain blank)

/*
 * Hardware configuration
 */

#define BOARD_SONOFF_POW_R2  0
#define BOARD_SONOFF_POW_R3  1
#define BOARD_NODEMCU        2

#ifndef USE_BOARD
#define USE_BOARD BOARD_SONOFF_POW_R3
#endif

//Enable CSE7766 if Sonoff powered by live wire (CAUTION - never plug USB and power inlet at the same time!)
#ifndef ENABLE_CSE7766
#define ENABLE_CSE7766 0
#endif

//Enable if PN532 attached via I2C
#ifndef ENABLE_PN532
#define ENABLE_PN532 0
#endif

#if USE_BOARD == BOARD_SONOFF_POW_R2 || USE_BOARD == BOARD_SONOFF_POW_R3
#define LED 13
#define LED_ON LOW
#define LED_OFF HIGH
#define RELAY 12
#if USE_BOARD == BOARD_SONOFF_POW_R2
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#elif USE_BOARD == BOARD_SONOFF_POW_R3
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#endif
#define RESET_BTN 0
#define RESET_BTN_DOWN LOW

#elif USE_BOARD == BOARD_NODEMCU
#define LED 2
#define LED_ON LOW
#define LED_OFF HIGH
#define RELAY 16
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#define RESET_BTN 10
#define RESET_BTN_DOWN LOW
#endif

/*
 * Main program
 */

void processRfidCard(const char *uuid); //process an RFID card (executed by the RFID driver)

CSE7766 cse7766;

float g_trackSmartChargingCurrent = 32.f;
bool g_relayEnabled = false;

void setup() {

    pinMode(LED, OUTPUT);
    pinMode(RELAY, OUTPUT);
    pinMode(RESET_BTN, INPUT);
    digitalWrite(RELAY, RELAY_OFF);

    /*
     * Initialize Serial and WiFi
     */ 
#if ENABLE_CSE7766
    cse7766.begin(); //this initializes the Serial port with 4800 baud
#else
    Serial.begin(115200);
    Serial.print(F("[main] Start in debugging mode (CSE7766 disabled)\n")); //set `ENABLE_CSE7766=1` for productive mode
#endif

#if ENABLE_PN532
    pn532_init(processRfidCard);
#endif //ENABLE_PN532

    //fast LED flash to show that the device boots
    for (uint8_t t = 4; t > 0; t--) {
        digitalWrite(LED, LED_ON);
        delay(100);
        digitalWrite(LED, LED_OFF);
        delay(300);
    }

    Serial.print(F("[main] Wait for WiFi: "));

#if defined(ESP8266)
    WiFiMulti.addAP(STASSID, STAPSK);
    while (WiFiMulti.run() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);

        //show that we're still waiting for the WiFi signal
        digitalWrite(LED, LED_ON);
        delay(100);
        digitalWrite(LED, LED_OFF);
    }
#elif defined(ESP32)
    WiFi.begin(STASSID, STAPSK);
    while (!WiFi.isConnected()) {
        Serial.print('.');
        delay(1000);

        //show that we're still waiting for the WiFi signal
        digitalWrite(LED, LED_ON);
        delay(100);
        digitalWrite(LED, LED_OFF);
    }
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

    Serial.println(F(" connected!"));

    //long LED flash to show that the WiFi connection has been successful
    digitalWrite(LED, LED_ON);
    delay(1500);
    digitalWrite(LED, LED_OFF);

    /*
     * Initialize the OCPP library
     */
    mocpp_initialize(
            OCPP_BACKEND_URL,
            OCPP_CHARGE_BOX_ID,
            "OCPP Socket",
            "My company name",
            MicroOcpp::FilesystemOpt::Use_Mount_FormatOnFail,
            OCPP_AUTH_KEY);

    /*
     * Add OCPP hardware bindings
     */
    setEvseReadyInput([]() {
        //report if the relay is closed and the socket actually allows charging
        return g_relayEnabled;
    });

    setEnergyMeterInput([]() {
        //take the energy register of the main electricity meter and return the value in watt-hours
        return cse7766.getEnergy() * (1.f / 3600.f); //convert Ws into Wh
    });

    setPowerMeterInput([]() {
        //take the energy register of the main electricity meter and return the value in watt-hours
        return cse7766.getActivePower(); //in watts
    });

    addMeterValueInput([]() {
            //take the energy register of the main electricity meter and return the value in watt-hours
            return cse7766.getVoltage(); //in volts
        },
        "Voltage",
        "V");

    addMeterValueInput([]() {
            //take the energy register of the main electricity meter and return the value in watt-hours
            return cse7766.getCurrent();
        },
        "Current.Import",
        "A");

    addMeterValueInput([]() {
            //ESP8266 free heap - useful troubleshooting info for OCPP server
            return ESP.getFreeHeap();
        },
        "Device.FreeHeap");

    setSmartChargingCurrentOutput([] (float limit) {
        //set the SAE J1772 Control Pilot value here. limit is the maximum charge rate in amps. If limit is negative, then no limit is defined
        g_trackSmartChargingCurrent = limit >= 0.f ? limit : 32.f;
    });

    Serial.println(F("[main] Initialization completed"));

    //... see MicroOcpp.h for more settings
}

void loop() {

    /*
     * Do all OCPP stuff (process WebSocket input, send recorded meter values to Central System, etc.)
     */
    mocpp_loop();

    cse7766.handle();

#if ENABLE_PN532
    pn532_loop();
#endif //ENABLE_PN532

    /*
     * Energize EV plug if OCPP transaction is up and running
     */
    if (ocppPermitsCharge()) {
        //OCPP set up and transaction running. Energize the EV plug here

        //Smart Charging: sufficient available current?
        if (g_trackSmartChargingCurrent >= 10.f) {
            //Yes, can charge
            digitalWrite(RELAY, RELAY_ON);
            g_relayEnabled = true;
        } else {
            //No, Smart Charging suspends energy flow
            digitalWrite(RELAY, RELAY_OFF);
            g_relayEnabled = false;
        }
    } else {
        //No transaction running at the moment. De-energize EV plug
        digitalWrite(RELAY, RELAY_OFF);
        g_relayEnabled = false;
    }

    /*
     * Connectivity info - flash LED when message received over WebSocket
     */
    auto& connection = getOcppContext()->getConnection(); //OCPP connection, i.e. WebSocket interface
    static unsigned long ledFlashTime = 0;
    static unsigned long trackLastRecv = connection.getLastRecv(); //timestamp of last incoming message. If this timestamp changed, then we flash the LED
    if (connection.getLastRecv() != trackLastRecv) {
        trackLastRecv = connection.getLastRecv();
        ledFlashTime = millis(); //set flash time to now. After 100ms, the LED will go off
    }

    if (millis() - ledFlashTime <= 100) {
        //during 100ms flash period
        digitalWrite(LED, LED_ON);
    } else {
        digitalWrite(LED, LED_OFF);
    }
}

//Called by the RFID driver when the user swipes a card
void processRfidCard(const char *idTag) {
    Serial.printf("[main] Card detected: %s\n", idTag);

    //check if OCPP lib is initialized
    if (!getOcppContext()) {
        return;
    }

    if (getTransactionIdTag()) {
        //OCPP lib still has idTag (i.e. transaction running or authorization pending) --> swiping card again invalidates idTag
        endTransaction(idTag);
    } else {
        //OCPP lib has no idTag --> swiped card is used for new transaction
        beginTransaction(idTag);
    }
}
