#include <Arduino.h>

#include "Temporizador.h"
#include "SoftwareSerialRx.h"

#define EV_RTS (EV_RTS_RISING | EV_RTS_FALLING)
#define EV_RX  (EV_RX_RISING  | EV_RX_FALLING )

class SoftwareSerialRx SoftwareSerialRx::instance;

// The globally accessible `SoftwareSerialRx` instance.
class SoftwareSerialRx& SoftwareSerialRx = SoftwareSerialRx::instance;

static inline void enablePortForPCINT(uint8_t pin) {
    bitSet(*digitalPinToPCICR(pin), digitalPinToPCICRbit(pin));
}

static inline void setPinPCINT(uint8_t pin, bool enable) {
    bitWrite(*digitalPinToPCMSK(pin), digitalPinToPCMSKbit(pin), enable);
}

void SoftwareSerialRx::begin(uint8_t rxPin, uint8_t rtsPin, uint8_t ctsPin, uint32_t baudRate, Parity parity) {
    _baudRate = baudRate;
    _parity = parity;
    _rxPin = rxPin;
    _rtsPin = rtsPin;
    _ctsPin = ctsPin;

    enablePortForPCINT(_rxPin);
    enablePortForPCINT(_rtsPin);

    resume(EV_NONE);
}

void SoftwareSerialRx::end() {
    setPinPCINT(_rtsPin, false);
    setPinPCINT(_rxPin, false);
    paraTemporizador();
}

bool SoftwareSerialRx::available() { return _buf_head != _buf_tail; }

// Reads values from the internal buffer
size_t SoftwareSerialRx::read(uint8_t* buf, size_t len) {
    size_t i;
    for (i = 0; i < len && popByte(buf[i]); i++);
    return i;
}

bool SoftwareSerialRx::overflowed() { return _flags.overflow; }

