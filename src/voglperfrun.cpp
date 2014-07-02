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

//$ TODO: Only msgsnd length of buffer when strings are involved.
//$ TODO: limit text length in message field in index.html?

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <argp.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/msg.h>

#include <histedit.h>
#include <pthread.h>

#include <algorithm>

#include "voglperf.h"
#include "voglutils.h"

#define F_DRYRUN         0x00000001
#define F_LDDEBUGSPEW    0x00000002
#define F_XTERM          0x00000004
#define F_VERBOSE        0x00000008
#define F_FPSPRINT       0x00000010
#define F_FPSSHOW        0x00000020
#define F_DEBUGGERPAUSE  0x00000040
#define F_LOGFILE        0x00000080
#define F_QUIT           0x00010000

static struct voglperf_options_t
{
    const char *name;
    int key;
    bool launch_setting; // Setting is only valid while launching.
    int flag;
    const char *desc;
} g_options[] =
{
    { "logfile"        , 'l' , true,  F_LOGFILE       , "Frame time logging on."                       },
    { "verbose"        , 'v' , false, F_VERBOSE       , "Verbose output."                              },
    { "fpsprint"       , 'f' , false, F_FPSPRINT      , "Print fps summary every second."              },
    { "fpsshow"        , 's' , false, F_FPSSHOW       , "Show fps in game."                            },
    { "dry-run"        , 'y' , true,  F_DRYRUN        , "Only echo commands which would be executed."  },
    { "ld-debug"       , 'd' , true,  F_LDDEBUGSPEW   , "Add LD_DEBUG=lib to game launch."             },
    { "xterm"          , 'x' , true,  F_XTERM         , "Launch game under xterm."                     },
    { "debugger-pause" , 'g' , true,  F_DEBUGGERPAUSE , "Pause the game in libvoglperf.so on startup." },
};

struct voglperf_data_t
{
    voglperf_data_t(const std::string& ipaddr_in, const std::string& port_in)
    {
        ipaddr = ipaddr_in;
        port = port_in;

        msqid = -1;
        flags = 0;

        run_data.pid = (uint64_t)-1;
        run_data.file = NULL;
        run_data.fileid = -1;
        run_data.is_local_file = false;
    }

    int msqid;              // Message queue id. Used to communicate with libvoglperf.so.
    std::string ipaddr;     // Web IP address.
    std::string port;       // Web port.

    unsigned int flags;     // Command line flags (F_DRYRUN, F_XTERM, etc.)
    std::string logfile;    // Logfile name.

    std::string gameid;     // Steam game id or local executable name.
    std::string game_args;  // Arguments for the "gameid" when it's a local executable.

    struct run_data_t
    {
        uint64_t pid;           // Pid of running app (or -1).
        FILE *file;             // popen file handle.
        int fileid;             // popen file id.
        bool is_local_file;     // true if we're launching a local file, false if it's a steam game.
        std::string game_name;  // game name or "gameid##" if not known.
        std::string launch_cmd; // Game launch command.
    } run_data;

    // Commands from user.
    std::vector<std::string> commands;

    // Array of game ids and names read from appids.txt.
    std::vector<gameid_t> installed_games;

    // Commands from our background thread - must be accessed with mutex.
    pthread_mutex_t lock;
    std::vector<std::string> thread_commands;
};

