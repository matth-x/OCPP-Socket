// -----------------------------------------------------------------------------
// CSE7766 based power monitor
// Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>
// http://www.chipsea.com/UploadFiles/2017/08/11144342F01B5662.pdf
// -----------------------------------------------------------------------------

// Rewrite:
// Matthias Akstaller 2019 - 2020

#ifndef CSE7766_H
#define CSE7766_H

void CSE7766_initialize();

void CSE7766_loop();

float CSE7766_readActivePower();
float CSE7766_readEnergy();
float CSE7766_readVoltage();
float CSE7766_readCurrent();

#endif
