#pragma once

#include "infrastructure/comm/i_result_transmitter.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace qf {
namespace infrastructure {

// Modbus TCP 从站：PC 监听端口，主站（Modbux / PLC）轮询 0x03 读保持寄存器。
// 只依赖 model + 系统 socket，不依赖 Qt。
//
// 线程模型：open() 起一个后台线程 accept 并逐个处理主站连接；
// 每个连接内循环读取 MBAP + PDU，按寄存器镜像回 0x03 响应。
// publish() 由调度线程调用，只更新寄存器镜像（加锁），不阻塞通讯线程。
class ModbusTcpTransmitter : public IResultTransmitter {
public:
    ModbusTcpTransmitter();
    ~ModbusTcpTransmitter() override;

    ModbusTcpTransmitter(const ModbusTcpTransmitter&) = delete;
    ModbusTcpTransmitter& operator=(const ModbusTcpTransmitter&) = delete;

    bool open(const model::CommConfig& cfg) override;
    void close() override;
    bool publish(const model::RunReport& report,
                 const model::RunMetrics& metrics) override;
    bool isOpen() const override;

    TransmitterStatus status() const override;
    RegisterImage registerImage() const override;
    std::vector<std::string> takeLog() override;

private:
    void serverLoop();
    void handleClient(intptr_t clientFd);
    void appendLog(const std::string& line);
    void setError(const std::string& error);

    model::CommConfig cfg_;
    mutable std::mutex mtx_;
    RegisterImage image_{};   // 寄存器镜像（受 mtx_ 保护）
    TransmitterStatus status_;
    std::vector<std::string> log_;
    uint16_t batchSequence_ = 0;

    std::atomic<bool> running_{false};
    std::thread thread_;
    intptr_t listenFd_ = -1;
};

}  // namespace infrastructure
}  // namespace qf
