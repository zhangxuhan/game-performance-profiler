# Game Performance Profiler

[中文](#中文) | [English](#english) | [日本語](#日本語)

---

<a name="中文"></a>
## 中文

Windows 游戏性能分析工具。

### 快速开始

1. **运行程序** - 下载 [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) 中的 `.zip`，解压后双击运行
2. **Attach 进程** - 点击 **🎯 Attach to Process**，选择目标游戏进程，右侧面板自动显示实时数据
3. **上传数据** - 点击 **📤 Upload .prof File** 可上传已保存的性能数据文件进行回放分析

### 功能一览

| 功能 | 说明 |
|------|------|
| FPS 监控 | 实时帧率 + 颜色指示 |
| 帧时间分布 | 直方图显示帧时间模式 |
| 内存追踪 | 内存使用 + 泄漏检测 |
| 函数耗时 | 每帧各函数时间分解 |
| 性能警报 | 自动检测异常并提醒 |
| 数据回放 | 上传 `.prof` 文件分析历史数据 |
| **趋势预测** | 线性回归预测帧率下降 / 内存泄漏，预测未来帧数 |
| **性能评分** | 综合评分 0-100，自动检测性能回归，趋势分析，生成优化建议 |
| **温度监控** | CPU/GPU温度追踪，热节流检测，智能降温建议 |
| **会话管理** | 命名分析会话，事件标记，书签导航，区域分析，会话对比 |
| **对比分析** | 会话对比，历史趋势，回归检测，性能评分变化 |

### 数据文件格式

`.prof` 文件为 JSON Lines 格式，每行一条帧记录：

```json
{"frame":1,"fps":60,"frameTime":16.67,"memory":52428800,"profiles":[{"name":"Update","duration":4.2},{"name":"Render","duration":3.1}]}
```

---

<a name="english"></a>
## English

Windows game performance profiling tool.

### Quick Start

1. **Run** - Download `.zip` from [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases), extract and run
2. **Attach** - Click **🎯 Attach to Process**, select target game process, real-time data shows on the right panel
3. **Upload** - Click **📤 Upload .prof File** to upload saved performance data for playback analysis

### Features

| Feature | Description |
|---------|-------------|
| FPS Monitor | Real-time framerate with color indicator |
| Frame Time Distribution | Histogram showing frame time patterns |
| Memory Tracking | Memory usage + leak detection |
| Function Profiling | Per-frame function time breakdown |
| Performance Alerts | Auto-detect anomalies and notify |
| Data Playback | Upload `.prof` file to analyze historical data |
| **Trend Prediction** | Linear regression to predict FPS drops / memory leaks |
| **Performance Scoring** | Overall score 0-100, regression detection, trend analysis, recommendations |
| **Thermal Monitoring** | CPU/GPU temperature tracking, throttling detection, cooling recommendations |
| **Session Management** | Named profiling sessions, event tagging, bookmark navigation, region analysis, session comparison |
| **Comparative Analysis** | Session comparison, historical trends, regression detection, performance score changes |

### Data File Format

`.prof` file uses JSON Lines format, one frame record per line:

```json
{"frame":1,"fps":60,"frameTime":16.67,"memory":52428800,"profiles":[{"name":"Update","duration":4.2},{"name":"Render","duration":3.1}]}
```

---

<a name="日本語"></a>
## 日本語

Windows用ゲームパフォーマンス解析ツール。

### クイックスタート

1. **実行** - [Releases](https://github.com/zhangxuhan/game-performance-profiler/releases) から `.zip` をダウンロード、解凍して実行
2. **アタッチ** - **🎯 Attach to Process** をクリック、対象のゲームプロセスを選択、右パネルにリアルタイムデータが表示されます
3. **アップロード** - **📤 Upload .prof File** をクリックして、保存したパフォーマンスデータをアップロードして再生解析

### 機能一覧

| 機能 | 説明 |
|------|------|
| FPS監視 | リアルタイムフレームレート + 色表示 |
| フレームタイム分布 | ヒストグラムでフレームタイムパターンを表示 |
| メモリ追跡 | メモリ使用量 + リーク検出 |
| 関数プロファイリング | フレームごとの関数時間内訳 |
| パフォーマンスアラート | 異常を自動検出して通知 |
| データ再生 | `.prof` ファイルをアップロードして履歴データを解析 |
| **トレンド予測** | 線形回帰でFPS低下/メモリリークを予測 |
| **パフォーマンススコア** | 総合スコア0-100、回帰検出、トレンド分析、推奨事項 |
| **温度監視** | CPU/GPU温度追跡、スロットリング検出、冷却推奨 |
| **セッション管理** | 名前付きプロファイリングセッション、イベントタグ付け、ブックマークナビゲーション、リージョン分析、セッション比較 |
| **比較分析** | セッション比較、履歴トレンド、回帰検出、パフォーマンススコア変化 |

### データファイル形式

`.prof` ファイルは JSON Lines 形式、1行に1フレームレコード：

```json
{"frame":1,"fps":60,"frameTime":16.67,"memory":52428800,"profiles":[{"name":"Update","duration":4.2},{"name":"Render","duration":3.1}]}
```

---

## 技术栈 / Tech Stack / 技術スタック

Vue 3 + ECharts 5 + Node.js + Electron 33

## License

MIT
