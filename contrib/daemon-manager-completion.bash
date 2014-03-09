#-*- mode: shell-script; -*-

_dmctl()
{
    COMPREPLY=()

    local awk_func='function pipe_to_comma(s) { gsub("\\|", ",", s); return s }'

    if (($COMP_CWORD == 1)); then
        local IFS=$',' # "dmctl list" outputs comma separated list of daemons
        # Include both the prefixed and non prefixed version, since dmctl works with either.
        # Also include the commands that take no id (status, rescan)
        local all=( $(dmctl list | sed -e "s:\([^,/]*/\)\([^,]*\):\1\2,\2:g") \
                    $(dmctl --help | awk "$awk_func;"'/dmctl/ { print pipe_to_comma($3 ? "" : $2) }') )
    else
        # Only include commands that come after the daemon id (opposite of the above which only takes commands with no id).
        local all=( $(dmctl --help | awk "$awk_func;"'/dmctl/ { print pipe_to_comma($3 ? $3 : "") }') )
    fi

    # Now prune out the completions that don't match
    local i=${#all[*]}
    while [[ $((--i)) -ge 0 ]]; do
        if [[ "${all[$i]}" =~ ^${COMP_WORDS[COMP_CWORD]} ]]; then
            COMPREPLY[${#COMPREPLY[*]}]=${all[$i]}
        fi
    done

    return 0
}
complete -F _dmctl dmctl
