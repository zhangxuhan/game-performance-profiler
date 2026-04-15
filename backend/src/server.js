/**
 * Game Performance Profiler - Backend Server
 * WebSocket server for receiving profiling data
 * Can be used standalone or embedded in Electron
 */

const WebSocket = require('ws');
const express = require('express');
const cors = require('cors');
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 8080;
const WS_PORT = process.env.WS_PORT || 8081;
const IS_ELECTRON = process.env.ELECTRON === 'true';
const IS_PRODUCTION = process.env.NODE_ENV === 'production' || (!process.env.NODE_ENV);

// For production, serve static frontend files
const FRONTEND_DIST = path.join(__dirname, '..', 'frontend-dist');

// Store server instances
let server = null;
let wsServer = null;
let wss = null;

// Store connected clients
const clients = new Set();

// Profiling data storage
let latestFrameData = null;
let frameHistory = [];
let memorySnapshots = [];

// Alert system state
let alerts = [];
let alertIdCounter = 0;
let alertStats = { totalAlerts: 0, infoCount: 0, warningCount: 0, criticalCount: 0, unacknowledgedCount: 0 };

// Alert configuration thresholds
const alertConfig = {
    fpsDropThreshold: 10,
    fpsCriticalThreshold: 30,
    fpsWarningThreshold: 45,
    frameTimeSpikeMultiplier: 2.0,
    frameTimeCriticalMs: 33.33,
    frameTimeWarningMs: 22.22,
    memoryGrowthRateThreshold: 1.0 * 1024 * 1024,
    memoryCriticalMB: 512,
    memoryWarningMB: 256,
    stabilityWarningThreshold: 70,
    stabilityCriticalThreshold: 50,
    deduplicationWindowMs: 5000,
    maxAlerts: 100
};

// Track last alert times for deduplication
const lastAlertTimes = {};

// Frame history for alert analysis
const alertFpsHistory = [];
const alertFrameTimeHistory = [];
const alertMemoryHistory = [];
const ALERT_HISTORY_WINDOW = 120;

// Simulation state
let simulationRunning = true;
let simulationTimer = null;
let frameCount = 0;
let baseFps = 60;
let baseMemory = 50 * 1024 * 1024;

// ==========================================
// Function Profiler - Realistic profile simulation
// ==========================================

const PROFILE_FUNCTIONS = ['Update', 'Render', 'Physics', 'Audio', 'AI', 'Networking', 'GC'];

// Track cumulative timing per function for realistic variation
const functionTimers = {};
PROFILE_FUNCTIONS.forEach(fn => {
    functionTimers[fn] = {
        base: fn === 'Update' ? 4000 : fn === 'Render' ? 3000 : fn === 'Physics' ? 1500 : 200 + Math.random() * 500,
        variance: fn === 'Update' ? 1500 : fn === 'Render' ? 1000 : fn === 'Physics' ? 500 : 100,
        spikeProbability: fn === 'GC' ? 0.02 : fn === 'Render' ? 0.01 : 0.005,
        spikeMultiplier: fn === 'GC' ? 15 : fn === 'Physics' ? 8 : 5
    };
});

// Generate realistic function profiling data
function generateFunctionProfiles() {
    const profiles = [];
    let totalTime = 0;

    PROFILE_FUNCTIONS.forEach(fn => {
        const timer = functionTimers[fn];
        let duration = timer.base + (Math.random() - 0.5) * 2 * timer.variance;

        // Occasional spike
        if (Math.random() < timer.spikeProbability) {
            duration *= timer.spikeMultiplier;
        }

        duration = Math.max(0, duration);
        profiles.push({ name: fn, duration: parseFloat(duration.toFixed(2)) });
        totalTime += duration;
    });

    return { profiles, totalTime: parseFloat(totalTime.toFixed(2)) };
}

// Track function profile history for percentile calculations
const profileHistory = [];
const PROFILE_HISTORY_MAX = 200;

/**
 * Compute function-level statistics from recent history
 * Returns: { functionName: { avg, min, max, p95 } }
 */
