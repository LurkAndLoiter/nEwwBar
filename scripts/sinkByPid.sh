#!/bin/bash
CALL=${1:-mute}
IDX=${2:-0}
DEST=${3:-0}

JSON=$(pactl -f json list sink-inputs 2>/dev/null | jq '[.[] | { index: .index, mute: .mute, pid: .properties["application.process.id"] }]')

PID=$(echo "$JSON" | jq -r --arg idx "$IDX" '
.[] | select(.index == ($idx|tonumber)) | .pid
')

INDICES=$(echo "$JSON" | jq -r --arg pid "$PID" '
  .[] | select(.pid == $pid) | .index
')

case "$CALL" in
  mute)
    MUTE=$(echo "$JSON" | jq -r --arg idx "$IDX" '
    .[] | select(.index == ($idx|tonumber)) | .mute
    ')

    for i in $INDICES; do
      if [ "$MUTE" = "true" ]; then
        pactl set-sink-input-mute "$i" false
      else
        pactl set-sink-input-mute "$i" true
      fi 
    done ;;
  move)
    for i in $INDICES; do
      pactl move-sink-input $i $DEST
    done ;;
esac
