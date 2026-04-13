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

// Simulation state
let simulationRunning = true;
let simulationTimer = null;
let frameCount = 0;
let baseFps = 60;
let baseMemory = 50 * 1024 * 1024;

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
        const frameData = {
            type: 'frame',
            data: {
                frame: frameCount,
                fps: Math.max(30, Math.min(144, baseFps + fpsNoise)),
                frameTime: 1000 / (baseFps + fpsNoise),
                memory: Math.max(10 * 1024 * 1024, baseMemory + memoryNoise),
                profiles: [
                    { name: 'Update', duration: Math.random() * 5000 },
                    { name: 'Render', duration: Math.random() * 3000 },
                    { name: 'Physics', duration: Math.random() * 2000 }
                ]
            }
        };
        handleProfilerData(frameData);
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
