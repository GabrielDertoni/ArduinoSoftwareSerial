#include <Arduino.h>

#include "Temporizador.h"

#define PRESCALER 256

void configuraTemporizador(int baud_rate){
    int frequencia = constrain(baud_rate, 1, 1500);
    //set timer1 interrupt
    TCCR1A = 0;// set entire TCCR1A register to 0
    TCCR1B = 0;// same for TCCR1B
    TCNT1  = 0;//initialize counter value to 0
               //OCR1A = contador;// = (16*10^6) / (10*1024) - 1 (must be <65536)
    OCR1A = (F_CPU / (PRESCALER * frequencia)) - 1;
    // turn on CTC mode (clear time on compare)
    TCCR1B |= (1 << WGM12);
    // Turn T1 off
    TCCR1B &= 0xF8;
    // Disable interrupts
    TIMSK1 = 0;
    TIFR1 = 0;
}

void iniciaTemporizador(){
#ifdef DEBUG
    Serial.println("T1 iniciado");
#endif
    TCNT1 = 0;//initialize counter value to 0
    TIFR1 = 0;
    // enable timer compare interrupt
    TIMSK1 |= (1 << OCIE1A);
    // Set CS10 and CS12 bits for 1024 prescaler
    // TCCR1B |= (1 << CS12) | (1 << CS10);
    TCCR1B |= (1 << CS12);
}

void paraTemporizador(){
#ifdef DEBUG
    Serial.println("T1 parado");
#endif
    // Turn T1 off
    TCCR1B &= 0xF8;
    TIMSK1 = 0;
}
