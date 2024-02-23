// -----------------------------------------------------------------------------
// CSE7766 based power monitor
// Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>
// http://www.chipsea.com/UploadFiles/2017/08/11144342F01B5662.pdf
// -----------------------------------------------------------------------------

// Rewrite:
// Matthias Akstaller 2019 - 2020

#define byte uint8_t
#define CSE7766_BAUDRATE                4800    // UART baudrate

#define CSE7766_V1R                     1.0     // 1mR current resistor
#define CSE7766_V2R                     1.0     // 1M voltage resistor

#include "CSE7766.h"

#include <Arduino.h>

byte inBuf[256];
byte bufL = 0;
byte bufR = 0;

bool _ready = false;

double _active = 0;
double _voltage = 0;
double _current = 0;
double _energy = 0;

double _ratioV = 1.0;
double _ratioC = 1.0;
double _ratioP = 1.0;

void processInput(byte input);

void CSE7766_initialize() {
  Serial.begin(CSE7766_BAUDRATE);
  _ready = true;
}

void CSE7766_loop() {
  if (!_ready) return;

  if (Serial.available() > 0) { //available "Bytes in input buffer"
    byte input = Serial.read();
    processInput(input);
    yield();
    return;
  }
}

void processInput(byte input) {

  /*
   * Buffer Layout:
   * case 1)
   * 
   * #########MY_MESSAGE######
   *          ^         ^
   *          bufL      bufR
   *          
   * case 2)
   * TE#######MY_MESSAGE_COMPLE
   *   ^      ^
   *   bufR   bufL
   */

  /*
   * Add char to buffer
   */
  
  inBuf[bufR++] = input;
  if (bufR == bufL) {
    bufL++;
  }

  /*
   * Interpret content of buffer
   */
  while (bufL + 0 != bufR && bufL + 1 != bufR &&
         bufL + 2 != bufR && bufL + 3 != bufR ) { //search for message start; check if it is a message if it's at least 4 bytes long

    // first byte must be 0x55 or 0xF?
    if (inBuf[(byte) (bufL + 0)] != 0x55 &&
        inBuf[(byte) (bufL + 0)] < 0xF0) {
        //No preamble. Try with next byte
        bufL++; 
        continue;
    }

    // second byte must be 0x5A
    if (inBuf[(byte) (bufL + 1)] != 0x5A) {
        bufL++; 
        continue;
    }

    /*
     * Beginning of a message found. Check if the message is already fully available
     */

    // Sample data:
    // 55 5A 02 E9 50 00 03 31 00 3E 9E 00 0D 30 4F 44 F8 00 12 65 F1 81 76 72 (w/ load)
    // F2 5A 02 E9 50 00 03 2B 00 3E 9E 02 D7 7C 4F 44 F8 CF A5 5D E1 B3 2A B4 (w/o load)


    //Exit condition: Check if full message is already in buffer
    for (byte i = bufL; i != (byte) (bufL + 24); i++) {
      if (i == bufR) {
        //No. Buffer ends before message ends. Wait for following input
        return;
      }
    }

    /*
     * At the start of the buffer, there is a complete message. Continue with rest of function
     */

    byte checksum = 0;
    for (int i = 2; i < 23; i++) {
        checksum += inBuf[(byte) (bufL + i)];
    }

    checksum -= inBuf[(byte) (bufL + 23)];

    if (checksum) {
        //discard this message as the checksum is wrong

        //bufL = bodyL + bodyLen; //What if input reads ## 23 24 04 FB ERR 00 23 24 02 FD 01 02 ... ? Then this jumps over the next message start and destroys it too
        bufL ++;
        continue;
    }

    // Calibration
    if (0xAA == inBuf[(byte) (bufL + 0)]) {
        //debugMsg("[CSE7766] Chip not calibrated! Ignore Message");
        bufL += 24;
        return;
    }

    if ((inBuf[(byte) (bufL + 0)] & 0xFC) > 0xF0) {
        //debugMsg("[CSE7766] Error! See manual of CSE7766 for further information");
        bufL += 24;
        return;
    }

    // Calibration coefficients
    unsigned long _coefV = (inBuf[(byte) (bufL + 2)]  << 16 | inBuf[(byte) (bufL + 3)]  << 8 | inBuf[(byte) (bufL + 4)] );              // 190770
    unsigned long _coefC = (inBuf[(byte) (bufL + 8)]  << 16 | inBuf[(byte) (bufL + 9)]  << 8 | inBuf[(byte) (bufL + 10)]);              // 16030
    unsigned long _coefP = (inBuf[(byte) (bufL + 14)] << 16 | inBuf[(byte) (bufL + 15)] << 8 | inBuf[(byte) (bufL + 16)]);              // 5195000

    // Adj: this looks like a sampling report
    uint8_t adj = inBuf[(byte) (bufL + 20)];                                                            // F1 11110001

    // Calculate voltage
    _voltage = 0;
    if ((adj & 0x40) == 0x40) {
        unsigned long voltage_cycle = inBuf[(byte) (bufL + 5)] << 16 | inBuf[(byte) (bufL + 6)] << 8 | inBuf[(byte) (bufL + 7)];        // 817
        _voltage = _ratioV * _coefV / voltage_cycle / CSE7766_V2R;                      // 190700 / 817 = 233.41
    }

    // Calculate power
    _active = 0;
    if ((adj & 0x10) == 0x10) {
        if ((inBuf[(byte) (bufL + 0)] & 0xF2) != 0xF2) {
            unsigned long power_cycle = inBuf[(byte) (bufL + 17)] << 16 | inBuf[(byte) (bufL + 18)] << 8 | inBuf[(byte) (bufL + 19)];   // 4709
            _active = _ratioP * _coefP / power_cycle / CSE7766_V1R / CSE7766_V2R;       // 5195000 / 4709 = 1103.20
        }
    }

    // Calculate current
    _current = 0;
    if ((adj & 0x20) == 0x20) {
        if (_active > 0) {
            unsigned long current_cycle = inBuf[(byte) (bufL + 11)] << 16 | inBuf[(byte) (bufL + 12)] << 8 | inBuf[(byte) (bufL + 13)]; // 3376
            _current = _ratioC * _coefC / current_cycle / CSE7766_V1R;                  // 16030 / 3376 = 4.75
        }
    }

    // Calculate energy
    unsigned int difference;
    static unsigned int cf_pulses_last = 0;
    unsigned int cf_pulses = inBuf[(byte) (bufL + 21)] << 8 | inBuf[(byte) (bufL + 22)];
    if (0 == cf_pulses_last) cf_pulses_last = cf_pulses;
    if (cf_pulses < cf_pulses_last) {
        difference = cf_pulses + (0xFFFF - cf_pulses_last) + 1;
    } else {
        difference = cf_pulses - cf_pulses_last;
    }
    _energy += difference * (float) _coefP / 1000000.0;
    cf_pulses_last = cf_pulses;

    //When loop iteration reached the end, message is consumed. Remove from buffer
    bufL += 24;
  }
}

float CSE7766_readActivePower() {
  return (float) _active;
}

float CSE7766_readEnergy() {
    return (float) _energy;
}

float CSE7766_readVoltage() {
  return (float) _voltage;
}

float CSE7766_readCurrent() {
  return (float) _current;
}
