#ifndef _SOFTWARE_SERIAL_RX
#define _SOFTWARE_SERIAL_RX

#define BUF_SIZE 64

#include <Arduino.h>

enum class Parity { Even, Odd };

// A software serial receiver implementation that never blocks (busy waits) for
// data. This class attempts to reproduce a similar API to the default Arduino
// `Serial` implementation. The `available()` function checks if there is data
// to be read. Then, the data can be retrieved via a `read()` call.
//
class SoftwareSerialRx {
private:
    // States of the Finite State Machine.
    typedef enum state {
        ERROR = 0,        // Will print an error message and then fall into a trap.
        ERROR_TRAP,       // An inescapable state that effectively halts the serial.
        START,            // Start and do some checks
        WAIT_RTS,         // Wait for RTS HIGH
        SET_CTS_HIGH,     // Set CTS HIGH
        RECV_START_BIT,   // Receive start bit
        CENTER_SIGNAL,    // Center the signal
        START_SYNC_TIMER, // Start the timer with baud rate
        SETUP_READ,       // Setup the reading of a byte
        READ_BIT,         // Read 8 bits (7 data bits + 1 parity bit)
        CHECK_PARITY,     // Check the bit parity.
        STOP_BIT,         // Read 1 stop bit (there may be others, but that's fine)
        PUSH_BYTE,        // Print a char to the serial monitor.
        END_TRANSMISSION, // End transmission when RTS LOW is received.
    } state_t;

    friend state_t operator++(volatile state_t& rhs) {
        return rhs = static_cast<state_t>(static_cast<int>(rhs) + 1);
    }

    // Error cases
    typedef enum {
        ERR_NONE = 0,
        ERR_RX_LOW_AT_START,
        ERR_RTS_HIGH_AT_START,
        ERR_PARITY,
        ERR_STOP_BIT_HIGH,
        ERR_UNEXPECTED_RTS_LOW,
    } error_t;

    // Events that can resume the computation. Events can be ORed together to
    // produce composite events. For example EV_RX_RISING | EV_RTS_FALLING will
    // be another event that will trigger when one of them occurs. Note that
    // no event will setup the interrupts to occur, they will just filter the
    // interrupts that do occur.
    typedef enum {
        EV_NONE        = 0b00000,
        EV_RX_RISING   = 0b00001,
        EV_RX_FALLING  = 0b00010,
        EV_RTS_RISING  = 0b00100,
        EV_RTS_FALLING = 0b01000,
        EV_TIMER1      = 0b10000,
    } event_t;

    friend event_t operator|=(volatile event_t& ev, int rhs) {
        return ev = static_cast<event_t>(ev | rhs);
    }

    friend event_t operator|(event_t lhs, int rhs) {
        return static_cast<event_t>(static_cast<int>(lhs) | rhs);
    }

    friend event_t operator&(event_t lhs, event_t rhs) {
        return static_cast<event_t>(static_cast<int>(lhs) & static_cast<int>(rhs));
    }

    volatile error_t _error;
    volatile state_t _state;
    volatile uint8_t _currByte;
    volatile uint8_t _currBitIdx;
    volatile uint8_t _recv_buf[BUF_SIZE];
    volatile size_t _buf_head;
    volatile size_t _buf_tail;

    volatile struct {
        // `1` when the buffer has overflowed, and `0` otherwise.
        uint8_t overflow : 1;
        // `1` when the timer is active
        uint8_t timer1Active : 1;
        // last recorded value of the Rx pin. Useful for detecting changes.
        uint8_t lastRx : 1;
        // last recorded value of the RTS pin.
        uint8_t lastRTS : 1;
        // Whether the `_currByte` is even up until `_currBitIdx`.
        uint8_t currByteIsEven : 1;
    } _flags;

    // The events which will cause the `resume` function to be called.
    volatile event_t subscribedEvents;

    uint8_t _rxPin;
    uint8_t _rtsPin;
    uint8_t _ctsPin;
    uint32_t _baudRate;
    Parity _parity;

    SoftwareSerialRx()
        : _error(ERR_NONE)
        , _state(START)
        , _currByte(0)
        , _currBitIdx(0)
        , _flags{ 0 }
        , _baudRate(0)
        , _recv_buf{ 0 }
        , _buf_head(0)
        , _buf_tail(0)
        , _rxPin(0)
        , _rtsPin(0)
        , _ctsPin(0)
        , _parity(Parity::Even)
    { }

public:
    static SoftwareSerialRx instance;

    // Initialize the `SoftwareSerialRx` instance.
    void begin(uint8_t rxPin, uint8_t rtsPin, uint8_t ctsPin, uint32_t baudRate, Parity parity);

    // Terminate the `SoftwareSerialRx` instance.
    void end();

    // Checks if data is available to be read from the serial buffer.
    bool available();

    // Checks if the buffer has overflowed causing the potential loss of data.
    bool overflowed();

    // Reads values from the internal buffer into another buffer.
    size_t read(uint8_t* buf, size_t len);

    // Public just so they can be accessed by interrupt handlers
    void _handleTimer();
    void _handlePCINT();

private:
    // Resumes computation (reads next bit, waits handshake, etc).
    void resume(event_t event);

    // Restricts the events that will cause the `resume()` function to be called.
    void resumeOnEvent(event_t event);

    // Pushes the current byte to the buffer
    void pushCurrByte();

    // Pops a byte from the buffer. Returns `true` if the operation was
    // successful and `false` otherwise.
    bool popByte(uint8_t& byte);

    // Configures the baud rate of the timer.
    void setTimer1BaudRate(uint32_t baudRate);

    // Initiates the timer
    void startTimer1();

    // Stops the timer
    void stopTimer1();

    // Returns a name corresponding with the current `_state` value.
    const char* stateName();

    // Returns the error message based on the `error_t` enum.
    const char* getErrMsg(error_t error);
};

// Globally accessible instance of the `SoftwareSerialRx` class.
extern SoftwareSerialRx& SoftwareSerialRx;

#endif
