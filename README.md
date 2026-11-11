# DefectX

**Industrial Surface Defect Inspection System** | 工业表面缺陷检测系统

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Qt](https://img.shields.io/badge/Qt-6-green.svg)]()
[![OpenCV](https://img.shields.io/badge/OpenCV-brightgreen.svg)]()

---

[English](#english) | [中文](#chinese)

---

<a name="english"></a>

## English

DefectX is a cross-platform industrial surface defect inspection tool built with C++17 / Qt 6 / OpenCV. It performs automated visual defect detection on metal surfaces, supporting 6 defect types with parallel classification, per-class confidence visualization and run statistics.

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

- Batch pipeline: scan folder → load → preprocess → 6-way parallel classification → result fusion
- Six-thread parallel classification — one worker per defect class, producer-consumer model with a generation guard against stale results
- Classic CV operators (Canny edges, adaptive threshold, morphology, Hough lines) — no deep learning
- Preprocessing chain: grayscale → resize → optional denoise / contrast / sharpen
- Per-class confidence score bars + click-to-filter legend (single-class view)
- Progress tracking with cancel support and per-image timeout
- Run statistics in the status bar (total / success / failed / avg / P95 latency)
- CSV / JSON report generator and JSON parameter loader

### Architecture

| Layer | Directory | Responsibility |
|-------|-----------|----------------|
| model | `src/model` | Header-only data types (scores, params, reports) |
| algorithm | `src/algorithm` | Preprocessing, six defect detectors, result fusion (no Qt) |
| application | `src/application` | Pipeline runner, parallel classifier, image loader, config, report (no Qt) |
| ui | `src/ui` | Qt 6 Widgets GUI (P5-styled custom widgets) |

### Tech Stack

- **Language:** C++17
- **GUI:** Qt 6 (Widgets, Core, Gui — LGPLv3)
- **Vision:** OpenCV (core, imgproc, imgcodecs)
- **Concurrency:** std::thread + condition variables
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

1. Click **Select Folder** to load an image directory
2. Click **Run** to start batch detection; **Stop** cancels at any time
3. Browse thumbnails in the result grid; click an image to view its six-class confidence bars
4. Click a legend chip to filter results by a single defect class
5. View run statistics (total / success / failed / avg / P95) in the status bar

---

<a name="chinese"></a>

## 中文

DefectX 是一款基于 C++17 / Qt 6 / OpenCV 的跨平台工业表面缺陷检测工具。支持 6 种缺陷类型的并行分类、逐类置信度可视化与运行统计。

### 支持缺陷类型

| 缩写 | 类型 | 说明 |
|:----:|------|------|
| Cr | 龟裂 (Crazing) | 表面网状裂纹 |
| In | 夹杂物 (Inclusion) | 非金属异物 |
| Pa | 斑块 (Patches) | 不规则色斑 |
| PS | 麻面 (Pitted Surface) | 细小表面凹坑 |
| Sc | 划痕 (Scratches) | 线状机械损伤 |
| RS | 轧入氧化皮 (Rolled-in Scale) | 氧化铁皮压入 |

### 功能特性

- 批量检测流水线：扫描文件夹 → 加载 → 预处理 → 六类并行识别 → 结果融合
- 六线程并行分类 —— 每类一线程，生产者-消费者模型 + 代际标签防过期结果
- 经典 CV 算子（Canny 边缘、自适应阈值、形态学、霍夫直线），无深度学习
- 预处理链路：灰度化 → 尺寸标准化 → 可选去噪 / 对比度 / 锐化
- 六类置信度得分条 + 图例点击「只看某类」
- 进度回调 + 随时取消 + 单图超时保护
- 状态栏运行统计（总数 / 成功 / 失败 / 平均 / P95 耗时）
- 内置 CSV / JSON 报告生成器与 JSON 参数加载器

### 架构

| 层 | 目录 | 职责 |
|----|------|------|
| model | `src/model` | 纯头文件数据类型（得分、参数、报告） |
| algorithm | `src/algorithm` | 预处理、六类检测算子、结果融合（不依赖 Qt） |
| application | `src/application` | 流水线执行、并行分类器、图像加载、配置、报告（不依赖 Qt） |
| ui | `src/ui` | Qt 6 Widgets 界面（P5 风格自绘控件） |

### 技术栈

- **语言:** C++17
- **界面:** Qt 6 (Widgets, Core, Gui — LGPLv3)
- **视觉:** OpenCV (core, imgproc, imgcodecs)
- **并发:** std::thread + 条件变量
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

1. 点击「选择文件夹」加载图片目录
2. 点击「启动」开始批量检测，可随时「中止」
3. 在结果网格浏览缩略图，点击某张查看六类置信度得分条
4. 点击图例标签筛选「只看某类」
5. 在状态栏查看运行统计（总数 / 成功 / 失败 / 平均 / P95 耗时）

---

## License

MIT © 2026 [STRIX6174](https://github.com/STRIX6174)