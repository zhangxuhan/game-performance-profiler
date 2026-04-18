# Game Performance Profiler

Windows 游戏性能分析工具。

---

## 快速开始

### 1. 运行程序

下载 [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) 中的 `.zip`，解压后双击运行。

### 2. Attach 进程

启动后点击 **🎯 Attach to Process**，选择目标游戏进程。

右侧面板将自动显示实时性能数据。

### 3. 上传数据文件

点击 **📤 Upload .prof File** 可上传已保存的性能数据文件进行回放分析。

---

## 功能一览

| 功能 | 说明 |
|------|------|
| FPS 监控 | 实时帧率 + 颜色指示 |
| 帧时间分布 | 直方图显示帧时间模式 |
| 内存追踪 | 内存使用 + 泄漏检测 |
| 函数耗时 | 每帧各函数时间分解 |
| 性能警报 | 自动检测异常并提醒 |
| 数据回放 | 上传 `.prof` 文件分析历史数据 |

---

## 数据接入

### Named Pipe 方式（推荐）

游戏端连接 `\\.\pipe\GameProfilerStream` 推送数据：

```cpp
// 游戏代码示例
WebSocket ws("ws://localhost:8081");
ws.send(json{ {"fps", fps}, {"frameTime", ms}, {"memory", bytes} });
```

### Process Monitor 方式

直接 Attach 到运行中的游戏进程，自动采样 CPU/内存。

---

## 数据文件格式

`.prof` 文件为 JSON 格式，每行一条帧记录：

```json
{"frame":1,"fps":60,"frameTime":16.67,"memory":52428800,"profiles":[...]}
{"frame":2,"fps":58,"frameTime":17.24,"memory":52500000,"profiles":[...]}
```

上传后可在界面中回放分析。

---

## 开发调试

```bash
cd backend && npm install && npm start
cd frontend && npm install && npm run dev
```

---

## 技术栈

Vue 3 + ECharts 5 + Node.js + Electron 33

---

## License

MIT