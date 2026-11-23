#include "infrastructure/comm/modbus_rtu_transmitter.h"

namespace qf {
namespace infrastructure {

bool ModbusRtuTransmitter::open(const model::CommConfig& cfg) {
    status_.open = false;
    status_.listening = false;
    status_.hasPeer = false;
    status_.transport = "rtu";
    status_.endpoint = cfg.rtu.port + "@" + std::to_string(cfg.rtu.baud);
    status_.lastError = "RTU 从站尚未实现（本期先支持 TCP，见待确认项 A.3）";
    return false;
}

void ModbusRtuTransmitter::close() {
    status_.open = false;
    status_.listening = false;
    status_.hasPeer = false;
}

bool ModbusRtuTransmitter::publish(const model::RunReport&,
                                   const model::RunMetrics&) {
    return false;
}

bool ModbusRtuTransmitter::isOpen() const {
    return status_.open;
}

TransmitterStatus ModbusRtuTransmitter::status() const {
    return status_;
}

RegisterImage ModbusRtuTransmitter::registerImage() const {
    return RegisterImage{};
}

std::vector<std::string> ModbusRtuTransmitter::takeLog() {
    return {};
}

}  // namespace infrastructure
}  // namespace qf
