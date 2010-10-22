//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.
#ifndef __KEY_EXISTS_H__
#define __KEY_EXISTS_H__

#include <map>

template<class T,class U>
bool key_exists(std::map<T,U> m, T k)
{
    return m.find(k) != m.end();
}

#endif /* __KEY_EXISTS_H__ */

