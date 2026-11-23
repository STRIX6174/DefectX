#include "infrastructure/comm/modbus_tcp_transmitter.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

namespace qf {
namespace infrastructure {

namespace {

bool ensureSocketRuntime() {
#ifdef _WIN32
    static std::once_flag flag;
    static bool ok = false;
    std::call_once(flag, []() {
        WSADATA data{};
        ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    });
    return ok;
#else
    return true;
#endif
}

void closeSocket(socket_t s) {
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

std::string nowClock() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    char out[48] = {0};
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms.count()));
    return std::string(out);
}

std::string hexDump(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; ++i) {
        if (i) out += ' ';
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
    }
    return out;
}

enum class RxResult { Ok, Timeout, Closed };

RxResult recvExact(socket_t s, uint8_t* buf, size_t need, const std::atomic<bool>& running) {
    size_t got = 0;
    while (got < need) {
        int n = ::recv(s, reinterpret_cast<char*>(buf) + got, static_cast<int>(need - got), 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return RxResult::Closed;
#ifdef _WIN32
        const int err = WSAGetLastError();
        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEINTR) {
            if (!running.load()) return RxResult::Closed;
            continue;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (!running.load()) return RxResult::Closed;
            continue;
        }
#endif
        return RxResult::Closed;
    }
    return RxResult::Ok;
}

bool sendAll(socket_t s, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(s, reinterpret_cast<const char*>(buf) + sent,
                       static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void setRecvTimeout(socket_t s, int ms) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(ms);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

void setReuseAddr(socket_t s) {
#ifdef _WIN32
    BOOL yes = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
}

}  // namespace

ModbusTcpTransmitter::ModbusTcpTransmitter() = default;

ModbusTcpTransmitter::~ModbusTcpTransmitter() {
    close();
}

bool ModbusTcpTransmitter::open(const model::CommConfig& cfg) {
    close();

    if (!ensureSocketRuntime()) {
        setError("WSAStartup 失败");
        appendLog("✗ 网络初始化失败");
        return false;
    }
    if (cfg.tcp.port <= 0 || cfg.tcp.port > 65535) {
        setError("TCP 端口非法");
        appendLog("✗ TCP 端口非法：" + std::to_string(cfg.tcp.port));
        return false;
    }

    cfg_ = cfg;

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) {
        setError("创建监听 socket 失败");
        appendLog("✗ 创建监听 socket 失败");
        return false;
    }
    setReuseAddr(fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(cfg.tcp.port));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        setError("绑定端口失败（可能被占用）");
        appendLog("✗ 绑定端口 " + std::to_string(cfg.tcp.port) + " 失败");
        closeSocket(fd);
        return false;
    }
    if (::listen(fd, SOMAXCONN) != 0) {
        setError("listen 失败");
        appendLog("✗ listen 失败");
        closeSocket(fd);
        return false;
    }

    listenFd_ = static_cast<intptr_t>(fd);

    {
        std::lock_guard<std::mutex> lk(mtx_);
        image_.fill(0);
        batchSequence_ = 0;
        status_.open = true;
        status_.listening = true;
        status_.hasPeer = false;
        status_.transport = "tcp";
        status_.endpoint = "0.0.0.0:" + std::to_string(cfg.tcp.port);
        status_.lastError.clear();
        status_.batchSequence = 0;
        status_.isPartial = false;
    }

    running_ = true;
    thread_ = std::thread(&ModbusTcpTransmitter::serverLoop, this);

    appendLog("TCP 从站监听 " + std::string("0.0.0.0:") +
              std::to_string(cfg.tcp.port) + "（主站 unit_id=" +
              std::to_string(cfg.tcp.unitId) + "）");
    return true;
}

void ModbusTcpTransmitter::close() {
    const bool wasRunning = running_.exchange(false);

    if (listenFd_ != -1) {
        socket_t fd = static_cast<socket_t>(listenFd_);
        listenFd_ = -1;
#ifdef _WIN32
        ::shutdown(fd, SD_BOTH);
#else
        ::shutdown(fd, SHUT_RDWR);
#endif
        closeSocket(fd);
    }
    if (thread_.joinable()) thread_.join();

    bool wasOpen = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        wasOpen = status_.open;
        status_.open = false;
        status_.listening = false;
        status_.hasPeer = false;
    }
    if (wasRunning || wasOpen) appendLog("TCP 从站已关闭");
}

void ModbusTcpTransmitter::serverLoop() {
    while (running_.load()) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLen = sizeof(peer);
#else
        socklen_t peerLen = sizeof(peer);
#endif
        socket_t client = ::accept(static_cast<socket_t>(listenFd_),
                                   reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (client == kInvalidSocket) {
            if (!running_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            status_.hasPeer = true;
        }
        appendLog("主站已连接");

        handleClient(static_cast<intptr_t>(client));

        closeSocket(client);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            status_.hasPeer = false;
        }
        appendLog("主站断开");
    }
}

