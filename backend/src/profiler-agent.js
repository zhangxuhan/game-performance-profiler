/**
 * profiler-agent.js
 * Named Pipe Server for receiving real-time profiling data from game processes.
 * 
 * Protocol:
 *   Game clients connect to \\.\pipe\GameProfilerStream and send JSON frames.
 *   Each JSON line must contain at least: { "frame": N, "fps": float, "frameTime": float, "memory": int }
 *   Optional: { "profiles": [{ "name": "Update", "duration": 1234 }, ...] }
 * 
 * Usage:
 *   const agent = require('./profiler-agent');
 *   agent.start((frameData) => { ... });  // callback fires on each parsed frame
 *   agent.stop();
 */

const net = require('net');
const EventEmitter = require('events');

const PIPE_NAME = 'GameProfilerStream';

// Maximum frame buffer size (prevent OOM from malicious clients)
const MAX_FRAME_SIZE = 1024 * 1024; // 1MB

class ProfilerAgent extends EventEmitter {
    constructor() {
        super();
        this.server = null;
        this.clients = new Set();
        this.running = false;
        this.frameCount = 0;
        this.lastError = null;
    }

    /**
     * Start listening on the named pipe.
     * @param {Function} onFrame - callback(frameData: object) called for each parsed frame
     */
    start(onFrame) {
        if (this.running) {
            console.log('[Agent] Already running');
            return;
        }

        this.running = true;
        this._onFrame = onFrame;

        this.server = net.createServer((socket) => {
            const clientAddr = socket.remoteAddress || 'unknown';
            console.log(`[Agent] Game client connected: ${clientAddr}`);
            this.clients.add(socket);

            let buffer = '';

            socket.on('data', (chunk) => {
                buffer += chunk.toString('utf8');
                
                // Process all complete lines in the buffer
                let newlineIdx;
                while ((newlineIdx = buffer.indexOf('\n')) !== -1) {
                    const line = buffer.slice(0, newlineIdx).trim();
                    buffer = buffer.slice(newlineIdx + 1);

                    if (!line) continue;
                    if (line.length > MAX_FRAME_SIZE) {
                        console.warn('[Agent] Frame too large, discarding:', line.length, 'bytes');
                        buffer = '';
                        continue;
                    }

                    try {
                        const data = JSON.parse(line);
                        this._handleFrame(data);
                    } catch (e) {
                        // Silently ignore parse errors to avoid log spam
                    }
                }
            });

            socket.on('end', () => {
                console.log(`[Agent] Client disconnected: ${clientAddr}`);
                this.clients.delete(socket);
            });

            socket.on('error', (err) => {
                console.warn(`[Agent] Client socket error: ${err.message}`);
                this.clients.delete(socket);
            });
        });

        this.server.on('error', (err) => {
            if (err.code === 'EACCES') {
                console.error('[Agent] Pipe access denied. Try running as administrator.');
                this.lastError = 'Access denied. Try running as administrator.';
            } else if (err.code === 'EADDRINUSE') {
                console.error('[Agent] Pipe already in use by another instance.');
                this.lastError = 'Pipe already in use.';
            } else {
                console.error('[Agent] Pipe server error:', err.message);
                this.lastError = err.message;
            }
            this.running = false;
        });

        const pipePath = `\\\\.\\pipe\\${PIPE_NAME}`;
        this.server.listen(pipePath, () => {
            console.log(`[Agent] Named pipe server listening on: ${pipePath}`);
            this.lastError = null;
        });
    }

    stop() {
        if (!this.running) return;
        this.running = false;

        // Close all client connections
        for (const client of this.clients) {
            try { client.destroy(); } catch {}
        }
        this.clients.clear();

        if (this.server) {
            this.server.close((err) => {
                if (err) console.error('[Agent] Error closing server:', err.message);
                else console.log('[Agent] Named pipe server stopped');
            });
            this.server = null;
        }
        this.frameCount = 0;
    }

    isRunning() {
        return this.running;
    }

    getConnectedClients() {
        return this.clients.size;
    }

    getError() {
        return this.lastError;
    }

    getPipeName() {
        return PIPE_NAME;
    }

    getFullPipePath() {
        return `\\\\.\\pipe\\${PIPE_NAME}`;
    }

    _handleFrame(data) {
        // Validate minimal required fields
        if (typeof data.fps !== 'number' || typeof data.frameTime !== 'number') {
            return;
        }

        this.frameCount++;

        // Attach metadata
        const enriched = {
            ...data,
            _source: 'pipe',
            _clientCount: this.clients.size,
            _receivedAt: Date.now()
        };

        if (this._onFrame) {
            this._onFrame(enriched);
        }

        this.emit('frame', enriched);
    }
}

module.exports = new ProfilerAgent();
