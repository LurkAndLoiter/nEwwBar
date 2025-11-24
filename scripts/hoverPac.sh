#!/usr/bin/env bash

mapfile -t pids < <(pgrep -f "hoverPac.sh")
if (( ${#pids[@]} > 1 )); then
    for pid in "${pids[@]::${#pids[@]}-1}"; do
        pkill -9 -P "$pid"
        kill -9 "$pid"
    done
fi

sleep 5
eww update hoverPacDots=false
sleep 1
eww update hoverPacman=false
