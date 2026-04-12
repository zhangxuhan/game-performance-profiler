# Game Performance Profiler

游戏性能分析工具 - 实时可视化游戏运行时的性能数据

## 特性

- 🎯 **FPS 监控** - 实时帧率监控与历史趋势
- 📊 **函数耗时分析** - 定位性能热点函数
- 🧠 **内存分析** - 内存分配追踪与泄漏检测
- 🔥 **热力图可视化** - 函数调用热度分布
- 📈 **数据导出** - 支持 CSV/JSON 格式导出

## 技术栈

- **核心**: C++ (采样/插桩引擎)
- **后端**: Node.js + WebSocket
- **前端**: Vue 3 + ECharts

## 快速开始

### 编译核心库

```bash
cd backend
mkdir build && cd build
cmake ..
make
```

### 启动服务

```bash
# 启动后端
cd backend
node server.js

# 启动前端 (新终端)
cd frontend
npm install
npm run dev
```

### 集成到游戏

在游戏代码中引入核心库并启动采样：

```cpp
#include "ProfilerCore.h"

int main() {
    ProfilerCore::GetInstance()->StartSampling();
    // ... 游戏主循环
    ProfilerCore::GetInstance()->StopSampling();
    return 0;
}
```

## 项目结构

```
game-performance-profiler/
├── backend/               # Node.js 后端服务
│   ├── src/
│   │   ├── server.js     # WebSocket 服务器
│   │   └── analyzer.js   # 数据分析模块
│   └── package.json
├── frontend/             # Vue 3 前端
│   ├── src/
│   │   ├── components/  # 可视化组件
│   │   └── views/       # 页面视图
│   └── package.json
├── src/
│   └── core/             # C++ 核心采样引擎
│       ├── ProfilerCore.cpp
│       ├── Sampler.cpp
│       └── MemoryTracker.cpp
├── docs/                 # 文档
└── README.md
```

## 迭代计划

### Day 1-7: 基础框架
- [ ] C++ 核心采样引擎
- [ ] WebSocket 通信
- [ ] 基础前端界面

### Day 8-14: 核心功能
- [ ] FPS 监控
- [ ] 函数耗时分析
- [ ] 内存追踪

### Day 15-21: 可视化增强
- [ ] 热力图组件
- [ ] 实时数据图表
- [ ] 数据导出

### Day 22-30: 优化与扩展
- [ ] 性能优化
- [ ] 更多平台支持
- [ ] 文档完善

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 PR！
