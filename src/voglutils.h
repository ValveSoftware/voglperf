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

#include <string>
#include <vector>

/*
 * Some general purpose utility functions.
 */
std::string get_ip_addr();
std::string get_file_contents(const char *filename);
std::string string_format(const char *fmt, ...);
std::string url_encode(const std::string &value);
std::string get_logfile_name(std::string basename_str);
std::string get_ld_preload_str(const char *lib32, const char *lib64, bool do_ld_debug);
void string_split(std::vector<std::string>& args, const std::string& str, const std::string& delims);

// Spew fatal error message and die.
void __attribute__ ((noreturn)) errorf(const char *format, ...);

// Parse appid.txt file.
struct gameid_t
{
    unsigned int id;        // Steam game id.
    std::string name;       // Steam game name.
};
bool parse_appid_file(std::vector<gameid_t> &installed_games);

/*
 * Web / Websocket Functions.
 */
struct webby_init_t
{
    const char *bind_address;
    unsigned short port;
    void *user_data;
    bool verbose;

    std::string (*ws_connected_pfn)(void *user_data);
    std::string (*uri_dispatch_pfn)(const char *request_uri, void *user_data);
};

void webby_start(const webby_init_t &init);
void webby_end();
void webby_update(std::vector<std::string> *commands);
void webby_ws_printf(const char *format, ...);
void webby_ws_write_buffer(struct WebbyConnection *connection, const char *buffer, size_t buffer_len);
unsigned int webby_ws_get_connection_count();
