<template>
  <div id="app">
    <!-- Header (when attached) -->
    <div class="header" v-if="isAttached">
      <div class="header-left">
        <span class="logo">📊</span>
        <span class="title">Game Performance Profiler</span>
      </div>
      <div class="header-right">
        <span class="attach-status-badge">
          🎮 {{ attachStatus.processName || 'PID ' + attachStatus.pid }}
        </span>
        <button class="detach-btn" @click="doDetachProcess">Detach</button>
      </div>
    </div>

    <!-- Header (not attached) -->
    <div class="header header-minimal" v-else>
      <div class="header-left">
        <span class="logo">📊</span>
        <span class="title">Game Performance Profiler</span>
      </div>
    </div>

    <!-- Toast Notifications -->
    <div class="toast-container">
      <transition-group name="toast-fade">
        <div v-for="toast in toasts" :key="toast.id"
             class="toast" :class="'toast-' + toast.type">
          <span class="toast-icon">{{ toastIcon(toast.type) }}</span>
          <span class="toast-msg">{{ toast.message }}</span>
        </div>
      </transition-group>
    </div>

    <!-- Settings Modal -->
    <div class="modal-overlay" v-if="showSettingsModal" @click.self="closeSettings">
      <div class="modal-dialog settings-dialog">
        <div class="modal-header">
          <span class="modal-title">⚙️ Settings</span>
          <button class="modal-close" @click="closeSettings">✕</button>
        </div>
        <div class="settings-body">
          <div class="settings-section">
            <div class="settings-section-title">🎯 Performance Targets</div>
            <div class="settings-row">
              <label class="settings-label">Target FPS</label>
              <select v-model.number="settings.targetFps" class="settings-select" @change="applySettings">
                <option :value="30">30 FPS</option>
                <option :value="60">60 FPS</option>
                <option :value="120">120 FPS</option>
                <option :value="144">144 FPS</option>
                <option :value="240">240 FPS</option>
              </select>
            </div>
            <div class="settings-row">
              <label class="settings-label">FPS Warning Threshold</label>
              <div class="settings-slider-row">
                <input type="range" v-model.number="settings.fpsWarningThreshold"
                       min="20" max="120" step="5" class="settings-slider" @change="applySettings" />
                <span class="settings-slider-val">{{ settings.fpsWarningThreshold }} FPS</span>
              </div>
            </div>
            <div class="settings-row">
              <label class="settings-label">FPS Critical Threshold</label>
              <div class="settings-slider-row">
                <input type="range" v-model.number="settings.fpsCriticalThreshold"
                       min="10" max="90" step="5" class="settings-slider" @change="applySettings" />
                <span class="settings-slider-val">{{ settings.fpsCriticalThreshold }} FPS</span>
              </div>
            </div>
          </div>
          <div class="settings-section">
            <div class="settings-section-title">💾 Memory Alerts</div>
            <div class="settings-row">
              <label class="settings-label">Memory Warning (MB)</label>
              <div class="settings-slider-row">
                <input type="range" v-model.number="settings.memoryWarningMB"
                       min="256" max="4096" step="128" class="settings-slider" @change="applySettings" />
                <span class="settings-slider-val">{{ settings.memoryWarningMB }} MB</span>
              </div>
            </div>
            <div class="settings-row">
              <label class="settings-label">Memory Critical (MB)</label>
              <div class="settings-slider-row">
                <input type="range" v-model.number="settings.memoryCriticalMB"
                       min="512" max="8192" step="256" class="settings-slider" @change="applySettings" />
                <span class="settings-slider-val">{{ settings.memoryCriticalMB }} MB</span>
              </div>
            </div>
          </div>
          <div class="settings-section">
            <div class="settings-section-title">📊 Display</div>
            <div class="settings-row">
              <label class="settings-label">Chart History Length</label>
              <select v-model.number="settings.chartHistoryLength" class="settings-select" @change="applySettings">
                <option :value="60">60 frames</option>
                <option :value="120">120 frames</option>
                <option :value="240">240 frames</option>
                <option :value="500">500 frames</option>
              </select>
            </div>
            <div class="settings-row">
              <label class="settings-label">Show Performance Gauge</label>
              <label class="settings-toggle">
                <input type="checkbox" v-model="settings.showGauge" @change="applySettings" />
                <span class="settings-toggle-slider"></span>
              </label>
            </div>
            <div class="settings-row">
              <label class="settings-label">Show Frame Time Histogram</label>
              <label class="settings-toggle">
                <input type="checkbox" v-model="settings.showHistogram" @change="applySettings" />
                <span class="settings-toggle-slider"></span>
              </label>
            </div>
          </div>
          <div class="settings-section">
            <div class="settings-section-title">⌨️ Keyboard Shortcuts</div>
            <div class="shortcuts-grid">
              <div class="shortcut-item"><kbd>Space</kbd><span>Toggle Simulation</span></div>
              <div class="shortcut-item"><kbd>S</kbd><span>Open Settings</span></div>
              <div class="shortcut-item"><kbd>Esc</kbd><span>Close Modal</span></div>
              <div class="shortcut-item"><kbd>A</kbd><span>Attach Process</span></div>
            </div>
          </div>
        </div>
        <div class="settings-footer">
          <button class="btn-cancel" @click="resetSettings">Reset Defaults</button>
          <button class="btn-attach" @click="closeSettings">Done</button>
        </div>
      </div>
    </div>

    <!-- Attach Process Modal -->
    <div class="modal-overlay" v-if="showAttachModal" @click.self="closeAttachModal">
      <div class="modal-dialog">
        <div class="modal-header">
          <span class="modal-title">🎯 Attach to Process</span>
          <button class="modal-close" @click="closeAttachModal">✕</button>
        </div>
        <div class="pipe-info-box" v-if="pipeInfo">
          <span class="pipe-info-label">Named Pipe:</span>
          <code class="pipe-info-path">{{ pipeInfo.pipePath }}</code>
          <span class="pipe-info-clients" v-if="pipeInfo.connectedClients > 0">{{ pipeInfo.connectedClients }} client(s)</span>
        </div>
        <div class="modal-tabs">
          <button class="tab-btn" :class="{ active: attachTab === 'process' }" @click="attachTab = 'process'; fetchProcessList()">Running Processes</button>
          <button class="tab-btn" :class="{ active: attachTab === 'pipe' }" @click="attachTab = 'pipe'">Named Pipe</button>
          <button class="tab-btn" :class="{ active: attachTab === 'custom' }" @click="attachTab = 'custom'">Custom Path</button>
        </div>
        <div v-if="attachTab === 'process'" class="modal-tab-content">
          <div class="process-search-row">
            <input v-model="processSearch" class="process-search" placeholder="Search..." />
            <button class="refresh-btn" @click="fetchProcessList" :disabled="loadingProcesses">↻</button>
          </div>
          <div class="process-list process-list-simple">
            <div v-for="proc in filteredProcesses" :key="proc.pid"
                 class="process-item-simple" :class="{ selected: selectedPid === proc.pid }"
                 @click="selectedPid = proc.pid; selectedProcessName = proc.name">
              <span class="process-name">{{ proc.name }}</span>
              <span class="process-pid">{{ proc.pid }}</span>
            </div>
            <div class="process-empty" v-if="!loadingProcesses && processList.length === 0">No processes found</div>
            <div class="process-empty" v-if="loadingProcesses">Loading...</div>
          </div>
          <div class="modal-footer">
            <button class="btn-cancel" @click="closeAttachModal">Cancel</button>
            <button class="btn-attach btn-attach-large" :disabled="!selectedPid || attaching" @click="doAttachProcess">
              {{ attaching ? 'Attaching...' : '▶ Attach' }}
            </button>
          </div>
        </div>
        <div v-if="attachTab === 'pipe'" class="modal-tab-content">
          <div class="pipe-desc">
            <p>Games with the <code>profiler-agent</code> library connect here and stream real profiling data (FPS, frame time, function breakdown) with zero instrumentation needed.</p>
          </div>
          <div class="pipe-steps">
            <div class="pipe-step"><span class="pipe-step-num">1</span><span>Add <code>profiler-agent</code> to your game</span></div>
            <div class="pipe-step"><span class="pipe-step-num">2</span><span>Game connects to <code>\\.\pipe\GameProfilerStream</code></span></div>
            <div class="pipe-step"><span class="pipe-step-num">3</span><span>Click <b>Start Named Pipe</b> then start your game</span></div>
          </div>
          <div class="pipe-status" v-if="attachStatus.mode === 'named_pipe'">
            <span class="pipe-active">● Named Pipe Active — {{ pipeInfo?.connectedClients || 0 }} client(s) connected</span>
          </div>
          <div class="pipe-error-msg" v-if="pipeInfo?.error">⚠️ {{ pipeInfo.error }}</div>
          <div class="modal-footer">
            <button class="btn-cancel" @click="closeAttachModal">Cancel</button>
            <button v-if="attachStatus.mode !== 'named_pipe'" class="btn-attach" @click="doStartNamedPipe">▶ Start Named Pipe Server</button>
            <button v-else class="btn-detach" @click="doStopNamedPipe">■ Stop Named Pipe</button>
          </div>
        </div>
        <div v-if="attachTab === 'custom'" class="modal-tab-content">
          <div class="custom-desc">
            <p>Enter the full path to a game executable. The profiler will launch it and monitor CPU/memory usage.</p>
          </div>
          <input v-model="customPath" class="custom-path-input" placeholder="C:\Games\MyGame.exe" />
          <div class="custom-note">⚠️ For games without profiler-agent, FPS is estimated from CPU activity. For best results, use Named Pipe mode.</div>
          <div class="modal-footer">
            <button class="btn-cancel" @click="closeAttachModal">Cancel</button>
            <button class="btn-attach" :disabled="!customPath.trim()" @click="doAttachCustomPath">▶ Launch &amp; Attach</button>
          </div>
        </div>
        <div class="attach-status-bar" v-if="attachStatus.mode === 'process_monitor'">
          <span class="attach-status-icon">🎮</span>
          <span class="attach-status-name">{{ attachStatus.processName || 'PID ' + attachStatus.pid }}</span>
          <span class="attach-status-pid">PID {{ attachStatus.pid }}</span>
          <button class="btn-detach-sm" @click="doDetachProcess">Detach</button>
        </div>
      </div>
    </div>

    <!-- Main Dashboard -->
    <div class="dashboard" v-if="isAttached">
      <!-- Big FPS Display -->
      <div class="hero-fps">
        <div class="fps-number" :style="{ color: fpsColor }">{{ fps }}</div>
        <div class="fps-label">FPS</div>
        <div class="fps-sub" :class="fpsStatusClass">{{ fpsStatus }}</div>
      </div>

      <!-- Stats Row -->
      <div class="stats-row">
        <div class="stat-card">
          <div class="stat-value">{{ frameTime }}</div>
          <div class="stat-label">Frame Time (ms)</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">{{ memory }}</div>
          <div class="stat-label">Memory (MB)</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">{{ frameCount }}</div>
          <div class="stat-label">Frames</div>
        </div>
        <div class="stat-card" :class="statCardClass('avgFps', avgFps)">
          <div class="stat-value">{{ avgFps }}</div>
          <div class="stat-label">Avg FPS</div>
        </div>
        <div class="stat-card" :class="statCardClass('p95Fps', p95Fps)">
          <div class="stat-value">{{ p95Fps }}</div>
          <div class="stat-label">P95 FPS</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">{{ stabilityScore }}%</div>
          <div class="stat-label">Stability</div>
        </div>
      </div>

      <!-- Performance Gauge + Frame Time Histogram Row -->
      <div class="gauge-hist-row" v-if="settings.showGauge || settings.showHistogram">
        <div class="chart-card gauge-card" v-if="settings.showGauge">
          <div class="chart-header">
            <span class="chart-title">Performance Score</span>
            <span class="chart-range" :class="perfScoreClass">{{ perfScoreLabel }}</span>
          </div>
          <div ref="gaugeChartRef" class="chart-area gauge-area"></div>
        </div>
        <div class="chart-card hist-card" v-if="settings.showHistogram">
          <div class="chart-header">
            <span class="chart-title">Frame Time Distribution</span>
            <span class="chart-range">{{ histogramBinCount }} bins · Last {{ settings.chartHistoryLength }} frames</span>
          </div>
          <div ref="histChartRef" class="chart-area hist-area"></div>
        </div>
      </div>

      <!-- Charts -->
      <div class="charts-row">
        <div class="chart-card">
          <div class="chart-header">
            <span class="chart-title">FPS History</span>
            <span class="chart-range">{{ chartRangeLabel }}</span>
          </div>
          <div ref="fpsChartRef" class="chart-area"></div>
        </div>
        <div class="chart-card">
          <div class="chart-header">
            <span class="chart-title">Memory Usage</span>
            <span class="chart-range">{{ chartRangeLabel }}</span>
          </div>
          <div ref="memoryChartRef" class="chart-area"></div>
        </div>
      </div>

      <!-- Function Profiler Section -->
      <div class="func-profiler-section">
        <div class="section-header">
          <span class="section-title">⚡ Function Profiler</span>
          <span class="section-sub">Per-frame time breakdown (microseconds)</span>
        </div>

        <!-- Function Profile Chart -->
        <div class="chart-card func-chart-card">
          <div class="chart-header">
            <span class="chart-title">Stacked Time — Last {{ maxFuncDataPoints }} Frames</span>
            <div class="chart-legend">
              <span v-for="fn in activeFunctions" :key="fn.name"
                    class="legend-item" :style="{ color: fn.color }">
                <span class="legend-dot" :style="{ background: fn.color }"></span>
                {{ fn.name }}
              </span>
            </div>
          </div>
          <div ref="funcChartRef" class="chart-area func-chart-area"></div>
        </div>

        <!-- Function Stats Table -->
        <div class="func-stats-panel">
          <div class="func-stats-header">
            <span class="func-stats-title">Function Statistics (Recent History)</span>
            <span class="func-stats-sub">Avg · Min · Max · P95 per function (µs)</span>
          </div>
          <div class="func-stats-grid">
            <div v-for="fn in functionStats" :key="fn.name"
                 class="func-stat-card"
                 :style="{ borderTopColor: fn.color }">
              <div class="func-stat-name" :style="{ color: fn.color }">{{ fn.name }}</div>
              <div class="func-stat-values">
                <div class="func-stat-row">
                  <span class="func-stat-label">Avg</span>
                  <span class="func-stat-value">{{ formatMicros(fn.avg) }}</span>
                </div>
                <div class="func-stat-row">
                  <span class="func-stat-label">Min</span>
                  <span class="func-stat-value">{{ formatMicros(fn.min) }}</span>
                </div>
                <div class="func-stat-row">
                  <span class="func-stat-label">Max</span>
                  <span class="func-stat-value func-stat-max">{{ formatMicros(fn.max) }}</span>
                </div>
                <div class="func-stat-row">
                  <span class="func-stat-label">P95</span>
                  <span class="func-stat-value func-stat-p95">{{ formatMicros(fn.p95) }}</span>
                </div>
              </div>
              <!-- Mini bar showing relative max vs avg -->
              <div class="func-mini-bar-wrap">
                <div class="func-mini-bar-avg"
                     :style="{ width: barWidth(fn.avg, maxFuncAvg) + '%', background: fn.color + '66' }">
                </div>
                <div class="func-mini-bar-max"
                     :style="{ width: barWidth(fn.max, maxFuncMax) + '%', background: fn.color }">
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Total Frame Time Stats -->
        <div class="frame-total-row">
          <div class="frame-total-card">
            <span class="frame-total-label">Avg Frame Total</span>
            <span class="frame-total-value">{{ formatMicros(avgFrameTotal) }} µs</span>
          </div>
          <div class="frame-total-card">
            <span class="frame-total-label">Max Frame Total</span>
            <span class="frame-total-value frame-total-max">{{ formatMicros(maxFrameTotal) }} µs</span>
          </div>
          <div class="frame-total-card">
            <span class="frame-total-label">P95 Frame Total</span>
            <span class="frame-total-value">{{ formatMicros(p95FrameTotal) }} µs</span>
          </div>
          <div class="frame-total-card">
            <span class="frame-total-label">Hottest Function</span>
            <span class="frame-total-value frame-total-hot"
                  :style="{ color: hottestFuncColor }">{{ hottestFuncName }}</span>
          </div>
        </div>
      </div>

      <!-- Alert Panel -->
      <div class="alert-panel" v-if="alerts.length > 0">
        <div class="alert-header">
          <span class="alert-title">⚠️ Performance Alerts</span>
          <div class="alert-header-actions">
            <span class="alert-count-badge" :class="{ 'has-critical': hasCriticalAlerts }">
              {{ unacknowledgedCount }} active
            </span>
            <button class="alert-ack-all-btn" @click="acknowledgeAllAlerts" v-if="unacknowledgedCount > 0">
              ✓ Ack All
            </button>
          </div>
        </div>
        <div class="alert-list">
          <div v-for="alert in visibleAlerts" :key="alert.id"
               class="alert-item" :class="'alert-' + alert.severity">
            <div class="alert-item-left">
              <span class="alert-icon">{{ alertSeverityIcon(alert.severity) }}</span>
              <div class="alert-item-content">
                <span class="alert-message">{{ alert.message }}</span>
                <span class="alert-detail">{{ alert.details }}</span>
                <span class="alert-meta">{{ alertMetricLabel(alert) }} · {{ formatAlertTime(alert.timestamp) }}</span>
              </div>
            </div>
            <div class="alert-item-right">
              <button v-if="!alert.acknowledged" class="alert-ack-btn" @click="acknowledgeAlert(alert.id)">
                ✓
              </button>
              <span v-else class="alert-acked">✓</span>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Attach Screen (when not attached) -->
    <div class="attach-screen" v-else>
      <div class="attach-content">
        <div class="attach-icon">🎮</div>
        <div class="attach-title">Attach to a Process</div>
        <div class="attach-sub">Select a game or application to start profiling</div>
        <button class="attach-button-large" @click="openAttachModal">
          🎯 Attach to Process
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'

