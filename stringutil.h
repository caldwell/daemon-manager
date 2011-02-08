//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __STRINGUTIL_H__
#define __STRINGUTIL_H__

static inline string trim(string s)
{
    string::iterator start;
    for (start = s.begin(); start != s.end() && isspace(*start); start++) // Front
        ;
    string::iterator end;
    for (end = s.end()-1; end >= start && isspace(*end); end--) // Back
        ;
    return string(start, end+1);
}

static inline string chomp(string s)
{
    if (s.length() && *(s.end()-1) == '\n')
        return s.substr(0, s.length()-1);
    return s;
}

// Sadly we have to pass in the list since we can't templatize over a return value. Weak. :-(
template<typename Container>
void split(Container &list, string s, string separator)
{
    size_t start = 0, sep = 0;
    while (start < s.length()) {
        sep = s.find(separator, start);
        if (sep == s.npos)
            sep = s.length();
        list.push_back(trim(s.substr(start, sep-start)));
        start = sep + separator.length();
    }
}

template<typename Container>
string join(Container list, string separator)
{
    string s;
    for (typename Container::iterator i = list.begin(); i != list.end(); i++)
        s += (i == list.begin() ? "" : separator) + *i;
    return s;
}
#endif /* __STRINGUTIL_H__ */

