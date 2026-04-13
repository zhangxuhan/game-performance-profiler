<template>
  <div id="app">
    <!-- Header -->
    <div class="header">
      <div class="header-left">
        <span class="logo">📊</span>
        <span class="title">Game Performance Profiler</span>
      </div>
      <div class="header-right">
        <span class="status-badge" :class="connected ? 'connected' : 'disconnected'">
          {{ connected ? '● Connected' : '○ Disconnected' }}
        </span>
        <button class="sim-btn" :class="{ active: simulationOn }" @click="toggleSimulation">
          <span class="dot"></span>
          {{ simulationOn ? 'Simulation ON' : 'Simulation OFF' }}
        </button>
      </div>
    </div>

    <!-- Main Dashboard -->
    <div class="dashboard" v-if="connected">
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
    </div>

    <!-- Disconnected State -->
    <div class="disconnected-state" v-else>
      <div class="disconnected-icon">📡</div>
      <div class="disconnected-title">Waiting for Data...</div>
      <div class="disconnected-sub">Make sure the backend server is running with simulation enabled.</div>
      <div class="sim-prompt">
        <button class="sim-btn large" :class="{ active: simulationOn }" @click="toggleSimulation">
          <span class="dot"></span>
          {{ simulationOn ? 'Simulation Running' : 'Start Simulation' }}
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
const maxDataPoints = 120

let fpsChart = null
let memoryChart = null
let ws = null
let reconnectTimer = null

const chartRangeLabel = computed(() => `Last ${maxDataPoints} frames`)

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

onMounted(() => {
  fpsChart = echarts.init(fpsChartRef.value)
  memoryChart = echarts.init(memoryChartRef.value)
  window.addEventListener('resize', handleResize)
  connectWebSocket()
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  if (reconnectTimer) clearTimeout(reconnectTimer)
  if (ws) ws.close()
  if (fpsChart) fpsChart.dispose()
  if (memoryChart) memoryChart.dispose()
})

function handleResize() {
  if (fpsChart) fpsChart.resize()
  if (memoryChart) memoryChart.resize()
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

  if (fpsHistory.value.length > maxDataPoints) fpsHistory.value.shift()
  if (memoryHistory.value.length > maxDataPoints) memoryHistory.value.shift()

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

/* Responsive */
@media (max-width: 900px) {
  .stats-row { grid-template-columns: repeat(3, 1fr); }
  .charts-row { grid-template-columns: 1fr; }
  .fps-number { font-size: 64px; }
}

@media (max-width: 600px) {
  .stats-row { grid-template-columns: repeat(2, 1fr); }
  .header { padding: 0 16px; }
  .dashboard { padding: 16px; }
}
</style>
