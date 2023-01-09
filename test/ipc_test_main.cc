// This file is mainly for verifying that boost's timeout API
// works properly. It may seem a little weird to be testing
// library code rather than our own code, but this gives
// confidence that if the driver is not handling timeouts
// properly, that's a bug in the driver.

#include <chrono>
#include <thread>
#include <cstdio>

#include "boost/process/child.hpp"
#include "boost/process/io.hpp"

#include "llvm/Support/JSON.h"

#include "indexer/Enforce.h"
#include "indexer/CliOptions.h"
#include "indexer/JsonIpcQueue.h"

using namespace scip_clang;
using namespace std::chrono_literals;

enum class Mode {
    Hang,
    Crash
};

std::string modeToString(Mode mode) {
    switch (mode) {
    case Mode::Hang: return "--hang";
    case Mode::Crash: return "--crash";
    }
}

Mode modeFromString(const char *s) {
    if (std::strcmp(s, "--hang") == 0) { return Mode::Hang; }
    ENFORCE(std::strcmp(s, "--crash") == 0);
    return Mode::Crash;
}

void toyWorkerMain(IpcOptions ipcOptions, Mode mode) {
    auto queues = MessageQueuePair::forWorker(ipcOptions);
    IpcTestMessage msg;
    auto err = queues.driverToWorker.timedReceive(msg, ipcOptions.receiveTimeout);
    ENFORCE(!err);
    switch (mode) {
    case Mode::Crash: {
        const char *p = nullptr;
        asm volatile("" ::: "memory");
        (void)*p;
    }
    case Mode::Hang: {
        std::this_thread::sleep_for(ipcOptions.receiveTimeout * 5);
        // This reply is too late!
        IpcTestMessage reply{"no u"};
        queues.workerToDriver.send(reply);
    }
    }
}

void toyDriverMain(const char *testExecutablePath, IpcOptions ipcOptions, Mode mode) {
    namespace boost_ip = boost::interprocess;
    auto d2w = scip_clang::driverToWorkerQueueName(ipcOptions.driverId, ipcOptions.workerId);
    auto w2d = scip_clang::workerToDriverQueueName(ipcOptions.driverId);
    boost_ip::message_queue::remove(d2w.c_str());
    boost_ip::message_queue::remove(w2d.c_str());
    JsonIpcQueue driverToWorker(std::make_unique<boost_ip::message_queue>(
        boost_ip::create_only, d2w.c_str(), 1, 256
    ));
    JsonIpcQueue workerToDriver(std::make_unique<boost_ip::message_queue>(
        boost_ip::create_only, w2d.c_str(), 1, 256
    ));

    std::vector<std::string> args;
    args.push_back(std::string(testExecutablePath));
    args.push_back(::modeToString(mode));
    args.push_back(ipcOptions.driverId);
    boost::process::child worker(args, boost::process::std_out > stdout);

    IpcTestMessage msg{"All your base are belong to us"};
    driverToWorker.send(msg);
    IpcTestMessage reply;
    auto err = workerToDriver.timedReceive(reply, ipcOptions.receiveTimeout);
    ENFORCE(err.isA<TimeoutError>());
}

int main(int argc, char *argv[]) {
    // If running as driver
    ENFORCE(argc >= 2, "expected --hang or --crash");
    std::string driverId;
    if (argc == 3) {
        driverId = std::string(argv[2]);
    } else {
        driverId = fmt::format("{}", ::getpid());
    }
    scip_clang::IpcOptions ipcOptions{1s, driverId, 0};
    Mode mode = ::modeFromString(argv[1]);
    if (argc == 3) {
        ::toyWorkerMain(ipcOptions, mode);
    } else {
        ::toyDriverMain(argv[0], ipcOptions, mode);
    }
}