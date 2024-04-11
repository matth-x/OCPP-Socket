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

#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>

#define STASSID "YOUR_WIFI_SSID"
#define STAPSK  "YOUR_WIFI_PW"

#define OCPP_BACKEND_URL   "ws://echo.websocket.events"
#define OCPP_CHARGE_BOX_ID ""
#define OCPP_AUTH_KEY      ""

#define BOARD_SONOFF_POW_R2_PROD 0 // Sonoff powered by live wire (CAUTION - never plug USB and power inlet at the same time!)
#define BOARD_SONOFF_POW_R2_DEV  1 // Sonoff connected to computer via USB
#define BOARD_NODEMCU            2

#ifndef USE_BOARD
#define USE_BOARD BOARD_SONOFF_POW_R2_PROD
#endif

#if USE_BOARD == BOARD_SONOFF_POW_R2_PROD || USE_BOARD == BOARD_SONOFF_POW_R2_DEV
#define LED 13
#define LED_ON LOW
#define LED_OFF HIGH
#define RELAY 12
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#define RESET_BTN 0
#define RESET_BTN_DOWN LOW
#elif USE_BOARD == BOARD_NODEMCU
// WHEN ON NodeMCU, use this
#define LED 2
#define LED_ON LOW
#define LED_OFF HIGH
#define RELAY 16
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#define RESET_BTN 10
#define RESET_BTN_DOWN LOW
#endif

#define PN532_USE

#ifdef PN532_USE
#include "pn532.h"
#endif

//
//  Settings which worked for my SteVe instance:
//
//#define OCPP_BACKEND_URL   "ws://192.168.178.100:8180/steve/websocket/CentralSystemService"
//#define OCPP_CHARGE_BOX_ID "esp-charger"

float g_trackSmartChargingCurrent = 32.f;
bool g_relayEnabled = false;

ulong cse7766_lastEnergyUpdate = 0;
float cse7766_energy_wh = 0.f;

void setup() {

    pinMode(LED, OUTPUT);
    pinMode(RELAY, OUTPUT);
    pinMode(RESET_BTN, INPUT);
    digitalWrite(RELAY, RELAY_OFF);

    /*
     * Initialize Serial and WiFi
     */ 
#if USE_BOARD == BOARD_SONOFF_POW_R2_PROD
    CSE7766_initialize();
#else
    Serial.begin(115200);
#endif

#ifdef PN532_USE
    pn532_init([] (String idTag) {
        Serial.printf("[main] Idtag detected! %s\n", idTag.c_str());
        if (!getOcppContext()) {
            return;
        }

        if (getTransactionIdTag()) {
            endTransaction(idTag.c_str());
        } else {
            beginTransaction(idTag.c_str());
        }
    });
#endif

    for (uint8_t t = 4; t > 0; t--) {
        digitalWrite(LED, LED_ON);
        delay(100);
        digitalWrite(LED, LED_OFF);
        delay(300);
    }

    MicroOcpp::configuration_init(MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Use_Mount_FormatOnFail));

    auto wifiSSID = MicroOcpp::declareConfiguration<const char*>("Cst_WiFiSSID", STASSID, "ws-conn.jsn", false, true);
    auto wifiPASS = MicroOcpp::declareConfiguration<const char*>("Cst_WiFiPASS", STAPSK, "ws-conn.jsn", false, true);

    auto backendUrl = MicroOcpp::declareConfiguration<const char*>("Cst_BackendUrl", OCPP_BACKEND_URL, "ws-conn.jsn", false, true);
    auto cbId = MicroOcpp::declareConfiguration<const char*>("Cst_ChargeBoxId", OCPP_CHARGE_BOX_ID, "ws-conn.jsn", false, true);
    auto authKey = MicroOcpp::declareConfiguration<const char*>("AuthorizationKey", OCPP_AUTH_KEY, "ws-conn.jsn", false, true);

    MicroOcpp::configuration_load("ws-conn.jsn");

    Serial.print(F("[main] Wait for WiFi: "));

#if defined(ESP8266)
    WiFiMulti.addAP(wifiSSID->getString(), wifiPASS->getString());
    //WiFiMulti.addAP(STASSID, STAPSK);
    while (WiFiMulti.run() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
#elif defined(ESP32)
    WiFi.begin(STASSID, STAPSK);
    while (!WiFi.isConnected()) {
        Serial.print('.');
        delay(1000);
    }
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

    Serial.println(F(" connected!"));

    for (uint8_t t = 4; t > 0; t--) {
        digitalWrite(LED, LED_ON);
        delay(100);
        digitalWrite(LED, LED_OFF);
        delay(100);
    }

    /*
     * Initialize the OCPP library
     */
    mocpp_initialize(
            backendUrl->getString(),
            cbId->getString(),
            "My Charging Station",
            "My company name", 
            MicroOcpp::FilesystemOpt::Use_Mount_FormatOnFail,
            authKey->getString());
    /*
     * Integrate OCPP functionality. You can leave out the following part if your EVSE doesn't need it.
     */
    setEvseReadyInput([]() {
        return g_relayEnabled;
    });

    setEnergyMeterInput([]() {
        //take the energy register of the main electricity meter and return the value in watt-hours
        return cse7766_energy_wh;
    });

    setPowerMeterInput([]() {
        //take the energy register of the main electricity meter and return the value in watt-hours
        return CSE7766_readActivePower();
    });

    addMeterValueInput([]() {
            //take the energy register of the main electricity meter and return the value in watt-hours
            return CSE7766_readVoltage();
        },
        "Voltage",
        "V");

    addMeterValueInput([]() {
            //take the energy register of the main electricity meter and return the value in watt-hours
            return CSE7766_readCurrent();
        },
        "Current.Import",
        "A");
    
    addMeterValueInput([]() {
            //ESP8266 free heap
            return ESP.getFreeHeap();
        },
        "Device.FreeHeap",
        "B");

    setSmartChargingCurrentOutput([](float limit) {
        //set the SAE J1772 Control Pilot value here
        g_trackSmartChargingCurrent = limit > -0.001f ? limit : 32.f;
    });

    setOnResetExecute([] (bool isHard) {
        ESP.reset();
    });

    //... see MicroOcpp.h for more settings
}

void loop() {

    /*
     * Do all OCPP stuff (process WebSocket input, send recorded meter values to Central System, etc.)
     */
    mocpp_loop();

    CSE7766_loop();

    auto t_now = millis();
    if (t_now - cse7766_lastEnergyUpdate >= 5000) {
        float dt = (float) (t_now - cse7766_lastEnergyUpdate) * 0.001f;
        cse7766_lastEnergyUpdate = t_now;

        cse7766_energy_wh += dt * CSE7766_readActivePower() * (1.f / 3600.f);
    }

#ifdef PN532_USE
    pn532_loop();
#endif

    /*
     * Energize EV plug if OCPP transaction is up and running
     */
    if (ocppPermitsCharge() && g_trackSmartChargingCurrent >= 10.f) {
        //OCPP set up and transaction running. Energize the EV plug here
        digitalWrite(RELAY, RELAY_ON);
        g_relayEnabled = true;
    } else {
        //No transaction running at the moment. De-energize EV plug
        digitalWrite(RELAY, RELAY_OFF);
        g_relayEnabled = false;
    }

    //... see MicroOcpp.h for more possibilities
}
