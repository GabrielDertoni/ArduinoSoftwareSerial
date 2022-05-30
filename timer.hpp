#ifndef _TIMER_H
#define _TIMER_H

#include <Arduino.h>

#include "option.hpp"

// reference: https://www.embarcados.com.br/timers-do-atmega328-no-arduino
class timer1 {
public:
    // static const uint32_t clock = 1 << 24;
    static const uint32_t clock = 16000000;

    static option<timer1> instance;

    static timer1& instantiate(uint32_t baud_rate, uint32_t prescaler) {
        if (!timer1::instance) timer1::instance = timer1(baud_rate, prescaler);
        return *timer1::instance;
    }

    void init() {
        // set timer1 interrupt
        TCCR1A = 0; // set entire TCCR1A register to 0
        TCCR1B = 0; // same for TCCR1B
        TCNT1  = 0; // initialize counter value to 0
        uint32_t t = (timer1::clock / (_prescaler * _baud_rate)) - 1;
        if (t >= 1 << 16) t = (1 << 16) - 1;
        OCR1A = t;
        // turn on CTC mode (clear time on compare)
        TCCR1B |= (1 << WGM12);
        // Turn T1 off
        TCCR1B &= 0xF8;
        // Disable interrupts
        TIMSK1 = 0;
        TIFR1 = 0;
    }

    void start() {
        Serial.println("T1 iniciado");
        TCNT1 = 0;//initialize counter value to 0
        TIFR1 = 0;
        // enable timer compare interrupt
        TIMSK1 |= (1 << OCIE1A);

        switch (_prescaler) {
            case    1: TCCR1B |= (1 << CS10);                             break;
            case    8: TCCR1B |= (1 << CS11);                             break;
            case   64: TCCR1B |= (1 << CS10) | (1 << CS11);               break;
            case  256: TCCR1B |= (1 << CS12);                             break;
            case 1024: TCCR1B |= (1 << CS10) | (1 << CS12);               break;
            default:   TCCR1B |= (1 << CS10) | (1 << CS12);               break;
        }
    }

    void stop() {
        TCCR1B &= 0xF8;
        TIMSK1 = 0;
    }

private:
    explicit timer1(uint32_t baud_rate, uint32_t prescaler)
        : _baud_rate(baud_rate)
        , _prescaler(prescaler)
    {
        init();
    }

    uint32_t _baud_rate;
    uint32_t _prescaler;
};

/*
void configuraTemporizador(int baud_rate){
  uint32_t frequencia = constrain(baud_rate,1,1500);
  //set timer1 interrupt
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  OCR1A = ((16 * pow(10,6))/(1024*frequencia)) - 1;
  // OCR1A = (uint16_t)((uint32_t)(1 << 24) / frequencia) - 1;
  // turn on CTC mode (clear time on compare)
  TCCR1B |= (1 << WGM12);
  // Turn T1 off
  TCCR1B &= 0xF8;
  // Disable interrupts
  TIMSK1 = 0;
  TIFR1 = 0;
}

void iniciaTemporizador(){
  // Serial.println("T1 iniciado");
  TCNT1 = 0;//initialize counter value to 0
  TIFR1 = 0;
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  // Set CS10 and CS12 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);

  // No prescaler
  // TCCR1B |= (1 << CS10);
}

void paraTemporizador(){
  // Serial.println("T1 parado");
    // Turn T1 off
  TCCR1B &= 0xF8;
  TIMSK1 = 0;
}
*/

#endif
