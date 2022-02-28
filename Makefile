CXX = g++
CPPFLAGS += `pkg-config --cflags protobuf grpc`
CXXFLAGS += -std=c++17 -O2 -march=native -mtune=native

SYSTEM ?= $(shell uname | cut -f 1 -d_)
LDFLAGS += `pkg-config --libs protobuf grpc++ grpc`
ifeq ($(SYSTEM),Darwin)
LDFLAGS += -lgrpc++_reflection
else
LDFLAGS += -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed
endif
LDFLAGS += -ldl

all: uds-ipc-latency grpc-ipc-latency

uds-ipc-latency: uds-ipc-latency.o
	$(CXX) $^ $(LDFLAGS) -o $@

grpc-ipc-latency: trivial.ipc.pb.o trivial.ipc.grpc.pb.o grpc-ipc-latency.o
	$(CXX) $^ $(LDFLAGS) -o $@

.PRECIOUS: %.grpc.pb.cc
%.grpc.pb.cc: %.proto
	protoc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $<

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto
	protoc --cpp_out=. $<

clean:
	rm -f *.o *.pb.cc *.pb.h uds-ipc-latency grpc-ipc-latency

