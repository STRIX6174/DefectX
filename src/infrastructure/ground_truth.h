#pragma once

#include "model/defect_class.h"

#include <optional>
#include <string>

namespace qf {
namespace infrastructure {

// 真值提供者接口：给定图片路径 → 返回真值类别（可空）。
// 返回 nullopt 表示"未标注"：该图不参与指标计算，但测量结果照常落库。
class GroundTruthProvider {
public:
    virtual ~GroundTruthProvider() = default;
    virtual std::optional<model::DefectClass> lookup(const std::string& imagePath) const = 0;
};

// 实现：父文件夹名解析（NEU-CLS 目录结构）。
//   ".../train/Cr/Cr_12.bmp" → Crazing；".../train/Sc/Sc_300.png" → Scratches
class FolderNameGroundTruth : public GroundTruthProvider {
public:
    std::optional<model::DefectClass> lookup(const std::string& imagePath) const override;
};

// 缩写 → 类别（大小写不敏感，忽略首尾空格）。"Cr" → Crazing 等；未知返回 nullopt。
std::optional<model::DefectClass> parseClassAbbr(const std::string& abbr);

}  // namespace infrastructure
}  // namespace qf