const fpsChartRef = ref(null)
const memoryChartRef = ref(null)
const funcChartRef = ref(null)
const gaugeChartRef = ref(null)
const histChartRef = ref(null)
const connected = ref(false)
const simulationOn = ref(true)
const fps = ref(0)
const frameTime = ref('0.00')
const memory = ref('0')
const frameCount = ref(0)
const avgFps = ref(0)
const p95Fps = ref(0)
const stabilityScore = ref(100)
const fpsHistory = ref([])
const memoryHistory = ref([])

// maxDataPoints is now controlled by settings.chartHistoryLength
const getDefaultDataPoints = () => 120

// Function profiler state
const maxFuncDataPoints = 80
const funcHistory = ref([])  // Array of { profiles: [{name, duration}] }
const funcStatsData = ref({})  // { name: { avg, min, max, p95 } }
const activeFunctions = ref([
  { name: 'Update',    color: '#e94560' },
  { name: 'Render',   color: '#4fc3f7' },
  { name: 'Physics',  color: '#69f0ae' },
  { name: 'Audio',    color: '#ffca28' },
  { name: 'AI',        color: '#ce93d8' },
  { name: 'Networking',color: '#ff8a65' },
  { name: 'GC',        color: '#ef5350' },
])

// Alert system state
const alerts = ref([])
const maxVisibleAlerts = 10

