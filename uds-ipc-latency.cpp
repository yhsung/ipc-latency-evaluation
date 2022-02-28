// uds-ipc-latency.cpp - Measure latency of unary IPC calls over a Unix domain socket using blocking I/O.
//
// Compile: g++ -std=c++17 -O2 -march=native -mtune=native -o uds-ipc-latency uds-ipc-latency.cpp
// Run on 1st and 2nd core: ./uds-ipc-latency 0x1 0x2 > uds-ipc-latencies-nsec.txt
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include <sys/socket.h>
#include <sys/errno.h>
#include <unistd.h>

#if __linux__
#include <sched.h>
#endif

using namespace std;

struct DummyMsg {
    int64_t i;
};

void die(const char *msg) {
    fprintf(stderr, "%s: %s (%d)\n", msg, strerror(errno), errno);
    exit(1);
}

void call(int socket, DummyMsg &msg) {
    if (int rc = write(socket, &msg, sizeof(msg)); rc != sizeof(msg))
        die("write() failed");

    if (int rc = read(socket, &msg, sizeof(msg)); rc != sizeof(msg))
        die("read() failed");
}

int main(int argc, const char *argv[])
{
#if __linux__
    cpu_set_t parent_mask, child_mask;
    CPU_ZERO(&parent_mask);
    CPU_ZERO(&child_mask);
    if (argc == 3) {
        sscanf(argv[1], "%x", &parent_mask);
        sscanf(argv[2], "%x", &child_mask);
    }
#endif

    int sockets[2];
    if (int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets); rc == -1)
        die("socketpair() failed");

    pid_t pid = fork();
    if (pid == -1)
        die("fork() failed");

    if (pid == 0) {  // child
        close(sockets[0]); // close parent's socket
        #if __linux__
        if (int rc = sched_setaffinity(0, sizeof(child_mask), &child_mask); rc == -1)
            die("sched_setaffinity() failed");
        #endif

        DummyMsg msg{.i = 0};
        for (int i = 0; i < 1000; i++)
            call(sockets[1], msg);  // warmup

        std::vector<int> latencies;
        latencies.reserve(1000000);
        for (int i = 0; i < 1000000; i++) {
            auto start = chrono::high_resolution_clock::now();
            call(sockets[1], msg);
            auto end = chrono::high_resolution_clock::now();

            latencies.push_back(chrono::duration_cast<chrono::nanoseconds>(end - start).count());
        }

        for (auto l : latencies)
            printf("%i\n", l);

        close(sockets[1]);
    } else {  // parent
        close(sockets[1]);  // close child's socket

        #if __linux__
        if (int rc = sched_setaffinity(0, sizeof(parent_mask), &parent_mask); rc == -1)
            die("sched_setaffinity() failed");
        #endif

        DummyMsg msg;
        while (1) {
            if (int rc = read(sockets[0], &msg, sizeof(msg)); rc != sizeof(msg))
                die("read() failed");

            msg.i++;

            if (int rc = write(sockets[0], &msg, sizeof(msg)); rc != sizeof(msg))
                die("write() failed");
        }

        close(sockets[0]);
    }
}
