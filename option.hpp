#ifndef _OPTION_H
#define _OPTION_H

#include <stdint.h>
#include <new.h>

#include "utils.hpp"

struct nullopt_t {
    explicit nullopt_t() {}
};

static nullopt_t nullopt;

template <typename T>
class option {
public:
    constexpr option() : _has_t(false) {
        static_assert(!is_same<T, nullopt_t>::value, "T cannot be nullopt_t");
    }

    option(T val) : _has_t(true) {
        static_assert(!is_same<T, nullopt_t>::value, "T cannot be nullopt_t");
        new (reinterpret_cast<char*>(_data)) T(val);
    }

    ~option() { if (_has_t) destroy_t(); }

    T& value() & { return *reinterpret_cast<T*>(_data); }

    operator bool() const { return _has_t; }
    T* operator->() { return reinterpret_cast<T*>(_data); }
    T& operator*() { return *reinterpret_cast<T*>(_data); }

    T& operator=(T&& val) {
        if (_has_t) destroy_t();
        _has_t = true;
        return *new (_data) T(move(val));
    }

    void operator=(nullopt_t _) {
        if (_has_t) destroy_t();
        _has_t = false;
        return;
    }


private:
    void destroy_t() {
        T* ptr = reinterpret_cast<T*>(_data);
        ptr->~T();
    }

    alignas(T) uint8_t _data[sizeof(T)];
    bool _has_t;
};

#endif