void ModbusTcpTransmitter::handleClient(intptr_t clientFd) {
    const socket_t client = static_cast<socket_t>(clientFd);
    setRecvTimeout(client, 200);

    while (running_.load()) {
        uint8_t header[7] = {0};
        RxResult rx = recvExact(client, header, sizeof(header), running_);
        if (rx != RxResult::Ok) break;

        const uint16_t transactionId = static_cast<uint16_t>((header[0] << 8) | header[1]);
        const uint16_t protocolId    = static_cast<uint16_t>((header[2] << 8) | header[3]);
        const uint16_t length        = static_cast<uint16_t>((header[4] << 8) | header[5]);
        const uint8_t  unitId        = header[6];

        if (protocolId != 0 || length < 2 || length > 254) {
            appendLog("✗ 非法 MBAP（protocol=" + std::to_string(protocolId) +
                      " length=" + std::to_string(length) + "），断开连接");
            break;
        }

        const size_t pduLen = static_cast<size_t>(length) - 1;  // length 含 unit id
        std::vector<uint8_t> pdu(pduLen, 0);
        if (recvExact(client, pdu.data(), pduLen, running_) != RxResult::Ok) break;

        const bool unitMatches = (cfg_.tcp.unitId == 0 || unitId == cfg_.tcp.unitId);
        if (!unitMatches) {
            appendLog("忽略 unit_id=" + std::to_string(unitId) + " 的请求");
            continue;
        }

        uint8_t responsePdu[260] = {0};
        size_t responseLen = 0;

        if (pdu[0] == 0x03) {
            ReadHoldingRequest req;
            if (parseReadHoldingRequest(pdu.data(), pdu.size(), req)) {
                RegisterImage snapshot;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    snapshot = image_;
                }
                responseLen = buildReadHoldingResponse(snapshot, req,
                                                       responsePdu, sizeof(responsePdu));
            } else {
                responsePdu[0] = static_cast<uint8_t>(0x03 | 0x80);
                responsePdu[1] = 0x03;  // 非法数据值
                responseLen = 2;
            }
        } else {
            responsePdu[0] = static_cast<uint8_t>(pdu[0] | 0x80);
            responsePdu[1] = 0x01;  // 非法功能
            responseLen = 2;
        }

        if (responseLen == 0) {
            appendLog("✗ 构造响应失败");
            break;
        }

        uint8_t frame[7 + 260];
        const uint16_t respLength = static_cast<uint16_t>(responseLen + 1);
        frame[0] = static_cast<uint8_t>(transactionId >> 8);
        frame[1] = static_cast<uint8_t>(transactionId & 0xFF);
        frame[2] = 0;
        frame[3] = 0;
        frame[4] = static_cast<uint8_t>(respLength >> 8);
        frame[5] = static_cast<uint8_t>(respLength & 0xFF);
        frame[6] = unitId;
        std::memcpy(frame + 7, responsePdu, responseLen);

        if (!sendAll(client, frame, 7 + responseLen)) {
            appendLog("✗ 发送响应失败");
            break;
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            ++status_.requestCount;
            status_.lastPollAt = nowClock();
        }
        appendLog("← " + hexDump(pdu.data(), pdu.size()) +
                  "  → " + hexDump(responsePdu, responseLen));
    }
}

bool ModbusTcpTransmitter::publish(const model::RunReport& report,
                                   const model::RunMetrics& metrics) {
    bool openNow = false;
    uint16_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        openNow = status_.open;
        seq = ++batchSequence_;
        image_ = encodeSummaryRegisters(report, metrics, seq, report.isPartial,
                                        cfg_.sendClassBreakdown);
        status_.batchSequence = seq;
        status_.isPartial = report.isPartial;
        ++status_.publishCount;
    }

    std::ostringstream oss;
    oss << "发布整批汇总 批次=" << seq
        << " 总图=" << report.totalImages
        << " 成功=" << report.successCount
        << " 失败=" << report.failedCount
        << " 平均=" << report.avgCycleTimeMs << "ms"
        << " P95=" << report.p95CycleTimeMs << "ms";
    if (metrics.hasGroundTruth) {
        oss << " Accuracy=" << metrics.accuracy
            << " MacroF1=" << metrics.macroF1;
    }
    appendLog(oss.str());

    if (!openNow) {
        appendLog("⚠ 通道未打开，寄存器镜像已更新但主站无法读取");
    }
    return openNow;
}

bool ModbusTcpTransmitter::isOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_.open;
}

TransmitterStatus ModbusTcpTransmitter::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_;
}

RegisterImage ModbusTcpTransmitter::registerImage() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return image_;
}

std::vector<std::string> ModbusTcpTransmitter::takeLog() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

void ModbusTcpTransmitter::appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(mtx_);
    log_.push_back(nowClock() + "  " + line);
    constexpr size_t kMaxLogLines = 500;
    if (log_.size() > kMaxLogLines) {
        log_.erase(log_.begin(), log_.begin() + (log_.size() - kMaxLogLines));
    }
}

void ModbusTcpTransmitter::setError(const std::string& error) {
    std::lock_guard<std::mutex> lk(mtx_);
    status_.lastError = error;
}

}  // namespace infrastructure
}  // namespace qf
