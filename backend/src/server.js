/**
 * Game Performance Profiler - Backend Server
 * WebSocket server for receiving profiling data
 */

const WebSocket = require('ws');
const express = require('express');
const cors = require('cors');
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 8080;
const WS_PORT = process.env.WS_PORT || 8081;
const SIMULATION = process.env.SIMULATION === 'true' || true; // 默认开启模拟数据

// Initialize express app
const app = express();
app.use(cors());
app.use(express.json());

// HTTP server for static files and REST API
const server = http.createServer(app);

// Separate server for WebSocket
const wsServer = http.createServer();

// WebSocket server
const wss = new WebSocket.Server({ server: wsServer });

// Store connected clients
const clients = new Set();

// Profiling data storage
let latestFrameData = null;
let frameHistory = [];
let memorySnapshots = [];

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
            handleProfilerData(data);
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
        clients: clients.size
    });
});

// Start servers
server.listen(PORT, () => {
    console.log(`[HTTP] Server running on http://localhost:${PORT}`);
});

wsServer.listen(WS_PORT, () => {
    console.log(`[WS] WebSocket server running on ws://localhost:${WS_PORT}`);
});

// 模拟数据生成器（自动生成测试数据展示）
if (SIMULATION) {
    let frameCount = 0;
    let baseFps = 60;
    let baseMemory = 50 * 1024 * 1024;
    
    console.log('[Sim] 模拟模式已开启，自动生成测试数据');
    
    setInterval(() => {
        frameCount++;
        
        // 模拟波动
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
    }, 100); // 每100ms发送一帧
}

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\n[Server] Shutting down...');
    wss.close();
    server.close();
    process.exit(0);
});