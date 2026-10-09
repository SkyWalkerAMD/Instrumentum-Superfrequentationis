// SPDX-License-Identifier: GPL-2.0-only
#include "linux_helper.h"
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

extern char **environ;
namespace octool {
namespace platform {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int ioTimeout = 10000;

int transfer(int fd, std::uint8_t *bytes, std::size_t size, bool writing,
             int timeoutMs, const std::atomic<bool> *cancelled)
{
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? ioTimeout : timeoutMs);
    std::size_t done = 0;
    while (done != size) {
        if (cancelled && cancelled->load()) return -ECANCELED;
        int remaining = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        // The helper can wait idle without a timer, but a partial frame always
        // has a deadline. There is no retry of a submitted register operation.
        if (timeoutMs < 0 && !done) remaining = -1;
        else if (remaining <= 0) return -ETIMEDOUT;
        if (cancelled && (remaining < 0 || remaining > 100)) remaining = 100;
        pollfd descriptor{fd, short(writing ? POLLOUT : POLLIN), 0};
        const int ready = poll(&descriptor, 1, remaining);
        if (ready < 0) { if (errno == EINTR) continue; return -errno; }
        if (!ready) continue;
        const auto count = writing ? send(fd, bytes + done, size - done, MSG_DONTWAIT | MSG_NOSIGNAL) :
            recv(fd, bytes + done, size - done, MSG_DONTWAIT);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -errno;
        }
        if (!count) return done ? -EPROTO : -EPIPE;
        if (!done && timeoutMs < 0) deadline = Clock::now() + std::chrono::milliseconds(ioTimeout);
        done += std::size_t(count);
    }
    return 0;
}

void retireChild(int pid)
{
    if (pid <= 0) return;
    int state;
    do { state = waitpid(pid, nullptr, WNOHANG); } while (state < 0 && errno == EINTR);
    if (state != 0) return;
    // A pending pkexec still has the invoking real UID and can be cancelled.
    // An authorized root helper exits on socket EOF, not on this signal.
    kill(pid, SIGTERM);
    {
        // Reaping performs no requests and retains no transport. It must not
        // block the GUI if a dying helper is stuck in an OS syscall.
        try {
            std::thread([pid] { while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }).detach();
        } catch (...) { /* The OS reaps it when this process exits. */ }
    }
}

class HelperBackend final : public core::HardwareBackend {
public:
    HelperBackend(int fd, int child) : fd_(fd), child_(child) {}
    ~HelperBackend() override { disconnect(); }
    core::Reply execute(const core::Request &request) override {
        ipc::Command command; command.operation = ipc::Operation::Execute; command.request = request;
        const auto response = transact(command);
        core::Reply reply; reply.error = response.error; reply.value = response.value;
        return reply;
    }
    core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        ipc::Command command; command.operation = ipc::Operation::Cpuid;
        command.request.cpu = cpu; command.leaf = leaf; command.subleaf = subleaf;
        const auto response = transact(command);
        core::CpuIdReply reply; reply.error = response.error;
        if (!reply.error) for (unsigned i = 0; i < 4; ++i) reply.words[i] = response.words[i];
        return reply;
    }
    core::Backend backend(core::Space space) const override {
        ipc::Command command; command.operation = ipc::Operation::Backend; command.request.space = space;
        const auto response = transact(command);
        return response.error ? core::Backend::Unavailable : core::Backend(response.value);
    }
private:
    ipc::Response transact(const ipc::Command &command) const {
        ipc::Response response;
        if (fd_ < 0) { response.error = -ENOTCONN; return response; }
        ipc::Frame frame;
        int error = sendHelperFrame(fd_, ipc::encodeCommand(command), ioTimeout);
        if (!error) error = receiveHelperFrame(fd_, frame, ioTimeout);
        if (!error && !ipc::decodeResponse(frame, command.operation, response)) error = -EPROTO;
        if (error) {
            disconnect(); response = ipc::Response{}; response.error = error;
        }
        return response;
    }
    void disconnect() const {
        if (fd_ >= 0) { shutdown(fd_, SHUT_RDWR); close(fd_); fd_ = -1; }
        retireChild(child_); child_ = -1;
    }
    mutable int fd_, child_;
};
} // namespace