// Resumes the computation
void SoftwareSerialRx::resume(event_t ev) {
#ifdef DEBUG
    Serial.print("resume: ");
    Serial.println(stateName());
#endif

// Allows the state machine to go to any other state with the infinite loop +
// switch trick.
#define GOTO(st) { _state = st; continue; }

// Check if `expr` is true, if it isn't, set the error value to `err` and goes
// to the error state from the state machine.
#define ASSERT(expr, err) \
    if (!(expr)) {        \
        _error = err;     \
        GOTO(ERROR);      \
    }

    // This infinite loop coupled with a switch composes the Finite State
    // Machine. Whenever the `resume` function is called, some processing is
    // done and then this function will return but record it's state in a
    // global. Next time the function is called it will jump to where it was
    // before based on the recorded state and continue the processing. Note that
    // the machine may change states without returning from the function in
    // between states. It is also possible to return to a previous state using
    // the GOTO macro.
    for (;;) {
        switch (_state) {
            case ERROR:
                Serial.println(getErrMsg(_error));
                resumeOnEvent(EV_NONE);
                stopTimer1();
                if (_error == ERR_PARITY) {
                    // Clear the error, just get the next byte
                    _error = ERR_NONE;
                    GOTO(RECV_START_BIT);
                }
                ++_state;

            // There is no way to get out of this state.
            case ERROR_TRAP: return;

            // Assert some stuff
            case START:
                ASSERT(digitalRead(_rxPin) == HIGH, ERR_RX_LOW_AT_START);
                ASSERT(digitalRead(_rtsPin) == LOW, ERR_RTS_HIGH_AT_START);
                _flags.lastRx = HIGH;
                _flags.lastRTS = LOW;
                setPinPCINT(_rxPin, true);
                setPinPCINT(_rtsPin, true);
                ++_state;

            // Sets up to wait for the next PCINT
            case WAIT_RTS:
                // Wait for RTS HIGH
#ifdef DEBUG
                Serial.println("Waiting RTS HIGH");
#endif
                resumeOnEvent(EV_RTS_RISING);
                ++_state;
                return;

            case SET_CTS_HIGH:
                digitalWrite(_ctsPin, HIGH);
                ++_state;

            case RECV_START_BIT:
                // While waiting for the start bit, RTS went LOW, end this
                // transmission.
                if (digitalRead(_rtsPin) == LOW) GOTO(END_TRANSMISSION);

                // Wait for RX LOW
                // if (digitalRead(_rxPin) != LOW) return; // Wait next PCINT
                if (digitalRead(_rxPin) == HIGH) {
                    resumeOnEvent(EV_RX_FALLING | EV_RTS_FALLING);
                    return; // Wait next PCINT
                }

                // Disable PCINT notifications for rx pin.
                ++_state;

            case CENTER_SIGNAL:
                setTimer1BaudRate(_baudRate * 2);
                startTimer1();
                resumeOnEvent(EV_TIMER1);
                ++_state;
                return; // Wait next Temporizador1 INT

            case START_SYNC_TIMER:
                stopTimer1();
                setTimer1BaudRate(_baudRate);
                startTimer1();
                resumeOnEvent(EV_TIMER1);
                ++_state;
                return; // Wait next Temporizador1 INT

            case SETUP_READ:
                _currByte = 0;
                _currBitIdx = 0;
                _flags.currByteIsEven = 0;
                ++_state;

            // There are two possible reasons for the function to be called in
            // this state: 1. pin interrupt, 2. timer interrupt.
            case READ_BIT:
            {
                ASSERT(digitalRead(_rtsPin) == HIGH, ERR_UNEXPECTED_RTS_LOW);

#ifdef DEBUG
                Serial.print("Read bit: ");
                Serial.println(digitalRead(_rxPin));
#endif
                uint8_t read = digitalRead(_rxPin);
                if (read) _flags.currByteIsEven = !_flags.currByteIsEven;
                bitWrite(_currByte, _currBitIdx, read);

                // Read an entire byte, including the parity bit.
                if (_currBitIdx++ < 8) return; // Wait for next Temporizador1 INT
                ++_state;
            }

            case CHECK_PARITY:
            {
                // Remove parity bit from `currByte`
                bitClear(_currByte, 7);
                ASSERT(_parity == Parity::Even ? _flags.currByteIsEven : !_flags.currByteIsEven, ERR_PARITY);
                ++_state;
                return; // Wait for next Temporizador1 INT
            }

            case STOP_BIT:
                ASSERT(digitalRead(_rtsPin) == HIGH, ERR_UNEXPECTED_RTS_LOW);
                ASSERT(digitalRead(_rxPin) == HIGH, ERR_STOP_BIT_HIGH);
                stopTimer1();
                ++_state;

            case PUSH_BYTE:
                pushCurrByte();
                GOTO(RECV_START_BIT);

            // The only way to get here is to detect RTS LOW. If we got here,
            // restart the wait loop.
            case END_TRANSMISSION:
                digitalWrite(_ctsPin, LOW);
                _currByte = '\n';
                pushCurrByte();
                GOTO(START);
        }
    }

#undef GOTO
#undef ASSERT
}

void SoftwareSerialRx::_handleTimer() {
    if (subscribedEvents & EV_TIMER1) resume(EV_TIMER1);
}

void SoftwareSerialRx::_handlePCINT() {
    event_t ev = EV_NONE;

    if (subscribedEvents & EV_RX) {
        uint8_t rxRead = digitalRead(_rxPin);
        switch ((_flags.lastRx << 1) | rxRead) {
            case 0b01: ev |= EV_RX_RISING;  break;
            case 0b10: ev |= EV_RX_FALLING; break;
            default:                        break; // No change
        }
        _flags.lastRx = rxRead;
    }

    if (subscribedEvents & EV_RTS) {
        uint8_t rtsRead = digitalRead(_rtsPin);
        switch ((_flags.lastRTS << 1) | rtsRead) {
            case 0b01: ev |= EV_RTS_RISING;  break;
            case 0b10: ev |= EV_RTS_FALLING; break;
            default:                         break; // No change
        }
        _flags.lastRTS = rtsRead;
    }

    if ((ev & subscribedEvents) != EV_NONE) resume(ev & subscribedEvents);
}

