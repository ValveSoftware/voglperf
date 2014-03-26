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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <argp.h>
#include <signal.h>

#include <fcntl.h>
#include <sys/msg.h>

#include <ifaddrs.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <net/if.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "webby/webby.h"

#include "voglperf.h"

#define F_DRYRUN        0x00000001
#define F_LDDEBUGSPEW   0x00000002
#define F_XTERM         0x00000004
#define F_VERBOSE       0x00000008
#define F_FPS           0x00000010

//$ TODO: limit text length in message field in index.html?

struct gameid_t
{
    unsigned int id;        // Steam game id.
    std::string name;       // Steam game name.
};

struct arguments_t
{
    unsigned int flags;     // Command line flags (F_DRYRUN, F_XTERM, etc.)
    std::string cmdline;    // Command line arguments for VOGL_CMD_LINE (--showfps, etc.)
    std::string logfile;    // Logfile name.
    std::string gameid;     // Game id from command line.
    std::string prog_args;  // arguments for the "gameid"

    // Array of game ids and names. Usually read from appids.txt.
    bool appids_file_found;
    std::vector<gameid_t> installed_games;
};

struct launch_data_t
{
    int msqid;              // Our message queue id. Used to communicate with libvoglperf.so.
    bool is_local_file;     // true if we're launching a local file, false if it's a steam game.

    std::string ipaddr;
    std::string port;

    arguments_t args;       // Command line arguments.
    std::string gameid_str; // game name or "gameid##" if not known.
    std::string LD_PRELOAD; // LD_PRELOAD string.
    std::string VOGL_CMD_LINE; // VOGL_CMD_LINE string.
    std::string launch_cmd; // Game launch command.
    std::string prog_args;  // args passed to the program to benchmark
};

struct webby_data_t
{
    // Maximum WebSocket connections.
    enum { MAX_WSCONN = 8 };
    std::vector<WebbyConnection *> ws_connections;
    
    // Command strings sent from websocket interfaces.
    std::vector<std::string> ws_commands;

    void *memory;
    int memory_size;
    struct WebbyServer *server;
    struct WebbyServerConfig config;
};
static webby_data_t g_webby_data;

//----------------------------------------------------------------------------------------------------------------------
// Spew out error and die.
//----------------------------------------------------------------------------------------------------------------------
static void __attribute__ ((noreturn)) errorf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    exit(-1);
}

//----------------------------------------------------------------------------------------------------------------------
// Read in file.
//----------------------------------------------------------------------------------------------------------------------
static std::string get_file_contents(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        std::string str;

        fseek(fp, 0, SEEK_END);
        str.resize(ftell(fp));
        rewind(fp);

        fread(&str[0], 1, str.size(), fp);
        fclose(fp);
        return str;
    }

    return "";
}

//----------------------------------------------------------------------------------------------------------------------
// Printf style formatting for std::string.
//----------------------------------------------------------------------------------------------------------------------
static std::string string_format(const char *fmt, ...) 
{
    std::string str; 
    int size = 256; 

    for (;;) 
    {    
        va_list ap;

        va_start(ap, fmt);
        str.resize(size);
        int n = vsnprintf((char *)str.c_str(), size, fmt, ap); 
        va_end(ap);

        if ((n > -1) && (n < size))
        {
            str.resize(n);
            return str; 
        }

        size = (n > -1) ? (n + 1) : (size * 2);
    }    

    return str; 
}

//----------------------------------------------------------------------------------------------------------------------
// string_split: split str by delims into args.
//   http://stackoverflow.com/questions/53849/how-do-i-tokenize-a-string-in-c 
//----------------------------------------------------------------------------------------------------------------------
static void string_split(std::vector<std::string>& args, const std::string& str, const std::string& delims)
{
    size_t start = 0;
    size_t end = 0;

    while (end != std::string::npos)
    {
        end = str.find(delims, start);

        // If at end, use length = maxLength, else use length = end - start.
        args.push_back(str.substr(start, (end == std::string::npos) ? std::string::npos : end - start));

        // If at end, use start = maxSize, else use start = end + delimiter.
        start = ((end > (std::string::npos - delims.size())) ? std::string::npos : end + delims.size());
    }

    // Make sure we have at least two args.
    args.push_back("");
    args.push_back("");
}

