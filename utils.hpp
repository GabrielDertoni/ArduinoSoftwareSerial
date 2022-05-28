#ifndef _UTILS_H
#define _UTILS_H

// Source: https://en.cppreference.com/w/cpp/types/remove_reference
template< class T > struct remove_reference      { typedef T type; };
template< class T > struct remove_reference<T&>  { typedef T type; };
template< class T > struct remove_reference<T&&> { typedef T type; };

struct true_type {
    static constexpr bool value = true;
    constexpr operator bool() { return true; }
};

struct false_type {
    static constexpr bool value = false;
    constexpr operator bool() { return true; }
};

template< class Lhs, class Rhs > struct is_same : false_type { };
template< class T > struct is_same<T, T> : true_type {};

// Source: https://stackoverflow.com/questions/7510182/how-does-stdmove-transfer-values-into-rvalues
template <typename T>
typename remove_reference<T>::type&& move(T&& arg)
{
  return static_cast<typename remove_reference<T>::type&&>(arg);
}

#endif
