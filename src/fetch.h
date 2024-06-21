/**
 * @file
 */
#pragma once

#include "color.h"
#include "config.h"
#include <sys/errno.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

// fetch.cpp

string getuser();

string gethostname(string path);

string getOS(string path);

string getHardwarePlatform();

string getHost(string path);

string getKernel(string path);

string getUpTime(string path);

string getRAM(string path);

string getSHELL(string path);

string getDE();

bool resCheck();

string getRES(string path);

string getTheme();

string getIcons();

string getCPU(string path);

bool CpuTempCheck();

int getCPUtemp(string path);

vector<string> getGPU();

string getPackages();

void printBattery(string path);

void print(string color, string distro_name);

string getColor(string);

/**
 * A Path represents a path.
 */
class Path
{
  private:
    filesystem::path path;
    filesystem::file_status status;

    Path(filesystem::path path, filesystem::file_status status)
    {
        this->path = path;
        this->status = status;
    }

  public:
    /**
     * @returns Path object
     * @param path
     */
    static Path of(const string &path)
    {
        return Path(filesystem::path(path), filesystem::status(path));
    }

    /**
     * @returns the filename of the path.
     */
    Path getFilename()
    {
        filesystem::path p = this->path.filename();
        return Path(p, filesystem::status(p));
    }

    /**
     * @returns exists and is a regular file
     */
    bool isRegularFile()
    {
        return filesystem::is_regular_file(status);
    }

    /**
     * @returns exists and is a executable(or searchable)
     */
    bool isExecutable()
    {
        if (status.permissions() == filesystem::perms::unknown)
            return false;
        return (status.permissions() & filesystem::perms::others_exec) !=
               filesystem::perms::none;
    }

    /**
     * @returns exists and is a directory
     */
    bool isDirectory()
    {
        return filesystem::is_directory(status);
    }

    /**
     * @returns readable reprsentation for dev.
     */
    string toString() const
    {
        return path.string();
    }

    /**
     * @returns a vector containing absolute paths to contents at the given path
     */
    vector<Path> getDirectoryContents()
    {
        vector<Path> contents;
        if (!this->isDirectory())
        {
            return contents;
        }

        for (const auto &entry : filesystem::directory_iterator(path))
        {
            contents.push_back(Path::of(entry.path()));
        }
        return contents;
    }
};

/**
 * Command executes a command in a subshell(shell runs on a forked process).
 *
 * Sample code:
 * ```cpp
 * auto c = Command::exec("ls -l");
 * if (c.getExitcode() == 0) {
 *     std::cout << c.getOutput();
 * }
 * ```
 */
class Command
{
  public:
    typedef std::function<void(Command *)> func_type;

  private:
    int exit_code;
    string output;
    int lines;
    static std::vector<std::thread> ths;
    static std::vector<std::runtime_error> exceptions;
    static std::mutex mtx;

    Command()
    {
        output = string();
        lines = 0;
    }

    static void func1(string cmd, int out, int err)
    {
        char** argv = split(cmd);
        dup2(out, fileno(stdout)); // TODO: returns -1 if error occured
        dup2(err, fileno(stderr)); // TODO: returns -1 if error occured
        execvp(argv[0], argv); // TODO: error
        cout << "Debug: cannot execute: " << strerror(errno) << ": " << argv[0] << endl;
    }

    static char** split(string cmd) {
        vector<string> v;

        string token;
        for(char c : cmd) {
            if (c == ' ') {
                v.push_back(token);
                token = "";
                continue;
            }
            token += c;
        }
        v.push_back(token);

        char **argv = (char **)calloc(v.size() + 1, sizeof(char *)); // +1 for the terminating NULL pointer 
        if (argv == NULL) {
            cout << "Debug: cannot calloc" << endl;
        }
        char **p = argv;
        for (string s : v) {
            if((*p = strdup(s.c_str())) == NULL) {
                cout << "Debug: cannot strdup" << endl;
            }
            p++;
        }
        *p = (char *)0; // terminated by a NULL pointer

        return argv;
    }

