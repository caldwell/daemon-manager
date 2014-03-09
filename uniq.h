//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __UNIQ_H__
#define __UNIQ_H__

#include <vector>
#include <algorithm>

template<class T>
std::vector<T> uniq(std::vector<T> list)
{
    sort(list.begin(), list.end());
    typename std::vector<T>::const_iterator end = unique(list.begin(), list.end());
    list.resize(end - list.begin());
    return list;
}

template<class T>
std::vector<T> uniq(std::vector<T> list, bool (*binary_predicate)(typename std::vector<T>::forward_iterator a,
                                                                  typename std::vector<T>::forward_iterator b))
{
    sort(list.begin(), list.end());
    typename std::vector<T>::const_iterator end = unique(list.begin(), list.end(), binary_predicate);
    list.resize(end - list.begin());
    return list;
}

#endif /* __UNIQ_H__ */

