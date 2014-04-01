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

To build amd64 and i386 packages:
> make

To build just i386:
> make voglperf32

To build just amd64:
> make voglperf64

To delete the build32, build64, and bin build files:
> make clean

Building voglperf on SteamOS
--------

 - Click **"Settings"** on top right.
 - Click **"Interface"** on left.
 - Check **"Enable access to the Linux desktop"**.
 - Head back to main menu, click **"Exit"**, **"Return to Desktop"** (or hit ctrl+alt+F8)
 - Click **"Activities"** on top left, then **"Applications"**.
 - Click **"Terminal"** icon.
 - Type **`passwd`** and enter a password.
 - Install build packages:
  * `sudo apt-get install steamos-dev `
  * `echo "deb http://ftp.debian.org/debian wheezy main contrib non-free" | sudo tee -a /etc/apt/sources.list`
  * `sudo apt-get update`
  * `sudo apt-get install git ca-certificates cmake g++ gcc-multilib g++-multilib`
  * `sudo apt-get install mesa-common-dev libedit-dev libtinfo-dev libtinfo-dev:i386`

 - Get the volgperf source:
  * `git clone https://github.com/ValveSoftware/voglperf.git`

 - Build:
  * `cd voglperf`
  * `make`

Run voglperf on SteamOS
--------

 - Run voglperf as "_steam_" user.

  * `sudo -u steam bin/voglperfrun64`

 - You should see something like:
  * `Starting web server...`
  * `Started http://172.16.10.93:8081`

 - Double click **"Return to Steam"** (or hit ctrl+alt+f7)
 - Connect to voglperf url with Chrome or Firefox browser from another computer.
 - Browser should show something like:

```
Connected to ws://172.16.10.93:8081/ws  
  Welcome!
```

```
Gameid: ''  
    WS Connections: 1  
    logfile: Off (Launch option)  
    verbose: Off  
    fpsspew: Off  
    fpsshow: Off (Launch option)  
    dry-run: Off (Launch option)  
    ld-debug: Off (Launch option)  
    xterm: Off (Launch option)  
    debugger-pause: Off (Launch option)
```

 - To launch TF2, do:
  * `game start 440`
  * Click **OK** button on "Allow game launch" dialog.

 - Full AppID game list: <http://steamdb.info/linux/>

 - To capture logfile for 10 seconds, type:

  * `logfile start 10` ; 

 - Should see something like:

  * ` Logfile started: /tmp/voglperf.Team-Fortress-2.2014_04_01-06_28_16.csv (10 seconds).`  

  * `Logfile stopped: http://172.16.10.93:8081/logfile/tmp/voglperf.Team-Fortress-2.2014_04_01-06_28_16.csv`

 - Right click on logfile link and say "Open in New Tab" (or whatever).

Run vogl w/ SSH on SteamOS
--------

 - Run `ip addr` and note IP address of your SteamOS box.
  * `ssh desktop@127.16.10.93`
  * `cd voglperf`
  * `sudo -u steam bin/voglperfrun64`
  * Run various commands:
     * `help`
     * `status`
     * `showfps on`
     * `game start 440`
     * etc.

### Notes ###
 - HTML needs to be cleaned up.
 - Occasionally web client will think two clients are connected and duplicated messages. (Needs to be tracked down.)
 - We are currently adding voglperf as a SteamOS package.

Logfiles
--------

Display graph in gnuplot (install gnuplot-x11):

> gnuplot -p -e 'set terminal wxt size 1280,720;set ylabel "milliseconds";set yrange [0:100]; plot "/tmp/voglperf.Team-Fortress-2.2014_02_13-13_06_20.csv" with lines'

Output graph to blah.png:

> gnuplot -p -e 'set output "blah.png";set terminal pngcairo size 1280,720 enhanced;set ylabel "milliseconds";set yrange [0:100]; plot "/tmp/voglperf.Team-Fortress-2.2014_02_13-13_06_20.csv" with lines'

Example Screenshot
------------------

![Example screenshot](https://raw.github.com/ValveSoftware/voglperf/master/screenshot.png)
