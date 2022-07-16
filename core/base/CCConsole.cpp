/****************************************************************************
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2020 C4games Ltd.
 Copyright (c) 2021 Bytedance Inc.

 https://axis-project.github.io/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "base/CCConsole.h"

#include <thread>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
#    include <io.h>
#    if defined(__MINGW32__)
#        include "platform/win32/inet_pton_mingw.h"
#    endif
#    define bzero(a, b) memset(a, 0, b)
#    include "ntcvt/ntcvt.hpp"
#endif

#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "platform/CCPlatformConfig.h"
#include "base/CCConfiguration.h"
#include "2d/CCScene.h"
#include "platform/CCFileUtils.h"
#include "renderer/CCTextureCache.h"
#include "base/base64.h"
#include "base/ccUtils.h"
#include "base/ccUTF8.h"

// !FIXME: the previous version of axis::log not thread safe
// since axis make it multi-threading safe by default
#if !defined(AX_LOG_MULTITHREAD)
#    define AX_LOG_MULTITHREAD 1
#endif

#if !defined(AX_LOG_TO_CONSOLE)
#    define AX_LOG_TO_CONSOLE 1
#endif

NS_AX_BEGIN

extern const char* axisVersion(void);

#define PROMPT "> "
#define DEFAULT_COMMAND_SEPARATOR '|'

static const size_t SEND_BUFSIZ = 512;

/** private functions */
namespace
{
#if defined(__MINGW32__)
// inet
const char* inet_ntop(int af, const void* src, char* dst, int cnt)
{
    struct sockaddr_in srcaddr;

    memset(&srcaddr, 0, sizeof(struct sockaddr_in));
    memcpy(&(srcaddr.sin_addr), src, sizeof(srcaddr.sin_addr));

    srcaddr.sin_family = af;
    if (WSAAddressToStringA((struct sockaddr*)&srcaddr, sizeof(struct sockaddr_in), 0, dst, (LPDWORD)&cnt) != 0)
    {
        return nullptr;
    }
    return dst;
}
#endif

//
// Free functions to log
//

#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
void SendLogToWindow(const char* log)
{
    static const int AXLOG_STRING_TAG = 1;
    // Send data as a message
    COPYDATASTRUCT myCDS;
    myCDS.dwData = AXLOG_STRING_TAG;
    myCDS.cbData = (DWORD)strlen(log) + 1;
    myCDS.lpData = (PVOID)log;
    if (Director::getInstance()->getOpenGLView())
    {
        HWND hwnd = Director::getInstance()->getOpenGLView()->getWin32Window();
        // use non-block version of SendMessage
        PostMessage(hwnd, WM_COPYDATA, (WPARAM)(HWND)hwnd, (LPARAM)(LPVOID)&myCDS);
    }
}
#endif
}  // namespace

void log(const char* format, ...)
{
#define AX_VSNPRINTF_BUFFER_LENGTH 512
    va_list args;

    va_start(args, format);
    auto buf = StringUtils::vformat(format, args);
    va_end(args);

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "axis debug info", "%s", buf.c_str());

#elif AX_TARGET_PLATFORM == AX_PLATFORM_WIN32
    buf.push_back('\n');

    // print to debugger output window
    std::wstring wbuf = ntcvt::from_chars(buf);

    OutputDebugStringW(wbuf.c_str());

#    if AX_LOG_TO_CONSOLE
    auto hStdout = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout)
    {
        // print to console if possible
        // since we use win32 API, the ::fflush call doesn't required.
        // see: https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers#return-value
        DWORD wcch = static_cast<DWORD>(wbuf.size());
        ::WriteConsoleW(hStdout, wbuf.c_str(), wcch, nullptr, 0);
    }
#    endif

#    if !AX_LOG_MULTITHREAD
    // print to log window
    SendLogToWindow(buf.c_str());
#    endif
#else
    buf.push_back('\n');
    // Linux, Mac, iOS, etc
    fprintf(stdout, "%s", buf.c_str());
    fflush(stdout);
#endif

#if !AX_LOG_MULTITHREAD
    Director::getInstance()->getConsole()->log(buf.c_str());
#endif
}

// FIXME: Deprecated
// void CCLog(const char * format, ...);

//
//  Utility code
//

std::string Console::Utility::_prompt(PROMPT);

// TODO: these general utils should be in a separate class
//
//  Trimming functions were taken from: http://stackoverflow.com/a/217605
//  Since c++17, some parts of the standard library were removed, include "ptr_fun".

// trim from start

std::string& Console::Utility::ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
    return s;
}

// trim from end
std::string& Console::Utility::rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

// trim from both ends
std::string& Console::Utility::trim(std::string& s)
{
    return Console::Utility::ltrim(Console::Utility::rtrim(s));
}

