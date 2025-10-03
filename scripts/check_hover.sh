#!/bin/bash
eww update hover_state=""
sleep 0.5
if ! eww get hover_state | grep -q "$1"; then
    if eww active-windows | grep -q "$1"; then
        eww close "$1"
    fi
fi