// Settings state
const showSettingsModal = ref(false)
const settings = ref({
  targetFps: 60,
  fpsWarningThreshold: 45,
  fpsCriticalThreshold: 30,
  memoryWarningMB: 1024,
  memoryCriticalMB: 2048,
  chartHistoryLength: 120,
  showGauge: true,
  showHistogram: true
})

// Toast notification state
const toasts = ref([])
let toastIdCounter = 1

// Computed for histogram bin count
const histogramBinCount = 20

// Attach process state
const showAttachModal = ref(false)
const attachTab = ref('process')
const attachStatus = ref({ mode: 'none', pid: null, processName: '' })
const pipeInfo = ref(null)
const processList = ref([])
const loadingProcesses = ref(false)
const processSearch = ref('')
const selectedPid = ref(null)
const selectedProcessName = ref('')
const attaching = ref(false)
const customPath = ref('')

// Computed: is currently attached to a process
const isAttached = computed(() => {
  return attachStatus.value.mode === 'process' && attachStatus.value.pid !== null
})

let fpsChart = null
let memoryChart = null
let funcChart = null
let gaugeChart = null
let histChart = null
let ws = null
let reconnectTimer = null

const chartRangeLabel = computed(() => `Last ${settings.value.chartHistoryLength} frames`)

const hasCriticalAlerts = computed(() => alerts.value.some(a => a.severity === 'critical' && !a.acknowledged))
const unacknowledgedCount = computed(() => alerts.value.filter(a => !a.acknowledged).length)

// Performance score computed (0-100)
const performanceScore = computed(() => {
  if (fpsHistory.value.length < 10) return 100
  const avg = avgFps.value
  const target = settings.value.targetFps
  const stability = stabilityScore.value
  // Score based on avg FPS vs target (60%) and stability (40%)
  const fpsRatio = Math.min(1, avg / target)
  const fpsScore = fpsRatio * 60
  const stabilityPortion = (stability / 100) * 40
  return Math.round(fpsScore + stabilityPortion)
})

const perfScoreLabel = computed(() => {
  const s = performanceScore.value
  if (s >= 90) return 'Excellent'
  if (s >= 75) return 'Good'
  if (s >= 50) return 'Fair'
  if (s >= 25) return 'Poor'
  return 'Critical'
})

const perfScoreClass = computed(() => {
  const s = performanceScore.value
  if (s >= 90) return 'excellent'
  if (s >= 75) return 'good'
  if (s >= 50) return 'fair'
  if (s >= 25) return 'poor'
  return 'critical'
})

// Data mode indicator
const dataMode = computed(() => attachStatus.value.mode || (simulationOn.value ? 'simulation' : 'none'))
const modeLabel = computed(() => {
  const m = dataMode.value
  if (m === 'simulation') return '🎲 Sim'
  if (m === 'named_pipe') return '🔌 Pipe'
  if (m === 'process_monitor') return '🎮 PID ' + (attachStatus.value.pid || '')
  return '—'
})
const visibleAlerts = computed(() => {
  const active = alerts.value.filter(a => !a.acknowledged)
  const acked = alerts.value.filter(a => a.acknowledged)
  return [...active.slice(-maxVisibleAlerts), ...acked.slice(-3)]
})

// Function profiler computed values
const functionStats = computed(() => {
  const stats = []
  const data = funcStatsData.value
  activeFunctions.value.forEach(fn => {
    const s = data[fn.name]
    if (s) {
      stats.push({ name: fn.name, color: fn.color, avg: s.avg, min: s.min, max: s.max, p95: s.p95 })
    }
  })
  return stats
})

const maxFuncAvg = computed(() => {
  const vals = functionStats.value.map(s => s.avg).filter(v => v > 0)
  return vals.length ? Math.max(...vals) : 1
})

const maxFuncMax = computed(() => {
  const vals = functionStats.value.map(s => s.max).filter(v => v > 0)
  return vals.length ? Math.max(...vals) : 1
})

const avgFrameTotal = computed(() => {
  if (!funcHistory.value.length) return 0
  return funcHistory.value.reduce((sum, f) => {
    if (!f.profiles) return sum
    return sum + f.profiles.reduce((s, p) => s + (p.duration || 0), 0)
  }, 0) / funcHistory.value.length
})

const maxFrameTotal = computed(() => {
  if (!funcHistory.value.length) return 0
  return Math.max(...funcHistory.value.map(f => {
    if (!f.profiles) return 0
    return f.profiles.reduce((s, p) => s + (p.duration || 0), 0)
  }))
})

const p95FrameTotal = computed(() => {
  if (funcHistory.value.length < 2) return 0
  const totals = funcHistory.value.map(f => {
    if (!f.profiles) return 0
    return f.profiles.reduce((s, p) => s + (p.duration || 0), 0)
  }).sort((a, b) => a - b)
  const idx = Math.min(Math.floor(totals.length * 0.95), totals.length - 1)
  return totals[idx] || 0
})

const hottestFuncName = computed(() => {
  if (!functionStats.value.length) return '—'
  const hottest = functionStats.value.reduce((best, fn) => fn.avg > best.avg ? fn : best, { avg: 0, name: '—' })
  return hottest.name
})

const hottestFuncColor = computed(() => {
  const fn = functionStats.value.find(f => f.name === hottestFuncName.value)
  return fn ? fn.color : '#888'
})

function formatMicros(us) {
  if (us === undefined || us === null || isNaN(us)) return '—'
  if (us >= 1000) return (us / 1000).toFixed(2) + ' ms'
  return us.toFixed(1) + ' µs'
}

function barWidth(value, max) {
  return max > 0 ? Math.min(100, (value / max) * 100) : 0
}

function alertSeverityIcon(severity) {
  if (severity === 'critical') return '🔴'
  if (severity === 'warning') return '🟡'
  return 'ℹ️'
}

function alertMetricLabel(alert) {
  const labels = {
    'fps': 'FPS',
    'frameTime': 'Frame Time',
    'memory': 'Memory',
    'memoryGrowthRate': 'Mem Growth',
    'stabilityScore': 'Stability',
    'sustainedLowFps': 'Sustained Low FPS'
  }
  return labels[alert.metric] || alert.metric
}

function formatAlertTime(timestamp) {
  const diff = Date.now() - timestamp
  if (diff < 1000) return 'just now'
  if (diff < 60000) return `${Math.floor(diff / 1000)}s ago`
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
  return new Date(timestamp).toLocaleTimeString()
}

function acknowledgeAlert(alertId) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'acknowledge_alert', alertId }))
  }
  const alert = alerts.value.find(a => a.id === alertId)
  if (alert) {
    alert.acknowledged = true
    alert.acknowledgedAt = Date.now()
  }
}

function acknowledgeAllAlerts() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'acknowledge_all_alerts' }))
  }
  alerts.value.forEach(a => {
    a.acknowledged = true
    a.acknowledgedAt = Date.now()
  })
}

const fpsColor = computed(() => {
  if (fps.value >= 55) return '#00e676'
  if (fps.value >= 45) return '#ffca28'
  if (fps.value >= 30) return '#ff7043'
  return '#ef5350'
})

const fpsStatus = computed(() => {
  if (fps.value >= 55) return 'Excellent'
  if (fps.value >= 45) return 'Good'
  if (fps.value >= 30) return 'Fair'
  return 'Poor'
})

const fpsStatusClass = computed(() => {
  if (fps.value >= 55) return 'excellent'
  if (fps.value >= 45) return 'good'
  if (fps.value >= 30) return 'fair'
  return 'poor'
})

function statCardClass(metric, value) {
  if (metric === 'avgFps' || metric === 'p95Fps') {
    if (value >= 55) return 'stat-excellent'
    if (value >= 45) return 'stat-good'
    if (value >= 30) return 'stat-fair'
    if (value > 0) return 'stat-poor'
  }
  return ''
}

function buildFpsOption(data) {
  return {
    backgroundColor: 'transparent',
    grid: { top: 8, right: 16, bottom: 24, left: 48 },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      axisLine: { lineStyle: { color: '#2a2a4a' } },
      axisTick: { show: false },
      axisLabel: { color: '#666', fontSize: 10 },
      data: data.map((_, i) => i)
    },
    yAxis: {
      type: 'value',
      min: 0,
      splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } },
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: { color: '#888', fontSize: 10 }
    },
    series: [
      {
        type: 'line',
        data,
        smooth: 0.5,
        symbol: 'none',
        lineStyle: { color: '#e94560', width: 2 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(233,69,96,0.25)' },
              { offset: 1, color: 'rgba(233,69,96,0.02)' }
            ]
          }
        },
        markLine: simulationOn.value ? {
          silent: true,
          symbol: 'none',
          lineStyle: { color: 'rgba(255,202,40,0.4)', type: 'dashed', width: 1 },
          data: [{ yAxis: 60 }]
        } : undefined
      }
    ],
    animation: false
  }
}

