#include <new.h>

#include <Arduino.h>
#include "HardwareSerial.h"
#include "Temporizador.hpp"
#include "function.hpp"
#include "option.hpp"
#include "future.hpp"

#define PIN_TX 13
#define PIN_RTS 2
#define PIN_CTS 3
#define BAUD_RATE 100
#define PRESCALER 256
#define PARITY ODD

#define DEFINE_STATES(...)                                          \
    typedef enum { __VA_ARGS__ } state_t;                           \
    friend state_t operator++(state_t& st) {                        \
        return st = static_cast<state_t>(static_cast<int>(st) + 1); \
    }                                                               \


enum parity {
    EVEN = 0,
    ODD,
};

option<timer1> timer1::instance;

static waker* volatile waker_next_tick = nullptr;
static waker* volatile waker_pin_interrupt[16] = { 0 };
static waker* volatile waker_loop = nullptr;
static waker* volatile waker_serial_ev = nullptr;

template <uint8_t Pin>
void wake_on_interrupt_for(waker& waker) {
    static_assert(Pin == 2 || Pin == 3, "Only pins 2 and 3 are supported for interrupts in the Arduino Uno");
    waker_pin_interrupt[Pin] = waker.clone();
}

void wake_on_loop(waker& waker) { waker_loop = waker.clone(); }
void wake_on_next_tick(waker& waker) { waker_next_tick = waker.clone(); }
void wake_on_serial_event(waker& waker) { waker_serial_ev = waker.clone(); }

template <uint8_t P>
void pin_interrupt_handler();

// Calcula bit de paridade - Par ou impar
bool bitParidade(char dado){
  uint8_t count;
  for (count = 0; dado; count += dado & 1, dado >>= 1);
  return count % 2 != PARITY;
}

class serial_send_byte_fsm : public future<int> {
public:
    serial_send_byte_fsm(char c)
        : _c(c)
        , _state(START)
        , _curr_bit(0)
        , _end_bit_count(0)
    { }

    poll_result<int> poll(waker& waker) {
        switch (_state) {
            case START:
                Serial.println("Write start bit");
                digitalWrite(PIN_TX, LOW);
                ++_state;
                timer1::instance->start();
                wake_on_next_tick(waker);
                return pending;

            case SENDING:
                Serial.print("Send bit: ");
                Serial.println(bitRead(_c, _curr_bit));
                digitalWrite(PIN_TX, bitRead(_c, _curr_bit++));
                if (_curr_bit >= 7) ++_state;
                wake_on_next_tick(waker);
                return pending;

            case PARITY:
                Serial.print("Send parity bit: ");
                Serial.println(bitParidade(_c));
                digitalWrite(PIN_TX, bitParidade(_c));
                ++_state;
                wake_on_next_tick(waker);
                return pending;

            case END:
                if (_end_bit_count++ < 2) {
                    Serial.println("Send end bit");
                    digitalWrite(PIN_TX, HIGH);
                    wake_on_next_tick(waker);
                    return pending;
                }
                timer1::instance->stop();

                ++_state;
                return 0;

            default:
                Serial.println("Invalid state");
                return 1;
        }
    }

private:
    typedef enum {
        START = 0,
        SENDING,
        PARITY,
        END,
        N_STATES = 4
    } state_t;

    friend state_t operator++(state_t& st) {
        return st = static_cast<state_t>(static_cast<int>(st) + 1);
    }

    char _c;
    state_t _state;
    size_t _curr_bit;
    uint8_t _end_bit_count;
};

class serial_send_string_fsm : public future<int> {
public:

    serial_send_string_fsm(String s)
        : _s(s)
        , _state(START)
        , _idx(0)
    { }

