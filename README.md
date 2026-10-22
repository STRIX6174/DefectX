# DefectX

**Industrial Surface Defect Inspection System** | 工业表面缺陷检测系统

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Qt](https://img.shields.io/badge/Qt-6-green.svg)]()
[![OpenCV](https://img.shields.io/badge/OpenCV-required-brightgreen.svg)]()

---

[English](#english) | [中文](#chinese)

---

<a name="english"></a>

## English

DefectX is a cross-platform industrial surface defect inspection tool built with C++/Qt/OpenCV. It performs automated quality control (QC) and visual defect detection on metal surfaces, supporting 6 defect types with configurable detection pipelines and detailed reporting.

### Supported Defect Types

| Abbr | Type | Description |
|:----:|------|-------------|
| Cr | Crazing | Surface crack networks |
| In | Inclusion | Non-metallic foreign material |
| Pa | Patches | Irregular surface discoloration |
| PS | Pitted Surface | Small surface cavities |
| Sc | Scratches | Linear mechanical marks |
| RS | Rolled-in Scale | Oxidized iron fragments |

### Features

- Automated batch image processing with pipeline runner
- Configurable QC operators (Laplacian sharpness, over/under-exposure detection)
- Ground-truth comparison and report generation
- Progress tracking with cancel support
- Marked defect image output
- Bilingual UI (Chinese / English)

### Tech Stack

- **Language:** C++17
- **GUI:** Qt 6 (Widgets)
- **Vision:** OpenCV (core, imgproc, imgcodecs)
- **Build:** CMake 3.16+

### Build

```bash
# Prerequisites: Qt6, OpenCV, CMake, ninja (or MSVC)
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### Usage

```bash
./DefectX
```

1. Load your image folder
2. (Optional) Load a ground-truth CSV for accuracy comparison
3. Configure QC & defect parameters in `Gate` panel
4. Click **Run** and view results in the result list

---

<a name="chinese"></a>

## 中文

DefectX 是一款基于 C++/Qt/OpenCV 的跨平台工业表面缺陷检测工具。可实现金属表面自动化质检与视觉缺陷识别，支持 6 种缺陷类型、可配置检测流程及详细报告输出。

### 支持缺陷类型

| 缩写 | 类型 | 说明 |
|:----:|------|------|
| Cr | 裂纹 (Crazing) | 表面网状裂纹 |
| In | 夹杂物 (Inclusion) | 非金属异物 |
| Pa | 斑块 (Patches) | 不规则色斑 |
| PS | 麻点 (Pitted Surface) | 细小表面凹坑 |
| Sc | 划痕 (Scratches) | 线状机械损伤 |
| RS | 轧入氧化皮 (Rolled-in Scale) | 氧化铁皮压入 |

### 功能特性

- 批量图像自动检测流水线
- 可配置 QC 算子（Laplacian 锐度、过曝/欠曝检测）
- Ground-Truth 对比与报告生成
- 进度回调与随时取消
- 缺陷标记图像输出
- 中英双语界面

### 技术栈

- **语言:** C++17
- **界面:** Qt 6 (Widgets)
- **视觉:** OpenCV
- **构建:** CMake 3.16+

### 构建

```bash
# 依赖：Qt6, OpenCV, CMake, ninja（或 MSVC）
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### 使用

```bash
./DefectX
```

1. 加载图像文件夹
2. （可选）加载 Ground-Truth CSV 做准确率对比
3. 在 `Gate` 面板调整 QC 与缺陷参数
4. 点击 **运行**，在结果列表中查看检测结果

---

## License

MIT © 2026 [STRIX6174](https://github.com/STRIX6174)