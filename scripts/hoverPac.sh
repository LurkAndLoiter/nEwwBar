#!/usr/bin/env bash

mapfile -t pids < <(pgrep -f "hoverPac.sh")
if (( ${#pids[@]} > 1 )); then
    for pid in "${pids[@]::${#pids[@]}-1}"; do
        pkill -9 -P "$pid"
        kill -9 "$pid"
    done
fi

eww update hoverPacDots=false
sleep 0.6
eww update hoverPacman=false
