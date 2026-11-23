#pragma once

#include "infrastructure/comm/i_result_transmitter.h"

namespace qf {
namespace infrastructure {

// Modbus RTU 从站（串口）：本次仅保留接口位，后续迭代补实现。
// 约定不变：PC 做从站，PLC 做主站轮询；RTU 与 TCP 共享 modbus_common 的 PDU。
class ModbusRtuTransmitter : public IResultTransmitter {
public:
    ModbusRtuTransmitter() = default;
    ~ModbusRtuTransmitter() override = default;

    bool open(const model::CommConfig& cfg) override;
    void close() override;
    bool publish(const model::RunReport& report,
                 const model::RunMetrics& metrics) override;
    bool isOpen() const override;

    TransmitterStatus status() const override;
    RegisterImage registerImage() const override;
    std::vector<std::string> takeLog() override;

private:
    TransmitterStatus status_;
};

}  // namespace infrastructure
}  // namespace qf
