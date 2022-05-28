#ifndef _FUNC_H
#define _FUNC_H

#include <stdlib.h>
#include <stdint.h>

#include "utils.hpp"

template <size_t SZ, typename T>
class static_allocd_func;

template <size_t SZ, typename R, typename... Args>
class static_allocd_func<SZ, R(Args...)> {
public:

    template <typename Functor>
    static_allocd_func(Functor f) {
        static_assert(sizeof(Functor) <= SZ, "Functor does not fit in size");
        static_assert(alignof(Functor) <= alignof(size_t), "Functor does not have the correct alignment");
        _callable = new (_data) functor_impl<Functor>(f);
    }

    static_allocd_func(const static_allocd_func& other) {
        _callable = other._callable->copy_to(_data);
    }

    static_allocd_func(const static_allocd_func&& other) {
        _callable = other._callable->move_to(move(_data));
    }

    ~static_allocd_func() { _callable->~callable_base(); }

    R operator()(Args&&... args) { return _callable->operator()(args...); }

private:
    class callable_base {
    public:
        virtual R operator()(Args&&...) = 0;
        virtual callable_base* copy_to(void* ptr) noexcept = 0;
        virtual callable_base* move_to(void* ptr) noexcept = 0;
        virtual ~callable_base() {}
    };

    template <typename Functor>
    class functor_impl : public callable_base {
    public:
        explicit functor_impl(Functor f) : _f(f) {}
        explicit functor_impl(const functor_impl& other) : _f(other._f) {}

        R operator()(Args&&... args) override { return _f(args...); }
        callable_base* copy_to(void* ptr) noexcept override { return new (ptr) functor_impl(*this); };
        callable_base* move_to(void* ptr) noexcept override { return new (ptr) functor_impl(move(*this)); };
        ~functor_impl() { }

    private:
        Functor _f;
    };

    alignas(size_t) uint8_t _data[SZ];
    callable_base* _callable;
    size_t _data_size;
};

#endif