//----------------------------------------------------------------------------------------------------------------------
// parse_options
//----------------------------------------------------------------------------------------------------------------------
static error_t parse_options(int key, char *arg, struct argp_state *state)
{
    voglperf_data_t *arguments = (voglperf_data_t *)state->input;
    extern char *program_invocation_short_name;

    // Check g_options array for this key.
    for (size_t i = 0; i < sizeof(g_options) / sizeof(g_options[0]); i++)
    {
        if (g_options[i].key == key)
        {
            arguments->flags |= g_options[i].flag;
            return 0;
        }
    }

    switch (key)
    {
    case 0:
        if (arg && arg[0])
        {
            if (arguments->gameid.size())
            {
                // These are arguments to our program, e.g. if the user wants to run:
                //   "voglperfrun -- glxgears -info",
                // we get "-info".
                arguments->game_args += std::string("\"") + state->argv[state->next - 1] + "\" ";
            }
            else
            {
                arguments->gameid = arg;
            }
        }
        break;

    case '?':
        printf("\nUsage:\n");
        printf("  %s [options] [SteamGameID | ExecutableName]\n", program_invocation_short_name);

        printf("\n");
        argp_state_help(state, stdout, ARGP_HELP_LONG);

        printf("\nGameIDS (please see the appids.txt file to modify this list):\n");
        for (size_t i = 0; i < arguments->installed_games.size(); i++)
        {
            printf("  %-6u - %s\n", arguments->installed_games[i].id, arguments->installed_games[i].name.c_str());
        }

        printf("\n");
        printf("To view frametime graph with gnpulot:\n");
        printf("  gnuplot -p -e 'set terminal wxt size 1280,720;set ylabel \"milliseconds\";set yrange [0:100]; plot \"FILENAME\" with lines'\n");
        printf("\n");
        printf("Create frametime graph png file:\n");
        printf("  gnuplot -p -e 'set output \"blah.png\";set terminal pngcairo size 1280,720 enhanced;set ylabel \"milliseconds\";set yrange [0:100]; plot \"FILENAME\" with lines'\n");

        exit(0);

    case -2:
        // --show-type-list: Whitespaced list of options for bash autocomplete.
        for (size_t i = 0;; i++)
        {
            const char *name = state->root_argp->options[i].name;
            if (!name)
                break;

            printf("--%s ", name);
        }
        exit(0);
    }

    return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// update_app_output
//----------------------------------------------------------------------------------------------------------------------
static void update_app_output(voglperf_data_t &data, bool close_pipe = false)
{
    if (!data.run_data.file)
        return;

    for(;;)
    {
        char buf[4096 + 1];

        // Try to read from command pipe.
        ssize_t r = read(data.run_data.fileid, buf, sizeof(buf) - 1);

        if ((r == -1) && (errno == EAGAIN))
        {
            // No data.
            break;
        }
        else if (r > 0)
        {
            // Got some data.
            buf[r] = 0;
            webby_ws_printf("%s\n", buf);
        }
        else
        {
            // Pipe is closed:
            close_pipe = true;
            break;
        }
    }

    if (close_pipe)
    {
        pclose(data.run_data.file);
        data.run_data.file = NULL;
        data.run_data.fileid = -1;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// game_start_init_launch_cmd
//----------------------------------------------------------------------------------------------------------------------
static bool game_start_init_launch_cmd(voglperf_data_t &data)
{
    if (!data.gameid.size())
    {
        webby_ws_printf("ERROR: Gameid must be set to launch game.\n");
        return false;
    }

    data.run_data.is_local_file = false;

    // Check if gameid is name of local file on drive.
    if (access(data.gameid.c_str(), F_OK) == 0)
    {
        char filename[PATH_MAX];
        if (realpath(data.gameid.c_str(), filename))
        {
            // This is a local executable.
            data.run_data.is_local_file = true;
            data.gameid = filename;
        }
    }

    // We couldn't find the local file so check if it's in our steam gameid list.
    if (!data.run_data.is_local_file)
    {
        // lower case what the user gave us.
        std::transform(data.gameid.begin(), data.gameid.end(), data.gameid.begin(), ::tolower);

        // Try to find the name and map it back to the steam id.
        for (size_t i = 0; i < data.installed_games.size(); i++)
        {
            std::string name = data.installed_games[i].name;

            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name == data.gameid)
            {
                data.gameid = string_format("%u", data.installed_games[i].id);
                break;
            }
        }
    }

    if (!data.run_data.is_local_file)
    {
        // Not a local file, so we should have a real steam game id.
        unsigned int gameid = atoi(data.gameid.c_str());
        if (!gameid)
        {
            webby_ws_printf("ERROR: Could not find game number for %s.\n", data.gameid.c_str());
            return false;
        }

        // Go through our list of games and try to find the game name and print that out also.
        data.run_data.game_name = "gameid" + data.gameid;
        for (size_t i = 0; i < data.installed_games.size(); i++)
        {
            if (gameid == data.installed_games[i].id)
            {
                data.run_data.game_name = data.installed_games[i].name;
                break;
            }
        }

        webby_ws_printf("\nGameID: %u (%s)\n", gameid, data.run_data.game_name.c_str());
    }
    else
    {
        // Local file - just print the exe name.
        data.run_data.game_name = basename((char *)data.gameid.c_str());
        webby_ws_printf("\nGame: %s\n", data.gameid.c_str());
    }

    // Set up LD_PRELOAD string.
    std::string LD_PRELOAD = get_ld_preload_str("./libvoglperf32.so", "./libvoglperf64.so", !!(data.flags & F_LDDEBUGSPEW));
    webby_ws_printf("\n%s\n", LD_PRELOAD.c_str());

    // set up VOGLPERF_CMD_LINE string
    std::string VOGL_CMD_LINE = "VOGLPERF_CMD_LINE=\"";

    // Hand out our message queue id so we can get framerate data back, etc.
    VOGL_CMD_LINE += string_format("--msqid=%u ", data.msqid);

    // When the logfile starts, we should get a message and it will record the name here.
    data.logfile = "";
    if (data.flags & F_LOGFILE)
    {
        std::string logfile = get_logfile_name(data.run_data.game_name);
        VOGL_CMD_LINE += "--logfile='" + logfile + "'";
    }

    if (data.flags & F_FPSSHOW)
        VOGL_CMD_LINE += " --showfps";
    if (data.flags & F_DEBUGGERPAUSE)
        VOGL_CMD_LINE += " --debugger-pause";
    if (data.flags & F_VERBOSE)
        VOGL_CMD_LINE += " --verbose";

    VOGL_CMD_LINE += "\"";

    webby_ws_printf("\n%s\n", VOGL_CMD_LINE.c_str());

    // Build entire launch_cmd string.
    if (!data.run_data.is_local_file)
    {
        std::string steam_cmd;

        // Add xterm string.
        if (data.flags & F_XTERM)
            steam_cmd = "xterm -geom 120x80+20+20 -e ";

        steam_cmd += "%command%";

        // set up steam string
        std::string steam_str = "steam steam://run/" + data.gameid + "//";
        std::string steam_args = VOGL_CMD_LINE + " " + LD_PRELOAD + " " + steam_cmd;

        data.run_data.launch_cmd = steam_str + url_encode(steam_args);

        // Spew this whole mess out.
        webby_ws_printf("\nSteam url string:\n  %s%s\n", steam_str.c_str(), steam_args.c_str());
    }
    else
    {
        data.run_data.launch_cmd = VOGL_CMD_LINE + " " + LD_PRELOAD + " \"" + data.gameid + "\" " + data.game_args;
    }

    webby_ws_printf("\nLaunch string:\n  %s\n", data.run_data.launch_cmd.c_str());
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// game_stop
//----------------------------------------------------------------------------------------------------------------------
static void game_stop(voglperf_data_t &data)
{
    if (data.run_data.pid == (uint64_t)-1)
    {
        webby_ws_printf("ERROR: Game not running.\n");
        return;
    }

    webby_ws_printf("Exiting game...\n");

    int ret = kill(data.run_data.pid, SIGTERM);
    webby_ws_printf("signal(%" PRIu64 ", SIGTERM): %s\n", data.run_data.pid,
                    (ret ? strerror(errno) : "Success"));

    //$ TODO: Send SIGKILL if the above didn't work?
}

//----------------------------------------------------------------------------------------------------------------------
// game_start
//----------------------------------------------------------------------------------------------------------------------
static void game_start(voglperf_data_t &data)
{
    if (data.run_data.pid != (uint64_t)-1)
    {
        webby_ws_printf("ERROR: Game already running.\n");
        return;
    }

    // Make sure we've closed our app handles.
    update_app_output(data, true);

    // Set up the launch_cmd string.
    if (!game_start_init_launch_cmd(data))
        return;

    // If we're dry running it, bail now.
    if (data.flags & (F_DRYRUN | F_QUIT))
        return;

    // Launch game.
    data.run_data.file = popen((data.run_data.launch_cmd + " 2>&1").c_str(), "r");
    if (!data.run_data.file)
    {
        webby_ws_printf("ERROR: peopen(%s) failed: %s\n", data.run_data.launch_cmd.c_str(), strerror(errno));
        return;
    }

    // Set FILE to non-blocking.
    data.run_data.fileid = fileno(data.run_data.file);
    fcntl(data.run_data.fileid, F_SETFL, O_NONBLOCK);

    // Grab app output.
    update_app_output(data);

    // Try to get the MSGTYPE_PID message for ~ 30 seconds.
    int time = 30000;

    webby_ws_printf("Waiting for child process to start...\n");
    while ((time >= 0) && !(data.flags & F_QUIT))
    {
        struct mbuf_pid_t mbuf;

        usleep(500 * 1000);
        time -= 500;

        if (msgrcv(data.msqid, &mbuf, sizeof(mbuf), MSGTYPE_PID_NOTIFY, IPC_NOWAIT) != -1)
        {
            data.run_data.pid = mbuf.pid;
            break;
        }

        // Grab app output.
        update_app_output(data);

        // Check for user typing quit command.
        webby_update(&data.commands, NULL);
        for (size_t i = 0; i < data.commands.size(); i++)
        {
            if (data.commands[i] == "quit" || data.commands[i] == "q" || data.commands[i] == "exit")
            {
                data.flags |= F_QUIT;
                break;
            }
        }
    }

    if (data.run_data.pid == (uint64_t)-1)
    {
        webby_ws_printf("ERROR: Could not retrieve pid of launched game.\n");

        // Close our game pipe handles.
        update_app_output(data, true);
    }
    else
    {
        std::string banner(78, '#');

        webby_ws_printf("\n%s\n", banner.c_str());
        webby_ws_printf("Voglperf launched pid %" PRIu64 ".\n", data.run_data.pid);
        webby_ws_printf("%s\n", banner.c_str());
    }
}

//----------------------------------------------------------------------------------------------------------------------
// get_vogl_status_str
//----------------------------------------------------------------------------------------------------------------------
static std::string get_vogl_status_str(voglperf_data_t &data)
{
    std::string status_str = string_format("Gameid: '%s'\n", data.gameid.c_str());

    status_str += string_format("  WS Connections: %u\n", webby_ws_get_connection_count());

    if (data.run_data.pid != (uint64_t)-1)
    {
        status_str += string_format("  Game: %s\n", data.run_data.game_name.c_str());
        status_str += string_format("  Logfile: '%s'\n", data.logfile.c_str());
        status_str += string_format("  Pid: %" PRIu64 "\n", data.run_data.pid);
        status_str += data.run_data.launch_cmd;
    }

    if (data.game_args.size())
        status_str += string_format("  Game Args: %s\n", data.game_args.c_str());

    std::string launch_str(" (Launch option)");
    for (size_t i = 0; i < sizeof(g_options) / sizeof(g_options[0]); i++)
    {
        status_str += string_format("  %s: %s%s\n", g_options[i].name, (data.flags & g_options[i].flag) ? "On" : "Off",
                                    g_options[i].launch_setting ? launch_str.c_str() : "");
    }

    return status_str;
}

//----------------------------------------------------------------------------------------------------------------------
// process_commands
//----------------------------------------------------------------------------------------------------------------------
static void process_commands(voglperf_data_t &data)
{
    static const char *s_commands[] =
    {
        "game start [steamid | filename]: Start game.",
        "game stop: Send SIGTERM signal to game.",

        "game set (steamid | filename): Set gameid to launch.",
        "game args: set game arguments.",

        "logfile start [seconds]: Start capturing frame time data to filename.",
        "logfile stop: Stop capturing frame time data.",

        "status: Print status and options.",
        "quit: Quit voglperfrun.",
    };

    for (size_t i = 0; i < data.commands.size(); i++)
    {
        bool handled = false;
        std::string ws_reply;
        std::vector<std::string> args;
        std::string &command = data.commands[i];

        printf("> %s\n", command.c_str());

        string_split(args, command, " ");

        bool on = (args[1] == "on" || args[1] == "1");
        bool off = (args[1] == "off" || args[1] == "0");
        if (on && (data.run_data.pid != (uint64_t)-1) && (args[0] == "logfile"))
        {
            // Special case someone typing "logfile on" while the game is running.
            // Turn it into a "logfile start" command.
            args[1] = "start";
        }
        else if (!args[1].size() || on || off)
        {
            unsigned int flags_orig = data.flags;

            for (size_t j = 0; j < sizeof(g_options) / sizeof(g_options[0]); j++)
            {
                if (args[0] == g_options[j].name)
                {
                    if (on)
                        data.flags |= g_options[j].flag;
                    else if(off)
                        data.flags &= ~g_options[j].flag;

                    ws_reply += string_format("%s: %s\n", g_options[j].name, (data.flags & g_options[j].flag) ? "On" : "Off");

                    // This is a launch option and the game is already running - warn them.
                    if (on && g_options[j].launch_setting && (data.run_data.pid != (uint64_t)-1))
                        ws_reply += "  Option used with next game launch...\n";

                    handled = true;
                }
            }

            if (data.run_data.pid != (uint64_t)-1)
            {
                // If the verbose or fpsshow args have changed, send msg.
                if ((flags_orig ^ data.flags) & (F_VERBOSE | F_FPSSHOW))
                {
                    mbuf_options_t mbuf;

                    mbuf.mtype = MSGTYPE_OPTIONS;
                    mbuf.fpsshow = !!(data.flags & F_FPSSHOW);
                    mbuf.verbose = !!(data.flags & F_VERBOSE);

                    int ret = msgsnd(data.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), IPC_NOWAIT);
                    if (ret == -1)
                    {
                        ws_reply += string_format("ERROR: msgsnd failed: %s\n", strerror(errno));
                    }
                }
            }
        }

        if (handled)
        {
            // Handled with g_options above...
        }
        else if (args[0] == "status")
        {
            ws_reply += get_vogl_status_str(data);

            handled = true;
        }
        else if (args[0] == "help")
        {
            ws_reply += "Commands:\n";

            for (size_t j = 0; j < sizeof(s_commands) / sizeof(s_commands[0]); j++)
                ws_reply += string_format("  %s\n", s_commands[j]);

            for (size_t j = 0; j < sizeof(g_options) / sizeof(g_options[0]); j++)
                ws_reply += string_format("  %s [on | off]: %s\n", g_options[j].name, g_options[i].desc);

            handled = true;
        }
        else if (args[0] == "quit" || args[0] == "q" || args[0] == "exit")
        {
            data.flags |= F_QUIT;
            ws_reply += "Quitting...\n";

            handled = true;
        }
        else if (args[0] == "game")
        {
            if (args[1] == "args")
            {
                size_t pos = command.find("args") + 5;
                data.game_args = command.substr(pos);
                
                handled = true;
            }
            else if ((args[1] == "set") && args[2].size())
            {
                data.gameid = args[2];
                ws_reply += "Gameid set to '" + data.gameid + "'";

                handled = true;
            }
            else if (args[1] == "start")
            {
                if (args[2].size())
                    data.gameid = args[2];

                game_start(data);

                handled = true;
            }
            else if (args[1] == "stop")
            {
                game_stop(data);

                handled = true;
            }
        }
        else if (args[0] == "logfile")
        {
            if (data.run_data.pid == (uint64_t)-1)
            {
                ws_reply += "ERROR: Game not running.\n";

                handled = true;
            }
            else if (args[1] == "start")
            {
                mbuf_logfile_start_t mbuf;
                std::string logfile = get_logfile_name(data.run_data.game_name);

                mbuf.mtype = MSGTYPE_LOGFILE_START;

                strncpy(mbuf.logfile, logfile.c_str(), sizeof(mbuf.logfile));
                mbuf.logfile[sizeof(mbuf.logfile) - 1] = 0;
                mbuf.time = (uint64_t)atoi(args[2].c_str());

                int ret = msgsnd(data.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), IPC_NOWAIT);
                if (ret == -1)
                {
                    ws_reply += string_format("ERROR: msgsnd failed: %s\n", strerror(errno));
                }

                handled = true;
            }
            else if (args[1] == "stop")
            {
                mbuf_logfile_stop_t mbuf;

                mbuf.mtype = MSGTYPE_LOGFILE_STOP;
                mbuf.logfile[0] = 0;

                int ret = msgsnd(data.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), IPC_NOWAIT);
                if (ret == -1)
                {
                    ws_reply += string_format("ERROR: msgsnd failed: %s\n", strerror(errno));
                }

                handled = true;
            }
        }

        if (!handled)
        {
            ws_reply += string_format("ERROR: Unknown command '%s'.\n", command.c_str());
        }

        if (ws_reply.size())
        {
            webby_ws_write_buffer(NULL, ws_reply.c_str(), ws_reply.size());
        }
    }

    data.commands.clear();
}