function computeFunctionStats() {
    const stats = {};
    if (profileHistory.length < 2) return stats;

    PROFILE_FUNCTIONS.forEach(fn => {
        const values = profileHistory.map(h => {
            const entry = h.find(p => p.name === fn);
            return entry ? entry.duration : 0;
        });

        if (values.length === 0) return;

        const sorted = [...values].sort((a, b) => a - b);
        const sum = values.reduce((a, b) => a + b, 0);
        const avg = sum / values.length;
        const p95Index = Math.min(Math.floor(values.length * 0.95), values.length - 1);

        stats[fn] = {
            avg: parseFloat(avg.toFixed(2)),
            min: parseFloat(Math.min(...values).toFixed(2)),
            max: parseFloat(Math.max(...values).toFixed(2)),
            p95: parseFloat(sorted[p95Index].toFixed(2))
        };
    });

    return stats;
}

// Handle profiler data
function handleProfilerData(data) {
    const timestamp = Date.now();

    if (data.type === 'frame' || data.type === 'frame_update') {
        latestFrameData = {
            ...data.data,
            receivedAt: timestamp
        };

        frameHistory.push(latestFrameData);

        // Keep only last 1000 frames
        if (frameHistory.length > 1000) {
            frameHistory = frameHistory.slice(-1000);
        }

        // Broadcast to all clients
        broadcast({
            type: 'frame_update',
            data: latestFrameData
        });

        // Run alert analysis on frame data
        analyzeFrameForAlerts(latestFrameData);
    }
    else if (data.type === 'memory') {
        memorySnapshots.push({
            ...data.data,
            receivedAt: timestamp
        });

        if (memorySnapshots.length > 1000) {
            memorySnapshots = memorySnapshots.slice(-1000);
        }

        broadcast({
            type: 'memory_update',
            data: data.data
        });
    }
}

// Broadcast message to all clients
function broadcast(message) {
    const msgString = JSON.stringify(message);
    clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(msgString);
        }
    });
}

// REST API endpoints
function setupRoutes(app) {
    // Get latest frame data
    app.get('/api/frames/latest', (req, res) => {
        res.json(latestFrameData || { error: 'No data available' });
    });

    // Get frame history
    app.get('/api/frames/history', (req, res) => {
        const limit = parseInt(req.query.limit) || 100;
        res.json(frameHistory.slice(-limit));
    });

    // Get memory snapshots
    app.get('/api/memory', (req, res) => {
        const limit = parseInt(req.query.limit) || 100;
        res.json(memorySnapshots.slice(-limit));
    });

    // Get statistics
    app.get('/api/stats', (req, res) => {
        if (frameHistory.length === 0) {
            res.json({ error: 'No data available' });
            return;
        }

        const fpsValues = frameHistory.map(f => f.fps).filter(f => f !== undefined);
        const memoryValues = frameHistory.map(f => f.memory?.currentUsage || 0);

        const avgFps = fpsValues.length > 0
            ? fpsValues.reduce((a, b) => a + b, 0) / fpsValues.length
            : 0;

        const minFps = fpsValues.length > 0 ? Math.min(...fpsValues) : 0;
        const maxFps = fpsValues.length > 0 ? Math.max(...fpsValues) : 0;

        const avgMemory = memoryValues.length > 0
            ? memoryValues.reduce((a, b) => a + b, 0) / memoryValues.length
            : 0;

        res.json({
            frameCount: frameHistory.length,
            avgFps: Math.round(avgFps * 100) / 100,
            minFps: Math.round(minFps * 100) / 100,
            maxFps: Math.round(maxFps * 100) / 100,
            avgMemory: Math.round(avgMemory / 1024 / 1024), // MB
            connectedClients: clients.size
        });
    });

    // Get function profiler stats
    app.get('/api/profiler/functions', (req, res) => {
        const stats = computeFunctionStats();
        const historyLimit = parseInt(req.query.history) || 60;
        res.json({
            stats,
            functions: PROFILE_FUNCTIONS,
            history: profileHistory.slice(-historyLimit)
        });
    });

    // Health check
    app.get('/api/health', (req, res) => {
        res.json({
            status: 'ok',
            uptime: process.uptime(),
            clients: clients.size,
            simulation: simulationRunning
        });
    });

    // Simulation control
    app.post('/api/simulation/toggle', (req, res) => {
        const running = toggleSimulation();
        res.json({ running });
    });
    app.post('/api/simulation/start', (req, res) => {
        startSimulation();
        res.json({ running: true });
    });
    app.post('/api/simulation/stop', (req, res) => {
        stopSimulation();
        res.json({ running: false });
    });

    // Alert API endpoints
    app.get('/api/alerts', (req, res) => {
        const severity = req.query.severity;
        const type = req.query.type;
        let result = [...alerts];
        if (severity) result = result.filter(a => a.severity === severity);
        if (type) result = result.filter(a => a.type === type);
        res.json({ alerts: result, total: result.length });
    });

    app.get('/api/alerts/active', (req, res) => {
        const active = getActiveAlerts();
        res.json({ alerts: active, count: active.length });
    });

    app.get('/api/alerts/stats', (req, res) => {
        res.json({
            ...alertStats,
            hasCritical: alerts.some(a => a.severity === 'critical' && !a.acknowledged),
            hasUnacknowledged: alerts.some(a => !a.acknowledged),
            activeCount: getActiveAlerts().length
        });
    });

    app.post('/api/alerts/:id/acknowledge', (req, res) => {
        const id = parseInt(req.params.id);
        const alert = alerts.find(a => a.id === id);
        if (!alert) {
            res.status(404).json({ error: 'Alert not found' });
            return;
        }
        alert.acknowledged = true;
        alert.acknowledgedAt = Date.now();
        alertStats.unacknowledgedCount = alerts.filter(a => !a.acknowledged).length;

        broadcast({ type: 'alert_acknowledged', data: alert });
        res.json({ success: true, alert });
    });

    app.post('/api/alerts/acknowledge-all', (req, res) => {
        let count = 0;
        alerts.forEach(a => {
            if (!a.acknowledged) {
                a.acknowledged = true;
                a.acknowledgedAt = Date.now();
                count++;
            }
        });
        alertStats.unacknowledgedCount = 0;

        broadcast({ type: 'alerts_all_acknowledged', data: { count } });
        res.json({ success: true, acknowledgedCount: count });
    });

    app.delete('/api/alerts/:id', (req, res) => {
        const id = parseInt(req.params.id);
        const idx = alerts.findIndex(a => a.id === id);
        if (idx === -1) {
            res.status(404).json({ error: 'Alert not found' });
            return;
        }
        const removed = alerts.splice(idx, 1)[0];
        alertStats.totalAlerts = alerts.length;
        alertStats.unacknowledgedCount = alerts.filter(a => !a.acknowledged).length;
        res.json({ success: true, removed });
    });
}

