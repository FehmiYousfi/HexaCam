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
    FILE* pipe = popen("ps -aux | grep JoystickIdentifier | grep -v grep", "r");
    if (!pipe) {
        perror("popen failed");
        return;
    }

    std::vector<pid_t> pids;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;
        
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        if (tokens.size() > 1) {
            try {
                pid_t pid = static_cast<pid_t>(std::stoi(tokens[1]));
                pids.push_back(pid);
            } catch (const std::exception& e) {
                // Invalid PID format, skip
            }
        }
    }
    pclose(pipe);

    for (pid_t pid : pids) {
        if (kill(pid, SIGKILL) == 0) {
            printf("Killed process %d\n", pid);
        } else {
            perror(("Failed to kill process " + std::to_string(pid)).c_str());
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
