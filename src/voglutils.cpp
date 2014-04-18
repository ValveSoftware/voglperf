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
#include <limits.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>

#include <iomanip>
#include <sstream>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "webby/webby.h"
#include "voglutils.h"

struct webby_data_t
{
    webby_init_t init;

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
static inline webby_data_t& webby_data()
{
    static webby_data_t s_webby_data;
    return s_webby_data;
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
// get_config_dir
//----------------------------------------------------------------------------------------------------------------------
static std::string get_config_dir()
{
    std::string config_dir;
    static const char *xdg_config_home = getenv("XDG_CONFIG_HOME");

    if (xdg_config_home && xdg_config_home[0])
    {
        config_dir = xdg_config_home;
    }
    else
    {
        static const char *home = getenv("HOME");

        if (!home || !home[0])
        {
            passwd *pw = getpwuid(geteuid());
            home = pw->pw_dir;
        }

        if (home && home[0])
        {
            config_dir = string_format("%s/.config", home);
        }
    }

    if (!config_dir.size())
    {
        // Egads, can't find home dir - just fall back to using tmp dir.
        config_dir = P_tmpdir;
    }

    config_dir += "/voglperf";

    mkdir(config_dir.c_str(), 0700);
    return config_dir;
}

//----------------------------------------------------------------------------------------------------------------------
// Spew out error and die.
//----------------------------------------------------------------------------------------------------------------------
void __attribute__ ((noreturn)) errorf(const char *format, ...)
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
std::string get_file_contents(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        std::string str;

        fseek(fp, 0, SEEK_END);
        str.resize(ftell(fp));
        rewind(fp);

        size_t ret = fread(&str[0], 1, str.size(), fp);
        if (ret != str.size())
            webby_ws_printf("WARNING: Reading %s failed: %s\n", filename, strerror(errno));
        fclose(fp);

        return str;
    }

    return "";
}

//----------------------------------------------------------------------------------------------------------------------
// Write a file.
//----------------------------------------------------------------------------------------------------------------------
static void write_file_contents(const char *filename, std::string data)
{
    FILE *fp = fopen(filename, "wb");
    if (fp)
    {
        size_t ret = fwrite(data.c_str(), 1, data.size(), fp);
        if (ret != data.size())
            webby_ws_printf("WARNING: Writing %s failed: %s\n", filename, strerror(errno));
        fclose(fp);
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Printf style formatting for std::string.
//----------------------------------------------------------------------------------------------------------------------
std::string string_format(const char *fmt, ...)
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
}

//----------------------------------------------------------------------------------------------------------------------
// string_split: split str by delims into args.
//   http://stackoverflow.com/questions/53849/how-do-i-tokenize-a-string-in-c
//----------------------------------------------------------------------------------------------------------------------
void string_split(std::vector<std::string>& args, const std::string& str, const std::string& delims)
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
// url_encode
//----------------------------------------------------------------------------------------------------------------------
std::string url_encode(const std::string &value)
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
// get_logfile_name
//----------------------------------------------------------------------------------------------------------------------
std::string get_logfile_name(std::string basename_str)
{
    char timestr[128];
    time_t t = time(NULL);

    timestr[0] = 0;
    struct tm *tmp = localtime(&t);
    if (tmp)
    {
        strftime(timestr, sizeof(timestr), "%Y_%m_%d-%H_%M_%S", tmp);
    }

    std::string basename = basename_str;
    for (size_t i = 0; i < basename.size(); i++)
    {
        if (isspace(basename[i]) || ispunct(basename[i]))
            basename[i] = '-';
    }

    return string_format("%s/voglperf.%s.%s.csv", P_tmpdir, basename.c_str(), timestr);
}

//----------------------------------------------------------------------------------------------------------------------
// parse_appid_file
//----------------------------------------------------------------------------------------------------------------------
bool parse_appid_file(std::vector<gameid_t>& installed_games)
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
        char exe[PATH_MAX];
        if (readlink("/proc/self/exe", exe, sizeof(exe)) > 0)
        {
            // Try where the binary currently exists.
            std::string filename = dirname(exe);
            filename += "/appids.txt";
            file = fopen(filename.c_str(), "r");
        }
    }