//----------------------------------------------------------------------------------------------------------------------
// parse_opt
//----------------------------------------------------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    arguments_t *arguments = (arguments_t *)state->input;
    extern char *program_invocation_short_name;

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
                arguments->prog_args += std::string(" \"") + state->argv[state->next - 1] + "\"";
            }
            else
            {
                arguments->gameid = arg;
            }
        }
        break;

    case '?':
        fprintf(stdout, "\nUsage:\n");
        fprintf(stdout, "  %s [options] [SteamGameID | ExecutableName]\n", program_invocation_short_name);

        fprintf(stdout, "\n");
        argp_state_help(state, stdout, ARGP_HELP_LONG);

        fprintf(stdout, "\nGameIDS (please see the appids.txt file to modify this list):\n");
        for (size_t i = 0; i < arguments->installed_games.size(); i++)
        {
            fprintf(stdout, "  %-6u - %s\n", arguments->installed_games[i].id, arguments->installed_games[i].name.c_str());
        }

        exit(0);

    case -1:
        arguments->cmdline += " ";
        arguments->cmdline += state->argv[state->next - 1];
        break;

    case 'l':
        // If no argument was included, set logfile to '.' and we'll create our name later.
        if (arg && (arg[0] == '='))
            arg++;
        arguments->logfile = arg ? arg : ".";
        break;

    case 'y': arguments->flags |= F_DRYRUN; break;
    case 'd': arguments->flags |= F_LDDEBUGSPEW; break;
    case 'x': arguments->flags |= F_XTERM; break;
    case 'f': arguments->flags |= F_FPS; break;
    case 'v': arguments->flags |= F_VERBOSE; break;

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
// url_encode
//----------------------------------------------------------------------------------------------------------------------
static std::string url_encode(const std::string &value)
{
    std::ostringstream escaped;

    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
        std::string::value_type c = (*i);

        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            escaped << c;
        else if (c == ' ')
            escaped << "%20";
        else
            escaped << '%' << std::setw(2) << ((int)c) << std::setw(0);
    }

    return escaped.str();
}

//----------------------------------------------------------------------------------------------------------------------
// get_full_path
//----------------------------------------------------------------------------------------------------------------------
static std::string get_full_path(const char *filename)
{
    // Get the full path to our executable.
    char exe[PATH_MAX];
    if (readlink("/proc/self/exe", exe, sizeof(exe)) <= 0)
    {
        exe[0] = '.';
        exe[1] = 0;
    }

    // Get the directory name and relative path to libvoglperf.so.
    std::string filedir = dirname(exe);
    filedir += "/";
    filedir += filename;

    char fullpath[PATH_MAX];
    if (!realpath(filedir.c_str(), fullpath))
    {
        printf("WARNING: realpath %s failed '%s.'\n", filedir.c_str(), strerror(errno));
        return filename;
    }

    if (access(fullpath, F_OK) != 0)
    {
        printf("WARNING: %s file does not exist.\n", fullpath);
        return filename;
    }

    return fullpath;
}