function buildMemoryOption(data) {
  return {
    backgroundColor: 'transparent',
    grid: { top: 8, right: 16, bottom: 24, left: 48 },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      axisLine: { lineStyle: { color: '#2a2a4a' } },
      axisTick: { show: false },
      axisLabel: { color: '#666', fontSize: 10 },
      data: data.map((_, i) => i)
    },
    yAxis: {
      type: 'value',
      min: 0,
      splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } },
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: { color: '#888', fontSize: 10 }
    },
    series: [{
      type: 'line',
      data,
      smooth: 0.5,
      symbol: 'none',
      lineStyle: { color: '#4fc3f7', width: 2 },
      areaStyle: {
        color: {
          type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [
            { offset: 0, color: 'rgba(79,195,247,0.2)' },
            { offset: 1, color: 'rgba(79,195,247,0.02)' }
          ]
        }
      }
    }],
    animation: false
  }
}

function buildGaugeOption(score) {
  const getColor = (s) => {
    if (s >= 90) return '#00e676'
    if (s >= 75) return '#69f0ae'
    if (s >= 50) return '#ffca28'
    if (s >= 25) return '#ff7043'
    return '#ef5350'
  }
  return {
    backgroundColor: 'transparent',
    series: [{
      type: 'gauge',
      startAngle: 200,
      endAngle: -20,
      min: 0,
      max: 100,
      splitNumber: 5,
      center: ['50%', '60%'],
      radius: '85%',
      axisLine: {
        lineStyle: {
          width: 16,
          color: [
            [0.25, '#ef5350'],
            [0.5, '#ff7043'],
            [0.75, '#ffca28'],
            [0.9, '#69f0ae'],
            [1, '#00e676']
          ]
        }
      },
      pointer: {
        icon: 'path://M12,2A10,10 0 0,0 12,22A10,10 0 0,0 12,2',
        length: '60%',
        width: 6,
        offsetCenter: [0, '-10%'],
        itemStyle: { color: getColor(score) }
      },
      axisTick: { show: false },
      splitLine: { show: false },
      axisLabel: { show: false },
      title: { show: false },
      detail: {
        valueAnimation: true,
        fontSize: 36,
        fontWeight: 700,
        offsetCenter: [0, '25%'],
        formatter: '{value}',
        color: getColor(score)
      },
      data: [{ value: score, name: '' }]
    }],
    animation: true,
    animationDuration: 600,
    animationEasing: 'cubicOut'
  }
}

function buildHistogramOption(fpsData, bins) {
  if (!fpsData || fpsData.length < 2) {
    return {
      backgroundColor: 'transparent',
      grid: { top: 8, right: 16, bottom: 28, left: 48 },
      xAxis: { type: 'category', data: [], axisLine: { lineStyle: { color: '#2a2a4a' } }, axisLabel: { color: '#555' } },
      yAxis: { type: 'value', splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } }, axisLabel: { color: '#555' } },
      series: [],
      animation: false
    }
  }
  // Compute frame times in ms
  const frameTimes = fpsData.map(fps => fps > 0 ? 1000 / fps : 0).filter(t => t > 0)
  if (frameTimes.length < 2) {
    return {
      backgroundColor: 'transparent',
      grid: { top: 8, right: 16, bottom: 28, left: 48 },
      xAxis: { type: 'category', data: [], axisLine: { lineStyle: { color: '#2a2a4a' } }, axisLabel: { color: '#555' } },
      yAxis: { type: 'value', splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } }, axisLabel: { color: '#555' } },
      series: [],
      animation: false
    }
  }

  const minTime = Math.min(...frameTimes)
  const maxTime = Math.max(...frameTimes)
  const binWidth = (maxTime - minTime) / bins

  // Build histogram bins
  const histogram = new Array(bins).fill(0)
  frameTimes.forEach(t => {
    const idx = Math.min(Math.floor((t - minTime) / binWidth), bins - 1)
    if (idx >= 0 && idx < bins) histogram[idx]++
  })

  // Build X-axis labels (bin centers in ms)
  const xLabels = histogram.map((_, i) => {
    const center = minTime + (i + 0.5) * binWidth
    return center.toFixed(1)
  })

  // Target frame time marker (for target FPS)
  const targetFrameTime = 1000 / settings.value.targetFps

  return {
    backgroundColor: 'transparent',
    grid: { top: 8, right: 16, bottom: 28, left: 48 },
    xAxis: {
      type: 'category',
      data: xLabels,
      axisLine: { lineStyle: { color: '#2a2a4a' } },
      axisTick: { show: false },
      axisLabel: { color: '#666', fontSize: 10, interval: Math.floor(bins / 6) },
      name: 'ms',
      nameTextStyle: { color: '#444', fontSize: 10 },
      nameLocation: 'end'
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } },
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: { color: '#888', fontSize: 10 }
    },
    series: [{
      type: 'bar',
      data: histogram,
      barWidth: '80%',
      itemStyle: {
        color: {
          type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [
            { offset: 0, color: 'rgba(233,69,96,0.6)' },
            { offset: 1, color: 'rgba(233,69,96,0.2)' }
          ]
        },
        borderRadius: [2, 2, 0, 0]
      },
      emphasis: {
        itemStyle: { color: 'rgba(233,69,96,0.8)' }
      }
    }],
    animation: false
  }
}

function buildFuncChartOption(history) {
  if (!history.length) {
    return {
      backgroundColor: 'transparent',
      grid: { top: 8, right: 16, bottom: 24, left: 48 },
      xAxis: {
        type: 'category',
        boundaryGap: false,
        axisLine: { lineStyle: { color: '#2a2a4a' } },
        axisTick: { show: false },
        axisLabel: { color: '#666', fontSize: 10 },
        data: []
      },
      yAxis: {
        type: 'value',
        min: 0,
        splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } },
        axisLine: { show: false },
        axisTick: { show: false },
        axisLabel: {
          color: '#888',
          fontSize: 10,
          formatter: (v) => v >= 1000 ? (v / 1000).toFixed(1) + 'k' : v
        },
        name: 'Time (µs)',
        nameTextStyle: { color: '#555', fontSize: 10 }
      },
      series: [],
      animation: false
    }
  }

  const fnOrder = ['Update', 'Render', 'Physics', 'Audio', 'AI', 'Networking', 'GC']
  const colors = {
    Update: '#e94560', Render: '#4fc3f7', Physics: '#69f0ae',
    Audio: '#ffca28', AI: '#ce93d8', Networking: '#ff8a65', GC: '#ef5350'
  }

  const xData = history.map((_, i) => i)

  // Build stacked series (reverse order so first function is on top)
  const series = fnOrder.map(fnName => ({
    name: fnName,
    type: 'line',
    stack: 'func',
    data: history.map(f => {
      const entry = f.profiles ? f.profiles.find(p => p.name === fnName) : null
      return entry ? entry.duration : 0
    }),
    smooth: 0.4,
    symbol: 'none',
    lineStyle: { width: 0 },
    areaStyle: {
      color: colors[fnName] || '#888',
      opacity: 0.7
    },
    emphasis: { disabled: true },
    animation: false
  }))

  return {
    backgroundColor: 'transparent',
    grid: { top: 8, right: 16, bottom: 24, left: 52 },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      axisLine: { lineStyle: { color: '#2a2a4a' } },
      axisTick: { show: false },
      axisLabel: { color: '#666', fontSize: 10 },
      data: xData
    },
    yAxis: {
      type: 'value',
      min: 0,
      splitLine: { lineStyle: { color: '#1e1e3a', type: 'dashed' } },
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: {
        color: '#888',
        fontSize: 10,
        formatter: (v) => v >= 1000 ? (v / 1000).toFixed(0) + 'k' : v
      },
      name: 'µs',
      nameTextStyle: { color: '#555', fontSize: 10 }
    },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'line', lineStyle: { color: '#2a2a5a', type: 'dashed' } },
      backgroundColor: 'rgba(13,13,26,0.95)',
      borderColor: '#2a2a4a',
      textStyle: { color: '#c0c0d0', fontSize: 12 },
      formatter: (params) => {
        const total = params.reduce((s, p) => s + (p.value || 0), 0)
        const lines = params
          .filter(p => p.value > 0)
          .sort((a, b) => b.value - a.value)
          .map(p => {
            const pct = total > 0 ? ((p.value / total) * 100).toFixed(1) : 0
            const color = colors[p.seriesName] || '#888'
            const val = p.value >= 1000 ? (p.value / 1000).toFixed(2) + ' ms' : p.value.toFixed(1) + ' µs'
            return `<span style="color:${color}">■</span> ${p.seriesName}: <b>${val}</b> <span style="color:#555">(${pct}%)</span>`
          })
        const totalStr = total >= 1000 ? (total / 1000).toFixed(2) + ' ms' : total.toFixed(1) + ' µs'
        return `<div style="font-size:11px;line-height:1.8"><b>Frame ${params[0].axisValue}</b><br/>` +
               lines.join('<br/>') +
               `<br/><span style="color:#888">Total: ${totalStr}</span></div>`
      }
    },
    legend: { show: false },
    series,
    animation: false
  }
}

// ─────────────────────────────────────────────
// Keyboard Shortcuts
// ─────────────────────────────────────────────

function handleKeyDown(e) {
  // Ignore if user is typing in an input
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') return
  
  if (e.key === ' ') {
    e.preventDefault()
    toggleSimulation()
    showToast(simulationOn.value ? 'Simulation started' : 'Simulation stopped', 'info')
  } else if (e.key === 'Escape') {
    if (showSettingsModal.value) closeSettings()
    else if (showAttachModal.value) closeAttachModal()
  } else if (e.key.toLowerCase() === 's') {
    if (!showAttachModal.value && !showSettingsModal.value) {
      openSettings()
    }
  } else if (e.key.toLowerCase() === 'a') {
    if (!showAttachModal.value && !showSettingsModal.value) {
      openAttachModal()
    }
  }
}