  public:
    /**
     * Wait for all threads to be finished.
     */
    static void wait()
    {
        for (auto &t : ths)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    /**
     * @returns exceptions
     */
    static vector<runtime_error> &getExceptions()
    {
        return exceptions;
    }

    /**
     * Executes the specified command in a new thread.
     * @param cmd containing the command to call and its arguments
     * @param func to be performed after cmd is finished
     */
    static void exec_async(const string &cmd, const func_type &func)
    {
        ths.push_back(std::thread([=]() {
            try
            {
                auto result = exec(cmd);
                func(result);
            }
            catch (const runtime_error &e)
            {
                std::lock_guard<std::mutex> lock(mtx);
                exceptions.push_back(e);
            }
        }));
    }

    /**
     * Executes the specified command in a new thread.
     * @param cmd containing the command
     * @param arg arguments
     * @param func to be performed after cmd is finished
     */
    static void exec_async(const Path &cmd, const string &arg,
                           const func_type &func)
    {
        exec_async(cmd.toString() + " " + arg, func);
    }

    /**
     * Executes the specified command.
     * @param cmd containing the command to call and its arguments
     * @returns Command object for getting the results.
     * @throws runtime_error failed to popen(3)
     */
    static Command *exec(const string &cmd)
    {
        auto result = new Command();
        int out_fd[2], err_fd[2];
        pid_t pid;

        if(pipe(out_fd) == -1) {
            cout << "Debug: cannot create pipe";
        }
        if(pipe(err_fd) == -1) {
            cout << "Debug: cannot create pipe";
        }

        if ((pid = fork()) < 0) {
            // TODO: error
            cout << "Debug: cannot fork";
            exit(1);
        } else if (pid == 0) { // child
            // TODO: error handling
            close(out_fd[0]);
            close(err_fd[0]);
            func1(cmd, out_fd[1], err_fd[1]);
            // return nullptr;
            exit(1);
        }

        // parent
        close(out_fd[1]);
        close(err_fd[1]);
        FILE* out = fdopen(out_fd[0], "r"); // TODO: error
        if (out == NULL) {
            cout << "Debug: cannot fdopen";
        }
        FILE* err = fdopen(err_fd[0], "r"); // TODO: error
        if (err == NULL) {
            cout << "Debug: cannot fdopen";
        }

        int c;
        while ((c = fgetc(out)) != EOF)
        {
            if (c == '\n')
            {
                result->lines += 1;
            }
            result->output += c;
        }

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            result->exit_code = WEXITSTATUS(status) ;
        } else {
            cout << "Debug: status: " << status;
            exit(1);
            // TODO: error
        }

        return result;
    }

    /**
     * @returns get contents written by the command to standard output
     */
    string getOutput()
    {
        return output;
    }

    /**
     * @returns get the new line counts of the output.
     */
    int getOutputLines()
    {
        return lines;
    }

    /**
     * @returns get the exit code of the command.
     */
    int getExitCode()
    {
        return exit_code;
    }
};

/**
 * String Styling for terminal.
 */
class Crayon
{
  private:
    string escape_codes;
    inline static map<string, string> m = {
        {"RED", RED},         {"GREEN", GREEN},   {"BLACK", BLACK},
        {"YELLOW", YELLOW},   {"BLUE", BLUE},     {"MAGENTA", MAGENTA},
        {"CYAN", CYAN},       {"WHITE", WHITE},   {"BBLACK", BBLACK},
        {"BGRAY", BGRAY},     {"BRED", BRED},     {"BGREEN", BGREEN},
        {"BYELLOW", BYELLOW}, {"BBLUE", BBLUE},   {"BMAGENTA", BMAGENTA},
        {"BCYAN", BCYAN},     {"BWHITE", BWHITE},
    };

    static string getColor(string s)
    {
        return m[s];
    }

  public:
    /**
     * default constructor
     */
    Crayon()
    {
        escape_codes = ""s;
    }
    /**
     * set bright mode
     */
    Crayon bright()
    {
        escape_codes += BRIGHT;
        return *this;
    }
    /**
     * set underscore mode
     */
    Crayon underscore()
    {
        escape_codes += UNDERSCORE;
        return *this;
    }
    /**
     * set color
     * @param color
     */
    Crayon color(string color)
    {
        escape_codes += getColor(color);
        return *this;
    }
    /** set color red */
    Crayon red()
    {
        escape_codes += RED;
        return *this;
    }
    /** set color green */
    Crayon green()
    {
        escape_codes += GREEN;
        return *this;
    }
    /** set color yellow */
    Crayon yellow()
    {
        escape_codes += YELLOW;
        return *this;
    }
    /**
     * @param s
     * @returns styled text
     */
    string text(string s)
    {
        return escape_codes + s + RESET;
    }
};

class Context
{
  public:
    static string PACKAGE_DELIM;
};

enum class Mode
{
    NORMAL,
    SHOW_VERSION,
};

class Options
{
  public:
    Mode mode = Mode::NORMAL;
    string color_name = "def"s;
    string distro_name = "def"s;
    bool show_battery = false;

    Options(){};
    Options(int argc, char *argv[])
    {
        int opt;
        while ((opt = getopt(argc, argv, "a:d:vb")) != -1)
        {

            switch (opt)
            {
            case 'a':
                color_name = string(optarg);
                break;
            case 'd':
                distro_name = string(optarg);
                break;
            case 'b':
                show_battery = true;
                break;
            case 'v':
                mode = Mode::SHOW_VERSION;
                break;
            default:
                exit(1);
            }
        }
    }
};
