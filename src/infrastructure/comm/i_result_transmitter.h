#pragma once

#include "infrastructure/comm/modbus_common.h"
#include "model/pipeline_params.h"
#include "model/run_report.h"
#include "model/validation_metrics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace qf {
namespace infrastructure {

// 通讯状态快照（供 CommPanel 轮询展示，只读）。
// 说明：PC 是从站，主站（PLC）主动轮询；requestCount / lastPollAt 描述主站侧行为。
struct TransmitterStatus {
    bool open = false;        // 通道已建立（监听中 / 串口已打开）
    bool listening = false;   // TCP 正在 listen 或 RTU 串口已就绪
    bool hasPeer = false;     // 当前是否有主站连接
    std::string transport;    // "tcp" / "rtu"
    std::string endpoint;     // "0.0.0.0:502" / "COM3@115200"
    uint64_t requestCount = 0;
    uint64_t publishCount = 0;
    std::string lastPollAt;   // 最近一次主站请求时间；空 = 尚未轮询
    std::string lastError;
    uint16_t batchSequence = 0;
    bool isPartial = false;
};

// 结果传输统一接口：PC 做从站、PLC 做主站轮询读取。
// publish() 语义 = 把"整批汇总"编码进本地寄存器镜像，由后台线程响应读请求，
// 并非主动往外推；只在整批跑完后调用一次。
// RTU 与 TCP 共享同一套 Modbus PDU（见 modbus_common），只外层帧不同。
class IResultTransmitter {
public:
    virtual ~IResultTransmitter() = default;

    virtual bool open(const model::CommConfig& cfg) = 0;   // 打开通道 / 建从站
    virtual void close() = 0;

    // ★ 整批汇总，非单张结果：
    //   report  提供数量/耗时（总图/成功/失败/平均/P95），
    //   metrics 提供指标/六类得分（Accuracy/Macro 系列 + 六类预测数量）。
    virtual bool publish(const model::RunReport& report,
                         const model::RunMetrics& metrics) = 0;

    virtual bool isOpen() const = 0;

    // ---- 状态与诊断（职责2 CommPanel 用，轮询式）----
    virtual TransmitterStatus status() const = 0;
    virtual RegisterImage registerImage() const = 0;
    // 取走并清空诊断日志（请求帧 / 响应帧 / 异常）。
    virtual std::vector<std::string> takeLog() = 0;
};

}  // namespace infrastructure
}  // namespace qf
