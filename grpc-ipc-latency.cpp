// grpc-ipc-latency.cpp - Measure latency of unary IPC calls over a Unix domain socket using gRPC.
//
// Compile: g++ -std=c++17 -O2 -march=native -mtune=native -o grpc-ipc-latency grpc-ipc-latency.cpp
// Run on 1st and 2nd core: ./grpc-ipc-latency 0x1 0x2 > grpc-ipc-latencies-nsec.txt
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/errno.h>
#include <unistd.h>

#if __linux__
#include <sched.h>
#endif

#include <grpcpp/grpcpp.h>

#include "trivial.ipc.grpc.pb.h"

using namespace std;
using namespace trivial::ipc;
using namespace grpc;
using namespace std::chrono_literals;

void die(const char *msg) {
    fprintf(stderr, "%s: %s (%d)\n", msg, strerror(errno), errno);
    exit(1);
}

class RPCServiceImpl final : public RPCService::Service {
  Status UnaryCall(ServerContext* /*context*/, const UnaryCallRequest* request,
                   UnaryCallReply* reply) override {
    reply->set_i(request->i() + 1);
    return Status::OK;
  }
};

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

    std::string address{"unix:///tmp/test.socket"};

    pid_t pid = fork();
    if (pid == -1)
        die("fork() failed");

    if (pid == 0) {  // child
        #if __linux__
        if (int rc = sched_setaffinity(0, sizeof(child_mask), &child_mask); rc == -1)
            die("sched_setaffinity() failed");
        #endif

        std::this_thread::sleep_for(100ms);  // wait for setup

        auto client = RPCService::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));

        UnaryCallRequest req{};
        req.set_i(0);

        UnaryCallReply rep{};
        for (int i = 0; i < 1000; i++) {  // warmup
            ClientContext ctx{};
            if (grpc::Status status = client->UnaryCall(&ctx, req, &rep); !status.ok())
                die("UnaryCall failed");
        }

        std::vector<int> latencies;
        latencies.reserve(1000000);
        for (int i = 0; i < 1000000; i++) {
            auto start = chrono::high_resolution_clock::now();
            ClientContext ctx{};
            if (grpc::Status status = client->UnaryCall(&ctx, req, &rep); !status.ok())
                die("UnaryCall failed");
            auto end = chrono::high_resolution_clock::now();

            latencies.push_back(chrono::duration_cast<chrono::nanoseconds>(end - start).count());
        }

        for (auto l : latencies)
            printf("%i\n", l);
    } else {  // parent
        #if __linux__
        if (int rc = sched_setaffinity(0, sizeof(parent_mask), &parent_mask); rc == -1)
            die("sched_setaffinity() failed");
        #endif

        RPCServiceImpl service;
        ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        std::unique_ptr<Server> server(builder.BuildAndStart());
        server->Wait();
    }
}