// Start simulation
function startSimulation() {
    if (simulationTimer) return;
    console.log('[Sim] Simulation started');
    simulationRunning = true;
    simulationTimer = setInterval(() => {
        frameCount++;
        const fpsNoise = (Math.random() - 0.5) * 10;
        const memoryNoise = (Math.random() - 0.5) * 1024 * 1024;

        // Generate realistic function profiling data
        const { profiles, totalTime } = generateFunctionProfiles();

        const frameData = {
            type: 'frame',
            data: {
                frame: frameCount,
                fps: Math.max(30, Math.min(144, baseFps + fpsNoise)),
                frameTime: 1000 / (baseFps + fpsNoise),
                memory: Math.max(10 * 1024 * 1024, baseMemory + memoryNoise),
                profiles,
                profileTotalTime: totalTime
            }
        };
        handleProfilerData(frameData);

        // Track profile history for statistics
        profileHistory.push(profiles);
        if (profileHistory.length > PROFILE_HISTORY_MAX) {
            profileHistory.shift();
        }

        // Occasionally trigger a spike scenario (for demo)
        if (frameCount % 300 === 0) {
            // Every 30 seconds, simulate a render spike
            functionTimers['Render'].base *= 3;
            setTimeout(() => { functionTimers['Render'].base /= 3; }, 2000);
        }
    }, 100);
}

// Stop simulation
function stopSimulation() {
    if (simulationTimer) {
        clearInterval(simulationTimer);
        simulationTimer = null;
    }
    simulationRunning = false;
    console.log('[Sim] Simulation stopped');
}

// Toggle simulation
function toggleSimulation() {
    if (simulationRunning) {
        stopSimulation();
    } else {
        startSimulation();
    }
    return simulationRunning;
}

// ==========================================
// Alert Analysis System
// ==========================================

