This is a video streaming program I wrote for my SDR final project. 
I originally wrote it in python, but found that it was far too slow.

This streams video in chunks over UDP, only updating parts of the image
which changed since last frame, except for occasional keyframes.

## Requirements
This program requires OpenCV for both the python and C++ versions.
For C++ this requires gtk to be included during the compilation of OpenCV.

## Build Instructions
Make a `build` directory and `cd` into it. Then run `cmake ..` then `make`.