// ─────────────────────────────────────────────
// Toast Notifications
// ─────────────────────────────────────────────

function showToast(message, type = 'info') {
  const id = toastIdCounter++
  toasts.value.push({ id, message, type, timestamp: Date.now() })
  // Auto-remove after 3 seconds
  setTimeout(() => {
    const idx = toasts.value.findIndex(t => t.id === id)
    if (idx !== -1) toasts.value.splice(idx, 1)
  }, 3000)
}

function toastIcon(type) {
  if (type === 'success') return '✓'
  if (type === 'error') return '✕'
  if (type === 'warning') return '⚠'
  return 'ℹ'
}

// ─────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────

function openSettings() {
  showSettingsModal.value = true
}

function closeSettings() {
  showSettingsModal.value = false
}

function applySettings() {
  // Trim history if needed
  const maxLen = settings.value.chartHistoryLength
  while (fpsHistory.value.length > maxLen) fpsHistory.value.shift()
  while (memoryHistory.value.length > maxLen) memoryHistory.value.shift()
  showToast('Settings saved', 'success')
}

function resetSettings() {
  settings.value = {
    targetFps: 60,
    fpsWarningThreshold: 45,
    fpsCriticalThreshold: 30,
    memoryWarningMB: 1024,
    memoryCriticalMB: 2048,
    chartHistoryLength: 120,
    showGauge: true,
    showHistogram: true
  }
  showToast('Settings reset to defaults', 'info')
}

onMounted(() => {
  fpsChart = echarts.init(fpsChartRef.value)
  memoryChart = echarts.init(memoryChartRef.value)
  funcChart = echarts.init(funcChartRef.value)
  gaugeChart = echarts.init(gaugeChartRef.value)
  histChart = echarts.init(histChartRef.value)
  window.addEventListener('resize', handleResize)
  window.addEventListener('keydown', handleKeyDown)
  connectWebSocket()
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  window.removeEventListener('keydown', handleKeyDown)
  if (reconnectTimer) clearTimeout(reconnectTimer)
  if (ws) ws.close()
  if (fpsChart) fpsChart.dispose()
  if (memoryChart) memoryChart.dispose()
  if (funcChart) funcChart.dispose()
  if (gaugeChart) gaugeChart.dispose()
  if (histChart) histChart.dispose()
})

function handleResize() {
  if (fpsChart) fpsChart.resize()
  if (memoryChart) memoryChart.resize()
  if (funcChart) funcChart.resize()
  if (gaugeChart) gaugeChart.resize()
  if (histChart) histChart.resize()
}

function connectWebSocket() {
  const wsUrl = 'ws://localhost:8081'
  ws = new WebSocket(wsUrl)

  ws.onopen = () => {
    connected.value = true
  }

  ws.onclose = () => {
    connected.value = false
    reconnectTimer = setTimeout(connectWebSocket, 2000)
  }

  ws.onerror = () => {}

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data)
      if (msg.type === 'frame_update' || msg.type === 'frame') {
        updateData(msg.data)
      } else if (msg.type === 'simulation_status') {
        simulationOn.value = msg.running
      } else if (msg.type === 'alert') {
        handleIncomingAlert(msg.data)
      } else if (msg.type === 'alert_acknowledged') {
        const alert = alerts.value.find(a => a.id === msg.data.id)
        if (alert) {
          alert.acknowledged = true
          alert.acknowledgedAt = msg.data.acknowledgedAt
        }
      } else if (msg.type === 'alerts_all_acknowledged') {
        alerts.value.forEach(a => { a.acknowledged = true })
      } else if (msg.type === 'attach_status') {
        attachStatus.value = msg.data || { mode: 'none' }
        pipeInfo.value = msg.data?.pipeInfo || null
      } else if (msg.type === 'attach_process_exited') {
        // Process exited, detach
        attachStatus.value = { mode: 'none', pid: null, processName: '' }
      }
    } catch {}
  }
}

function updateData(data) {
  fps.value = Math.round(data.fps || 0)
  frameTime.value = (data.frameTime || 0).toFixed(2)
  memory.value = Math.round((data.memory || 0) / 1024 / 1024)
  frameCount.value = data.frame || 0

  const fpsVal = data.fps || 0
  const memVal = (data.memory || 0) / 1024 / 1024

  fpsHistory.value.push(fpsVal)
  memoryHistory.value.push(memVal)

  const maxLen = settings.value.chartHistoryLength
  if (fpsHistory.value.length > maxLen) fpsHistory.value.shift()
  if (memoryHistory.value.length > maxLen) memoryHistory.value.shift()

  // Recalculate stats from history
  if (fpsHistory.value.length > 0) {
    const sorted = [...fpsHistory.value].sort((a, b) => a - b)
    avgFps.value = Math.round(sorted.reduce((a, b) => a + b, 0) / sorted.length)
    const p95Index = Math.floor(sorted.length * 0.95)
    p95Fps.value = Math.round(sorted[p95Index] || 0)
    const mean = sorted.reduce((a, b) => a + b, 0) / sorted.length
    const variance = sorted.reduce((a, b) => a + (b - mean) ** 2, 0) / sorted.length
    const cv = mean > 0 ? Math.sqrt(variance) / mean : 0
    stabilityScore.value = Math.max(0, Math.round((1 - Math.min(cv, 1)) * 100))
  }

  // Update function profiler data
  if (data.profiles && data.profiles.length > 0) {
    funcHistory.value.push({ profiles: data.profiles })
    if (funcHistory.value.length > maxFuncDataPoints) {
      funcHistory.value.shift()
    }
    // Compute rolling stats for each function
    recomputeFuncStats()
  }

  if (fpsChart) {
    fpsChart.setOption({
      xAxis: { data: fpsHistory.value.map((_, i) => i) },
      series: [{ data: fpsHistory.value }]
    })
  }
  if (memoryChart) {
    memoryChart.setOption({
      xAxis: { data: memoryHistory.value.map((_, i) => i) },
      series: [{ data: memoryHistory.value }]
    })
  }
  if (funcChart && funcHistory.value.length > 0) {
    funcChart.setOption(buildFuncChartOption(funcHistory.value))
  }
  // Update gauge and histogram charts
  if (gaugeChart && settings.value.showGauge) {
    gaugeChart.setOption(buildGaugeOption(performanceScore.value))
  }
  if (histChart && settings.value.showHistogram && fpsHistory.value.length >= 10) {
    histChart.setOption(buildHistogramOption(fpsHistory.value, histogramBinCount))
  }
}

function recomputeFuncStats() {
  const history = funcHistory.value
  if (history.length < 2) return

  const fnNames = activeFunctions.value.map(f => f.name)
  const result = {}

  fnNames.forEach(fn => {
    const values = history
      .map(f => f.profiles ? f.profiles.find(p => p.name === fn) : null)
      .filter(Boolean)
      .map(p => p.duration || 0)

    if (values.length === 0) return

    const sorted = [...values].sort((a, b) => a - b)
    const sum = values.reduce((a, b) => a + b, 0)
    const avg = sum / values.length
    const p95idx = Math.min(Math.floor(values.length * 0.95), values.length - 1)

    result[fn] = {
      avg: parseFloat(avg.toFixed(2)),
      min: parseFloat(Math.min(...values).toFixed(2)),
      max: parseFloat(Math.max(...values).toFixed(2)),
      p95: parseFloat(sorted[p95idx].toFixed(2))
    }
  })

  funcStatsData.value = result
}

function toggleSimulation() {
  simulationOn.value = !simulationOn.value
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({
      type: 'simulation_control',
      action: simulationOn.value ? 'start' : 'stop'
    }))
  }
}

function handleIncomingAlert(alert) {
  // Avoid duplicates
  if (alerts.value.some(a => a.id === alert.id)) return
  alerts.value.push(alert)
  // Keep only last 50 alerts in memory
  if (alerts.value.length > 50) {
    alerts.value = alerts.value.slice(-50)
  }
}

// ─────────────────────────────────────────────
// Attach Process Functions
// ─────────────────────────────────────────────

async function fetchProcessList() {
  loadingProcesses.value = true
  try {
    const res = await fetch('http://localhost:8080/api/processes')
    const data = await res.json()
    processList.value = data.processes || []
  } catch {
    processList.value = []
  } finally {
    loadingProcesses.value = false
  }
}

const filteredProcesses = computed(() => {
  const q = processSearch.value.toLowerCase()
  if (!q) return processList.value
  return processList.value.filter(p =>
    p.name.toLowerCase().includes(q) ||
    (p.windowTitle && p.windowTitle.toLowerCase().includes(q)) ||
    p.pid.toString().includes(q)
  )
})

function openAttachModal() {
  showAttachModal.value = true
  attachTab.value = 'process'
  selectedPid.value = null
  selectedProcessName.value = ''
  fetchProcessList()
  // Fetch current attach status
  fetchAttachStatus()
}

function closeAttachModal() {
  showAttachModal.value = false
}

async function fetchAttachStatus() {
  try {
    const res = await fetch('http://localhost:8080/api/attach-status')
    const data = await res.json()
    attachStatus.value = data
    pipeInfo.value = data.pipeInfo || null
  } catch {}
}

async function doAttachProcess() {
  if (!selectedPid.value) return
  attaching.value = true
  try {
    const res = await fetch('http://localhost:8080/api/attach', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pid: selectedPid.value, processName: selectedProcessName.value })
    })
    const data = await res.json()
    if (data.status) attachStatus.value = data.status
    closeAttachModal()
    showToast(`Attached to ${selectedProcessName.value || 'PID ' + selectedPid.value}`, 'success')
  } catch (e) {
    console.error('Attach failed:', e)
    showToast('Failed to attach to process', 'error')
  } finally {
    attaching.value = false
  }
}

