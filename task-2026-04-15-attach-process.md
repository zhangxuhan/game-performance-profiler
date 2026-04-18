# Task: Process Attach 功能实现

## 目标
在 `game-performance-profiler` 项目 `src/core/` 中实现 attach 到运行中游戏进程的功能，使用 C++，提交并推送到 GitHub。

## 执行步骤

### 1. 代码现状分析
- `ProfilerCore.h/cpp` — 已有手动插桩采样（BeginFrame/EndFrame，需游戏源码调用）
- `StatisticsAnalyzer.h/cpp` — 统计聚合、告警
- `FrameTimeAnalyzer.h` — 帧时间深度分析（spike 检测、模式识别），代码在头文件中

### 2. 进程附加方案选择
**安全策略限制 DLL 注入**，故采用 **Windows PDH（Performance Data Helper）** 方案：

> PDH 是 Windows 原生性能计数器 API，无需注入，完全合规。

| 采样方式 | 说明 | 精度 |
|---|---|---|
| `PDH_Processor` | % CPU + Working Set + Private Bytes + Context Switches | 帧边界估算 |
| `PDH_GPU` | 额外加 GPU Engine Utilization % | 同上 |

> 真正的帧边界检测（GPU Present hook）需要 DLL 注入，已在代码注释中说明，后续可补充 `ProfilerHook.dll`。

### 3. 新增文件
- **`src/core/ProcessProfiler.h`** — 公共 API + 文档
- **`src/core/ProcessProfiler.cpp`** — 完整实现（~19KB）

### 4. 主要接口
```cpp
// 进程发现
std::vector<ProcessInfo> EnumerateProcesses(filter, maxResults);
std::optional<ProcessInfo> FindProcess(name);
std::optional<ProcessInfo> GetProcessInfo(pid);

// 附加/采样
AttachResult Attach(pid, SamplingMethod::PDH_Processor);
void Detach();
void StartSampling();  // 后台 ~60Hz 线程
void StopSampling();

// 回调
void SetFrameCallback(fn);   // 每帧数据
void SetStatsCallback(fn);   // 原始 PDH 统计
void SetStatusCallback(fn);   // 状态变化

// 数据访问（线程安全）
std::optional<RemoteFrameData> GetLatestFrame();
std::vector<RemoteFrameData> GetRecentFrames(count);

// RAII 保护
ScopedProcessAttach guard(profiler, pid);
```

### 5. PDH 帧率估算原理
Windows PDH 无法直接给出 FPS，帧时间通过 CPU 利用率间接估算：
- 单核 100% 利用率 = 1 个线程满载
- 4 核系统：单线程满载 ≈ 25% 总 CPU
- 帧时间估算 = `elapsedMs × (cpuFraction / 1.0)`

> 精度说明：此方法为 CPU 调度代理信号，GPU-bound 游戏（常见 3A）误差较大。真正精确帧边界需 Present hook。

### 6. 提交记录
```
commit 6e4dcb6
feat(core): add ProcessProfiler for PDH-based process attach

- ProcessProfiler: attach to running game process via Windows PDH
  (Performance Data Helper), no DLL injection required
  - EnumerateProcesses / FindProcess / GetProcessInfo for process discovery
  - Attach(pid, method): PDH_Processor or PDH_GPU sampling
  - StartSampling / StopSampling on background ~60 Hz thread
  - SetFrameCallback / SetStatsCallback for real-time data delivery
  - Ring buffer (600 frames) + GetRecentFrames / GetLatestFrame accessors
  - ScopedProcessAttach RAII guard for auto-detach
  [...完整变更列表...]
```

## 待后续扩展
- `ProfilerHook.dll` + DLL 注入（需用户主动启用，配合安全白名单）
- Present Hook（IDXGISwapChain::Present，真正 GPU 帧边界）
- 与 Electron/Node.js 后端的 WebSocket 集成
