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

## 技术栈 / Tech Stack / 技術スタック

- **核心 / Core**: C++ (采样引擎 / sampling engine)
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
    ProfilerCore::GetInstance()->StartSampling();
    // ... 游戏主循环 / game loop / ゲームループ
    ProfilerCore::GetInstance()->StopSampling();
    return 0;
}
```

## 项目结构 / Project Structure / プロジェクト構造

```
game-performance-profiler/
├── backend/               # Node.js 后端服务
│   └── src/server.js     # WebSocket 服务器
├── frontend/             # Vue 3 前端
│   └── index.html        # 可视化仪表板
├── src/core/             # C++ 核心采样引擎
└── README.md
```

## 许可证 / License

MIT License

## 贡献 / Contribution / コントリビューション

欢迎提交 Issue 和 PR！/ Issues and PRs welcome! / Issue と PR は大歓迎です！
