#include "infrastructure/comm/modbus_common.h"

#include <algorithm>
#include <cmath>

namespace qf {
namespace infrastructure {

namespace {

constexpr double kMaxU16 = 65535.0;

uint16_t clampU16(double v) {
    if (v < 0.0) return 0;
    if (v > kMaxU16) return static_cast<uint16_t>(kMaxU16);
    return static_cast<uint16_t>(v);
}

// 指标 → 寄存器：×1000 取整，NaN 或验证模式无效时写 0。
uint16_t metricToRegister(double value, bool hasGroundTruth) {
    if (!hasGroundTruth || std::isnan(value)) return 0;
    return clampU16(std::round(value * 1000.0));
}

}  // anonymous namespace

RegisterImage encodeSummaryRegisters(const model::RunReport& report,
                                     const model::RunMetrics& metrics,
                                     uint16_t batchSequence,
                                     bool isPartial,
                                     bool sendClassBreakdown) {
    RegisterImage image{};
    image[registers::kBatchSequence] = batchSequence;
    image[registers::kRunStatus]     = isPartial ? 1 : 0;
    image[registers::kTotalImages]   = clampU16(report.totalImages);
    image[registers::kSuccessCount]  = clampU16(report.successCount);
    image[registers::kFailedCount]   = clampU16(report.failedCount);
    image[registers::kAccuracy]       = metricToRegister(metrics.accuracy, metrics.hasGroundTruth);
    image[registers::kMacroPrecision] = metricToRegister(metrics.macroPrecision, metrics.hasGroundTruth);
    image[registers::kMacroRecall]    = metricToRegister(metrics.macroRecall, metrics.hasGroundTruth);
    image[registers::kMacroF1]        = metricToRegister(metrics.macroF1, metrics.hasGroundTruth);
    image[registers::kAvgCycleTime]   = clampU16(report.avgCycleTimeMs);
    image[registers::kP95CycleTime]   = clampU16(report.p95CycleTimeMs);

    if (sendClassBreakdown) {
        for (int c = 0; c < 6; ++c) {
            image[registers::kClassScoreBase + c] =
                clampU16(metrics.perClass[c].predictedCount);
        }
    }
    // 六类得分段默认保持 0。

    return image;
}

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

bool parseReadHoldingRequest(const uint8_t* pdu, size_t pduLen, ReadHoldingRequest& out) {
    if (!pdu || pduLen < 5) return false;
    if (pdu[0] != 0x03) return false;  // 本场景仅支持读保持寄存器
    out.startAddress = static_cast<uint16_t>((pdu[1] << 8) | pdu[2]);
    out.quantity     = static_cast<uint16_t>((pdu[3] << 8) | pdu[4]);
    // Modbus 规范：0x03 一次读 1~125 个寄存器。
    return (out.quantity >= 1 && out.quantity <= 125);
}

size_t buildReadHoldingResponse(const RegisterImage& image,
                                const ReadHoldingRequest& req,
                                uint8_t* outBuf, size_t outBufSize) {
    if (req.quantity < 1 || req.quantity > 125) return 0;

    const size_t byteCount = static_cast<size_t>(req.quantity) * 2;
    const size_t total = 2 + byteCount;  // 功能码 + 字节数 + 数据
    if (!outBuf || outBufSize < total) return 0;

    outBuf[0] = 0x03;
    outBuf[1] = static_cast<uint8_t>(byteCount);

    for (size_t i = 0; i < req.quantity; ++i) {
        const uint32_t addr = static_cast<uint32_t>(req.startAddress) + static_cast<uint32_t>(i);
        uint16_t value = 0;
        if (addr < image.size()) value = image[addr];  // 越界补 0

        outBuf[2 + i * 2]     = static_cast<uint8_t>((value >> 8) & 0xFF);  // 大端高字节
        outBuf[2 + i * 2 + 1] = static_cast<uint8_t>(value & 0xFF);         // 大端低字节
    }
    return total;
}

}  // namespace infrastructure
}  // namespace qf