//  Copyright (c) 2011 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __JSON_ESCAPE_H__
#define __JSON_ESCAPE_H__

#include <string>

std::string json_escape(std::string s);
std::string json_unescape(std::string s);

#endif /* __JSON_ESCAPE_H__ */

