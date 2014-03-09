//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __FOREACH_H__
#define __FOREACH_H__

#if __cplusplus >= 201103L || defined(__clang__) && __clang_major__ >= 3

#define foreach(var, container) for (var : container)

#else

#include <stdlib.h>

template<class T> typename T::const_iterator __begin(T &x)       { return x.begin(); }
template<class T> typename T::const_iterator __end(T &x, size_t) { return x.end(); }

template<class T> T *__begin(T x[])           { return &x[0]; }
template<class T> T *__end(T x[], size_t len) { return &x[len/sizeof(*x)]; }

#define foreach(var, container)                                         \
    if (bool __init = true)                                             \
    for (const typeof(container)& __c = (container); __init;)           \
    for (typeof(__begin(__c)) __b = __begin(__c); __init; )             \
    for (typeof(__end(__c,0)) __e = __end(__c, sizeof(__c)); __init;)   \
    if (bool __break = __init = false) {} else                          \
    for (typeof(__begin(__c)) _var_it = __b; !__break && _var_it != __e; _var_it++) \
        if (bool __once = false) {} else                                \
        if (!(__break = true)) {} else                                  \
        for (var = *_var_it; !__once; __once = true, __break=false)

#endif

#endif /* __FOREACH_H__ */