//----------------------------------------------------------------------------------------------------------------------
// update_app_messages
//----------------------------------------------------------------------------------------------------------------------
static void update_app_messages(voglperf_data_t &data)
{
    if (data.run_data.pid == (uint64_t)-1)
        return;

    bool app_finished = false;

    // Try to get FPS messages.
    struct mbuf_fps_t mbuf_fps;
    int ret = msgrcv(data.msqid, &mbuf_fps, sizeof(mbuf_fps) - sizeof(mbuf_fps.mtype), MSGTYPE_FPS_NOTIFY, IPC_NOWAIT);
    if (ret != -1)
    {
        if (mbuf_fps.frame_count == (uint32_t)-1)
        {
            // Frame count of -1 comes in when game exits.
            app_finished = true;
        }
        else  if (data.flags & F_FPSPRINT)
        {
            webby_ws_printf("%.2f fps frames:%u time:%.2fms min:%.2fms max:%.2fms\n",
                            mbuf_fps.fps, mbuf_fps.frame_count, mbuf_fps.frame_time, mbuf_fps.frame_min, mbuf_fps.frame_max);
        }
    }

    struct mbuf_logfile_start_t mbuf_start;
    ret = msgrcv(data.msqid, &mbuf_start, sizeof(mbuf_start) - sizeof(mbuf_start.mtype), MSGTYPE_LOGFILE_START_NOTIFY, IPC_NOWAIT);
    if (ret != -1)
    {
        std::string time = mbuf_start.time ? string_format(" (%" PRId64 " seconds).", mbuf_start.time) : "";

        webby_ws_printf("Logfile started: %s%s\n", mbuf_start.logfile, time.c_str());
        data.logfile = mbuf_start.logfile;
    }

    struct mbuf_logfile_stop_t mbuf_stop;
    ret = msgrcv(data.msqid, &mbuf_stop, sizeof(mbuf_stop) - sizeof(mbuf_stop.mtype), MSGTYPE_LOGFILE_STOP_NOTIFY, IPC_NOWAIT);
    if (ret != -1)
    {
        std::string url = string_format("http://%s:%s/logfile%s\n", data.ipaddr.c_str(), data.port.c_str(), mbuf_stop.logfile);

        webby_ws_printf("Logfile stopped: <a href=\"%s\">%s</a>\n", url.c_str(), url.c_str());
        data.logfile = "";
    }

    // Check if the app has finished.
    std::string proc_status_file = string_format("/proc/%" PRIu64 "/status", data.run_data.pid);

    if (app_finished || (access(proc_status_file.c_str(), F_OK) != 0))
    {
        // Close handles, etc.
        update_app_output(data, true);

        // Set pid back to -1.
        data.run_data.pid = (uint64_t)-1;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_connected_callback
//----------------------------------------------------------------------------------------------------------------------
static std::string webby_connected_callback(void *user_data)
{
    voglperf_data_t *data = (voglperf_data_t *)user_data;

    return get_vogl_status_str(*data);
}

//----------------------------------------------------------------------------------------------------------------------
// webby_uri_dispatch_callback
//----------------------------------------------------------------------------------------------------------------------
static std::string webby_uri_dispatch_callback(const char *request_uri, void *user_data)
{
    static const char logfile_prefix[] = "/logfile";
    static const std::string logfile_str = string_format("%s%s/voglperf.", logfile_prefix, P_tmpdir);

    if (!strncmp(logfile_str.c_str(), request_uri, logfile_str.size()))
    {
        std::string file = get_file_contents(request_uri + sizeof(logfile_prefix) - 1);
        return file;
    }

    return "";
}

/* To print out the prompt you need to use a function.  This could be
   made to do something special, but I opt to just have a static prompt. */
static const char * prompt(EditLine *e)
{
    static const char *prompt_str = "";
    return prompt_str;
}

static void cleanup_handler(void *arg)
{
    EditLine *el = (EditLine *)arg;
    el_end(el);
}

static void *editline_threadproc(void *arg)
{
    HistEvent ev;
    EditLine *el; /* This holds all the state for our line editor */
    History *myhistory; /* This holds the info for our history */
    voglperf_data_t *data = (voglperf_data_t *)arg;

    /* Initialize the EditLine state to use our prompt function and
       emacs style editing. */
    el = el_init("voglperfun", stdin, stdout, stderr);
    el_set(el, EL_PROMPT, &prompt);
    el_set(el, EL_EDITOR, "vi");

    /* Initialize the history */
    myhistory = history_init();
    if (myhistory == 0)
    {
        fprintf(stderr, "history could not be initialized\n");
        return (void *)-1;
    }

    /* Set the size of the history */
    history(myhistory, &ev, H_SETSIZE, 800);

    /* This sets up the call back functions for history functionality */
    el_set(el, EL_HIST, history, myhistory);

    pthread_cleanup_push(cleanup_handler, el);

    while (!(data->flags & F_QUIT))
    {
        /* count is the number of characters read.
           line is a const char* of our command line with the tailing \n */
        int count;
        const char *line = el_gets(el, &count);

        /* In order to use our history we have to explicitly add commands
           to the history */
        if (count > 0)
        {
            size_t line_len = strlen(line);

            while ((line_len > 0) && isspace(line[line_len - 1]))
                line_len--;

            if (line_len)
            {
                std::string command(line, line_len);

                pthread_mutex_lock(&data->lock);
                data->thread_commands.push_back(command);
                pthread_mutex_unlock(&data->lock);

                history(myhistory, &ev, H_ENTER, line);
            }
        }
    }

    pthread_cleanup_pop(0);

    /* Clean up our memory */
    history_end(myhistory);
    el_end(el);
    return NULL;
}

//----------------------------------------------------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    voglperf_data_t data(get_ip_addr(), "8081");

    parse_appid_file(data.installed_games);

    /*
     * Parse command line.
     */
    std::vector<struct argp_option> argp_options;

    // Add argp options.
    for (size_t i = 0; i < sizeof(g_options) / sizeof(g_options[0]); i++)
    {
        struct argp_option opt;

        memset(&opt, 0, sizeof(opt));
        opt.name = g_options[i].name;
        opt.key = g_options[i].key;
        opt.doc = g_options[i].desc;
        opt.group = 1;

        argp_options.push_back(opt);
    }

    static const struct argp_option s_options[] =
    {
        { "ipaddr"         , 'i' , "IPADDR" , 0 , "Web IP address."                                                         , 2 },
        { "port"           , 'p' , "PORT"   , 0 , "Web port."                                                               , 2 },

        { "show-type-list" , -2  , 0        , 0 , "Produce list of whitespace-separated words used for command completion." , 3 },
        { "help"           , '?' , 0        , 0 , "Print this help message."                                                , 3 },

        { 0, 0, 0, 0, NULL, 0 }
    };
    argp_options.insert(argp_options.end(), s_options, s_options + sizeof(s_options) / sizeof(s_options[0]));

    struct argp argp = { &argp_options[0], parse_options, 0, "Vogl perf launcher.", NULL, NULL, NULL };
    argp_parse(&argp, argc, argv, ARGP_NO_HELP, 0, &data);

    /*
     * Initialize our message queue used to communicate with our hook.
     */
    data.msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (data.msqid == -1)
    {
        errorf("ERROR: msgget() failed: %s\n", strerror(errno));
    }

    /*
     * Start our web server...
     */
    webby_init_t init;

    init.bind_address = data.ipaddr.c_str();
    init.port = (unsigned short)atoi(data.port.c_str());
    init.user_data = &data;
    init.verbose = !!(data.flags & F_VERBOSE);
    init.ws_connected_pfn = webby_connected_callback;
    init.uri_dispatch_pfn = webby_uri_dispatch_callback;

    webby_start(init);

    pthread_mutex_init(&data.lock, NULL);

    pthread_t threadid = (pthread_t)-1;
    if (pthread_create(&threadid, NULL, &editline_threadproc, (void *)&data) != 0)
        printf("WARNING: pthread_create failed: %s\n", strerror(errno));

    /*
     * Main loop.
     */

    // If we were specified a game to start on the command line, then start it
    //  and exit after it finishes.
    bool quit_on_game_exit = !!data.gameid.size();
    if (quit_on_game_exit)
    {
        data.commands.push_back("status");
        data.commands.push_back("game start");
    }

    while (!(data.flags & F_QUIT))
    {
        struct timeval timeout;

        // Handle commands from stdin.
        if (data.thread_commands.size())
        {
            pthread_mutex_lock(&data.lock);
            data.commands.insert(data.commands.end(), data.thread_commands.begin(), data.thread_commands.end());
            data.thread_commands.clear();
            pthread_mutex_unlock(&data.lock);
        }

        // Have Webby wait .5s unless there are commands to execute.
        timeout.tv_sec = 0;
        timeout.tv_usec = data.commands.size() ? 5 : 500 * 1000;

        // Update web page.
        webby_update(&data.commands, &timeout);

        // Handle any commands.
        process_commands(data);

        // Grab output if game is running.
        update_app_output(data);

        // Grab messages from running game.
        update_app_messages(data);

        if (quit_on_game_exit && (data.run_data.pid == (uint64_t)-1))
            data.commands.push_back("quit");
    }

    /*
     * Shutdown.
     */
    webby_ws_printf("\nDone.\n");

    if (threadid != (pthread_t)-1)
    {
        void *status = NULL;
        pthread_cancel(threadid);
        pthread_join(threadid, &status);
    }

    // Update and terminate web server.
    webby_update(&data.commands, NULL);
    webby_end();

    pthread_mutex_destroy(&data.lock);

    // Destroy our message queue.
    msgctl(data.msqid, IPC_RMID, NULL);
    data.msqid = -1;
    return 0;
}
