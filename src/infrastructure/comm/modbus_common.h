#pragma once

#include "model/run_report.h"
#include "model/validation_metrics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace qf {
namespace infrastructure {

// ============================================================================
// Modbus 公共模块：RTU 与 TCP 共享的 PDU 编解码 + 整批汇总寄存器映射。
// 只依赖 model，不依赖 Qt / OpenCV / SQLite，可独立单测。
//
// 约定（与《2.0增强功能-通讯协议》§四/§五、蓝图2.1 §8.3 一致）：
//  - PC 做从站、PLC 做主站，主站轮询读保持寄存器（功能码 0x03）；
//  - 0~1 的小数（Accuracy / Macro 系列）统一 ×1000 取整，PLC 端 ÷1000 还原；
//  - 整批跑完只发布一次汇总，不是逐张结果。
// ============================================================================

// 汇总保持寄存器映射（17 个：0~16）。
namespace registers {

constexpr uint16_t kCount = 17;          // 保持寄存器总数
constexpr uint16_t kFixedCount = 11;     // 固定段长度（寄存器 0~10）
constexpr uint16_t kClassScoreCount = 6; // 六类得分段长度（寄存器 11~16）

constexpr uint16_t kBatchSequence  = 0;   // 批次序号（0~65535 循环自增）
constexpr uint16_t kRunStatus      = 1;   // 0=正常完成，1=部分/中止
constexpr uint16_t kTotalImages    = 2;   // 总图片数
constexpr uint16_t kSuccessCount   = 3;   // 成功数
constexpr uint16_t kFailedCount    = 4;   // 失败数
constexpr uint16_t kAccuracy       = 5;   // ×1000；验证模式有效，否则 0
constexpr uint16_t kMacroPrecision = 6;   // ×1000；同上
constexpr uint16_t kMacroRecall    = 7;   // ×1000；同上
constexpr uint16_t kMacroF1        = 8;   // ×1000；同上
constexpr uint16_t kAvgCycleTime   = 9;   // 单位 ms
constexpr uint16_t kP95CycleTime   = 10;  // 单位 ms
constexpr uint16_t kClassScoreBase = 11;  // 11~16 六类得分段（可选）

}  // namespace registers

using RegisterImage = std::array<uint16_t, registers::kCount>;

// 把「整批汇总」编码进保持寄存器镜像。
//   report            提供总数/成功/失败/平均/P95 耗时；
//   metrics           提供 Accuracy/Macro 系列与六类预测数量；
//   batchSequence     本批序号（由 transmitter 每批自增）；
//   isPartial         映射为运行状态（0=正常完成，1=部分/中止）；
//   sendClassBreakdown 是否写入 11~16 六类得分段（关闭时该段保持 0）。
// 所有数值按 uint16 范围截断；NaN（无有效指标）写入 0。
RegisterImage encodeSummaryRegisters(const model::RunReport& report,
                                     const model::RunMetrics& metrics,
                                     uint16_t batchSequence,
                                     bool isPartial,
                                     bool sendClassBreakdown);

// Modbus RTU CRC16（多项式 0xA001，初值 0xFFFF，低字节在前，帧尾两字节为 CRC 小端）。
uint16_t crc16(const uint8_t* data, size_t len);

// 读保持寄存器（0x03）请求 PDU：功能码 + 起始地址 + 数量。
struct ReadHoldingRequest {
    uint16_t startAddress = 0;
    uint16_t quantity = 0;
};

// 解析 0x03 请求 PDU（pdu[0] 应为功能码 0x03）。数量需为 1~125。失败返回 false。
bool parseReadHoldingRequest(const uint8_t* pdu, size_t pduLen, ReadHoldingRequest& out);

// 根据寄存器镜像构造 0x03 响应 PDU：
//   [功能码 0x03][字节数][寄存器值大端序 × N]
// 越界寄存器补 0。返回写入 outBuf 的字节数；缓冲区不足返回 0。
size_t buildReadHoldingResponse(const RegisterImage& image,
                                const ReadHoldingRequest& req,
                                uint8_t* outBuf, size_t outBufSize);

}  // namespace infrastructure
}  // namespace qf