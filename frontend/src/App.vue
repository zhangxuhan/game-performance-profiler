<template>
  <div id="app">
    <div class="header">
      <h1>🎮 Game Performance Profiler <span id="status" class="status disconnected">Disconnected</span></h1>
    </div>
    
    <div class="container">
      <div class="stats-grid">
        <div class="stat-card">
          <div class="value" id="fps">{{ fps }}</div>
          <div class="label">FPS</div>
        </div>
        <div class="stat-card">
          <div class="value" id="frame-time">{{ frameTime }}</div>
          <div class="label">Frame Time (ms)</div>
        </div>
        <div class="stat-card">
          <div class="value" id="memory">{{ memory }}</div>
          <div class="label">Memory (MB)</div>
        </div>
        <div class="stat-card">
          <div class="value" id="frames">{{ frames }}</div>
          <div class="label">Frames</div>
        </div>
      </div>
      
      <div class="chart-container">
        <div class="chart-title">FPS History</div>
        <div ref="fpsChartRef" style="width: 100%; height: 300px;"></div>
      </div>
      
      <div class="chart-container">
        <div class="chart-title">Memory Usage</div>
        <div ref="memoryChartRef" style="width: 100%; height: 300px;"></div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

const fpsChartRef = ref(null)
const memoryChartRef = ref(null)

const fps = ref(0)
const frameTime = ref('0.00')
const memory = ref(0)
const frames = ref(0)

let fpsChart = null
let memoryChart = null
let ws = null
const fpsData = []
const memoryData = []
const maxDataPoints = 100

onMounted(() => {
  // Initialize charts
  fpsChart = echarts.init(fpsChartRef.value)
  memoryChart = echarts.init(memoryChartRef.value)

  const commonOptions = {
    grid: { left: 50, right: 20, top: 20, bottom: 30 },
    xAxis: { type: 'time', splitLine: { show: false } },
    yAxis: { type: 'value', splitLine: { lineStyle: { color: '#333' } } },
    series: [{ type: 'line', smooth: true, showSymbol: false }]
  }

  fpsChart.setOption({
    ...commonOptions,
    yAxis: { ...commonOptions.yAxis, name: 'FPS', min: 0 },
    series: [{ ...commonOptions.series, itemStyle: { color: '#e94560' } }]
  })

  memoryChart.setOption({
    ...commonOptions,
    yAxis: { ...commonOptions.yAxis, name: 'MB', min: 0 },
    series: [{ ...commonOptions.series, itemStyle: { color: '#4caf50' } }]
  })

  // Handle window resize
  window.addEventListener('resize', handleResize)

  // Connect to WebSocket
  connectWebSocket()
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  if (ws) ws.close()
  if (fpsChart) fpsChart.dispose()
  if (memoryChart) memoryChart.dispose()
})

function handleResize() {
  if (fpsChart) fpsChart.resize()
  if (memoryChart) memoryChart.resize()
}

function connectWebSocket() {
  // In Electron, connect to local backend
  const wsUrl = window.electronAPI?.isElectron 
    ? 'ws://localhost:8081' 
    : 'ws://localhost:8081'
  
  ws = new WebSocket(wsUrl)

  ws.onopen = () => {
    document.getElementById('status').textContent = 'Connected'
    document.getElementById('status').className = 'status connected'
  }

  ws.onclose = () => {
    document.getElementById('status').textContent = 'Disconnected'
    document.getElementById('status').className = 'status disconnected'
    // Reconnect after 2 seconds
    setTimeout(connectWebSocket, 2000)
  }

  ws.onerror = (error) => {
    console.error('WebSocket error:', error)
  }

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data)
      if (data.type === 'frame_update') {
        updateCharts(data.data)
      }
    } catch (e) {
      console.error('Error parsing message:', e)
    }
  }
}

function updateCharts(frameData) {
  fps.value = Math.round(frameData.fps || 0)
  frameTime.value = (frameData.frameTime || 0).toFixed(2)
  memory.value = Math.round((frameData.memory || 0) / 1024 / 1024)
  frames.value = frameData.frame || 0

  const now = new Date().toLocaleTimeString()
  fpsData.push({ name: now, value: [now, frameData.fps || 0] })
  memoryData.push({ name: now, value: [now, (frameData.memory || 0) / 1024 / 1024] })

  if (fpsData.length > maxDataPoints) fpsData.shift()
  if (memoryData.length > maxDataPoints) memoryData.shift()

  if (fpsChart) fpsChart.setOption({
    series: [{ data: fpsData }]
  })
  if (memoryChart) memoryChart.setOption({
    series: [{ data: memoryData }]
  })
}
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
  background: #1a1a2e;
  color: #eee;
  min-height: 100vh;
}
#app { min-height: 100vh; }
.header {
  background: #16213e;
  padding: 20px;
  border-bottom: 1px solid #0f3460;
}
.header h1 { font-size: 24px; color: #e94560; }
.container {
  max-width: 1400px;
  margin: 0 auto;
  padding: 20px;
}
.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 20px;
  margin-bottom: 20px;
}
.stat-card {
  background: #16213e;
  padding: 20px;
  border-radius: 8px;
  text-align: center;
}
.stat-card .value {
  font-size: 36px;
  font-weight: bold;
  color: #e94560;
}
.stat-card .label {
  font-size: 14px;
  color: #888;
  margin-top: 5px;
}
.chart-container {
  background: #16213e;
  padding: 20px;
  border-radius: 8px;
  margin-bottom: 20px;
}
.chart-title {
  font-size: 18px;
  margin-bottom: 15px;
}
.status {
  display: inline-block;
  padding: 5px 10px;
  border-radius: 4px;
  font-size: 12px;
  margin-left: 10px;
}
.status.connected { background: #4caf50; }
.status.disconnected { background: #f44336; }
</style>
