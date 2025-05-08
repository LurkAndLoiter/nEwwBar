#!/bin/bash
sleep 0.5
if [ "$(eww get hover_state)" != "$1" ]; then
  eww close "$1"
fi