std::vector<std::string>& Console::Utility::split(std::string_view s, char delim, std::vector<std::string>& elems)
{
    std::stringstream ss;
    ss << s;
    std::string item;
    while (std::getline(ss, item, delim))
    {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> Console::Utility::split(std::string_view s, char delim)
{
    std::vector<std::string> elems;
    Console::Utility::split(s, delim, elems);
    return elems;
}

// isFloat taken from http://stackoverflow.com/questions/447206/c-isfloat-function
bool Console::Utility::isFloat(std::string_view myString)
{
    std::stringstream ss;
    ss << myString;
    float f;
    ss >> std::noskipws >> f;  // noskipws considers leading whitespace invalid
    // Check the entire string was consumed and if either failbit or badbit is set
    return ss.eof() && !ss.fail();
}

ssize_t Console::Utility::sendToConsole(int fd, const void* buffer, size_t length, int flags)
{
    if (_prompt.length() == length)
    {
        if (strncmp(_prompt.c_str(), static_cast<const char*>(buffer), length) == 0)
        {
            fprintf(stderr, "bad parameter error: a buffer is the prompt string.\n");
            return 0;
        }
    }

    const char* buf = static_cast<const char*>(buffer);
    ssize_t retLen  = 0;
    for (size_t i = 0; i < length;)
    {
        size_t len = length - i;
        if (SEND_BUFSIZ < len)
            len = SEND_BUFSIZ;
        retLen += send(fd, buf + i, len, flags);
        i += len;
    }
    return retLen;
}

// dprintf() is not defined in Android
// so we add our own 'dpritnf'
ssize_t Console::Utility::mydprintf(int sock, const char* format, ...)
{
    va_list args;
    char buf[16386];

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return sendToConsole(sock, buf, strlen(buf));
}

void Console::Utility::sendPrompt(int fd)
{
    const char* prompt = _prompt.c_str();
    send(fd, prompt, strlen(prompt), 0);
}

void Console::Utility::setPrompt(std::string_view prompt)
{
    _prompt = prompt;
}

std::string_view Console::Utility::getPrompt()
{
    return _prompt;
}

//
// Command code
//

Console::Command::Command() : _callback(nullptr) {}

Console::Command::Command(std::string_view name, std::string_view help) : _name(name), _help(help), _callback(nullptr)
{}

Console::Command::Command(std::string_view name, std::string_view help, const Callback& callback)
    : _name(name), _help(help), _callback(callback)
{}

Console::Command::Command(const Command& o)
{
    *this = o;
}

Console::Command::Command(Command&& o)
{
    *this = std::move(o);
}

Console::Command::~Command()
{
    for (const auto& e : _subCommands)
    {
        delete e.second;
    }
}

Console::Command& Console::Command::operator=(const Command& o)
{
    if (this != &o)
    {
        _name     = o._name;
        _help     = o._help;
        _callback = o._callback;

        for (const auto& e : _subCommands)
            delete e.second;

        _subCommands.clear();
        for (const auto& e : o._subCommands)
        {
            Command* subCommand   = e.second;
            auto newCommand       = new Command(*subCommand);
            _subCommands[e.first] = newCommand;
        }
    }

    return *this;
}

Console::Command& Console::Command::operator=(Command&& o)
{
    if (this != &o)
    {
        _name       = std::move(o._name);
        _help       = std::move(o._help);
        _callback   = std::move(o._callback);
        o._callback = nullptr;

        for (const auto& e : _subCommands)
            delete e.second;

        _subCommands.clear();
        _subCommands = std::move(o._subCommands);
    }

    return *this;
}

void Console::Command::addCallback(const Callback& callback)
{
    _callback = callback;
}

void Console::Command::addSubCommand(const Command& subCmd)
{
    auto iter = _subCommands.find(subCmd._name);
    if (iter != _subCommands.end())
    {
        delete iter->second;
        _subCommands.erase(iter);
    }

    Command* cmd               = new Command();
    *cmd                       = subCmd;
    _subCommands[subCmd._name] = cmd;
}

const Console::Command* Console::Command::getSubCommand(std::string_view subCmdName) const
{
    auto it = _subCommands.find(subCmdName);
    if (it != _subCommands.end())
    {
        auto& subCmd = it->second;
        return subCmd;
    }
    return nullptr;
}

void Console::Command::delSubCommand(std::string_view subCmdName)
{
    auto iter = _subCommands.find(subCmdName);
    if (iter != _subCommands.end())
    {
        delete iter->second;
        _subCommands.erase(iter);
    }
}

void Console::Command::commandHelp(int fd, std::string_view /*args*/)
{
    if (!_help.empty())
    {
        Console::Utility::mydprintf(fd, "%s\n", _help.c_str());
    }

    if (!_subCommands.empty())
    {
        sendHelp(fd, _subCommands, "");
    }
}

void Console::Command::commandGeneric(int fd, std::string_view args)
{
    // The first argument (including the empty)
    std::string key(args);
    auto pos = args.find(' ');
    if ((pos != std::string::npos) && (0 < pos))
    {
        key = args.substr(0, pos);
    }

    // help
    if (key == "help" || key == "-h")
    {
        commandHelp(fd, args);
        return;
    }

    // find sub command
    auto it = _subCommands.find(key);
    if (it != _subCommands.end())
    {
        auto subCmd = it->second;
        if (subCmd->_callback)
        {
            subCmd->_callback(fd, args);
        }
        return;
    }

    // can not find
    if (_callback)
    {
        _callback(fd, args);
    }
}

//
// Console code
//

Console::Console()
    : _commandSeparator(DEFAULT_COMMAND_SEPARATOR)
    , _listenfd(-1)
    , _running(false)
    , _endThread(false)
    , _isIpv6Server(false)
    , _sendDebugStrings(false)
    , _bindAddress("")
{
    createCommandAllocator();
    createCommandConfig();
    createCommandDebugMsg();
    createCommandDirector();
    createCommandExit();
    createCommandFileUtils();
    createCommandFps();
    createCommandHelp();
    createCommandProjection();
    createCommandResolution();
    createCommandSceneGraph();
    createCommandTexture();
    createCommandTouch();
    createCommandUpload();
    createCommandVersion();
}

Console::~Console()
{
    stop();

    for (auto& e : _commands)
        delete e.second;
}

bool Console::listenOnTCP(int port)
{
    socket_native_type listenfd = -1;
    int n;
    const int on = 1;
    struct addrinfo hints, *res, *ressave;
    char serv[30];

    snprintf(serv, sizeof(serv) - 1, "%d", port);

    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
    WSADATA wsaData;
    n = WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    if ((n = getaddrinfo(nullptr, serv, &hints, &res)) != 0)
    {
#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
        fprintf(stderr, "net_listen error for %s: %s", serv, gai_strerrorA(n));
#else
        fprintf(stderr, "net_listen error for %s: %s", serv, gai_strerror(n));
#endif
        return false;
    }

    ressave = res;

    do
    {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (listenfd < 0)
            continue; /* error, try next one */

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

        // bind address
        if (!_bindAddress.empty())
        {
            if (res->ai_family == AF_INET)
            {
                struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
                inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin_addr);
            }
            else if (res->ai_family == AF_INET6)
            {
                struct sockaddr_in6* sin = (struct sockaddr_in6*)res->ai_addr;
                inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin6_addr);
            }
        }

        if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
            break; /* success */

/* bind error, close and try next one */
#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
        closesocket(listenfd);
#else
        close(listenfd);
#endif
    } while ((res = res->ai_next) != nullptr);

    if (res == nullptr)
    {
        perror("net_listen:");
        freeaddrinfo(ressave);
        return false;
    }

    listen(listenfd, 50);

    if (res->ai_family == AF_INET)
    {
        _isIpv6Server             = false;
        char buf[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in* sin   = (struct sockaddr_in*)res->ai_addr;
        if (inet_ntop(res->ai_family, &sin->sin_addr, buf, sizeof(buf)) != nullptr)
            axis::log("Console: IPV4 server is listening on %s:%d", buf, ntohs(sin->sin_port));
        else
            perror("inet_ntop");
    }
    else if (res->ai_family == AF_INET6)
    {
        _isIpv6Server              = true;
        char buf[INET6_ADDRSTRLEN] = {0};
        struct sockaddr_in6* sin   = (struct sockaddr_in6*)res->ai_addr;
        if (inet_ntop(res->ai_family, &sin->sin6_addr, buf, sizeof(buf)) != nullptr)
            axis::log("Console: IPV6 server is listening on [%s]:%d", buf, ntohs(sin->sin6_port));
        else
            perror("inet_ntop");
    }

    freeaddrinfo(ressave);
    return listenOnFileDescriptor(listenfd);
}

bool Console::listenOnFileDescriptor(int fd)
{
    if (_running)
    {
        axis::log("Console already started. 'stop' it before calling 'listen' again");
        return false;
    }

    _listenfd = fd;
    _thread   = std::thread(std::bind(&Console::loop, this));

    return true;
}

void Console::stop()
{
    if (_running)
    {
        _endThread = true;
        if (_thread.joinable())
        {
            _thread.join();
        }
    }
}

void Console::addCommand(const Command& cmd)
{
    Command* newCommand = new Command(cmd);
    auto iter           = _commands.find(cmd.getName());
    if (iter != _commands.end())
    {
        delete iter->second;
        _commands.erase(iter);
    }
    _commands.emplace(cmd.getName(), newCommand);  // _commands[cmd.getName()] = newCommand;
}

void Console::addSubCommand(std::string_view cmdName, const Command& subCmd)
{
    auto it = _commands.find(cmdName);
    if (it != _commands.end())
    {
        auto& cmd = it->second;
        addSubCommand(*cmd, subCmd);
    }
}

void Console::addSubCommand(Command& cmd, const Command& subCmd)
{
    cmd.addSubCommand(subCmd);
}

const Console::Command* Console::getCommand(std::string_view cmdName)
{
    auto it = _commands.find(cmdName);
    if (it != _commands.end())
    {
        auto& cmd = it->second;
        return cmd;
    }
    return nullptr;
}

const Console::Command* Console::getSubCommand(std::string_view cmdName, std::string_view subCmdName)
{
    auto it = _commands.find(cmdName);
    if (it != _commands.end())
    {
        auto& cmd = it->second;
        return getSubCommand(*cmd, subCmdName);
    }
    return nullptr;
}

const Console::Command* Console::getSubCommand(const Command& cmd, std::string_view subCmdName)
{
    return cmd.getSubCommand(subCmdName);
}

void Console::delCommand(std::string_view cmdName)
{
    auto it = _commands.find(cmdName);
    if (it != _commands.end())
    {
        delete it->second;
        _commands.erase(it);
    }
}

void Console::delSubCommand(std::string_view cmdName, std::string_view subCmdName)
{
    auto it = _commands.find(cmdName);
    if (it != _commands.end())
    {
        auto& cmd = it->second;
        delSubCommand(*cmd, subCmdName);
    }
}

void Console::delSubCommand(Command& cmd, std::string_view subCmdName)
{
    cmd.delSubCommand(subCmdName);
}

void Console::log(const char* buf)
{
    if (_sendDebugStrings)
    {
        _DebugStringsMutex.lock();
        _DebugStrings.push_back(buf);
        _DebugStringsMutex.unlock();
    }
}

void Console::setBindAddress(std::string_view address)
{
    _bindAddress = address;
}

bool Console::isIpv6Server() const
{
    return _isIpv6Server;
}

//
// Main Loop
//
void Console::loop()
{
    fd_set copy_set;
    struct timeval timeout, timeout_copy;

    _running = true;

    FD_ZERO(&_read_set);
    FD_SET(_listenfd, &_read_set);
    _maxfd = _listenfd;

    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    while (!_endThread)
    {

        copy_set     = _read_set;
        timeout_copy = timeout;

        int nready = select(_maxfd + 1, &copy_set, nullptr, nullptr, &timeout_copy);

        if (nready == -1)
        {
            /* error */
            if (errno != EINTR)
                axis::log("Abnormal error in select()\n");
            continue;
        }
        else if (nready == 0)
        {
            /* timeout. do something ? */
        }
        else
        {
            /* new client */
            if (FD_ISSET(_listenfd, &copy_set))
            {
                addClient();
                if (--nready <= 0)
                    continue;
            }

            /* data from client */
            std::vector<int> to_remove;
            for (const auto& fd : _fds)
            {
                if (FD_ISSET(fd, &copy_set))
                {
                    // fix Bug #4302 Test case ConsoleTest--ConsoleUploadFile crashed on Linux
                    // On linux, if you send data to a closed socket, the sending process will
                    // receive a SIGPIPE, which will cause linux system shutdown the sending process.
                    // Add this ioctl code to check if the socket has been closed by peer.

#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
                    u_long n = 0;
                    ioctlsocket(fd, FIONREAD, &n);
#else
                    int n = 0;
                    if (ioctl(fd, FIONREAD, &n) < 0)
                    {
                        axis::log("Abnormal error in ioctl()\n");
                        break;
                    }
#endif
                    if (n == 0)
                    {
                        // no data received, or fd is closed
                        // fix #18620. readable and no pending data means that the fd is closed.
                        to_remove.push_back(fd);
                        continue;
                    }

                    if (!parseCommand(fd))
                    {
                        to_remove.push_back(fd);
                    }
                    if (--nready <= 0)
                        break;
                }
            }

            /* remove closed connections */
            for (int fd : to_remove)
            {
                FD_CLR(fd, &_read_set);
                _fds.erase(std::remove(_fds.begin(), _fds.end(), fd), _fds.end());
            }
        }

        /* Any message for the remote console ? send it! */
        if (!_DebugStrings.empty())
        {
            if (_DebugStringsMutex.try_lock())
            {
                for (const auto& str : _DebugStrings)
                {
                    for (auto fd : _fds)
                    {
                        Console::Utility::sendToConsole(fd, str.c_str(), str.length());
                    }
                }
                _DebugStrings.clear();
                _DebugStringsMutex.unlock();
            }
        }
    }

    // clean up: ignore stdin, stdout and stderr
    for (const auto& fd : _fds)
    {
#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
        closesocket(fd);
#else
        close(fd);
#endif
    }

#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
    closesocket(_listenfd);
    WSACleanup();
#else
    close(_listenfd);
#endif
    _running = false;
}

//
// Helpers
//

ssize_t Console::readline(socket_native_type fd, char* ptr, size_t maxlen)
{
    size_t n, rc;
    char c;

    for (n = 0; n < maxlen - 1; n++)
    {
        if ((rc = recv(fd, &c, 1, 0)) == 1)
        {
            *ptr++ = c;
            if (c == '\n')
            {
                break;
            }
        }
        else if (rc == 0)
        {
            return 0;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            return -1;
        }
    }

    *ptr = 0;
    return n;
}

ssize_t Console::readBytes(socket_native_type fd, char* buffer, size_t maxlen, bool* more)
{
    size_t n, rc;
    char c, *ptr = buffer;
    *more = false;
    for (n = 0; n < maxlen; n++)
    {
        if ((rc = recv(fd, &c, 1, 0)) == 1)
        {
            *ptr++ = c;
            if (c == '\n')
            {
                return n;
            }
        }
        else if (rc == 0)
        {
            return 0;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            return -1;
        }
    }
    *more = true;
    return n;
}

bool Console::parseCommand(socket_native_type fd)
{
    char buf[512];
    bool more_data;
    auto h = readBytes(fd, buf, 6, &more_data);
    if (h < 0)
    {
        return false;
    }
    if (strncmp(buf, "upload", 6) == 0)
    {
        char c = '\0';
        recv(fd, &c, 1, 0);
        if (c == ' ')
        {
            commandUpload(fd);
            Console::Utility::sendPrompt(fd);
            return true;
        }
        else
        {
            const char err[] = "upload: invalid args! Type 'help' for options\n";
            Console::Utility::sendToConsole(fd, err, strlen(err));
            Console::Utility::sendPrompt(fd);
            return true;
        }
    }
    if (!more_data)
    {
        buf[h] = 0;
    }
    else
    {
        char* pb = buf + 6;
        auto r   = readline(fd, pb, sizeof(buf) - 6);
        if (r < 0)
        {
            const char err[] = "Unknown error!\n";
            Console::Utility::sendPrompt(fd);
            Console::Utility::sendToConsole(fd, err, strlen(err));
            return false;
        }
    }
    std::string cmdLine;
    cmdLine       = std::string(buf);
    auto commands = Console::Utility::split(cmdLine, _commandSeparator);
    try
    {
        for (auto command : commands)
        {
            performCommand(fd, Console::Utility::trim(command));
        }
    }
    catch (const std::runtime_error& e)
    {
        Console::Utility::sendToConsole(fd, e.what(), strlen(e.what()));
    }

    Console::Utility::sendPrompt(fd);

    return true;
}

void Console::performCommand(socket_native_type fd, std::string_view command)
{
    std::vector<std::string> args = Console::Utility::split(command, ' ');
    if (args.empty())
    {
        throw std::runtime_error("Unknown command. Type 'help' for options\n");
    }

    auto it = _commands.find(Console::Utility::trim(args[0]));
    if (it != _commands.end())
    {
        std::string args2;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1)
            {
                args2 += ' ';
            }
            args2 += Console::Utility::trim(args[i]);
        }
        auto cmd = it->second;
        cmd->commandGeneric(fd, args2);
    }
    else
    {
        throw std::runtime_error(std::string{"Unknown command "}.append(command).append(". Type 'help' for options\n"));
    }
}