// NOTE: if the event is RISING, the state of the corresponding pin is assumed
// to be LOW, and if the event is FALLING, the state is assumed to be HIGH.
//
// TODO: It might be possible to remove the interrupt on a pin whenever there is
// no subscription to it's event. However, I wasn't able to do it in my
// attempts.
void SoftwareSerialRx::resumeOnEvent(event_t event) {
    if (!(subscribedEvents & EV_RTS) && event & EV_RTS) {
        _flags.lastRTS = event & EV_RTS_RISING ? LOW : HIGH;
    }

    if (!(subscribedEvents & EV_RX) && event & EV_RX) {
        _flags.lastRx = event & EV_RX_RISING ? LOW : HIGH;
    }

    subscribedEvents = event;
}

void SoftwareSerialRx::pushCurrByte() {
    uint8_t next = (_buf_head + 1) % BUF_SIZE;
    if (next != _buf_tail) {
        _recv_buf[_buf_head] = _currByte;
        _buf_head = next;
    } else {
        _flags.overflow = true;
    }
}

bool SoftwareSerialRx::popByte(uint8_t& byte) {
    if (_buf_head == _buf_tail) return false;
    byte = _recv_buf[_buf_tail];
    _buf_tail = (_buf_tail + 1) % BUF_SIZE;
    return true;
}

void SoftwareSerialRx::setTimer1BaudRate(uint32_t baudRate) {
    if (_flags.timer1Active) stopTimer1();
    configuraTemporizador(baudRate);
}

void SoftwareSerialRx::startTimer1() {
    _flags.timer1Active = true;
    iniciaTemporizador();
}

void SoftwareSerialRx::stopTimer1()  {
    _flags.timer1Active = false;
    paraTemporizador();
}

const char* SoftwareSerialRx::getErrMsg(error_t error) {
    switch (error) {
        case ERR_RX_LOW_AT_START:    return "Erro: esperava RX HIGH no inicio";
        case ERR_RTS_HIGH_AT_START:  return "Erro: esperava RTS LOW no inicio";
        case ERR_PARITY:             return "Erro: bit de paridade não consistente.";
        case ERR_STOP_BIT_HIGH:      return "Erro: esperava stop bit LOW";
        case ERR_UNEXPECTED_RTS_LOW: return "Erro: RTS LOW (fim de transmissão inesperado)";
        default:                     return "";
    }
}

const char* SoftwareSerialRx::stateName() {
    switch (_state) {
        case ERROR:            return "ERROR";
        case ERROR_TRAP:       return "ERROR_TRAP";
        case START:            return "START";
        case WAIT_RTS:         return "WAIT_RTS";
        case SET_CTS_HIGH:     return "SET_CTS_HIGH";
        case RECV_START_BIT:   return "RECV_START_BIT";
        case CENTER_SIGNAL:    return "CENTER_SIGNAL";
        case START_SYNC_TIMER: return "START_SYNC_TIMER";
        case SETUP_READ:       return "SETUP_READ";
        case READ_BIT:         return "READ_BIT";
        case CHECK_PARITY:     return "CHECK_PARITY";
        case STOP_BIT:         return "STOP_BIT";
        case PUSH_BYTE:        return "PUSH_BYTE";
        case END_TRANSMISSION: return "END_TRANSMISSION";
        default:               return NULL;
    }
}

ISR(TIMER1_COMPA_vect) { SoftwareSerialRx._handleTimer(); }
ISR(PCINT0_vect)       { SoftwareSerialRx._handlePCINT(); }

#if defined(PCINT1_vect)
ISR(PCINT1_vect, ISR_ALIASOF(PCINT0_vect));
#endif

#if defined(PCINT2_vect)
ISR(PCINT2_vect, ISR_ALIASOF(PCINT0_vect));
#endif

#if defined(PCINT3_vect)
ISR(PCINT3_vect, ISR_ALIASOF(PCINT0_vect));
#endif
