# gst-fifosink
A gstreamer element for writing to a fifo(named pipe) using vmsplice(2).
It is supported only Linux.

## How to build

Prepare tools and libraries.

```shell-session
$ sudo apt install build-essential libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-good1.0-dev libgstreamer-plugins-bad1.0-dev indent
```

Get the source code and build.
```shell-session
$ make install
```

Inspect the element.
```shell-session
$ gst-inspect-1.0 fifosink
```

## Example

```shell
#!/bin/sh -eux

WIDTH=320
HEIGHT=240
FRAMERATE=30

FIFO=$PWD/.fifo
rm -f $FIFO
mkfifo $FIFO

ffmpeg -f rawvideo -pix_fmt yuv420p -s ${WIDTH}x${HEIGHT} -r $FRAMERATE -i $FIFO -c:v libx264 out.m2ts &
GST_DEBUG_NO_COLOR=1 GST_DEBUG=fifosink:6 gst-launch-1.0 -v videotestsrc pattern=ball ! \
    "video/x-raw, format=(string)I420, width=$WIDTH, height=$HEIGHT, framerate=$FRAMERATE/1" ! \
    fifosink location=$FIFO >log 2>&1

```
