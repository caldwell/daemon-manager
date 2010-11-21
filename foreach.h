//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.
#ifndef __FOREACH_H__
#define __FOREACH_H__

#include <stdlib.h>
template<class T> class bool_container {
  public:
    bool_container(T item) : item(item) {}
    mutable T item;
    operator bool() const { return false; }
};

template<class T> typename T::const_iterator __begin(T &x)       { return x.begin(); }
template<class T> typename T::const_iterator __end(T &x, size_t) { return x.end(); }

template<class T> T *__begin(T x[])           { return &x[0]; }
template<class T> T *__end(T x[], size_t len) { return &x[len/sizeof(*x)]; }

#define foreach(var, container)                                         \
    if (bool_container<const typeof(container)&> __c = container) {}  else    \
    if (bool_container<typeof(__begin(__c.item))> __b = __begin(__c.item)) {}  else \
    if (bool_container<typeof(__end(__c.item,0))> __e = __end(__c.item, sizeof(__c.item)))   {}  else \
    if (bool __break = false) {} else                                   \
    for (typeof(__begin(__c.item)) _var_it = __b.item; !__break && _var_it != __e.item; _var_it++) \
        if (bool __once = false) {} else                                \
        if (!(__break = true)) {} else                                  \
        for (var = *_var_it; !__once; __once = true, __break=false)

#endif /* __FOREACH_H__ */
