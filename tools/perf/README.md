# Performance report

Draws the same test world (a floor, 144 blocks, 6 lamps: the startup benchmark's world)
at every quality level, on the phone profile, with battery saver, and at High with one
costly effect taken away at a time. It writes how long a frame took, the most frames a
second that allows, how busy the graphics chip is at the frame limit (60, or 30 on
battery saver: less busy = less battery and heat), and the program's memory.

```sh
GB_PERF_REPORT=perf.txt ./GutsAndBoltsPlayer
# options: GB_PERF_SIZE=1920x1080  GB_PERF_FRAMES=30
```

On Linux without a screen: `xvfb-run -a env GB_PERF_REPORT=perf.txt ./GutsAndBoltsPlayer`.

Run it on the device you care about (a phone build, an old laptop) and compare lines:
the absolute numbers only mean something on the same machine. On a computer with no
real graphics chip (llvmpipe, SwiftShader) the processor does the drawing, so every
number is far bigger than on a phone, but which effects cost the most still shows.
"memory MB" is the whole program; with a software driver it includes the graphics memory.
