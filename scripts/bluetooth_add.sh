#!/bin/env bash

# callscript Powered Pairable discoverable device Trusted NoYELLING

# turn on adapter if off.
if ! $1; then
  bluetoothctl power on;
fi
# enable pairing to adapter if disabled
if ! $2; then
  bluetoothctl pairable on;
fi
# broadcast adapter name for 15 seconds
if ! $3; then
  bluetoothctl discoverable on;
fi
# if device is not trusted yet; trust
if ! $5; then
  bluetoothctl trust $4;
fi

bluetoothctl -a NoInputNoOutput pair $4;
bluetoothctl connect $4;

sleep 0.4; # race condition with pulseaudio registering the device
if [ -n "$6" ]; then
  # I DONT LIKE BEING YELLED AT!!!!!
  pactl set-sink-volume $(pactl --format=json list sinks | jq -r '.[] | select(.properties["device.string"]=="'"$4"'") | .name') $6%
fi

sleep 14.6; # keeps discoverable on