int sendHelperFrame(int fd, const ipc::Frame &frame, int timeoutMs)
{
    auto bytes = frame;
    return transfer(fd, bytes.data(), bytes.size(), true, timeoutMs, nullptr);
}
int receiveHelperFrame(int fd, ipc::Frame &frame, int timeoutMs, const std::atomic<bool> *cancelled)
{
    return transfer(fd, frame.data(), frame.size(), false, timeoutMs, cancelled);
}

int serveHardwareRequests(core::HardwareBackend &backend, int input, int output)
{
    const auto hello = ipc::encodeResponse(ipc::Operation::Hello, ipc::Response{});
    if (sendHelperFrame(output, hello, ioTimeout)) return 1;
    for (;;) {
        ipc::Frame frame;
        const int error = receiveHelperFrame(input, frame, -1);
        if (error == -EPIPE) return 0;
        if (error) return 1;
        ipc::Command command;
        if (!ipc::decodeCommand(frame, command)) return 2;
        auto response = ipc::dispatch(backend, command);
        if (sendHelperFrame(output, ipc::encodeResponse(command.operation, response), ioTimeout)) return 1;
    }
}

std::unique_ptr<core::HardwareBackend> makeHelperBackend(int fd, int childPid)
{
    return std::unique_ptr<core::HardwareBackend>(new HelperBackend(fd, childPid));
}

BackendConnection authorizeHardware(const std::atomic<bool> &cancelled)
{
    BackendConnection result;
    if (cancelled.load()) { result.error = -ECANCELED; return result; }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) { result.error = -errno; return result; }
    // A desktop launcher may start without stdin/stdout. Keep the socket
    // descriptors above stdio before constructing dup/close spawn actions.
    for (auto &fd : pair) {
        if (fd > 2) continue;
        const int replacement = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) { result.error = -errno; close(pair[0]); close(pair[1]); return result; }
        close(fd); fd = replacement;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) { close(pair[0]); close(pair[1]); result.error = -error; return result; }
    error = posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, pair[0]);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, pair[1]);
    char executable[] = "/usr/bin/pkexec";
    char noAgent[] = "--disable-internal-agent";
    char helper[] = "/opt/octool/bin/octool-hwio-helper";
    char *arguments[] = {executable, noAgent, helper, nullptr};
    pid_t child = -1;
    if (!error) error = posix_spawn(&child, executable, &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (error) { close(pair[0]); result.error = -error; return result; }
    ipc::Frame frame;
    error = receiveHelperFrame(pair[0], frame, 120000, &cancelled);
    ipc::Response hello;
    if (!error && !ipc::decodeResponse(frame, ipc::Operation::Hello, hello)) error = -EPROTO;
    if (!error) error = hello.error;
    if (!error && cancelled.load()) error = -ECANCELED;
    if (error) {
        close(pair[0]);
        int status = 0;
        pid_t state = 0;
        // EOF may precede pkexec's waitable exit by a scheduler tick. Give its
        // documented denial/cancel status a bounded chance to arrive.
        const auto exitDeadline = Clock::now() + std::chrono::milliseconds(200);
        do {
            state = waitpid(child, &status, WNOHANG);
            if (state < 0 && errno == EINTR) continue;
            if (state || error != -EPIPE || cancelled.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (Clock::now() < exitDeadline);
        if (state == child) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 126) error = -ECANCELED;
            else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) error = -EACCES;
        } else if (!state) retireChild(child);
        result.error = error; return result;
    }
    result.backend = makeHelperBackend(pair[0], child);
    return result;
}

} // namespace platform
} // namespace octool
