#!/usr/bin/env bash
#
# Copyright (c) 2026 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Run Arcanist linting checks

export LC_ALL=C

red="\e[31m" brown="\e[33m" bold="\e[1m" norm="\e[0m"

if ! command -v arc > /dev/null; then
    echo -e "${red}${bold}Skipping Arcanist linting since \`arc\` is not installed.${norm}"
    exit 0
fi

tmp_log=$(mktemp)

# Use the default interactive "console" renderer if running on a TTY. Otherwise use "compiler"
renderer=()
if [ ! -t 0 ]; then
    renderer=(--output compiler)
fi

commit_name=$(git rev-parse --abbrev-ref HEAD)
commit_ref=$(git rev-parse HEAD)
if [[ "$commit_name" == "master" ]]; then
    echo "Running static checks for master (commit ${commit_ref})"
    arc lint --everything "${renderer[@]}" | tee "$tmp_log"
    status=${PIPESTATUS[0]}
else
    mr_start=$(git log --merges -n1 --format=%H)
    echo "Running static checks for commit $commit_ref from merge $mr_start"
    arc lint --rev "$mr_start" "${renderer[@]}" | tee "$tmp_log"
    status=${PIPESTATUS[0]}
fi

if [ ! -t 0 ]; then
    # Annoyingly, when run with `--output compiler`, arc returns success even if an auto-fix issue is encountered (which it
    # can't actually fix in non-interactive mode). So we check the log to return failure in such cases.
    if grep -q 'Auto-Fix' "$tmp_log"; then
        status=1
    fi
    if [ $status -ne 0 ]; then
        arc_cmd="arc lint"
        if [[ "$commit_name" == "master" ]]; then
            arc_cmd="arc lint --everything"
        fi
        echo -e "${brown}These errors might be fixed automatically by running:"
        echo -e "${bold}${arc_cmd}${norm}"
    fi
fi

rm "$tmp_log"
exit "$status"