void Console::addClient()
{
    struct sockaddr_in6 ipv6Addr;
    struct sockaddr_in ipv4Addr;
    struct sockaddr* addr = _isIpv6Server ? (struct sockaddr*)&ipv6Addr : (struct sockaddr*)&ipv4Addr;
    socklen_t addrLen     = _isIpv6Server ? sizeof(ipv6Addr) : sizeof(ipv4Addr);

    /* new client */
    socket_native_type fd = accept(_listenfd, addr, &addrLen);

    // add fd to list of FD
    if (fd != -1)
    {
        FD_SET(fd, &_read_set);
        _fds.push_back(fd);
        _maxfd = (std::max)(_maxfd, fd);

        Console::Utility::sendPrompt(fd);

        /**
         * A SIGPIPE is sent to a process if it tried to write to socket that had been shutdown for
         * writing or isn't connected (anymore) on iOS.
         *
         * The default behaviour for this signal is to end the process.So we make the process ignore SIGPIPE.
         */
#if AX_TARGET_PLATFORM == AX_PLATFORM_IOS
        int set = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
#endif
    }
}

//
// create commands
//

void Console::createCommandAllocator()
{
    addCommand({"allocator", "Display allocator diagnostics for all allocators. Args: [-h | help | ]",
                AX_CALLBACK_2(Console::commandAllocator, this)});
}