//----------------------------------------------------------------------------------------------------------------------
// parse_appid_file
//----------------------------------------------------------------------------------------------------------------------
static void parse_appid_file(launch_data_t &ld)
{
    // Random selection of games...
    static const gameid_t gameids[] =
    {
        { 214910, "AirConflicts" },
        { 400, "Portal1" },
        { 218060, "BitTripRunner" },
        { 570, "Dota2" },
        { 35720, "Trine2" },
        { 440, "TF2" },
        { 41070, "Sam3" },
        { 1500, "Darwinia" },
        { 550, "L4D2" },
        { 1500, "Darwinia2" },
        { 570, "Dota2Beta" },
        { 221810, "TheCave" },
        { 220200, "KerbalSpaceProgram" },
        { 44200, "GalconFusion" },
        { 201040, "GalconLegends" },
        { 25000, "Overgrowth" },
        { 211820, "Starbound" }, // 64-bit game
    };

    // Try to find our appids.txt file.
    FILE *file = fopen("appids.txt", "r");
    if (!file)
    {
        // If we're running from the voglperf project.
        file = fopen("../../vogl/src/voglperf/appids.txt", "r");
        if (!file)
        {
            char exe[PATH_MAX];
            if (readlink("/proc/self/exe", exe, sizeof(exe)) >= 0)
            {
                // Try where the binary currently exists.
                std::string filename = dirname(exe);
                filename += "/appids.txt";
                file = fopen(filename.c_str(), "r");
            }
        }
    }

    if (file)
    {
        char line[256];
        gameid_t gameid;

        // Parse the appid and game name from lines like this:
        //   AppID 400 : "Portal" : /home/mikesart/.local/share/Steam/steamapps/common/Portal 
        while (fgets(line, sizeof(line), file))
        {
            // If line starts with AppID, grab the gameid right after that.
            if (!strncmp("AppID", line, 5))
            {
                // Grab the ID and search for first quote.
                gameid.id = atoi(line + 5);
                const char *name = strchr(line, '"');
                if (gameid.id && name)
                {
                    name++;

                    const char *name_end = strchr(name, '"');
                    if (name_end)
                    {
                        gameid.name = std::string(name, name_end - name);
                        if (gameid.name.size())
                        {
                            ld.args.installed_games.push_back(gameid);
                            ld.args.appids_file_found = true;
                        }
                    }
                }
            }
        }

        fclose(file);
    }

    if (!ld.args.installed_games.size())
    {
        for (size_t i = 0; i < sizeof(gameids) / sizeof(gameids[0]); i++)
        {
            ld.args.installed_games.push_back(gameids[i]);
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// init_gameid
//----------------------------------------------------------------------------------------------------------------------
static void init_gameid(launch_data_t &ld)
{
    // If gameid isn't a number then check for local file or steam game name.
    if (atoi(ld.args.gameid.c_str()) == 0)
    {
        // If gameid is a string, check to see if it's the name of a local file.
        if (access(ld.args.gameid.c_str(), F_OK) == 0)
        {
            char filename[PATH_MAX];
            if (realpath(ld.args.gameid.c_str(), filename))
            {
                // This is a local executable.
                ld.is_local_file = true;
                ld.args.gameid = filename;
            }
        }

        // We couldn't find the local file so check if it's in our steam gameid list.
        if (!ld.is_local_file)
        {
            // lower case what the user gave us.
            std::transform(ld.args.gameid.begin(), ld.args.gameid.end(), ld.args.gameid.begin(), ::tolower);

            // Try to find the name and map it back to the id.
            for (size_t i = 0; i < ld.args.installed_games.size(); i++)
            {
                std::string name = ld.args.installed_games[i].name;

                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == ld.args.gameid)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%u", ld.args.installed_games[i].id);
                    ld.args.gameid = buf;
                    break;
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// init_gameid_str
//----------------------------------------------------------------------------------------------------------------------
static void init_gameid_str(launch_data_t &ld)
{
    if (!ld.is_local_file)
    {
        // Not a local file, so we should have a real steam game id.
        unsigned int gameid = atoi(ld.args.gameid.c_str());
        if (!gameid)
            errorf("ERROR: Could not find game number for %s.\n", ld.args.gameid.c_str());

        printf("\nGameID: %u", gameid);

        // Go through our list of games and try to find the game name and print that out also.
        ld.gameid_str = "gameid" + ld.args.gameid;
        for (size_t i = 0; i < ld.args.installed_games.size(); i++)
        {
            if (gameid == ld.args.installed_games[i].id)
            {
                printf(" (%s)", ld.args.installed_games[i].name.c_str());
                ld.gameid_str = ld.args.installed_games[i].name;
                break;
            }
        }

        printf("\n");
    }
    else
    {
        // Local file - just print the exe name.
        ld.gameid_str = basename((char *)ld.args.gameid.c_str());
        printf("\nGame: %s\n", ld.args.gameid.c_str());
    }
}

//----------------------------------------------------------------------------------------------------------------------
// get_logfile_name
//----------------------------------------------------------------------------------------------------------------------
static std::string get_logfile_name(launch_data_t &ld)
{
    char timestr[128];
    time_t t = time(NULL);

    timestr[0] = 0;
    struct tm *tmp = localtime(&t);
    if (tmp)
    {
        strftime(timestr, sizeof(timestr), "%Y_%m_%d-%H_%M_%S", tmp);
    }

    std::string gameid_str = ld.gameid_str;
    for (size_t i = 0; i < gameid_str.size(); i++)
    {
        if (isspace(gameid_str[i]) || ispunct(gameid_str[i]))
            gameid_str[i] = '-';
    }

    return string_format("%s/voglperf.%s.%s.csv", P_tmpdir, gameid_str.c_str(), timestr);
}

//----------------------------------------------------------------------------------------------------------------------
// init_logfile
//----------------------------------------------------------------------------------------------------------------------
static void init_logfile(launch_data_t &ld)
{
    // If it's a '.', then no arg was supplied and we come up with our own logfile name.
    if (ld.args.logfile == ".")
    {
        ld.args.logfile = get_logfile_name(ld);
    }

    // Get a full path for our log file as steam will launch in a different directory.
    if (ld.args.logfile.size())
    {
        char dirname[PATH_MAX];
        const char *logfile = ld.args.logfile.c_str();
        const char *filename = strrchr(logfile, '/');

        if (!filename)
        {
            // No / in the path - assume current directory.
            filename = logfile;
            dirname[0] = '.';
            dirname[1] = 0;
        }
        else
        {
            // Dirname is logfile up to filename portion.
            snprintf(dirname, sizeof(dirname), "%.*s", (int)(filename - logfile), logfile);
            filename++;
        }

        // Canonicalize the directory - which should exist.
        char realdirname[PATH_MAX];
        if (!realpath(dirname, realdirname))
            errorf("ERROR: realpath failed on directory %s: %s\n", dirname, strerror(errno));

        // Add filename (which might not exist) back on directory for full logfile path.
        snprintf(dirname, sizeof(dirname), "%s/%s", realdirname, filename);
        ld.args.logfile = dirname;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// init_LD_PRELOAD
//----------------------------------------------------------------------------------------------------------------------
static void init_LD_PRELOAD(launch_data_t &ld)
{ 
    // set up LD_PRELOAD string
    std::string voglperf32 = get_full_path("./libvoglperf32.so");
    std::string voglperf64 = get_full_path("./libvoglperf64.so");

    ld.LD_PRELOAD = "LD_PRELOAD=";

    // Add both 32 and 64-bit shared objects as we don't know what arch the target is.
    ld.LD_PRELOAD += voglperf32;
    ld.LD_PRELOAD += ":";
    ld.LD_PRELOAD += voglperf64;

    // If this is steam or LD_PRELOAD is already set, we need to append :$LD_PRELOAD.
    if (!ld.is_local_file || getenv("LD_PRELOAD"))
        ld.LD_PRELOAD += ":$LD_PRELOAD";

    // Add LD_DEBUG=lib..
    if (ld.args.flags & F_LDDEBUGSPEW)
        ld.LD_PRELOAD += " LD_DEBUG=libs";

    printf("\n%s\n", ld.LD_PRELOAD.c_str());
}

//----------------------------------------------------------------------------------------------------------------------
// init_CMD_LINE
//----------------------------------------------------------------------------------------------------------------------
static void init_CMD_LINE(launch_data_t &ld)
{
    // set up VOGLPERF_CMD_LINE string
    ld.VOGL_CMD_LINE = "VOGLPERF_CMD_LINE=\"";

    if (ld.msqid != -1)
    {
        char buf[64];

        // Hand out our message queue id so we can get framerate data back.
        snprintf(buf, sizeof(buf), "--msqid=%u ", ld.msqid);
        ld.VOGL_CMD_LINE += buf;
    }

    if (ld.args.logfile.size())
        ld.VOGL_CMD_LINE += "--logfile='" + ld.args.logfile + "'";

    ld.VOGL_CMD_LINE += ld.args.cmdline;
    ld.VOGL_CMD_LINE += "\"";

    printf("\n%s\n", ld.VOGL_CMD_LINE.c_str());
}

//----------------------------------------------------------------------------------------------------------------------
// init_launch_cmd
//----------------------------------------------------------------------------------------------------------------------
static void init_launch_cmd(launch_data_t &ld)
{
    if (!ld.is_local_file)
    {
        std::string steam_cmd;

        // Add xterm string.
        if (ld.args.flags & F_XTERM)
            steam_cmd = "xterm -geom 120x80+20+20 -e ";

        steam_cmd += "%command%";

        // set up steam string
        std::string steam_str = "steam steam://run/" + ld.args.gameid + "//";
        std::string steam_args = ld.VOGL_CMD_LINE + " " + ld.LD_PRELOAD + " " + steam_cmd;
        
        ld.launch_cmd = steam_str + url_encode(steam_args);

        // Spew this whole mess out.
        printf("\nSteam url string:\n  %s%s\n", steam_str.c_str(), steam_args.c_str());
    }
    else
    {
        ld.launch_cmd = ld.VOGL_CMD_LINE + " " + ld.LD_PRELOAD + " \"" + ld.args.gameid + "\" " + ld.prog_args;
    }

    printf("\nLaunch string:\n  %s\n", ld.launch_cmd.c_str());
}

//----------------------------------------------------------------------------------------------------------------------                                                            
// Print string to websocket connections and stdout.
//----------------------------------------------------------------------------------------------------------------------
static void webby_printf(const char *format, ...)
{
    va_list args;
    char buffer[4096];

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Print to stdout
    printf("%s", buffer);

    // And to webby...
    if (g_webby_data.ws_connections.size())
    {
        int buffer_len = (int)strlen(buffer);

        for (size_t i = 0; i < g_webby_data.ws_connections.size(); ++i)
        {
            WebbyBeginSocketFrame(g_webby_data.ws_connections[i], WEBBY_WS_OP_TEXT_FRAME);
            WebbyWrite(g_webby_data.ws_connections[i], buffer, buffer_len);
            WebbyEndSocketFrame(g_webby_data.ws_connections[i]);
        }
    }
}
   
//----------------------------------------------------------------------------------------------------------------------
// webby_log
//----------------------------------------------------------------------------------------------------------------------
static void webby_log(const char* text)
{
    printf("[webby] %s\n", text);
}

//----------------------------------------------------------------------------------------------------------------------
// webby_dispatch
//----------------------------------------------------------------------------------------------------------------------
static int webby_dispatch(struct WebbyConnection *connection)
{
    static const char logfile_prefix[] = "/logfile";
    static const std::string logfile_str = string_format("%s%s/voglperf.", logfile_prefix, P_tmpdir);

    if (!strncmp(logfile_str.c_str(), connection->request.uri, logfile_str.size()))
    {
        std::string file = get_file_contents(connection->request.uri + sizeof(logfile_prefix) - 1);

#if 1
           static const struct WebbyHeader headers[] =
           {
             { "Content-Type", "text/plain" },
           }; 
           
        WebbyBeginResponse(connection, 200, file.size(), headers, 1);
        WebbyWrite(connection, file.c_str(), file.size());
        WebbyEndResponse(connection);
#else
        size_t size = file.size();
        const char *data = file.c_str();

        printf("size: %d\n", size);
        
        WebbyBeginResponse(connection, 200, -1, NULL, 0);
        while(size)
        {
            size_t chunk = (size > 16*1024) ? 16*1024 : size;

            printf("writing chunk: %d\n", chunk);
            WebbyWrite(connection, data, chunk);
            data += chunk;
            size -= chunk;
        printf("size: %d\n", size);
        }
        WebbyEndResponse(connection);
#endif
        return 0;
    }
    else
    {
        std::string index_html = get_file_contents("index.html");

        if (index_html.size() > 0)
        {
            WebbyBeginResponse(connection, 200, index_html.size(), NULL, 0);
            WebbyWrite(connection, index_html.c_str(), index_html.size());
            WebbyEndResponse(connection);
        }
        else
        {
            WebbyBeginResponse(connection, 200, -1, NULL, 0);
            WebbyPrintf(connection, "%s\n", "ERROR: Could not read index.html");
            WebbyEndResponse(connection);
        }
        return 0;
    }

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_connect
//----------------------------------------------------------------------------------------------------------------------
static int webby_ws_connect(struct WebbyConnection *connection)
{
    // Allow websocket upgrades on /ws.
    if (!strcmp(connection->request.uri, "/ws"))
    {
        if (g_webby_data.ws_connections.size() >= webby_data_t::MAX_WSCONN)
        {
            printf("[webby] WARNING: No more websocket connections left (%d).\n", webby_data_t::MAX_WSCONN);
            return 1;
        }

        return 0;
    }

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_connected
//----------------------------------------------------------------------------------------------------------------------
static void webby_ws_connected(struct WebbyConnection *connection)
{
    g_webby_data.ws_connections.push_back(connection);

    printf("[webby] WebSocket connected %s on %s\n", connection->request.method, connection->request.uri);

    launch_data_t *ld = (launch_data_t *)connection->user_data;
    webby_printf("Welcome!\n");
    webby_printf("%s\n", ld ? ld->launch_cmd.c_str() : "ERROR: Can't determine voglperf cmd line.");
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_closed
//----------------------------------------------------------------------------------------------------------------------
static void webby_ws_closed(struct WebbyConnection *connection)
{
    printf("[webby] WebSocket closed %s on %s\n", connection->request.method, connection->request.uri);

    for (size_t i = 0; i < g_webby_data.ws_connections.size(); i++)
    {
        if (g_webby_data.ws_connections[i] == connection)
        {
            g_webby_data.ws_connections.erase(g_webby_data.ws_connections.begin() + i);
            break;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_frame
//----------------------------------------------------------------------------------------------------------------------
static int webby_ws_frame(struct WebbyConnection *connection, const struct WebbyWsFrame *frame)
{
    launch_data_t *ld = (launch_data_t *)connection->user_data;
    bool verbose = (ld && (ld->args.flags & F_VERBOSE));

    if (verbose)
    {
        printf("WebSocket frame incoming\n");
        printf("  Frame OpCode: %d\n", frame->opcode);
        printf("  Final frame?: %s\n", (frame->flags & WEBBY_WSF_FIN) ? "yes" : "no");
        printf("  Masked?     : %s\n", (frame->flags & WEBBY_WSF_MASKED) ? "yes" : "no");
        printf("  Data Length : %d\n", (int) frame->payload_length);
    }

    std::string command;
    
    int i = 0;
    while (i < frame->payload_length)
    {
        unsigned char buffer[16];
        int remain = frame->payload_length - i;
        size_t read_size = remain > (int) sizeof(buffer) ? sizeof(buffer) : (size_t)remain;
        size_t k;

        if (verbose)
        {
            printf("%08x ", (int) i);
        }

        if (0 != WebbyRead(connection, buffer, read_size))
            break;

        if (verbose)
        {
            for (k = 0; k < read_size; ++k)
                printf("%02x ", buffer[k]);

            for (k = read_size; k < 16; ++k)
                printf("   ");

            printf(" | ");

            for (k = 0; k < read_size; ++k)
                printf("%c", isprint(buffer[k]) ? buffer[k] : '?');

            printf("\n");
        }

        command += std::string((char *)buffer, read_size);
        
        i += read_size;
    }

    g_webby_data.ws_commands.push_back(command);
    return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_start
//----------------------------------------------------------------------------------------------------------------------
static int webby_start(launch_data_t &ld)
{
    memset(&g_webby_data.config, 0, sizeof(g_webby_data.config));

    g_webby_data.config.user_data = &ld;
    g_webby_data.config.bind_address = ld.ipaddr.c_str();
    g_webby_data.config.listening_port = atoi(ld.port.c_str());
    g_webby_data.config.flags = WEBBY_SERVER_WEBSOCKETS;
    g_webby_data.config.connection_max = 4;
    g_webby_data.config.request_buffer_size = 2048;
    g_webby_data.config.io_buffer_size = 8192;
    g_webby_data.config.dispatch = &webby_dispatch;
    g_webby_data.config.log = &webby_log;
    g_webby_data.config.ws_connect = &webby_ws_connect;
    g_webby_data.config.ws_connected = &webby_ws_connected;
    g_webby_data.config.ws_closed = &webby_ws_closed;
    g_webby_data.config.ws_frame = &webby_ws_frame;

    if (ld.args.flags & F_VERBOSE)
    {
        g_webby_data.config.flags |= WEBBY_SERVER_LOG_DEBUG;
    }

    g_webby_data.memory_size = WebbyServerMemoryNeeded(&g_webby_data.config);
    g_webby_data.memory = malloc(g_webby_data.memory_size);
    g_webby_data.server = WebbyServerInit(&g_webby_data.config, g_webby_data.memory, g_webby_data.memory_size);

    if (!g_webby_data.server)
    {
        fprintf(stderr, "WARNING: Web server failed to initialize.\n");
        return -1;
    }

    printf("Started http://%s:%u\n\n", g_webby_data.config.bind_address, g_webby_data.config.listening_port);
    return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_update
//----------------------------------------------------------------------------------------------------------------------
static void webby_update(launch_data_t &ld)
{
    if (g_webby_data.server)
    {
        WebbyServerUpdate(g_webby_data.server);
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_end
//----------------------------------------------------------------------------------------------------------------------
static void webby_end(launch_data_t &ld)
{
    g_webby_data.ws_connections.empty();

    if (g_webby_data.server)
    {
        WebbyServerShutdown(g_webby_data.server);
        g_webby_data.server = NULL;
    }

    if (g_webby_data.memory)
    {
        free(g_webby_data.memory);
        g_webby_data.memory = NULL;
        g_webby_data.memory_size = 0;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// retrieve_app_output
//----------------------------------------------------------------------------------------------------------------------
static FILE *retrieve_app_output(FILE *file, int fileid)
{
    if (file)
    {
        for(;;)
        {
            char buf[4096 + 1];

            // Try to read from command pipe.
            ssize_t r = read(fileid, buf, sizeof(buf) - 1);

            if ((r == -1) && (errno == EAGAIN))
            {
                // No data.
                break;
            }
            else if (r > 0)
            {
                // Got some data.
                buf[r] = 0;
                printf("%s\n", buf);
            }
            else
            {
                // Pipe is closed:
                pclose(file);
                file = NULL;
                break;
            }
        }
    }

    return file;
}

//----------------------------------------------------------------------------------------------------------------------
// handle_webby_command
//----------------------------------------------------------------------------------------------------------------------
bool handle_webby_command(uint64_t pid, launch_data_t &ld)
{
    bool ret = true;
    static const struct
    {
        const char *command;
        const char *description;
    } s_commands[] =
    {
        { "help", ": This help." },
        { "quit", ": Quit voglperfrun." },
        { "game exit", ": Send SIGTERM signal to game." },
        { "logfile start filename [seconds]", ": Start capturing frame time data to filename." },
        { "logfile stop", ": Stop capturing frame time data." },
        { "fps", "[on|off]: Turn fps logging on/off." },
    };

    for (size_t i = 0; i < g_webby_data.ws_commands.size(); i++)
    {
        bool handled = false;
        std::vector<std::string> args;

        std::string &command = g_webby_data.ws_commands[i];

        printf("WebSocket CMD: '%s'\n", command.c_str());

        string_split(args, command, " ");

        if (args[0] == "help")
        {
            for (size_t c = 0; c < g_webby_data.ws_connections.size(); ++c)
            {
                WebbyConnection *conn = g_webby_data.ws_connections[c];

                WebbyBeginSocketFrame(conn, WEBBY_WS_OP_TEXT_FRAME);

                WebbyPrintf(conn, "<pre>Commands:\n");
                for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++)
                {
                    WebbyPrintf(conn, " %s%s\n", s_commands[i].command, s_commands[i].description);
                }
                WebbyPrintf(conn, "</pre>");

                WebbyEndSocketFrame(conn);
            }

            handled = true;
        }
        else if (args[0] == "quit")
        {
            webby_printf("Quitting...\n");
            ret = false;

            handled = true;
        }
        else if ((args[0] == "game") && (args[1] == "exit"))
        {
            webby_printf("Exiting game...\n");

            int ret = kill(pid, SIGTERM);
            printf("signal(%" PRIu64 ", SIGKILL): %s\n", pid, (ret ? strerror(errno) : "Success"));

            handled = true;
        }
        else if (args[0] == "logfile")
        {
            if (args[1] == "start")
            {
                mbuf_logfile_start_t mbuf;
                std::string logfile = get_logfile_name(ld);

                mbuf.mtype = MSGTYPE_LOGFILE_START;

                strncpy(mbuf.logfile, logfile.c_str(), sizeof(mbuf.logfile));
                mbuf.logfile[sizeof(mbuf.logfile) - 1] = 0;
                mbuf.time = (uint64_t)atoi(args[2].c_str());

                int ret = msgsnd(ld.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), IPC_NOWAIT);
                if (ret == -1)
                    webby_printf("ERROR: msgsnd failed: %s\n", strerror(errno));

                handled = true;
            }
            else if (args[1] == "stop")
            {
                mbuf_logfile_stop_t mbuf;

                mbuf.mtype = MSGTYPE_LOGFILE_STOP;
                mbuf.logfile[0] = 0;

                int ret = msgsnd(ld.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), IPC_NOWAIT);
                if (ret == -1)
                    webby_printf("ERROR: msgsnd failed: %s\n", strerror(errno));

                handled = true;
            }
        }
        else if (command == "fps off")
        {
            ld.args.flags &= ~F_FPS;
            handled = true;
        }
        else if (command == "fps on")
        {
            ld.args.flags |= F_FPS;
            handled = true;
        }

        if (!handled)
        {
            webby_printf("ERROR: Unknown command '%s'.\n", command.c_str());
        }
    }
    g_webby_data.ws_commands.clear();

    return ret;
}

//----------------------------------------------------------------------------------------------------------------------
// retrieve_fps_data
//----------------------------------------------------------------------------------------------------------------------
static void retrieve_fps_data(launch_data_t &ld)
{
    // Launch it.
    FILE *file = popen((ld.launch_cmd + " 2>&1").c_str(), "r");
    if (!file)
        errorf("ERROR: peopen(%s) failed: %s\n", ld.launch_cmd.c_str(), strerror(errno));

    // Set FILE to non-blocking.
    int fileid = fileno(file);
    fcntl(fileid, F_SETFL, O_NONBLOCK);

    file = retrieve_app_output(file, fileid);
    webby_update(ld);

    if (ld.msqid != -1)
    {
        printf("\nStarting web server...\n");
        webby_start(ld);

        printf("Waiting for child process to start...\n");

        // Try to get the MSGTYPE_PID message for ~ 30 seconds.
        int sleeptime = 30000;
        uint64_t pid = (uint64_t)-1;

        while (sleeptime >= 0)
        {
            struct mbuf_pid_t mbuf;

            usleep(500 * 1000);
            sleeptime -= 500;

            if (msgrcv(ld.msqid, &mbuf, sizeof(mbuf), MSGTYPE_PID_NOTIFY, IPC_NOWAIT) != -1)
            {
                pid = mbuf.pid;
                break;
            }

            // Check web for updates.
            file = retrieve_app_output(file, fileid);
            webby_update(ld);
        }

        if (pid == (uint64_t)-1)
        {
            // If we never got the pid message then bail.
            printf(" ERROR: Child process not found. msgrcv() failed.\n");
        }
        else
        {
            // Found our child. Grab the FPS messages and spew them out.
            std::string banner(78, '#');

            printf("\n%s\n", banner.c_str());
            printf("Voglperf framerates from pid %" PRIu64 ".\n", pid);
            printf("%s\n", banner.c_str());

            char proc_status_file[PATH_MAX];
            snprintf(proc_status_file, sizeof(proc_status_file), "/proc/%" PRIu64 "/status", pid);

            for(;;)
            {
                struct mbuf_fps_t mbuf;
                struct mbuf_logfile_start_t mbuf_start;
                struct mbuf_logfile_stop_t mbuf_stop;

                // Try to get any FPS type messages.
                int ret = msgrcv(ld.msqid, &mbuf, sizeof(mbuf) - sizeof(mbuf.mtype), MSGTYPE_FPS_NOTIFY, IPC_NOWAIT);
                if (ret == -1)
                {
                    // Failed. Double check that the game is still running.
                    if (access(proc_status_file, F_OK) != 0)
                        break;

                    // Pause for half a second.
                    usleep(500 * 1000);
                }
                else
                {
                    // Frame count of -1 comes in when vogl_perf_destructor_func() is called and the
                    //  game has exited.
                    if (mbuf.frame_count == (uint32_t)-1)
                        break;

                    // Spew this message.
                    if (ld.args.flags & F_FPS)
                    {
                        webby_printf("%.2f fps frames:%u time:%.2fms min:%.2fms max:%.2fms\n",
                                     mbuf.fps, mbuf.frame_count, mbuf.frame_time, mbuf.frame_min, mbuf.frame_max);
                    }
                }

                ret = msgrcv(ld.msqid, &mbuf_start, sizeof(mbuf_start) - sizeof(mbuf_start.mtype), MSGTYPE_LOGFILE_START_NOTIFY, IPC_NOWAIT);
                if (ret != -1)
                {
                    std::string time = mbuf_start.time ? string_format(" (%" PRId64 " seconds).", mbuf_start.time) : "";
                    webby_printf("Logfile started: %s%s\n", mbuf_start.logfile, time.c_str());
                }
                    
                ret = msgrcv(ld.msqid, &mbuf_stop, sizeof(mbuf_stop) - sizeof(mbuf_stop.mtype), MSGTYPE_LOGFILE_STOP_NOTIFY, IPC_NOWAIT);
                if (ret != -1)
                {
                    std::string url = string_format("http://%s:%s/logfile%s\n", ld.ipaddr.c_str(), ld.port.c_str(), mbuf_stop.logfile);
                    webby_printf("Logfile stopped: <a href=\"%s\">%s</a>\n", url.c_str(), url.c_str());
                }

                // Check web for updates.
                file = retrieve_app_output(file, fileid);
                webby_update(ld);
                
                if (!handle_webby_command(pid, ld))
                    break;
            }

            // Destroy our message queue.            
            msgctl(ld.msqid, IPC_RMID, NULL);
            ld.msqid = -1;
        }

        while (file) 
        {
            file = retrieve_app_output(file, fileid);
            webby_update(ld);
        }

        // Update and terminate web server.
        webby_update(ld);
        webby_end(ld);
    }
    
}

//----------------------------------------------------------------------------------------------------------------------
// get_ip_addr
//   http://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine/3120382#3120382 
//----------------------------------------------------------------------------------------------------------------------
std::string get_ip_addr()
{
    std::string ret4;
    std::string ret6;
    struct ifaddrs * ifAddrStruct = NULL;

    getifaddrs(&ifAddrStruct);

    for (struct ifaddrs *ifa = ifAddrStruct; ifa; ifa = ifa->ifa_next)
    {
        if (ifa ->ifa_addr->sa_family == AF_INET)
        {
            // IP4 address.
            char addressBuffer[INET_ADDRSTRLEN];
            void *tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;

            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);

            if (!(ifa->ifa_flags & IFF_LOOPBACK) || !ret4.length())
                ret4 = addressBuffer;
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6)
        {
            // IP6 address.
            char addressBuffer[INET6_ADDRSTRLEN];
            void *tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;

            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);

            if (!(ifa->ifa_flags & IFF_LOOPBACK) || !ret6.length())
                ret6 = addressBuffer;
        } 
    }

    if (ifAddrStruct)
    {
        freeifaddrs(ifAddrStruct);
        ifAddrStruct = NULL;
    }

    // Prefer IP4?
    if (ret4.length())
        return ret4;
    else if(ret6.length())
        return ret6;

    return "127.0.0.1";
}

//----------------------------------------------------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    launch_data_t ld;

    ld.msqid = -1;
    ld.args.flags = 0;
    ld.args.appids_file_found = false;
    ld.is_local_file = false;

    ld.ipaddr = get_ip_addr();
    ld.port = "8081";

    static struct argp_option options[] =
    {
        { "showfps"        , -1  , 0          , 0                   , "Show fps in game."                                                       , 1 },
        { "logfile"        , 'l' , "FILENAME" , OPTION_ARG_OPTIONAL , "Log file to write individual frame times."                               , 1 },
        { "dry-run"        , 'y' , 0          , 0                   , "Echo which commands would be executed."                                  , 1 },

        { "fps"            , 'f' , 0          , 0                   , "Show fps summary every second."                                          , 2 },
        { "xterm"          , 'x' , 0          , 0                   , "Start game under xterm."                                                 , 2 },
        { "verbose"        , 'v' , 0          , 0                   , "Produce verbose output from libvoglperf hook."                           , 2 },
        { "ld-debug"       , 'd' , 0          , 0                   , "Add LD_DEBUG=lib to game launch."                                        , 2 },
        { "debugger-pause" , -1  , 0          , 0                   , "Pause the game in libvoglperf on startup."                               , 2 },
        { "port"           , 'p' , "PORT"     , 0                   , "Web port."                                                               , 2 },
        { "ipaddr"         , 'i' , "IPADDR"   , 0                   , "IP bin address."                                                         , 2 },

        { "show-type-list" , -2  , 0          , 0                   , "Produce list of whitespace-separated words used for command completion." , 3 },
        { "help"           , '?' , 0          , 0                   , "Print this help message."                                                , 3 },

        { 0, 0, 0, 0, NULL, 0 }
    };

    parse_appid_file(ld);

    struct argp argp = { options, parse_opt, 0, "Vogl perf launcher.", NULL, NULL, NULL };
    argp_parse(&argp, argc, argv, ARGP_NO_HELP, 0, &ld.args);

    if (!ld.args.gameid.size())
        errorf("ERROR: No application or gameid specified.\n");

    // Set up gameid and local file check.
    init_gameid(ld);

    // set up args for gameid
    ld.prog_args = ld.args.prog_args;

    // Initialize game name.
    init_gameid_str(ld);

    // Initialize logfile names and paths.
    init_logfile(ld);

    // Set up LD_PRELOAD string.
    init_LD_PRELOAD(ld);

    ld.msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR | S_IWUSR); 
    if (ld.msqid == -1)
        printf("WARNING: msgget() failed: %s\n", strerror(errno));

    // Set up the command line.
    init_CMD_LINE(ld);

    // Set up the launch_cmd string.
    init_launch_cmd(ld);

    if (!(ld.args.flags & F_DRYRUN))
    {
        // Try to retrieve frame rate data from game.
        retrieve_fps_data(ld);

        if (ld.args.logfile.size())
        {
            printf("\n");
            printf("View frametime graph:\n");
            printf("  gnuplot -p -e 'set terminal wxt size 1280,720;set ylabel \"milliseconds\";set yrange [0:100]; plot \"%s\" with lines'\n", ld.args.logfile.c_str());
            printf("\n");
            printf("Create frametime graph png file:\n");
            printf("  gnuplot -p -e 'set output \"blah.png\";set terminal pngcairo size 1280,720 enhanced;set ylabel \"milliseconds\";set yrange [0:100]; plot \"%s\" with lines'\n", ld.args.logfile.c_str());
        }
    }

    printf("\nDone.\n");
    return 0;
}