    if (file)
    {
        char line[512];
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
                            installed_games.push_back(gameid);
                        }
                    }
                }
            }
        }

        fclose(file);
    }

    bool found = (installed_games.size() > 0);
    if (!found)
    {
        // Just give up and populate with a random assortment of games.
        for (size_t i = 0; i < sizeof(gameids) / sizeof(gameids[0]); i++)
        {
            installed_games.push_back(gameids[i]);
        }
    }

    return found;
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
// get_ld_preload_str
//----------------------------------------------------------------------------------------------------------------------
std::string get_ld_preload_str(const char *lib32, const char *lib64, bool do_ld_debug)
{
    // set up LD_PRELOAD string
    std::string vogllib32 = get_full_path(lib32);
    std::string vogllib64 = get_full_path(lib64);

    std::string ld_preload_str = "LD_PRELOAD=";

    // Add both 32 and 64-bit shared objects as we don't know what arch the target is.
    ld_preload_str += vogllib32 + ":" + vogllib64;

    // Append :$LD_PRELOAD.
    ld_preload_str += ":$LD_PRELOAD";

    // Add LD_DEBUG=lib..
    if (do_ld_debug)
        ld_preload_str += " LD_DEBUG=libs";

    return ld_preload_str;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_write_buffer
//----------------------------------------------------------------------------------------------------------------------
void webby_ws_write_buffer(struct WebbyConnection *connection, const char *buffer, size_t buffer_len)
{
    // Print to stdout
    printf("%s", buffer);

    if (buffer_len == (size_t)-1)
        buffer_len = strlen(buffer);

    if (buffer_len)
    {
        if (connection)
        {
            WebbyBeginSocketFrame(connection, WEBBY_WS_OP_TEXT_FRAME);
            WebbyWrite(connection, buffer, buffer_len);
            WebbyEndSocketFrame(connection);
        }
        else if (webby_data().ws_connections.size())
        {
            for (size_t i = 0; i < webby_data().ws_connections.size(); ++i)
            {
                connection = webby_data().ws_connections[i];

                WebbyBeginSocketFrame(connection, WEBBY_WS_OP_TEXT_FRAME);
                WebbyWrite(connection, buffer, buffer_len);
                WebbyEndSocketFrame(connection);
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Print string to websocket connections and stdout.
//----------------------------------------------------------------------------------------------------------------------
void webby_ws_printf(const char *format, ...)
{
    va_list args;
    char buffer[4096];

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    webby_ws_write_buffer(NULL, buffer, (size_t)-1);
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
    if (webby_data().init.uri_dispatch_pfn)
    {
        std::string data = webby_data().init.uri_dispatch_pfn(connection->request.uri, connection->user_data);

        if (data.size())
        {
            //$ TODO: Allow something other than text files to be served up?
            static const struct WebbyHeader headers[] =
            {
                { "Content-Type", "text/plain" },
            };

            WebbyBeginResponse(connection, 200, data.size(), headers, 1);
            WebbyWrite(connection, data.c_str(), data.size());
            WebbyEndResponse(connection);
            return 0;
        }
    }

    std::string index_html_file = get_config_dir() + "/index_v1.html";
    std::string index_html = get_file_contents(index_html_file.c_str());

    if (!index_html.size())
    {
        extern char _binary_index_html_start;
        extern char _binary_index_html_end;
        size_t size = &_binary_index_html_end - &_binary_index_html_start;

        index_html = std::string(&_binary_index_html_start, size);

        write_file_contents(index_html_file.c_str(), index_html);
    }

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

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_connect
//----------------------------------------------------------------------------------------------------------------------
static int webby_ws_connect(struct WebbyConnection *connection)
{
    // Allow websocket upgrades on /ws.
    if (!strcmp(connection->request.uri, "/ws"))
    {
        if (webby_data().ws_connections.size() >= webby_data_t::MAX_WSCONN)
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
    webby_data().ws_connections.push_back(connection);

    printf("[webby] WebSocket connected %s on %s\n", connection->request.method, connection->request.uri);

    webby_ws_write_buffer(connection, "Welcome!\n", -1);

    if (webby_data().init.ws_connected_pfn)
    {
        std::string data = webby_data().init.ws_connected_pfn(connection->user_data);

        if (data.size())
        {
            WebbyBeginSocketFrame(connection, WEBBY_WS_OP_TEXT_FRAME);
            WebbyWrite(connection, data.c_str(), data.size());
            WebbyEndSocketFrame(connection);
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_closed
//----------------------------------------------------------------------------------------------------------------------
static void webby_ws_closed(struct WebbyConnection *connection)
{
    if (webby_data().init.verbose)
    {
        printf("[webby] WebSocket closed %s on %s\n", connection->request.method, connection->request.uri);
    }

    for (size_t i = 0; i < webby_data().ws_connections.size(); i++)
    {
        if (webby_data().ws_connections[i] == connection)
        {
            webby_data().ws_connections.erase(webby_data().ws_connections.begin() + i);
            break;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_get_connection_count
//----------------------------------------------------------------------------------------------------------------------
unsigned int webby_ws_get_connection_count()
{
    if (webby_data().init.verbose)
    {
        printf("webby_ws_get_connection_count\n");
        for (size_t i = 0; i < webby_data().ws_connections.size(); i++)
        {
            printf("  0x%p\n", webby_data().ws_connections[i]);
        }
    }

    return webby_data().ws_connections.size();
}

//----------------------------------------------------------------------------------------------------------------------
// webby_ws_frame
//----------------------------------------------------------------------------------------------------------------------
static int webby_ws_frame(struct WebbyConnection *connection, const struct WebbyWsFrame *frame)
{
    if (webby_data().init.verbose)
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

        if (webby_data().init.verbose)
        {
            printf("%08x ", (int) i);
        }

        if (0 != WebbyRead(connection, buffer, read_size))
            break;

        if (webby_data().init.verbose)
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

    webby_data().ws_commands.push_back(command);
    return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// webby_start
//----------------------------------------------------------------------------------------------------------------------
void webby_start(const webby_init_t& init)
{
    printf("\nStarting web server...\n");

    memset(&webby_data().config, 0, sizeof(webby_data().config));

    webby_data().init = init;

    webby_data().config.user_data = init.user_data;
    webby_data().config.bind_address = init.bind_address;
    webby_data().config.listening_port = init.port;
    webby_data().config.flags = WEBBY_SERVER_WEBSOCKETS;
    webby_data().config.connection_max = 4;
    webby_data().config.request_buffer_size = 2048;
    webby_data().config.io_buffer_size = 8192;
    webby_data().config.dispatch = &webby_dispatch;
    webby_data().config.log = &webby_log;
    webby_data().config.ws_connect = &webby_ws_connect;
    webby_data().config.ws_connected = &webby_ws_connected;
    webby_data().config.ws_closed = &webby_ws_closed;
    webby_data().config.ws_frame = &webby_ws_frame;

    if (init.verbose)
    {
        webby_data().config.flags |= WEBBY_SERVER_LOG_DEBUG;
    }

    webby_data().memory_size = WebbyServerMemoryNeeded(&webby_data().config);
    webby_data().memory = malloc(webby_data().memory_size);
    webby_data().server = WebbyServerInit(&webby_data().config, webby_data().memory, webby_data().memory_size);

    if (!webby_data().server)
        errorf("ERROR: Web server failed to initialize.\n");

    printf("  Started http://%s:%u\n\n", webby_data().config.bind_address, webby_data().config.listening_port);
}

//----------------------------------------------------------------------------------------------------------------------
// webby_update
//----------------------------------------------------------------------------------------------------------------------
void webby_update(std::vector<std::string> *commands, struct timeval *timeoutval)
{
    if (webby_data().server)
    {
        WebbyServerUpdate(webby_data().server, timeoutval);

        // If we were passed a command array, add new commands to it.
        if (commands && webby_data().ws_commands.size())
        {
            commands->insert(commands->end(), webby_data().ws_commands.begin(), webby_data().ws_commands.end());
            webby_data().ws_commands.clear();
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// webby_end
//----------------------------------------------------------------------------------------------------------------------
void webby_end()
{
    webby_data().ws_connections.empty();

    if (webby_data().server)
    {
        WebbyServerShutdown(webby_data().server);
        webby_data().server = NULL;
    }

    if (webby_data().memory)
    {
        free(webby_data().memory);
        webby_data().memory = NULL;
        webby_data().memory_size = 0;
    }
}
