//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#include <string>
#include <vector>

enum arg_type { arg_none, arg_required, /* arg_optional not supported yet */ };

class options {
  public:
    options(int c, char **v);
    bool get(std::string long_opt, char short_opt, arg_type type = arg_none);
    bool get(std::string long_opt, arg_type type = arg_none);
    std::vector<std::string> argm; // after get this is set to an array of all the options of that type
    std::string arg;               // after get this is set to the last option of that type or "" if nothing exists.
    bool bad_args();               // prints a list of errors to stderr.
    std::vector<std::string> args; // Arguments that haven't been parsed yet.
  private:
    std::vector<std::string> bad;
    size_t end;
};

// The standard technique:
// int main(int argv, char **argv)
// {
//    options o(argc, argv);
//    if (o.get("help",       'h'))               { /* the return value of o.get() is enough to know that a flag was present */ }
//    if (o.get("config",     'c', arg_required)) { /* o.arg is set to last -c or --config on the command line */ }
//    if (o.get("verbose",    'v'))               { /* o.argm.size() is a count of how many were specified (-v -v -v) */ }
//    if (o.get("output",     'o', arg_required)) { /* o.argm is vactor<string> of --output or -o command line args:
//                                                     "-o f1 -o f2 --output f3" would give { "f1", "f2", "f3" } */ }
//    if (o.get("foreground"))                    { /* Short option is not required */ }
//    if (o.get("version"))                       { /* o.arg == "--version" for flags */ }
//    if (o.bad_args() ||                         // o.bad_args() will print to stderr and return true if there were errors [1].
//        o.args.size() != 1)                     // Anything not parsed is left over in o.args.
//           usage(argv[0], EXIT_FAILURE);        // argv is left untouched.
//    ...
//
// [1] Missing argument errors are caught and saved during calls to o.get(). o.bad_args() will also go through
// the remaining arguments (o.args) and report unknown argument errors if something in there starts with '-'.
//
// The '--' arg separator is supported. The first one encountered will set it up so that o.get() and
// o.bad_args() will never go past that point when parsing options. Everything after it will still be
// available in o.args(). The '--' itself will be removed from the list though.

#endif /* __OPTIONS_H__ */