void Console::createCommandConfig()
{
    addCommand({"config", "Print the Configuration object. Args: [-h | help | ]",
                AX_CALLBACK_2(Console::commandConfig, this)});
}

void Console::createCommandDebugMsg()
{
    addCommand({"debugmsg",
                "Whether or not to forward the debug messages on the console. Args: [-h | help | on | off | ]",
                AX_CALLBACK_2(Console::commandDebugMsg, this)});
    addSubCommand("debugmsg",
                  {"on", "enable debug logging", AX_CALLBACK_2(Console::commandDebugMsgSubCommandOnOff, this)});
    addSubCommand("debugmsg",
                  {"off", "disable debug logging", AX_CALLBACK_2(Console::commandDebugMsgSubCommandOnOff, this)});
}

void Console::createCommandDirector()
{
    addCommand({"director", "director commands, type -h or [director help] to list supported directives"});
    addSubCommand("director",
                  {"pause", "pause all scheduled timers, the draw rate will be 4 FPS to reduce CPU consumption",
                   AX_CALLBACK_2(Console::commandDirectorSubCommandPause, this)});
    addSubCommand("director", {"resume", "resume all scheduled timers",
                               AX_CALLBACK_2(Console::commandDirectorSubCommandResume, this)});
    addSubCommand("director", {"stop", "Stops the animation. Nothing will be drawn.",
                               AX_CALLBACK_2(Console::commandDirectorSubCommandStop, this)});
    addSubCommand(
        "director",
        {"start", "Restart the animation again, Call this function only if [director stop] was called earlier",
         AX_CALLBACK_2(Console::commandDirectorSubCommandStart, this)});
    addSubCommand("director", {"end", "exit this app.", AX_CALLBACK_2(Console::commandDirectorSubCommandEnd, this)});
}

