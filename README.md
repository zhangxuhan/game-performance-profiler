# 📊 Game Performance Profiler

Windows 游戏性能分析工具 — 开箱即用，无需配置。

> 下载 releases 中的 `.zip` 文件，解压后双击 `Game Performance Profiler.exe` 即可运行，自动生成模拟数据。

---

## 使用步骤

1. **下载** — 进入 [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) 页面，下载最新 `.zip`
2. **解压** — 解压到任意目录
3. **运行** — 双击 `Game Performance Profiler.exe`，自动显示实时数据

---

## 功能说明

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

## 接入真实游戏数据

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

## 开发调试

```bash
cd backend && npm install && npm start
cd frontend && npm install && npm run dev
```

---

## 项目结构

```
game-performance-profiler/
├── backend/           # Node.js 后端 + Electron
│   ├── main.js        # Electron 主进程
│   └── src/server.js  # WebSocket + REST API
├── frontend/          # Vue 3 可视化界面
└── src/core/          # C++ 原生采样引擎（可选）
```

---

## 技术栈

Vue 3 + ECharts 5 + Node.js + Electron 33

---

## License

MIT

---

# 📊 Game Performance Profiler (English)

A Windows game performance analysis tool — ready to use, no configuration needed.

> Download the `.zip` from Releases, extract, and double-click `Game Performance Profiler.exe` to run. Simulation data is generated automatically.

---

## Quick Start

1. **Download** — Go to [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) and download the latest `.zip`
2. **Extract** — Unzip to any folder
3. **Run** — Double-click `Game Performance Profiler.exe` to see real-time data

---

## Features

### Simulation Mode
- **Simulation** toggle in top-right corner controls data source
- **ON**：Built-in simulation data (default), great for demos and testing
- **OFF**：Receive real game data via WebSocket

### Metrics

| Metric | Description |
|--------|-------------|
| **FPS** | Real-time frame rate, color-coded |
| **Frame Time** | Per-frame time (ms) |
| **Memory** | Memory usage (MB) |
| **Frames** | Total frame count |
| **Avg FPS** | Average FPS |
| **P95 FPS** | 95th percentile FPS |
| **Stability** | Stability score (0-100%) |

### FPS Color Coding

| Color | Range | Status |
|-------|-------|--------|
| 🟢 Green | ≥ 55 FPS | Smooth |
| 🟡 Yellow | 45-54 FPS | Good |
| 🟠 Orange | 30-44 FPS | Fair |
| 🔴 Red | < 30 FPS | Laggy |

---

## Integrating Real Game Data

Send frame data via WebSocket from your game:

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

## Development

```bash
cd backend && npm install && npm start
cd frontend && npm install && npm run dev
```

---

## Tech Stack

Vue 3 + ECharts 5 + Node.js + Electron 33

---

# 📊 ゲームパフォーマンスプロファイラー (日本語)

Windows用ゲームパフォーマンス分析ツール — 設定不要で即座に使用可能。

> Releasesから`.zip`をダウンロード、解凍後`Game Performance Profiler.exe`をダブルクリックするだけで起動。シミュレーションデータが自動生成されます。

---

## 使い方

1. **ダウンロード** — [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases)ページで最新の`.zip`をダウンロード
2. **解凍** — 任意のフォルダに解凍
3. **実行** — `Game Performance Profiler.exe`をダブルクリック、リアルタイムデータが表示されます

---

## 機能

### シミュレーションモード
- 右上の**Simulation**スイッチでデータソースを切り替え
- **ON**：内蔵シミュレーションデータ（デフォルト）、デモやテストに最適
- **OFF**：WebSocket経由でリアルゲームデータを受信

### 指標

| 指標 | 説明 |
|------|------|
| **FPS** | リアルタイムフレームレート、色で状態表示 |
| **Frame Time** | フレーム時間（ミリ秒） |
| **Memory** | メモリ使用量（MB） |
| **Frames** | 総フレーム数 |
| **Avg FPS** | 平均FPS |
| **P95 FPS** | 95パーセンタイルFPS |
| **Stability** | 安定性スコア（0-100%） |

### FPSの色分け

| 色 | 範囲 | 状態 |
|----|------|------|
| 🟢 緑 | ≥ 55 FPS | 快適 |
| 🟡 黄 | 45-54 FPS | 良好 |
| 🟠 橙 | 30-44 FPS | 普通 |
| 🔴 赤 | < 30 FPS | カクつく |

---

## 実ゲームデータの連携

ゲームコードからWebSocketでフレームデータを送信：

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

## 開発

```bash
cd backend && npm install && npm start
cd frontend && npm install && npm run dev
```

---

## 技術スタック

Vue 3 + ECharts 5 + Node.js + Electron 33

---

## License

MIT
