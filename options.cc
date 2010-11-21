//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "options.h"
#include <vector>
#include <string>
#include <algorithm>
#include <stdio.h>

using namespace std;

class option_match {
    public: string &lopt, &sopt;
            option_match(string &lopt, string &sopt) : lopt(lopt), sopt(sopt) {}
            bool operator()(string s) {
                return s == "--"+lopt ||
                       s.find("--"+lopt+"=") == 0 ||
                       sopt.size() && s == "-" + sopt;
            }
};

options::options(int c, char **v)
{
    *v++; c--;
    end = -1;
    for (int i=0; i<c; i++)
        if (string(v[i]) == "--" && end == (size_t) -1)
            end = i;
        else
            args.push_back(v[i]);
    if (end == (size_t) -1)
        end = args.size();
}

bool options::get(string long_opt, char short_opt_char, arg_type type)
{
    string short_opt; short_opt += short_opt_char;
    arg="";
    argm.clear();
    vector<string>::iterator o = args.begin();
    while(1) {
        o = find_if(o, args.begin()+end, option_match(long_opt, short_opt));
        if (o == args.begin()+end)
            break;

        string opt = *o;
        o = args.erase(o);
        end--;
        bool islong = opt.find("--") == 0;
        string val;
        if (type == arg_none) {
            if (opt.find("=") != opt.npos) {
                bad.push_back("\""+opt+"\" does not take an argument");
                continue;
            }
            val = opt;
        }
        if (type == arg_required) {
            if (islong) {
                size_t vo = opt.find('=');
                if (vo == opt.npos) {
                    bad.push_back("missing argument for "+opt);
                    continue;
                }
                val = opt.substr(vo+1);
            } else {
                if (o == args.begin()+end) {
                    bad.push_back("missing argument for -"+short_opt);
                    continue;
                }
                val = *o;
                o = args.erase(o);
                end--;
            }
        }
        argm.push_back(val);
        arg = val;
    }
    return argm.size();
}

bool options::get(string long_opt, arg_type type)
{
    return get(long_opt, 0, type);
}

bool options::bad_args()
{
    bool is_bad = false;
    for (vector<string>::iterator ba = bad.begin(); ba != bad.end(); ba++)
        fprintf(stderr, "%s\n", ba->c_str());

    for (vector<string>::iterator a = args.begin(); a != args.begin()+end; a++)
        if (a->size() && a->at(0) == '-') {
            fprintf(stderr, "Unkown argument \"%s\"\n", a->c_str());
            is_bad = true;
        }
    return bad.size() || is_bad;
}