async function doDetachProcess() {
  try {
    await fetch('http://localhost:8080/api/detach', { method: 'POST' })
    attachStatus.value = { mode: 'none', pid: null, processName: '' }
    showToast('Detached from process', 'info')
  } catch {
    showToast('Failed to detach', 'error')
  }
}

async function doStartNamedPipe() {
  try {
    const res = await fetch('http://localhost:8080/api/named-pipe/start', { method: 'POST' })
    const data = await res.json()
    if (data.status) attachStatus.value = data.status
    pipeInfo.value = data.pipeInfo || null
    showToast('Named pipe server started', 'success')
  } catch {
    showToast('Failed to start named pipe', 'error')
  }
}

async function doStopNamedPipe() {
  try {
    await fetch('http://localhost:8080/api/named-pipe/stop', { method: 'POST' })
    attachStatus.value = { mode: 'none', pid: null, processName: '' }
    pipeInfo.value = null
    showToast('Named pipe server stopped', 'info')
  } catch {
    showToast('Failed to stop named pipe', 'error')
  }
}

async function doAttachCustomPath() {
  if (!customPath.value.trim()) return
  // Spawn the process, then attach
  try {
    const res = await fetch('http://localhost:8080/api/spawn-and-attach', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path: customPath.value.trim() })
    })
    const data = await res.json()
    if (data.status) attachStatus.value = data.status
    closeAttachModal()
  } catch {}
}
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }

body {
  font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif;
  background: #0d0d1a;
  color: #e0e0e0;
  min-height: 100vh;
  overflow-x: hidden;
}

#app { min-height: 100vh; }

/* Header */
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 28px;
  height: 60px;
  background: #13132b;
  border-bottom: 1px solid #1e1e3f;
  position: sticky;
  top: 0;
  z-index: 100;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.logo { font-size: 22px; }

.title {
  font-size: 17px;
  font-weight: 600;
  color: #e0e0f0;
  letter-spacing: 0.3px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.status-badge {
  font-size: 12px;
  font-weight: 500;
  padding: 4px 12px;
  border-radius: 20px;
}

.status-badge.connected {
  background: rgba(0, 230, 118, 0.12);
  color: #00e676;
  border: 1px solid rgba(0, 230, 118, 0.3);
}

.status-badge.disconnected {
  background: rgba(239, 83, 80, 0.12);
  color: #ef5350;
  border: 1px solid rgba(239, 83, 80, 0.3);
}

/* Simulation Button */
.sim-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 16px;
  border-radius: 20px;
  border: 1px solid #2a2a5a;
  background: #1a1a35;
  color: #888;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.sim-btn .dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #555;
  transition: all 0.2s;
}

.sim-btn.active {
  border-color: rgba(0, 230, 118, 0.4);
  background: rgba(0, 230, 118, 0.08);
  color: #00e676;
}

.sim-btn.active .dot {
  background: #00e676;
  box-shadow: 0 0 6px #00e676;
}

.sim-btn:hover {
  border-color: #3a3a6a;
  color: #aaa;
}

/* Dashboard */
.dashboard {
  padding: 28px;
  max-width: 1400px;
  margin: 0 auto;
}

/* Hero FPS */
.hero-fps {
  text-align: center;
  padding: 32px 0 20px;
}

.fps-number {
  font-size: 96px;
  font-weight: 700;
  line-height: 1;
  letter-spacing: -3px;
  transition: color 0.3s;
}

.fps-label {
  font-size: 20px;
  color: #555;
  margin-top: 4px;
  letter-spacing: 4px;
  text-transform: uppercase;
}

.fps-sub {
  font-size: 14px;
  margin-top: 8px;
  font-weight: 500;
  letter-spacing: 1px;
}

