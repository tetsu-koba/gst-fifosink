#!/bin/sh -eux

WIDTH=1280
HEIGHT=720
FRAMERATE=30

FIFO=$PWD/.fifo
rm -f $FIFO
mkfifo $FIFO

cat $FIFO > data.log &

GST_DEBUG_NO_COLOR=1 GST_DEBUG=fifosink:6 gst-launch-1.0 -v v4l2src device="/dev/video0" ! \
    "image/jpeg, width=$WIDTH, height=$HEIGHT, framerate=$FRAMERATE/1" ! \
    nvv4l2decoder mjpeg=1 ! 'video/x-raw(memory:NVMM)' ! \
    nvvidconv ! 'video/x-raw, format=(string)I420' ! \
    fifosink location=$FIFO >log 2>&1
