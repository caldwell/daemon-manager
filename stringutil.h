//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __STRINGUTIL_H__
#define __STRINGUTIL_H__

static inline std::string trim(std::string s)
{
    std::string::iterator start;
    for (start = s.begin(); start != s.end() && isspace(*start); start++) // Front
        ;
    std::string::iterator end;
    for (end = s.end()-1; end >= start && isspace(*end); end--) // Back
        ;
    return std::string(start, end+1);
}

static inline std::string chomp(std::string s)
{
    if (s.length() && *(s.end()-1) == '\n')
        return s.substr(0, s.length()-1);
    return s;
}

// Sadly we have to pass in the list since we can't templatize over a return value. Weak. :-(
template<typename Container>
void split(Container &list, std::string s, std::string separator)
{
    size_t start = 0, sep = 0;
    while (start < s.length()) {
        sep = s.find(separator, start);
        if (sep == s.npos)
            sep = s.length();
        std::string piece = trim(s.substr(start, sep-start));
        if (sep != s.length() || s.length() != 0) // Trailing commas don't create empty strings
            list.push_back(piece);
        start = sep + separator.length();
    }
}

template<typename Container>
std::string join(Container list, std::string separator)
{
    std::string s;
    for (typename Container::iterator i = list.begin(); i != list.end(); i++)
        s += (i == list.begin() ? "" : separator) + *i;
    return s;
}
#endif /* __STRINGUTIL_H__ */

