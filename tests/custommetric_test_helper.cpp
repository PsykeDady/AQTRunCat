#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace {

int number(const char *text)
{
    return std::atoi(text);
}

void sleepMilliseconds(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
        return 64;
    const std::string mode = argv[1];
    if (mode == "value" && argc == 3) {
        std::cout << argv[2] << '\n';
        return 0;
    }
    if (mode == "arguments") {
        for (int i = 2; i < argc; ++i) {
            if (i != 2)
                std::cout << '|';
            std::cout << argv[i];
        }
        std::cout << '\n';
        return 0;
    }
    if (mode == "cwd") {
        std::cout << std::filesystem::current_path().string() << '\n';
        return 0;
    }
    if (mode == "sleep" && argc == 4) {
        sleepMilliseconds(number(argv[2]));
        std::cout << argv[3] << '\n';
        return 0;
    }
    if (mode == "exit" && argc >= 3) {
        if (argc >= 4)
            std::cerr << argv[3] << '\n';
        return number(argv[2]);
    }
    if (mode == "multiline") {
        std::cout << "first\nsecond\n";
        return 0;
    }
    if (mode == "flood" && argc == 3) {
        const int bytes = number(argv[2]);
        for (int i = 0; i < bytes; ++i)
            std::cout.put('x');
        return 0;
    }
    if (mode == "flood-stderr" && argc == 3) {
        const int bytes = number(argv[2]);
        for (int i = 0; i < bytes; ++i)
            std::cerr.put('x');
        return 0;
    }
    if (mode == "counter" && argc == 3) {
        int value = 0;
        std::ifstream input(argv[2]);
        input >> value;
        input.close();
        std::ofstream output(argv[2], std::ios::trunc);
        output << ++value;
        std::cout << value << '\n';
        return 0;
    }
    if (mode == "overlap" && argc == 5) {
        const int descriptor = ::open(argv[2], O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor < 0) {
            std::ofstream violation(argv[3]);
            violation << errno;
        } else {
            ::close(descriptor);
        }
        sleepMilliseconds(number(argv[4]));
        ::unlink(argv[2]);
        std::cout << "done\n";
        return 0;
    }
    if (mode == "pid-sleep" && argc == 4) {
        std::ofstream pid(argv[2], std::ios::trunc);
        pid << ::getpid();
        pid.close();
        sleepMilliseconds(number(argv[3]));
        std::cout << "done\n";
        return 0;
    }
    if (mode == "grandchild-sleep" && argc == 4) {
        // A script-like command that leaves a helper process behind: the
        // grandchild records its pid, both sleep past any test deadline.
        const pid_t child = ::fork();
        if (child < 0)
            return 70;
        if (child == 0) {
            std::ofstream pid(argv[2], std::ios::trunc);
            pid << ::getpid();
            pid.close();
        }
        sleepMilliseconds(number(argv[3]));
        std::cout << "done\n";
        return 0;
    }
    // v2F1.3: what to do comes from a file the test rewrites between runs:
    // "<exit status>\n<standard output>\n<standard error...>".
    if (mode == "control" && argc == 3) {
        std::ifstream input(argv[2]);
        std::string status, out, line, err;
        std::getline(input, status);
        std::getline(input, out);
        while (std::getline(input, line))
            err += line + '\n';
        if (!out.empty())
            std::cout << out << '\n';
        std::cerr << err;
        return number(status.c_str());
    }
    if (mode == "touch" && argc == 3) {
        std::ofstream output(argv[2]);
        output << "executed";
        std::cout << "done\n";
        return 0;
    }
    return 65;
}