function analyzeFrameForAlerts(data) {
    const fps = data.fps || 0;
    const frameTime = data.frameTime || 0;
    const memoryMB = (data.memory || 0) / 1024 / 1024;

    // Update history
    alertFpsHistory.push(fps);
    alertFrameTimeHistory.push(frameTime);
    alertMemoryHistory.push(memoryMB);

    if (alertFpsHistory.length > ALERT_HISTORY_WINDOW) {
        alertFpsHistory.shift();
        alertFrameTimeHistory.shift();
        alertMemoryHistory.shift();
    }

    if (alertFpsHistory.length < 10) return;

    const avgFps = alertFpsHistory.reduce((a, b) => a + b, 0) / alertFpsHistory.length;
    const avgFrameTime = alertFrameTimeHistory.reduce((a, b) => a + b, 0) / alertFrameTimeHistory.length;

    // Check FPS drop
    const fpsDrop = avgFps - fps;
    if (fpsDrop >= alertConfig.fpsDropThreshold) {
        let severity = 'info';
        if (fps < alertConfig.fpsCriticalThreshold) severity = 'critical';
        else if (fps < alertConfig.fpsWarningThreshold) severity = 'warning';

        createAlert('FPS_DROP', severity,
            `FPS dropped by ${fpsDrop.toFixed(1)} to ${fps.toFixed(1)}`,
            `Average FPS: ${avgFps.toFixed(1)}, Current: ${fps.toFixed(1)}`,
            'fps', fps, avgFps);
    }

    // Check frame time spike
    if (avgFrameTime > 0 && frameTime > avgFrameTime * alertConfig.frameTimeSpikeMultiplier) {
        let severity = 'warning';
        if (frameTime > alertConfig.frameTimeCriticalMs) severity = 'critical';

        createAlert('FRAME_TIME_SPIKE', severity,
            `Frame time spike: ${frameTime.toFixed(2)}ms (${(frameTime/avgFrameTime).toFixed(1)}x avg)`,
            `Average frame time: ${avgFrameTime.toFixed(2)}ms`,
            'frameTime', frameTime, avgFrameTime);
    }

    // Check high memory usage
    if (memoryMB > alertConfig.memoryWarningMB) {
        let severity = 'warning';
        if (memoryMB > alertConfig.memoryCriticalMB) severity = 'critical';

        createAlert('HIGH_MEMORY', severity,
            `High memory usage: ${memoryMB.toFixed(1)} MB`,
            `Memory exceeds ${severity === 'critical' ? alertConfig.memoryCriticalMB : alertConfig.memoryWarningMB} MB threshold`,
            'memory', memoryMB, severity === 'critical' ? alertConfig.memoryCriticalMB : alertConfig.memoryWarningMB);
    }

    // Check for memory leak (sustained growth)
    if (alertMemoryHistory.length >= 60) {
        const recentMemory = alertMemoryHistory.slice(-60);
        const memGrowthRate = (recentMemory[recentMemory.length - 1] - recentMemory[0]) / recentMemory.length;

        if (memGrowthRate > alertConfig.memoryGrowthRateThreshold / 1024 / 1024) {
            createAlert('MEMORY_LEAK', memGrowthRate > 1 ? 'critical' : 'warning',
                `Potential memory leak: +${memGrowthRate.toFixed(3)} MB/frame growth`,
                `Memory growing at ${memGrowthRate.toFixed(3)} MB/frame over last 60 frames`,
                'memoryGrowthRate', memGrowthRate, alertConfig.memoryGrowthRateThreshold / 1024 / 1024);
        }
    }

    // Check function profiler alerts (expensive functions)
    if (data.profiles) {
        data.profiles.forEach(p => {
            // Detect abnormally high function time
            const fnStats = computeFunctionStats();
            if (fnStats[p.name]) {
                const { avg, p95 } = fnStats[p.name];
                if (p.duration > p95 * 3 && p95 > 0) {
                    createAlert(
                        'FUNCTION_SPIKE',
                        p.duration > p95 * 10 ? 'critical' : 'warning',
                        `Function "${p.name}" spike: ${p.duration.toFixed(0)}µs`,
                        `Avg: ${avg.toFixed(0)}µs, P95: ${p95.toFixed(0)}µs, Current: ${p.duration.toFixed(0)}µs`,
                        p.name, p.duration, p95 * 3
                    );
                }
            }
        });
    }
}

function createAlert(type, severity, message, details, metric, value, threshold) {
    const now = Date.now();

    // Deduplication: skip if same type+severity was emitted recently
    const dedupKey = `${type}_${severity}`;
    if (lastAlertTimes[dedupKey] && (now - lastAlertTimes[dedupKey]) < alertConfig.deduplicationWindowMs) {
        return;
    }

    lastAlertTimes[dedupKey] = now;

    const alert = {
        id: ++alertIdCounter,
        type,
        severity,
        message,
        details,
        timestamp: now,
        metric,
        value,
        threshold,
        acknowledged: false,
        acknowledgedAt: null
    };

    alerts.push(alert);

    // Update stats
    alertStats.totalAlerts = alerts.length;
    if (severity === 'info') alertStats.infoCount++;
    else if (severity === 'warning') alertStats.warningCount++;
    else if (severity === 'critical') alertStats.criticalCount++;
    alertStats.unacknowledgedCount = alerts.filter(a => !a.acknowledged).length;

    // Trim alerts
    while (alerts.length > alertConfig.maxAlerts) {
        alerts.shift();
    }

    // Broadcast alert to all WebSocket clients
    broadcast({
        type: 'alert',
        data: alert
    });
}