void Console::createCommandExit()
{
    addCommand(
        {"exit", "Close connection to the console. Args: [-h | help | ]", AX_CALLBACK_2(Console::commandExit, this)});
}

void Console::createCommandFileUtils()
{
    addCommand({"fileutils", "Flush or print the FileUtils info. Args: [-h | help | flush | ]",
                AX_CALLBACK_2(Console::commandFileUtils, this)});
    addSubCommand("fileutils", {"flush", "Purges the file searching cache.",
                                AX_CALLBACK_2(Console::commandFileUtilsSubCommandFlush, this)});
}

void Console::createCommandFps()
{
    addCommand(
        {"fps", "Turn on / off the FPS. Args: [-h | help | on | off | ]", AX_CALLBACK_2(Console::commandFps, this)});
    addSubCommand("fps", {"on", "Display the FPS on the bottom-left corner.",
                          AX_CALLBACK_2(Console::commandFpsSubCommandOnOff, this)});
    addSubCommand("fps", {"off", "Hide the FPS on the bottom-left corner.",
                          AX_CALLBACK_2(Console::commandFpsSubCommandOnOff, this)});
}

void Console::createCommandHelp()
{
    addCommand({"help", "Print this message. Args: [ ]", AX_CALLBACK_2(Console::commandHelp, this)});
}

void Console::createCommandProjection()
{
    addCommand({"projection", "Change or print the current projection. Args: [-h | help | 2d | 3d | ]",
                AX_CALLBACK_2(Console::commandProjection, this)});
    addSubCommand("projection", {"2d", "sets a 2D projection (orthogonal projection).",
                                 AX_CALLBACK_2(Console::commandProjectionSubCommand2d, this)});
    addSubCommand("projection", {"3d", "sets a 3D projection with a fovy=60, znear=0.5f and zfar=1500.",
                                 AX_CALLBACK_2(Console::commandProjectionSubCommand3d, this)});
}

void Console::createCommandResolution()
{
    addCommand({"resolution",
                "Change or print the window resolution. Args: [-h | help | width height resolution_policy | ]",
                AX_CALLBACK_2(Console::commandResolution, this)});
    addSubCommand("resolution", {"", "", AX_CALLBACK_2(Console::commandResolutionSubCommandEmpty, this)});
}

