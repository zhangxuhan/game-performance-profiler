# Game Performance Profiler

游戏性能分析工具 - 实时可视化游戏运行时的性能数据

Real-time game performance profiling tool with visual dashboard

ゲームパフォーマンス分析ツール - リアルタイム可視化ダッシュボード

## 特性 / Features / 機能

- 🎯 **FPS 监控** - 实时帧率监控与历史趋势
- 📊 **函数耗时分析** - 定位性能热点函数  
- 🧠 **内存分析** - 内存分配追踪与泄漏检测
- 🔥 **热力图可视化** - 函数调用热度分布
- 📈 **数据导出** - 支持 CSV/JSON 格式导出
- 📉 **统计分析** - P50/P90/P95/P99 百分位、帧时间标准差、稳定性评分
- 🚨 **性能告警** - 自动检测 FPS 下跌、内存泄漏、帧率不稳定等异常

## 技术栈 / Tech Stack / 技術スタック

- **核心 / Core**: C++ (采样引擎 + 统计分析 / sampling engine + statistics)
- **后端 / Backend**: Node.js + WebSocket
- **前端 / Frontend**: Vue 3 + ECharts

## 快速开始 / Quick Start / クイックスタート

### 启动后端 (含模拟数据) / Start Backend (with simulation)

```bash
cd backend
npm install
npm start
```

### 启动前端 / Start Frontend

```bash
cd frontend
npm install
npm run dev
```

打开浏览器 / Open browser / ブラウザを開く: **http://localhost:3000**

### 集成到游戏 / Integrate to Game / ゲームに組み込む

```cpp
#include "ProfilerCore.h"

int main() {
    auto profiler = ProfilerCore::ProfilerCore::GetInstance();
    
    // Configure analysis window and alert thresholds (optional)
    profiler->SetAnalyzerWindowSize(300);
    
    ProfilerCore::AlertThresholds thresholds;
    thresholds.minFpsWarning = 45.0;
    thresholds.minFpsCritical = 30.0;
    thresholds.memoryGrowthRateWarning = 1024 * 1024;  // 1MB/frame
    profiler->SetAnalyzerThresholds(thresholds);
    
    profiler->StartSampling();
    
    // Game loop
    while (running) {
        profiler->BeginFrame();
        // ... game update & render ...
        profiler->EndFrame();
    }
    
    // Check statistics and alerts
    auto* analyzer = profiler->GetAnalyzer();
    auto stats = analyzer->GetSummary();
    printf("Stability: %.1f%% | P95 FPS: %.1f | Alerts: %d\n",
           stats.stabilityScore, stats.p95Fps, stats.alertCount);
    
    profiler->StopSampling();
    return 0;
}
```

## 统计分析 / Statistics Analysis / 統計分析

`StatisticsAnalyzer` 提供滚动窗口内的聚合统计：

| 指标 / Metric | 说明 / Description |
|---|---|
| P50/P90/P95/P99 FPS | 各百分位帧率，定位尾部延迟 |
| 帧时间标准差 / Frame Time StdDev | 衡量帧间稳定性 |
| 稳定性评分 / Stability Score | 0-100 分，基于 FPS 变异系数 |
| 内存增长率 / Memory Growth Rate | 线性回归斜率，用于检测内存泄漏 |
| 峰值内存 / Peak Memory | 窗口内最大内存占用 |

### 告警规则 / Alert Rules / アラートルール

| 条件 / Condition | 级别 / Severity |
|---|---|
| Avg FPS < 45 | Warning |
| Avg FPS < 30 | Critical |
| P99 帧时间 > 22ms | Warning |
| P99 帧时间 > 33ms | Critical |
| 内存增长率 > 1MB/帧 | Warning |
| 内存增长率 > 5MB/帧 | Critical |
| 峰值内存 > 256MB | Warning |
| 峰值内存 > 512MB | Critical |
| 稳定性评分 < 50 | Warning |

## 项目结构 / Project Structure / プロジェクト構造

```
game-performance-profiler/
├── backend/               # Node.js 后端服务
│   └── src/server.js     # WebSocket 服务器
├── frontend/             # Vue 3 前端
│   └── index.html        # 可视化仪表板
├── src/core/             # C++ 核心采样引擎
│   ├── ProfilerCore.h/cpp          # 采样引擎主类
│   └── StatisticsAnalyzer.h/cpp    # 统计分析与告警模块
└── README.md
```

## 许可证 / License

MIT License

## 贡献 / Contribution / コントリビューション

欢迎提交 Issue 和 PR！/ Issues and PRs welcome! / Issue と PR は大歓迎です！