.fps-sub.excellent { color: #00e676; }
.fps-sub.good { color: #ffca28; }
.fps-sub.fair { color: #ff7043; }
.fps-sub.poor { color: #ef5350; }

/* Stats Row */
.stats-row {
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  gap: 12px;
  margin-bottom: 20px;
}

.stat-card {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 12px;
  padding: 16px;
  text-align: center;
  transition: border-color 0.3s;
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
  color: #e0e0f0;
  line-height: 1;
}

.stat-label {
  font-size: 11px;
  color: #555;
  margin-top: 6px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.stat-card.stat-excellent { border-color: rgba(0,230,118,0.25); }
.stat-card.stat-excellent .stat-value { color: #00e676; }
.stat-card.stat-good { border-color: rgba(255,202,40,0.2); }
.stat-card.stat-good .stat-value { color: #ffca28; }
.stat-card.stat-fair { border-color: rgba(255,112,67,0.2); }
.stat-card.stat-fair .stat-value { color: #ff7043; }
.stat-card.stat-poor { border-color: rgba(239,83,80,0.25); }
.stat-card.stat-poor .stat-value { color: #ef5350; }

/* Charts */
.charts-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.chart-card {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 12px;
  padding: 20px;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: #c0c0d0;
}

.chart-range {
  font-size: 11px;
  color: #444;
}

.chart-area {
  width: 100%;
  height: 220px;
}

/* Function Profiler Section */
.func-profiler-section {
  margin-top: 16px;
}

.section-header {
  display: flex;
  align-items: baseline;
  gap: 12px;
  margin-bottom: 12px;
}

.section-title {
  font-size: 16px;
  font-weight: 700;
  color: #d0d0e0;
}

.section-sub {
  font-size: 12px;
  color: #555;
}

.func-chart-card {
  margin-bottom: 12px;
}

.func-chart-area {
  width: 100%;
  height: 260px;
}

.chart-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 16px;
  justify-content: flex-end;
  max-width: 60%;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  font-weight: 500;
}

.legend-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

/* Function Stats Panel */
.func-stats-panel {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 12px;
  padding: 16px;
  margin-bottom: 12px;
}

.func-stats-header {
  display: flex;
  align-items: baseline;
  gap: 12px;
  margin-bottom: 14px;
}

.func-stats-title {
  font-size: 13px;
  font-weight: 600;
  color: #c0c0d0;
}

.func-stats-sub {
  font-size: 11px;
  color: #444;
}

.func-stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
  gap: 10px;
}

.func-stat-card {
  background: #0d0d1a;
  border: 1px solid #1e1e3f;
  border-radius: 8px;
  padding: 12px;
  border-top: 2px solid;
  transition: border-color 0.2s;
}

.func-stat-card:hover {
  background: #0f0f22;
}

.func-stat-name {
  font-size: 13px;
  font-weight: 600;
  margin-bottom: 8px;
}

.func-stat-values {
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.func-stat-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.func-stat-label {
  font-size: 10px;
  color: #555;
  font-weight: 500;
}

.func-stat-value {
  font-size: 12px;
  color: #b0b0c0;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.func-stat-max { color: #ff8a65; }
.func-stat-p95 { color: #ffca28; }

.func-mini-bar-wrap {
  margin-top: 8px;
  height: 4px;
  background: #1e1e3a;
  border-radius: 2px;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.func-mini-bar-avg {
  height: 2px;
  border-radius: 1px;
  transition: width 0.5s ease;
}

.func-mini-bar-max {
  height: 2px;
  border-radius: 1px;
  opacity: 0.8;
  transition: width 0.5s ease;
}

/* Frame Total Row */
.frame-total-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 10px;
}

.frame-total-card {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 10px;
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.frame-total-label {
  font-size: 11px;
  color: #555;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.frame-total-value {
  font-size: 18px;
  font-weight: 700;
  color: #d0d0e0;
  font-variant-numeric: tabular-nums;
}

.frame-total-max { color: #ff8a65; }
.frame-total-hot { color: #e94560; }

/* Alert Panel */
.alert-panel {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 12px;
  padding: 16px;
  margin-top: 16px;
}

.alert-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.alert-title {
  font-size: 14px;
  font-weight: 600;
  color: #c0c0d0;
}

.alert-header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.alert-count-badge {
  font-size: 11px;
  padding: 3px 10px;
  border-radius: 12px;
  background: rgba(255, 202, 40, 0.12);
  color: #ffca28;
  border: 1px solid rgba(255, 202, 40, 0.25);
  font-weight: 500;
}

.alert-count-badge.has-critical {
  background: rgba(239, 83, 80, 0.12);
  color: #ef5350;
  border-color: rgba(239, 83, 80, 0.3);
  animation: pulse-critical 2s infinite;
}

@keyframes pulse-critical {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.7; }
}

.alert-ack-all-btn {
  font-size: 11px;
  padding: 4px 12px;
  border-radius: 12px;
  border: 1px solid #2a2a5a;
  background: #1a1a35;
  color: #888;
  cursor: pointer;
  transition: all 0.2s;
}

.alert-ack-all-btn:hover {
  border-color: #4a4a7a;
  color: #bbb;
  background: #222245;
}

.alert-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-height: 300px;
  overflow-y: auto;
}

.alert-list::-webkit-scrollbar {
  width: 4px;
}

.alert-list::-webkit-scrollbar-thumb {
  background: #2a2a5a;
  border-radius: 2px;
}

.alert-item {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  padding: 10px 12px;
  border-radius: 8px;
  background: #0d0d1a;
  border-left: 3px solid transparent;
  transition: all 0.2s;
}

.alert-item.alert-critical {
  border-left-color: #ef5350;
  background: rgba(239, 83, 80, 0.05);
}

.alert-item.alert-warning {
  border-left-color: #ffca28;
  background: rgba(255, 202, 40, 0.04);
}

.alert-item.alert-info {
  border-left-color: #4fc3f7;
  background: rgba(79, 195, 247, 0.03);
}

.alert-item-left {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  flex: 1;
  min-width: 0;
}

.alert-icon {
  font-size: 14px;
  margin-top: 2px;
  flex-shrink: 0;
}

.alert-item-content {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.alert-message {
  font-size: 13px;
  color: #d0d0e0;
  font-weight: 500;
}

.alert-detail {
  font-size: 11px;
  color: #666;
  line-height: 1.3;
}

.alert-meta {
  font-size: 10px;
  color: #444;
  margin-top: 2px;
}

.alert-item-right {
  flex-shrink: 0;
  margin-left: 8px;
}

.alert-ack-btn {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  border: 1px solid #2a2a5a;
  background: #1a1a35;
  color: #666;
  cursor: pointer;
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}

.alert-ack-btn:hover {
  background: #222245;
  color: #00e676;
  border-color: rgba(0, 230, 118, 0.3);
}

.alert-acked {
  color: #00e676;
  font-size: 14px;
  opacity: 0.6;
}

/* Mode badge */
.mode-badge {
  font-size: 11px;
  font-weight: 600;
  padding: 4px 10px;
  border-radius: 12px;
  border: 1px solid;
  letter-spacing: 0.3px;
}
.mode-badge.mode-simulation { background: rgba(100,100,180,0.12); color: #8888cc; border-color: rgba(100,100,200,0.25); }
.mode-badge.mode-named_pipe { background: rgba(79,195,247,0.12); color: #4fc3f7; border-color: rgba(79,195,247,0.3); }
.mode-badge.mode-process_monitor { background: rgba(105,240,174,0.12); color: #69f0ae; border-color: rgba(105,240,174,0.3); }
.mode-badge.mode-none { background: rgba(80,80,80,0.12); color: #555; border-color: rgba(80,80,80,0.2); }

/* Attach button */
.attach-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 16px;
  border-radius: 20px;
  border: 1px solid rgba(105,240,174,0.35);
  background: rgba(105,240,174,0.08);
  color: #69f0ae;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}
.attach-btn:hover { background: rgba(105,240,174,0.15); border-color: rgba(105,240,174,0.5); }

/* Modal Overlay */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.7);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 999;
  backdrop-filter: blur(4px);
}

.modal-dialog {
  background: #13132b;
  border: 1px solid #2a2a5a;
  border-radius: 16px;
  width: 680px;
  max-width: 95vw;
  max-height: 85vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 0 24px 80px rgba(0,0,0,0.6);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px 0;
}

.modal-title {
  font-size: 18px;
  font-weight: 700;
  color: #e0e0f0;
}

.modal-close {
  width: 28px;
  height: 28px;
  border-radius: 8px;
  border: 1px solid #2a2a5a;
  background: #1a1a35;
  color: #666;
  cursor: pointer;
  font-size: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}
.modal-close:hover { background: #222245; color: #ddd; }

.pipe-info-box {
  margin: 12px 24px 0;
  padding: 8px 12px;
  background: rgba(79,195,247,0.05);
  border: 1px solid rgba(79,195,247,0.2);
  border-radius: 8px;
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.pipe-info-label { color: #555; }
.pipe-info-path { color: #4fc3f7; font-size: 11px; background: rgba(79,195,247,0.08); padding: 2px 6px; border-radius: 4px; }
.pipe-info-clients { color: #00e676; margin-left: auto; }

/* Modal Tabs */
.modal-tabs {
  display: flex;
  gap: 0;
  padding: 16px 24px 0;
  border-bottom: 1px solid #1e1e3f;
}

.tab-btn {
  padding: 8px 20px;
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  color: #555;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  margin-bottom: -1px;
}
.tab-btn:hover { color: #aaa; }
.tab-btn.active { color: #69f0ae; border-bottom-color: #69f0ae; }

.modal-tab-content {
  padding: 16px 24px;
  overflow-y: auto;
  flex: 1;
}

/* Process List */
.process-search-row {
  display: flex;
  gap: 8px;
  margin-bottom: 12px;
}

.process-search {
  flex: 1;
  background: #0d0d1a;
  border: 1px solid #2a2a5a;
  border-radius: 8px;
  padding: 8px 12px;
  color: #e0e0e0;
  font-size: 13px;
  outline: none;
}
.process-search:focus { border-color: rgba(105,240,174,0.4); }
.process-search::placeholder { color: #444; }

.refresh-btn {
  padding: 8px 16px;
  background: #1a1a35;
  border: 1px solid #2a2a5a;
  border-radius: 8px;
  color: #888;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
  white-space: nowrap;
}
.refresh-btn:hover { background: #222245; color: #aaa; }
.refresh-btn:disabled { opacity: 0.5; cursor: default; }

.process-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 320px;
  overflow-y: auto;
}
.process-list::-webkit-scrollbar { width: 4px; }
.process-list::-webkit-scrollbar-thumb { background: #2a2a5a; border-radius: 2px; }

.process-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 12px;
  border-radius: 8px;
  border: 1px solid transparent;
  cursor: pointer;
  transition: all 0.15s;
}
.process-item:hover { background: rgba(105,240,174,0.05); border-color: rgba(105,240,174,0.15); }
.process-item.selected { background: rgba(105,240,174,0.1); border-color: rgba(105,240,174,0.4); }

.process-item-left { display: flex; align-items: center; gap: 10px; min-width: 0; }
.process-icon { font-size: 18px; flex-shrink: 0; }
.process-info { display: flex; flex-direction: column; gap: 1px; min-width: 0; }
.process-name { font-size: 13px; font-weight: 600; color: #d0d0e0; }
.process-title { font-size: 11px; color: #555; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

.process-item-right { display: flex; flex-direction: column; align-items: flex-end; gap: 1px; flex-shrink: 0; }
.process-mem { font-size: 12px; color: #4fc3f7; font-weight: 500; }
.process-pid { font-size: 10px; color: #444; }

.process-empty {
  text-align: center;
  color: #444;
  font-size: 13px;
  padding: 32px;
}

/* Pipe tab */
.pipe-desc { color: #888; font-size: 13px; line-height: 1.6; margin-bottom: 16px; }
.pipe-desc code { background: rgba(79,195,247,0.08); color: #4fc3f7; padding: 1px 5px; border-radius: 4px; font-size: 12px; }

.pipe-steps { display: flex; flex-direction: column; gap: 10px; margin-bottom: 16px; }
.pipe-step { display: flex; align-items: flex-start; gap: 10px; font-size: 13px; color: #888; line-height: 1.4; }
.pipe-step-num {
  width: 22px; height: 22px; border-radius: 50%; background: rgba(105,240,174,0.15);
  color: #69f0ae; font-size: 12px; font-weight: 700; display: flex; align-items: center; justify-content: center; flex-shrink: 0;
}
.pipe-step code { background: rgba(79,195,247,0.08); color: #4fc3f7; padding: 1px 5px; border-radius: 4px; font-size: 12px; }

.pipe-status { padding: 10px 12px; background: rgba(79,195,247,0.05); border: 1px solid rgba(79,195,247,0.2); border-radius: 8px; margin-bottom: 12px; }
.pipe-active { color: #4fc3f7; font-size: 13px; font-weight: 600; }
.pipe-error-msg { color: #ef5350; font-size: 13px; padding: 8px 12px; background: rgba(239,83,80,0.06); border: 1px solid rgba(239,83,80,0.2); border-radius: 8px; margin-bottom: 12px; }

/* Custom path tab */
.custom-desc { color: #888; font-size: 13px; line-height: 1.6; margin-bottom: 12px; }
.custom-path-input {
  width: 100%; background: #0d0d1a; border: 1px solid #2a2a5a; border-radius: 8px;
  padding: 10px 12px; color: #e0e0e0; font-size: 13px; outline: none; margin-bottom: 12px;
}
.custom-path-input:focus { border-color: rgba(105,240,174,0.4); }
.custom-path-input::placeholder { color: #444; }
.custom-note { font-size: 12px; color: #555; line-height: 1.5; }

/* Modal footer */
.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  padding-top: 12px;
  border-top: 1px solid #1e1e3f;
  margin-top: 4px;
}

.btn-cancel {
  padding: 8px 20px; border-radius: 8px; border: 1px solid #2a2a5a;
  background: #1a1a35; color: #888; font-size: 13px; cursor: pointer; transition: all 0.2s;
}
.btn-cancel:hover { background: #222245; color: #bbb; }

.btn-attach {
  padding: 8px 20px; border-radius: 8px; border: 1px solid rgba(105,240,174,0.4);
  background: rgba(105,240,174,0.12); color: #69f0ae; font-size: 13px; font-weight: 600;
  cursor: pointer; transition: all 0.2s;
}
.btn-attach:hover:not(:disabled) { background: rgba(105,240,174,0.2); }
.btn-attach:disabled { opacity: 0.4; cursor: default; }

.btn-detach {
  padding: 8px 20px; border-radius: 8px; border: 1px solid rgba(239,83,80,0.4);
  background: rgba(239,83,80,0.08); color: #ef5350; font-size: 13px; font-weight: 600;
  cursor: pointer; transition: all 0.2s;
}
.btn-detach:hover { background: rgba(239,83,80,0.15); }

/* Attach status bar */
.attach-status-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 24px;
  background: rgba(105,240,174,0.06);
  border-top: 1px solid rgba(105,240,174,0.15);
  font-size: 12px;
}
.attach-status-icon { font-size: 14px; }
.attach-status-name { color: #69f0ae; font-weight: 600; }
.attach-status-pid { color: #555; margin-left: 4px; }
.btn-detach-sm {
  margin-left: auto; padding: 4px 12px; border-radius: 6px; border: 1px solid rgba(239,83,80,0.3);
  background: rgba(239,83,80,0.06); color: #ef5350; font-size: 11px; cursor: pointer; transition: all 0.2s;
}
.btn-detach-sm:hover { background: rgba(239,83,80,0.12); }

/* Disconnected State */
.disconnected-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 70vh;
  gap: 16px;
}

.disconnected-icon { font-size: 64px; opacity: 0.3; }

.disconnected-title {
  font-size: 22px;
  font-weight: 600;
  color: #555;
}

.disconnected-sub {
  font-size: 14px;
  color: #444;
  text-align: center;
  max-width: 360px;
}

.sim-prompt { margin-top: 20px; }

.sim-btn.large {
  padding: 12px 32px;
  font-size: 15px;
  border-radius: 24px;
}

/* Attach Screen (new simplified UI) */
.attach-screen {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 80vh;
}

.attach-content {
  text-align: center;
  padding: 60px;
}

.attach-icon {
  font-size: 80px;
  margin-bottom: 24px;
}

.attach-title {
  font-size: 28px;
  font-weight: 600;
  color: #333;
  margin-bottom: 12px;
}

.attach-sub {
  font-size: 16px;
  color: #666;
  margin-bottom: 40px;
}

.attach-button-large {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  border: none;
  padding: 20px 48px;
  font-size: 20px;
  font-weight: 600;
  border-radius: 16px;
  cursor: pointer;
  transition: all 0.2s;
  box-shadow: 0 4px 20px rgba(102, 126, 234, 0.4);
}

.attach-button-large:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 28px rgba(102, 126, 234, 0.5);
}

/* Header minimal */
.header-minimal {
  background: #f8f9fa;
  border-bottom: 1px solid #eee;
}

/* Attach status badge */
.attach-status-badge {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  padding: 8px 16px;
  border-radius: 20px;
  font-size: 14px;
  font-weight: 500;
}

.detach-btn {
  background: #ef5350;
  color: white;
  border: none;
  padding: 8px 20px;
  border-radius: 8px;
  cursor: pointer;
  font-size: 14px;
  font-weight: 500;
}

.detach-btn:hover {
  background: #e53935;
}

/* Simplified process list */
.process-list-simple {
  max-height: 300px;
  overflow-y: auto;
  border: 1px solid #eee;
  border-radius: 8px;
}

.process-item-simple {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 14px 16px;
  cursor: pointer;
  border-bottom: 1px solid #f5f5f5;
  transition: background 0.15s;
}

.process-item-simple:hover {
  background: #f8f9fa;
}

.process-item-simple.selected {
  background: linear-gradient(135deg, rgba(102, 126, 234, 0.1) 0%, rgba(118, 75, 162, 0.1) 100%);
  border-left: 4px solid #667eea;
}

.process-item-simple .process-name {
  font-weight: 500;
  color: #333;
}

.process-item-simple .process-pid {
  color: #999;
  font-size: 13px;
}

/* Larger attach button */
.btn-attach-large {
  padding: 14px 36px;
  font-size: 16px;
  border-radius: 10px;
}

/* Responsive */
@media (max-width: 900px) {
  .stats-row { grid-template-columns: repeat(3, 1fr); }
  .charts-row { grid-template-columns: 1fr; }
  .fps-number { font-size: 64px; }
  .func-stats-grid { grid-template-columns: repeat(2, 1fr); }
  .frame-total-row { grid-template-columns: repeat(2, 1fr); }
  .chart-legend { max-width: 100%; }
}

@media (max-width: 600px) {
  .stats-row { grid-template-columns: repeat(2, 1fr); }
  .header { padding: 0 16px; }
  .dashboard { padding: 16px; }
  .func-stats-grid { grid-template-columns: 1fr 1fr; }
  .frame-total-row { grid-template-columns: repeat(2, 1fr); }
}

/* Settings Button */
.settings-btn {
  width: 32px; height: 32px; border-radius: 8px;
  border: 1px solid #2a2a5a; background: #1a1a35;
  color: #888; font-size: 16px; cursor: pointer;
  display: flex; align-items: center; justify-content: center;
  transition: all 0.2s;
}
.settings-btn:hover { background: #222245; color: #ddd; border-color: #3a3a7a; }

/* Toast Notifications */
.toast-container {
  position: fixed; top: 72px; right: 28px; z-index: 1000;
  display: flex; flex-direction: column; gap: 8px; pointer-events: none;
}

.toast {
  display: flex; align-items: center; gap: 10px;
  padding: 12px 18px; border-radius: 10px;
  font-size: 13px; font-weight: 500;
  backdrop-filter: blur(8px);
  animation: slideInRight 0.3s ease;
  pointer-events: auto;
}

.toast.toast-success { background: rgba(0,230,118,0.15); border: 1px solid rgba(0,230,118,0.3); color: #00e676; }
.toast.toast-error { background: rgba(239,83,80,0.15); border: 1px solid rgba(239,83,80,0.3); color: #ef5350; }
.toast.toast-warning { background: rgba(255,202,40,0.15); border: 1px solid rgba(255,202,40,0.3); color: #ffca28; }
.toast.toast-info { background: rgba(79,195,247,0.15); border: 1px solid rgba(79,195,247,0.3); color: #4fc3f7; }

.toast-icon { font-size: 14px; font-weight: 700; }
.toast-msg { line-height: 1.3; }

.toast-fade-enter-active { animation: slideInRight 0.3s ease; }
.toast-fade-leave-active { animation: slideOutRight 0.2s ease; }

@keyframes slideInRight {
  from { opacity: 0; transform: translateX(30px); }
  to { opacity: 1; transform: translateX(0); }
}
@keyframes slideOutRight {
  from { opacity: 1; transform: translateX(0); }
  to { opacity: 0; transform: translateX(30px); }
}

/* Settings Modal */
.settings-dialog { width: 520px; }

.settings-body {
  padding: 16px 24px;
  max-height: 60vh;
  overflow-y: auto;
}

.settings-section {
  margin-bottom: 20px;
}

.settings-section-title {
  font-size: 13px;
  font-weight: 600;
  color: #c0c0d0;
  margin-bottom: 12px;
  padding-bottom: 6px;
  border-bottom: 1px solid #1e1e3f;
}

.settings-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.settings-label {
  font-size: 13px;
  color: #888;
}

.settings-select {
  background: #0d0d1a;
  border: 1px solid #2a2a5a;
  border-radius: 6px;
  padding: 6px 12px;
  color: #e0e0e0;
  font-size: 13px;
  outline: none;
  cursor: pointer;
  min-width: 120px;
}
.settings-select:focus { border-color: rgba(105,240,174,0.4); }

.settings-slider-row {
  display: flex; align-items: center; gap: 12px;
}

.settings-slider {
  -webkit-appearance: none;
  appearance: none;
  width: 140px;
  height: 4px;
  background: #2a2a5a;
  border-radius: 2px;
  outline: none;
  cursor: pointer;
}
.settings-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 14px; height: 14px;
  border-radius: 50%;
  background: #69f0ae;
  cursor: pointer;
  transition: transform 0.15s;
}
.settings-slider::-webkit-slider-thumb:hover { transform: scale(1.15); }

.settings-slider-val {
  font-size: 12px;
  color: #69f0ae;
  font-weight: 600;
  min-width: 70px;
  text-align: right;
}

/* Toggle Switch */
.settings-toggle {
  position: relative;
  display: inline-block;
  width: 44px;
  height: 24px;
  cursor: pointer;
}

.settings-toggle input {
  opacity: 0; width: 0; height: 0;
}

.settings-toggle-slider {
  position: absolute;
  inset: 0;
  background: #2a2a5a;
  border-radius: 12px;
  transition: all 0.2s;
}

.settings-toggle-slider::before {
  content: '';
  position: absolute;
  width: 18px; height: 18px;
  left: 3px; top: 3px;
  background: #555;
  border-radius: 50%;
  transition: all 0.2s;
}

.settings-toggle input:checked + .settings-toggle-slider {
  background: rgba(105,240,174,0.25);
}

.settings-toggle input:checked + .settings-toggle-slider::before {
  transform: translateX(20px);
  background: #69f0ae;
}

/* Shortcuts Grid */
.shortcuts-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}

.shortcut-item {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 12px;
  color: #888;
}

.shortcut-item kbd {
  display: inline-block;
  padding: 3px 8px;
  background: #1a1a35;
  border: 1px solid #2a2a5a;
  border-radius: 4px;
  font-family: inherit;
  font-size: 11px;
  color: #c0c0d0;
  min-width: 28px;
  text-align: center;
}

.settings-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  padding: 12px 24px;
  border-top: 1px solid #1e1e3f;
}

/* Gauge + Histogram Row */
.gauge-hist-row {
  display: grid;
  grid-template-columns: 1fr 2fr;
  gap: 16px;
  margin-bottom: 16px;
}

.gauge-card, .hist-card {
  background: #13132b;
  border: 1px solid #1e1e3f;
  border-radius: 12px;
  padding: 16px;
}

.gauge-area {
  width: 100%;
  height: 180px;
}

.hist-area {
  width: 100%;
  height: 180px;
}

.chart-range.excellent { color: #00e676; }
.chart-range.good { color: #69f0ae; }
.chart-range.fair { color: #ffca28; }
.chart-range.poor { color: #ff7043; }
.chart-range.critical { color: #ef5350; }

/* Responsive for gauge/histogram */
@media (max-width: 900px) {
  .gauge-hist-row { grid-template-columns: 1fr; }
}
</style>