void Console::createCommandSceneGraph()
{
    addCommand({"scenegraph", "Print the scene graph", AX_CALLBACK_2(Console::commandSceneGraph, this)});
}

void Console::createCommandTexture()
{
    addCommand({"texture", "Flush or print the TextureCache info. Args: [-h | help | flush | ] ",
                AX_CALLBACK_2(Console::commandTextures, this)});
    addSubCommand("texture", {"flush", "Purges the dictionary of loaded textures.",
                              AX_CALLBACK_2(Console::commandTexturesSubCommandFlush, this)});
}

void Console::createCommandTouch()
{
    addCommand({"touch", "simulate touch event via console, type -h or [touch help] to list supported directives"});
    addSubCommand("touch", {"tap", "touch tap x y: simulate touch tap at (x,y).",
                            AX_CALLBACK_2(Console::commandTouchSubCommandTap, this)});
    addSubCommand("touch", {"swipe", "touch swipe x1 y1 x2 y2: simulate touch swipe from (x1,y1) to (x2,y2).",
                            AX_CALLBACK_2(Console::commandTouchSubCommandSwipe, this)});
}

void Console::createCommandUpload()
{
    addCommand(
        {"upload", "upload file. Args: [filename base64_encoded_data]", AX_CALLBACK_1(Console::commandUpload, this)});
}

void Console::createCommandVersion()
{
    addCommand({"version", "print version string ", AX_CALLBACK_2(Console::commandVersion, this)});
}

//
// commands
//

void Console::commandAllocator(socket_native_type fd, std::string_view /*args*/)
{
#if AX_ENABLE_ALLOCATOR_DIAGNOSTICS
    auto info = allocator::AllocatorDiagnostics::instance()->diagnostics();
    Console::Utility::mydprintf(fd, info.c_str());
#else
    Console::Utility::mydprintf(
        fd, "allocator diagnostics not available. AX_ENABLE_ALLOCATOR_DIAGNOSTICS must be set to 1 in ccConfig.h\n");
#endif
}

void Console::commandConfig(socket_native_type fd, std::string_view /*args*/)
{
    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread([=]() {
        Console::Utility::mydprintf(fd, "%s", Configuration::getInstance()->getInfo().c_str());
        Console::Utility::sendPrompt(fd);
    });
}

void Console::commandDebugMsg(socket_native_type fd, std::string_view /*args*/)
{
    Console::Utility::mydprintf(fd, "Debug message is: %s\n", _sendDebugStrings ? "on" : "off");
}

void Console::commandDebugMsgSubCommandOnOff(socket_native_type /*fd*/, std::string_view args)
{
    _sendDebugStrings = (args.compare("on") == 0);
}

void Console::commandDirectorSubCommandPause(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director    = Director::getInstance();
    Scheduler* sched = director->getScheduler();
    sched->performFunctionInCocosThread([]() { Director::getInstance()->pause(); });
}

void Console::commandDirectorSubCommandResume(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director = Director::getInstance();
    director->resume();
}

void Console::commandDirectorSubCommandStop(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director    = Director::getInstance();
    Scheduler* sched = director->getScheduler();
    sched->performFunctionInCocosThread([]() { Director::getInstance()->stopAnimation(); });
}

void Console::commandDirectorSubCommandStart(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director = Director::getInstance();
    director->startAnimation();
}

void Console::commandDirectorSubCommandEnd(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director = Director::getInstance();
    director->end();
}

void Console::commandExit(socket_native_type fd, std::string_view /*args*/)
{
    FD_CLR(fd, &_read_set);
    _fds.erase(std::remove(_fds.begin(), _fds.end(), fd), _fds.end());
#if (AX_TARGET_PLATFORM == AX_PLATFORM_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
}

void Console::commandFileUtils(socket_native_type fd, std::string_view /*args*/)
{
    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread(std::bind(&Console::printFileUtils, this, fd));
}

void Console::commandFileUtilsSubCommandFlush(socket_native_type /*fd*/, std::string_view /*args*/)
{
    FileUtils::getInstance()->purgeCachedEntries();
}

void Console::commandFps(socket_native_type fd, std::string_view /*args*/)
{
    Console::Utility::mydprintf(fd, "FPS is: %s\n", Director::getInstance()->isStatsDisplay() ? "on" : "off");
}

void Console::commandFpsSubCommandOnOff(socket_native_type /*fd*/, std::string_view args)
{
    bool state       = (args.compare("on") == 0);
    Director* dir    = Director::getInstance();
    Scheduler* sched = dir->getScheduler();
    sched->performFunctionInCocosThread(std::bind(&Director::setStatsDisplay, dir, state));
}

void Console::commandHelp(socket_native_type fd, std::string_view /*args*/)
{
    sendHelp(fd, _commands, "\nAvailable commands:\n");
}

void Console::commandProjection(socket_native_type fd, std::string_view /*args*/)
{
    auto director = Director::getInstance();
    char buf[20];
    auto proj = director->getProjection();
    switch (proj)
    {
    case axis::Director::Projection::_2D:
        sprintf(buf, "2d");
        break;
    case axis::Director::Projection::_3D:
        sprintf(buf, "3d");
        break;
    case axis::Director::Projection::CUSTOM:
        sprintf(buf, "custom");
        break;

    default:
        sprintf(buf, "unknown");
        break;
    }
    Console::Utility::mydprintf(fd, "Current projection: %s\n", buf);
}

