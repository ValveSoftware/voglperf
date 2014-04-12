/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

enum
{
    // _NOTIFY messages sent from hook to voglperfrun.
    MSGTYPE_PID_NOTIFY = 1,
    MSGTYPE_FPS_NOTIFY = 2,
    MSGTYPE_LOGFILE_START_NOTIFY = 3,
    MSGTYPE_LOGFILE_STOP_NOTIFY = 4,
    // Messages send from voglperfrun to hook.
    MSGTYPE_LOGFILE_START = 5,
    MSGTYPE_LOGFILE_STOP = 6,
    MSGTYPE_OPTIONS = 7
};

struct mbuf_pid_t
{
    long mtype; // MSGTYPE_PID
    uint64_t pid;
};

struct mbuf_fps_t
{
    long mtype; // MSGTYPE_FPS
    float fps;
    uint32_t frame_count;
    float frame_time;
    float frame_min;
    float frame_max;
};

struct mbuf_logfile_start_t
{
    long mtype; // MSGTYPE_LOGFILE_START
    uint64_t time;
    char logfile[PATH_MAX];
};

struct mbuf_logfile_stop_t
{
    long mtype; // MSGTYPE_LOGFILE_STOP
    char logfile[PATH_MAX];
};

struct mbuf_options_t
{
    long mtype; // MSGTYPE_OPTIONS
    uint16_t fpsshow;
    uint16_t verbose;
};
