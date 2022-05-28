#ifndef _FUTURE_H

#define _FUTURE_H

#include "option.hpp"
#include "utils.hpp"

struct pending_t {
    explicit constexpr pending_t() {}
};

static pending_t pending;

template <class T>
class poll_result {
public:
    constexpr poll_result(T out) : _inner(out) {
        static_assert(!is_same<T, pending_t>::value, "Poll result cannot contain a `pending_t` inside");
    }

    constexpr poll_result(pending_t _) : _inner() {}

    operator bool() const { return (bool)_inner; }
    T& operator*() { return *_inner; }

private:
    option<T> _inner;
};

template<>
class poll_result<void> {
public:
    constexpr poll_result() : _done(true) { }
    constexpr poll_result(pending_t _) : _done(false) { }

    operator bool() const { return _done; }
    void operator*() { }

private:
    bool _done;
};

class waker {
public:
    virtual void wake() = 0;
    virtual waker* clone() = 0;
    virtual ~waker() {};
};

template <class Output>
class future {
public:
    virtual poll_result<Output> poll(waker& waker) = 0;
};

template <typename Functor>
class waker_fn : public waker {
public:
    waker_fn(Functor f) : _f(f) { }

    virtual waker* clone() override { return new waker_fn(Functor(_f)); };
    void wake() override { _f(); }

private:
    Functor _f;
};

template <typename Functor>
waker* make_waker_fn(Functor f) {
    return new waker_fn<Functor>(f);
}

class waker_fut : public waker {
public:
    waker_fut(future<void>* fut) : _fut(fut) { }

    virtual waker* clone() override { return new waker_fut(_fut); };
    void wake() override { _fut->poll(*this); }

private:
    future<void>* _fut;
};


/*
template <typename Fun>
class fut_from_fn : future<R> {
public:
    fut_from_fn(Fun fun) : _fun(fun) {}

    poll_result<R> poll() { return _fun(); }

private:
    Fun _fun;
};
*/

template <typename Future>
class void_future : public future<void> {
public:
    void_future(Future fut) : _fut(fut) { }

    poll_result<void> poll(waker& waker) override {
        if (!_fut.poll(waker)) return pending;
        return poll_result<void>();
    }

private:
    Future _fut;
};

#endif