void Console::commandProjectionSubCommand2d(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director    = Director::getInstance();
    Scheduler* sched = director->getScheduler();
    sched->performFunctionInCocosThread([=]() { director->setProjection(Director::Projection::_2D); });
}

void Console::commandProjectionSubCommand3d(socket_native_type /*fd*/, std::string_view /*args*/)
{
    auto director    = Director::getInstance();
    Scheduler* sched = director->getScheduler();
    sched->performFunctionInCocosThread([=]() { director->setProjection(Director::Projection::_3D); });
}

void Console::commandResolution(socket_native_type /*fd*/, std::string_view args)
{
    int policy;
    float width, height;
    std::stringstream stream;
    stream << args;
    stream >> width >> height >> policy;

    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread([=]() {
        Director::getInstance()->getOpenGLView()->setDesignResolutionSize(width, height,
                                                                          static_cast<ResolutionPolicy>(policy));
    });
}

void Console::commandResolutionSubCommandEmpty(socket_native_type fd, std::string_view /*args*/)
{
    auto director        = Director::getInstance();
    Vec2 points          = director->getWinSize();
    Vec2 pixels          = director->getWinSizeInPixels();
    auto glview          = director->getOpenGLView();
    Vec2 design          = glview->getDesignResolutionSize();
    ResolutionPolicy res = glview->getResolutionPolicy();
    Rect visibleRect     = glview->getVisibleRect();

    Console::Utility::mydprintf(fd,
                                "Window size:\n"
                                "\t%d x %d (points)\n"
                                "\t%d x %d (pixels)\n"
                                "\t%d x %d (design resolution)\n"
                                "Resolution Policy: %d\n"
                                "Visible Rect:\n"
                                "\torigin: %d x %d\n"
                                "\tsize: %d x %d\n",
                                (int)points.width, (int)points.height, (int)pixels.width, (int)pixels.height,
                                (int)design.width, (int)design.height, (int)res, (int)visibleRect.origin.x,
                                (int)visibleRect.origin.y, (int)visibleRect.size.width, (int)visibleRect.size.height);
}

void Console::commandSceneGraph(socket_native_type fd, std::string_view /*args*/)
{
    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread(std::bind(&Console::printSceneGraphBoot, this, fd));
}

void Console::commandTextures(socket_native_type fd, std::string_view /*args*/)
{
    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread([=]() {
        Console::Utility::mydprintf(fd, "%s",
                                    Director::getInstance()->getTextureCache()->getCachedTextureInfo().c_str());
        Console::Utility::sendPrompt(fd);
    });
}

void Console::commandTexturesSubCommandFlush(socket_native_type /*fd*/, std::string_view /*args*/)
{
    Scheduler* sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread([]() { Director::getInstance()->getTextureCache()->removeAllTextures(); });
}

void Console::commandTouchSubCommandTap(socket_native_type fd, std::string_view args)
{
    auto argv = Console::Utility::split(args, ' ');

    if ((argv.size() == 3) && (Console::Utility::isFloat(argv[1]) && Console::Utility::isFloat(argv[2])))
    {

        float x = (float)utils::atof(argv[1].c_str());
        float y = (float)utils::atof(argv[2].c_str());

        std::srand((unsigned)time(nullptr));
        _touchId         = rand();
        Scheduler* sched = Director::getInstance()->getScheduler();
        sched->performFunctionInCocosThread([&]() {
            Director::getInstance()->getOpenGLView()->handleTouchesBegin(1, &_touchId, &x, &y);
            Director::getInstance()->getOpenGLView()->handleTouchesEnd(1, &_touchId, &x, &y);
        });
    }
    else
    {
        const char msg[] = "touch: invalid arguments.\n";
        Console::Utility::sendToConsole(fd, msg, strlen(msg));
    }
}

void Console::commandTouchSubCommandSwipe(socket_native_type fd, std::string_view args)
{
    auto argv = Console::Utility::split(args, ' ');

    if ((argv.size() == 5) && (Console::Utility::isFloat(argv[1])) && (Console::Utility::isFloat(argv[2])) &&
        (Console::Utility::isFloat(argv[3])) && (Console::Utility::isFloat(argv[4])))
    {

        float x1 = (float)utils::atof(argv[1].c_str());
        float y1 = (float)utils::atof(argv[2].c_str());
        float x2 = (float)utils::atof(argv[3].c_str());
        float y2 = (float)utils::atof(argv[4].c_str());

        std::srand((unsigned)time(nullptr));
        _touchId = rand();

        Scheduler* sched = Director::getInstance()->getScheduler();
        sched->performFunctionInCocosThread([=]() {
            float tempx = x1, tempy = y1;
            Director::getInstance()->getOpenGLView()->handleTouchesBegin(1, &_touchId, &tempx, &tempy);
        });

        float dx  = std::abs(x1 - x2);
        float dy  = std::abs(y1 - y2);
        float _x_ = x1, _y_ = y1;
        if (dx > dy)
        {
            while (dx > 1)
            {

                if (x1 < x2)
                {
                    _x_ += 1;
                }
                if (x1 > x2)
                {
                    _x_ -= 1;
                }
                if (y1 < y2)
                {
                    _y_ += dy / dx;
                }
                if (y1 > y2)
                {
                    _y_ -= dy / dx;
                }
                sched->performFunctionInCocosThread([=]() {
                    float tempx = _x_, tempy = _y_;
                    Director::getInstance()->getOpenGLView()->handleTouchesMove(1, &_touchId, &tempx, &tempy);
                });
                dx -= 1;
            }
        }
        else
        {
            while (dy > 1)
            {
                if (x1 < x2)
                {
                    _x_ += dx / dy;
                }
                if (x1 > x2)
                {
                    _x_ -= dx / dy;
                }
                if (y1 < y2)
                {
                    _y_ += 1;
                }
                if (y1 > y2)
                {
                    _y_ -= 1;
                }
                sched->performFunctionInCocosThread([=]() {
                    float tempx = _x_, tempy = _y_;
                    Director::getInstance()->getOpenGLView()->handleTouchesMove(1, &_touchId, &tempx, &tempy);
                });
                dy -= 1;
            }
        }

        sched->performFunctionInCocosThread([=]() {
            float tempx = x2, tempy = y2;
            Director::getInstance()->getOpenGLView()->handleTouchesEnd(1, &_touchId, &tempx, &tempy);
        });
    }
    else
    {
        const char msg[] = "touch: invalid arguments.\n";
        Console::Utility::sendToConsole(fd, msg, strlen(msg));
    }
}

