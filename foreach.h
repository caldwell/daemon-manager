//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.
#ifndef __FOREACH_H__
#define __FOREACH_H__

template<class T> class bool_container {
  public:
    bool_container(T item) : item(item) {}
    mutable T item;
    operator bool() const { return false; }
};

#define foreach(var, container)                                         \
    if (bool_container<typeof(container)> __c = container) {}  else     \
    if (bool __break = false) {} else                                   \
    for (typeof(container.begin()) _var_it = __c.item.begin(); !__break && _var_it != __c.item.end(); _var_it++) \
        if (bool __once = false) {} else                                \
        if (!(__break = true)) {} else                                  \
        for (var = *_var_it; !__once; __once = true, __break=false)

#endif /* __FOREACH_H__ */
