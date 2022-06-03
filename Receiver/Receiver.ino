#include <Arduino.h>

#include "SoftwareSerialRx.h"

#define BAUD_RATE 50
#define PIN_RX  13
#define PIN_RTS 12
#define PIN_CTS 11

void setup() {
    pinMode(PIN_RX, INPUT_PULLUP);
    pinMode(PIN_RTS, INPUT); // Should be PULL DOWN
    pinMode(PIN_CTS, OUTPUT); // Should be PULL DOWN
    Serial.begin(9600);
    SoftwareSerialRx.begin(PIN_RX, PIN_RTS, PIN_CTS, BAUD_RATE, Parity::Odd);
    Serial.println("Receptor iniciado");
}

void loop() {
    while (SoftwareSerialRx.available()) {
        char byte;
        SoftwareSerialRx.read((uint8_t*)&byte, sizeof(byte));
        Serial.print(byte);
    }
}