static char invalid_filename_char[] = {':', '/', '\\', '?', '%', '*', '<', '>', '"', '|', '\r', '\n', '\t'};

void Console::commandUpload(socket_native_type fd)
{
    ssize_t n, rc;
    char buf[512] = {0};
    char c        = 0;
    char* ptr     = buf;
    // read file name
    for (n = 0; n < sizeof(buf) - 1; n++)
    {
        if ((rc = recv(fd, &c, 1, 0)) == 1)
        {
            for (char x : invalid_filename_char)
            {
                if (c == x)
                {
                    const char err[] = "upload: invalid file name!\n";
                    Console::Utility::sendToConsole(fd, err, strlen(err));
                    return;
                }
            }
            if (c == ' ')
            {
                break;
            }
            *ptr++ = c;
        }
        else if (rc == 0)
        {
            break;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }
    *ptr = 0;

    static std::string writablePath = FileUtils::getInstance()->getWritablePath();
    std::string filepath            = writablePath + std::string(buf);

    auto fs = FileUtils::getInstance()->openFileStream(filepath, FileStream::Mode::WRITE);
    if (!fs)
    {
        const char err[] = "can't create file!\n";
        Console::Utility::sendToConsole(fd, err, strlen(err));
        return;
    }

    while (true)
    {
        char data[4];
        for (int i = 0; i < 4; i++)
        {
            data[i] = '=';
        }
        bool more_data;
        readBytes(fd, data, 4, &more_data);
        if (!more_data)
        {
            break;
        }
        unsigned char* decode;
        unsigned char* in = (unsigned char*)data;
        int dt            = base64Decode(in, 4, &decode);
        if (dt > 0)
        {
            fs->write(decode, dt);
        }
        free(decode);
    }
}

void Console::commandVersion(socket_native_type fd, std::string_view /*args*/)
{
    Console::Utility::mydprintf(fd, "%s\n", axisVersion());
}

// helper free functions

int Console::printSceneGraph(socket_native_type fd, Node* node, int level)
{
    int total = 1;
    for (int i = 0; i < level; ++i)
        Console::Utility::sendToConsole(fd, "-", 1);

    Console::Utility::mydprintf(fd, " %s\n", node->getDescription().c_str());

    for (const auto& child : node->getChildren())
        total += printSceneGraph(fd, child, level + 1);

    return total;
}

void Console::printSceneGraphBoot(socket_native_type fd)
{
    Console::Utility::sendToConsole(fd, "\n", 1);
    auto scene = Director::getInstance()->getRunningScene();
    int total  = printSceneGraph(fd, scene, 0);
    Console::Utility::mydprintf(fd, "Total Nodes: %d\n", total);
    Console::Utility::sendPrompt(fd);
}

void Console::printFileUtils(socket_native_type fd)
{
    FileUtils* fu = FileUtils::getInstance();

    Console::Utility::mydprintf(fd, "\nSearch Paths:\n");
    auto& list = fu->getSearchPaths();
    for (const auto& item : list)
    {
        Console::Utility::mydprintf(fd, "%s\n", item.c_str());
    }

    Console::Utility::mydprintf(fd, "\nResolution Order:\n");
    auto& list1 = fu->getSearchResolutionsOrder();
    for (const auto& item : list1)
    {
        Console::Utility::mydprintf(fd, "%s\n", item.c_str());
    }

    Console::Utility::mydprintf(fd, "\nWritable Path:\n");
    Console::Utility::mydprintf(fd, "%s\n", fu->getWritablePath().c_str());

    Console::Utility::mydprintf(fd, "\nFull Path Cache:\n");
    auto& cache = fu->getFullPathCache();
    for (const auto& item : cache)
    {
        Console::Utility::mydprintf(fd, "%s -> %s\n", item.first.c_str(), item.second.c_str());
    }
    Console::Utility::sendPrompt(fd);
}

void Console::sendHelp(socket_native_type fd, const hlookup::string_map<Command*>& commands, const char* msg)
{
    Console::Utility::sendToConsole(fd, msg, strlen(msg));
    for (auto& it : commands)
    {
        auto command = it.second;
        if (command->getHelp().empty())
            continue;

        Console::Utility::mydprintf(fd, "\t%s", command->getName().data());
        ssize_t tabs = command->getName().length() / 8;
        tabs         = 3 - tabs;
        for (int j = 0; j < tabs; j++)
        {
            Console::Utility::mydprintf(fd, "\t");
        }
        Console::Utility::mydprintf(fd, "%s\n", command->getHelp().data());
    }
}

NS_AX_END
