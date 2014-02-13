voglperf
=============

Benchmarking tool for Linux OpenGL games. Spews frame information every second. Example:

    ##############################################################################
    Voglperf framerates from pid 12106.
    ##############################################################################
    3564.35 fps frames:3567 time:1000.74ms min:0.23ms max:14.72ms
    4144.01 fps frames:4145 time:1000.24ms min:0.23ms max:1.20ms
    4037.20 fps frames:4038 time:1000.20ms min:0.23ms max:1.20ms
    4059.60 fps frames:4060 time:1000.10ms min:0.23ms max:1.09ms

Can also write frame times to a log file which can then be graphed with gnuplot, etc.

    cat /tmp/voglperf.glxspheres64.2014_02_12-16_02_20.csv:
    # Feb 12 16:02:20 - glxspheres64                                                                                                                                                    
    # 3414.30 fps frames:3417 time:1000.79ms min:0.23ms max:15.00ms
    0.42
    0.34
    0.30
    0.30
    0.29
    0.29
    ...

Building
--------

We use cmake and the voglproj binaries are put into the bin directory. A Makefile is included to simplify this a bit and see how cmake is launched.

Voglperf depends on SDL2, so that will need to be installed to build.

To build amd64 and i386 packages:
> make

To build just i386:
> make voglproj32

To build just amd64:
> make voglproj64

To delete the build32, build64, and bin build files:
> make clean


Running
--------

Some examples of launching voglperf:

Launch tf2 with a frametime logfile:

> bin/voglperfrun64 --showfps --logfile "Team Fortress 2" 

Launch tf2 with steam gameid:

> bin/voglperfrun64 --showfps --logfile 440

Launch local executable:

> bin/voglperfrun64 ~/dev/voglproj/vogl_build/bin/glxspheres32

Logfiles
--------

gnuplot -p -e 'set terminal wxt size 1280,720;set ylabel "milliseconds";set yrange [0:100]; plot "/tmp/voglperf.Team-Fortress-2.2014_02_13-13_06_20.csv" with lines'

gnuplot -p -e 'set output "blah.png";set terminal pngcairo size 1280,720 enhanced;set ylabel "milliseconds";set yrange [0:100]; plot "/tmp/voglperf.Team-Fortress-2.2014_02_13-13_06_20.csv" with lines'