    poll_result<int> poll(waker& waker) {
        while (true) {
            switch (_state) {
                case START: ++_state;

                case SEND_BYTE_LOOP:
                    Serial.println("SEND_BYTE_LOOP");
                    digitalWrite(PIN_RTS, HIGH);

                    wake_on_interrupt_for<PIN_CTS>(waker);
                    attachInterrupt(digitalPinToInterrupt(PIN_CTS),
                            pin_interrupt_handler<PIN_CTS>, RISING);

                    Serial.println("Waiting CTS HIGH");

                    ++_state;
                    return pending;

                case RECEIVE_BEGIN_CTS:
                    Serial.println("RECEIVE_BEGIN_CTS");
                    _send_byte_fut = serial_send_byte_fsm(_s[_idx]);
                    ++_state;
                    detachInterrupt(digitalPinToInterrupt(PIN_CTS));

                case SENDING:
                {
                    auto res = _send_byte_fut->poll(waker);
                    if (!res) return pending;
                    if (*res != 0) return *res;

                    Serial.println("Waiting CTS LOW");

                    wake_on_interrupt_for<PIN_CTS>(waker);
                    attachInterrupt(digitalPinToInterrupt(PIN_CTS),
                            pin_interrupt_handler<PIN_CTS>, FALLING);
                    ++_state;
                    return pending;
                }

                case RECEIVE_END_CTS:
                    Serial.println("Done sending byte");
                    detachInterrupt(digitalPinToInterrupt(PIN_CTS));
                    if (++_idx < _s.length()) {
                        _state = SEND_BYTE_LOOP;
                        continue;
                    }
                    return 0;

                default:
                    Serial.println("Invalid state");
                    return 1;
            }
        }
    }

private:
    typedef enum {
        START = 0,
        SEND_BYTE_LOOP,
        RECEIVE_BEGIN_CTS,
        SENDING,
        RECEIVE_END_CTS,
        N_STATES,
    } state_t;

    friend state_t operator++(state_t& st) {
        return st = static_cast<state_t>(static_cast<int>(st) + 1);
    }

    state_t _state;
    String _s;
    size_t _idx;
    option<serial_send_byte_fsm> _send_byte_fut;
};

class main_fsm : public future<void> {
public:
    main_fsm() : _state(READ_STRING) { }

    poll_result<void> poll(waker& waker) {
        while (true) {
            switch (_state) {
                case READ_STRING:
                {
                    char c = ' ';
                    while (Serial.available()) {
                        c = Serial.read();
                        if (c == '\n') break;
                        _s.concat(c);
                    }

                    if (c == '\n' && _s.length() > 0) {
                        ++_state;
                        _send_string_fut = serial_send_string_fsm(_s);

                        Serial.print("Sending: '");
                        Serial.print(_s);
                        Serial.println("'");
                    } else {
                        wake_on_serial_event(waker);
                        return pending;
                    }
                }

                case SENDING:
                {
                    auto res = _send_string_fut->poll(waker);
                    if (!res) return pending;
                    if (*res != 0) {
                        Serial.println("Error: serial send with non-zero code");
                        _state = READ_STRING;
                        continue; // Go to READ_STRING
                    }
                    Serial.println("Done sending string");
                    ++_state;
                }

                case END:
                    _state = READ_STRING;
                    _s = String();
                    continue;

                default:
                    Serial.println("Invalid state");
                    return poll_result<void>();
            }
        }
    }

private:
    typedef enum { READ_STRING = 0, SENDING, END, N_STATES } state_t;
    friend state_t operator++(state_t& st) {
        return st = static_cast<state_t>(static_cast<int>(st) + 1);
    }

    state_t _state;
    String _s;
    option<serial_send_string_fsm> _send_string_fut;
};

// Executada uma vez quando o Arduino reseta
void setup(){
    // Desabilita interrupcoes
    noInterrupts();

    // Configura porta serial (Serial Monitor - Ctrl + Shift + M)
    Serial.begin(9600);

    // Inicializa TX ou RX
    pinMode(PIN_RTS, OUTPUT);
    pinMode(PIN_CTS, INPUT);
    pinMode(PIN_TX, OUTPUT);
    digitalWrite(PIN_TX, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);

    // Configura timer
    timer1::instantiate(BAUD_RATE, PRESCALER);

    // Habilita interrupcoes
    interrupts();

    future<void>* fut = new main_fsm();
    waker_loop = new waker_fut(fut);
    Serial.println("Ready!");
}

void loop() {
    if (waker_loop) {
        waker* waker = waker_loop;
        waker_loop = nullptr;
        waker->wake();
    }
}

// Rotina de interrupcao do timer1
// O que fazer toda vez que 1s passou?
ISR(TIMER1_COMPA_vect) {
    noInterrupts();
    if (waker_next_tick) {
        waker* waker = waker_next_tick;
        waker_next_tick = nullptr;
        waker->wake();
    }
    interrupts();
}

template <uint8_t Pin>
void pin_interrupt_handler() {
    noInterrupts();
    if (waker_pin_interrupt[Pin]) {
        waker* waker = waker_pin_interrupt[Pin];
        waker_pin_interrupt[Pin] = nullptr;
        waker->wake();
    }
    interrupts();
}

void serialEvent() {
    if (waker_serial_ev) {
        waker* waker = waker_serial_ev;
        waker_serial_ev = nullptr;
        waker->wake();
    }
}
