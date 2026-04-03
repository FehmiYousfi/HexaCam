#include "signalhandler.h"
#include <QCoreApplication>
#include <QDebug>
#include <csignal>
#include <unistd.h>

int SignalHandler::sigintFd[2];

#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

void killExistingInstances() {
    // Get our own binary name from /proc/self/exe so it works regardless
    // of what the binary is called.
    pid_t self = getpid();
    char exePath[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) return;
    exePath[len] = '\0';

    std::string fullPath(exePath);
    std::string binaryName = fullPath.substr(fullPath.rfind('/') + 1);

    std::string cmd = "pgrep -f " + binaryName;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    std::vector<pid_t> pids;
    char buffer[64];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        try {
            pid_t pid = static_cast<pid_t>(std::stoi(std::string(buffer)));
            if (pid != self) pids.push_back(pid);
        } catch (...) {}
    }
    pclose(pipe);

    // SIGTERM first for graceful shutdown, then SIGKILL stragglers
    for (pid_t pid : pids) {
        kill(pid, SIGTERM);
    }
    if (!pids.empty()) {
        usleep(2000000);
        for (pid_t pid : pids) {
            if (kill(pid, 0) == 0) {
                kill(pid, SIGKILL);
            }
        }
    }
}

SignalHandler::SignalHandler(QObject *parent) 
    : QObject(parent)
{
    if (::pipe(sigintFd) != 0) {
        qFatal("Failed to create pipe");
    }

    sn = new QSocketNotifier(sigintFd[0], QSocketNotifier::Read, this);
    connect(sn, &QSocketNotifier::activated, this, &SignalHandler::handleSignal);

    struct sigaction sa;
    sa.sa_handler = [](int) {
        char a = 1;
        ::write(sigintFd[1], &a, sizeof(a));
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        qFatal("Failed to install SIGINT handler");
    }
}

SignalHandler::~SignalHandler()
{
    close(sigintFd[0]);
    close(sigintFd[1]);
    delete sn;
}

void SignalHandler::handleSignal()
{
    char tmp;
    ::read(sigintFd[0], &tmp, sizeof(tmp));
    qDebug() << "Initiating shutdown sequence...";
    killExistingInstances();
    
    // Force immediate cleanup
    QCoreApplication::exit(0);
    QCoreApplication::processEvents();
}
