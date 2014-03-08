#-*- mode: shell-script; -*-

_dmctl()
{
    COMPREPLY=()

    if (($COMP_CWORD == 1)); then
        local IFS=$',' # "dmctl list" outputs comma separated list of daemons
        # Remove user's own prefix from the daemon name. But leave it in for other user's they may control
        local all=( $(dmctl list | sed -e "s,$USER/,,g") \
                    $(dmctl --help | awk -e '/dmctl/ { print gensub("\\|",",","g",$3 ? "" : $2) }') )
    else
        # Only include commands that come after the daemon id (opposite of the above which only takes commands with no id).
        local all=( $(dmctl --help | awk -e '/dmctl/ { print gensub("\\|"," ","g",$3 ? $3 : "") }') )
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
