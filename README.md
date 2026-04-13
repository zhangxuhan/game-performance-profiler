# 📊 Game Performance Profiler

Windows 游戏性能分析工具 — 开箱即用，无需配置。

> 下载 releases 中的 `.zip` 文件，解压后双击 `Game Performance Profiler.exe` 即可运行，自动生成模拟数据。

---

## 🎯 使用步骤

1. **下载** — 进入 [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) 页面，下载最新 `.zip`
2. **解压** — 解压到任意目录
3. **运行** — 双击 `Game Performance Profiler.exe`，自动显示实时数据

---

## 📌 功能说明

### 仿真模式
- 右上角 **Simulation** 开关控制数据来源
- **ON**：使用内置模拟数据（默认），适合演示和测试
- **OFF**：接收真实游戏通过 WebSocket 推送的数据

### 数据指标

| 指标 | 说明 |
|------|------|
| **FPS** | 实时帧率，颜色随数值变化 |
| **Frame Time** | 单帧耗时（毫秒） |
| **Memory** | 内存占用（MB） |
| **Frames** | 总帧数 |
| **Avg FPS** | 平均帧率 |
| **P95 FPS** | P95 分位帧率（尾部性能） |
| **Stability** | 稳定性评分（0-100%） |

### FPS 状态颜色

| 颜色 | 范围 | 含义 |
|------|------|------|
| 🟢 绿 | ≥ 55 FPS | 流畅 |
| 🟡 黄 | 45-54 FPS | 良好 |
| 🟠 橙 | 30-44 FPS | 一般 |
| 🔴 红 | < 30 FPS | 卡顿 |

---

## 🔌 接入真实游戏数据

在你的游戏代码中，通过 WebSocket 发送帧数据：

```cpp
WebSocket ws("ws://localhost:8081");

void onFrame(float fps, float frameTime, size_t memoryBytes) {
    json data = {
        {"frame", frameCount++},
        {"fps", fps},
        {"frameTime", frameTime},
        {"memory", memoryBytes}
    };
    ws.send(data.dump());
}
```

---

## 🛠️ 开发调试

```bash
cd backend && npm install && npm start
cd frontend && npm install && npm run dev
```

---

## 📁 项目结构

```
game-performance-profiler/
├── backend/           # Node.js 后端 + Electron
│   ├── main.js        # Electron 主进程
│   └── src/server.js  # WebSocket + REST API
├── frontend/          # Vue 3 可视化界面
└── src/core/          # C++ 原生采样引擎（可选）
```

---

## 📋 技术栈

Vue 3 + ECharts 5 + Node.js + Electron 33

---

## 📄 License

MIT
