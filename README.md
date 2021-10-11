# Raspberry Pi hello video
Fork of the Adafruit fork of hello_video example from Raspberry Pi sample code that provides seamless looping of a raw H264 video stream.  Original code is available at: https://github.com/raspberrypi/userland/tree/master/host_applications/linux/apps/hello_pi

## Play/Pause

This fork integrated a new funktionality: play/pause. The video playback can be paused by sending an USR1 signal to the player process and resumed using USR2.

## Building

Build by executing `rebuild.sh`.

Install by executing `sudo make install` inside hello_video directory.

Run by executing `hello_video.bin`.  With no parameters the usage will be printed.