function getActiveAlerts() {
    const cutoff = Date.now() - 30000; // Active within last 30s
    return alerts.filter(a => !a.acknowledged && a.timestamp > cutoff);
}

// Main server startup function
function startServer() {
    // Initialize express app
    const app = express();
    app.use(cors());
    app.use(express.json());

    // Serve static files in production
    if (IS_PRODUCTION) {
        console.log('[Server] Production mode - serving frontend from:', FRONTEND_DIST);
        if (fs.existsSync(FRONTEND_DIST)) {
            app.use(express.static(FRONTEND_DIST));
            app.get('*', (req, res) => {
                res.sendFile(path.join(FRONTEND_DIST, 'index.html'));
            });
        } else {
            console.log('[Server] Warning: frontend-dist not found at', FRONTEND_DIST);
        }
    }

    // Setup routes
    setupRoutes(app);

    // HTTP server for static files and REST API
    server = http.createServer(app);

    // Separate server for WebSocket
    wsServer = http.createServer();

    // WebSocket server
    wss = new WebSocket.Server({ server: wsServer });

    // WebSocket connection handler
    wss.on('connection', (ws) => {
        console.log('[WS] Client connected');
        clients.add(ws);

        // Send welcome message
        ws.send(JSON.stringify({
            type: 'welcome',
            message: 'Connected to Game Performance Profiler'
        }));

        // Send latest data if available
        if (latestFrameData) {
            ws.send(JSON.stringify({
                type: 'frame_update',
                data: latestFrameData
            }));
        }

        ws.on('message', (message) => {
            try {
                const data = JSON.parse(message);
                if (data.type === 'simulation_control') {
                    const wasRunning = simulationRunning;
                    if (data.action === 'start') startSimulation();
                    else if (data.action === 'stop') stopSimulation();
                    else if (data.action === 'toggle') toggleSimulation();
                    // Notify all clients of new simulation state
                    broadcast({ type: 'simulation_status', running: simulationRunning });
                } else if (data.type === 'acknowledge_alert') {
                    const alert = alerts.find(a => a.id === data.alertId);
                    if (alert) {
                        alert.acknowledged = true;
                        alert.acknowledgedAt = Date.now();
                        alertStats.unacknowledgedCount = alerts.filter(a => !a.acknowledged).length;
                        broadcast({ type: 'alert_acknowledged', data: alert });
                    }
                } else if (data.type === 'acknowledge_all_alerts') {
                    alerts.forEach(a => {
                        if (!a.acknowledged) {
                            a.acknowledged = true;
                            a.acknowledgedAt = Date.now();
                        }
                    });
                    alertStats.unacknowledgedCount = 0;
                    broadcast({ type: 'alerts_all_acknowledged', data: {} });
                } else {
                    handleProfilerData(data);
                }
            } catch (e) {
                console.error('[WS] Error parsing message:', e.message);
            }
        });

        ws.on('close', () => {
            console.log('[WS] Client disconnected');
            clients.delete(ws);
        });

        ws.on('error', (error) => {
            console.error('[WS] Error:', error.message);
        });
    });

    // Start servers
    server.listen(PORT, () => {
        console.log(`[HTTP] Server running on http://localhost:${PORT}`);
    });

    wsServer.listen(WS_PORT, () => {
        console.log(`[WS] WebSocket server running on ws://localhost:${WS_PORT}`);
    });

    // Start simulation if enabled
    if (IS_ELECTRON || process.env.SIMULATION === 'true') {
        startSimulation();
    }

    // Graceful shutdown
    process.on('SIGINT', () => {
        console.log('\n[Server] Shutting down...');
        if (wss) wss.close();
        if (server) server.close();
        process.exit(0);
    });
}

// Export for use in Electron
module.exports = { startServer };

// If run directly (not imported), start server immediately
if (require.main === module) {
    startServer();
}
