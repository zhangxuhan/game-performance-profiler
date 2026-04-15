/**
 * process-monitor.js
 * Windows process discovery and metrics monitoring.
 * 
 * Features:
 * - List running processes (filter by name, window title)
 * - Monitor CPU % and memory usage of specific processes
 * - Support attach by PID or process name pattern
 * - Track process lifetime (detect when attached process exits)
 */

const { exec } } = require('child_process');
const path = require('path');
const fs = require('fs');

// Cache process list to avoid hammering WMI
let processCache = [];
let processCacheTime = 0;
const PROCESS_CACHE_TTL = 2000; // ms

// Currently monitored processes (pid -> interval handle)
const monitoredProcesses = new Map();

/**
 * Get a list of running processes with window titles.
 * Returns: [{ pid, name, cmd, memoryMB, cpu, windowTitle }]
 */
function listProcesses() {
    return new Promise((resolve, reject) => {
        const now = Date.now();
        if (now - processCacheTime < PROCESS_CACHE_TTL && processCache.length > 0) {
            resolve(processCache);
            return;
        }

        // Use WMIC to get process list with window titles
        const cmd = 'wmic process get ProcessId,Name,CommandLine /format:csv';

        exec(cmd, { timeout: 5000, maxBuffer: 10 * 1024 * 1024 }, (err, stdout, stderr) => {
            if (err) {
                // Fallback: try tasklist
                exec('tasklist /fo csv /nh', { timeout: 5000 }, (err2, stdout2) => {
                    if (err2) { resolve([]); return; }
                    const procs = stdout2.trim().split('\n').map(line => {
                        const parts = line.split('","').map(p => p.replace(/"/g, ''));
                        if (parts.length < 2) return null;
                        return {
                            pid: parseInt(parts[1]) || 0,
                            name: parts[0].trim(),
                            cmd: '',
                            memoryMB: 0,
                            cpu: 0,
                            windowTitle: ''
                        };
                    }).filter(Boolean);
                    processCache = procs;
                    processCacheTime = now;
                    resolve(procs);
                });
                return;
            }

            try {
                const lines = stdout.trim().split('\n').filter(l => l.trim());
                const procs = [];

                for (let i = 1; i < lines.length; i++) {
                    const parts = lines[i].split(',');
                    if (parts.length < 3) continue;

                    const name = (parts[1] || '').trim();
                    const pid = parseInt(parts[2]) || 0;
                    const cmd = (parts[3] || '').trim();

                    if (!name || !pid) continue;

                    procs.push({
                        pid,
                        name,
                        cmd,
                        memoryMB: 0,
                        cpu: 0,
                        windowTitle: ''
                    });
                }

                processCache = procs;
                processCacheTime = now;
                resolve(procs);
            } catch (e) {
                resolve([]);
            }
        });
    });
}

/**
 * Get window title for a specific process.
 */
function getWindowTitle(pid) {
    return new Promise((resolve) => {
        const cmd = `powershell -NoProfile -Command "(Get-Process -Id ${pid} -ErrorAction SilentlyContinue).MainWindowTitle"`;
        exec(cmd, { timeout: 3000 }, (err, stdout) => {
            if (err) { resolve(''); return; }
            resolve(stdout.trim());
        });
    });
}

/**
 * Get detailed metrics for a single process using WMIC.
 */
function getProcessMetrics(pid) {
    return new Promise((resolve) => {
        const cmd = `wmic path Win32_PerfFormattedData_PerfProc_Process where "IDProcess=${pid}" get PercentProcessorTime,WorkingSetPrivate /format:csv`;
        exec(cmd, { timeout: 5000 }, (err, stdout) => {
            if (err) { resolve({ cpu: 0, memoryMB: 0 }); return; }
            try {
                const lines = stdout.trim().split('\n').filter(l => l.trim());
                if (lines.length < 2) { resolve({ cpu: 0, memoryMB: 0 }); return; }
                const parts = lines[1].split(',');
                if (parts.length < 3) { resolve({ cpu: 0, memoryMB: 0 }); return; }

                const cpu = parseFloat(parts[1]) || 0;
                const workingSet = parseFloat(parts[2]) || 0;
                const memoryMB = Math.round(workingSet / 1024 / 1024 * 10) / 10;

                resolve({ cpu, memoryMB });
            } catch {
                resolve({ cpu: 0, memoryMB: 0 });
            }
        });
    });
}

/**
 * Start monitoring a process. Calls callback periodically with metrics.
 * Returns a stop function.
 */
function monitorProcess(pid, intervalMs, onMetrics) {
    const existing = monitoredProcesses.get(pid);
    if (existing) {
        console.log(`[Monitor] Already monitoring PID ${pid}`);
        return existing.stop;
    }

    const stopInterval = setInterval(async () => {
        // Check if process is still running
        try {
            const metrics = await getProcessMetrics(pid);
            const title = await getWindowTitle(pid);
            onMetrics({ pid, ...metrics, windowTitle: title, timestamp: Date.now() });
        } catch {
            // Process likely exited
            onMetrics({ pid, cpu: 0, memoryMB: 0, windowTitle: '', timestamp: Date.now(), exited: true });
        }
    }, intervalMs);

    const stop = () => {
        clearInterval(stopInterval);
        monitoredProcesses.delete(pid);
        console.log(`[Monitor] Stopped monitoring PID ${pid}`);
    };

    monitoredProcesses.set(pid, { interval: stopInterval, stop });
    console.log(`[Monitor] Started monitoring PID ${pid} every ${intervalMs}ms`);

    return stop;
}

/**
 * Stop monitoring all tracked processes.
 */
function stopAllMonitoring() {
    for (const [, handle] of monitoredProcesses) {
        clearInterval(handle.interval);
    }
    monitoredProcesses.clear();
    console.log('[Monitor] Stopped all process monitoring');
}

/**
 * Filter processes to show only game-like processes (with windows and significant memory).
 */
async function listGameProcesses() {
    const procs = await listProcesses();

    // Enrich with memory and window title
    const enriched = await Promise.all(procs.slice(0, 200).map(async (p) => {
        const [metrics, title] = await Promise.all([
            getProcessMetrics(p.pid).catch(() => ({ cpu: 0, memoryMB: 0 })),
            getWindowTitle(p.pid).catch(() => '')
        ]);
        return { ...p, ...metrics, windowTitle: title };
    }));

    // Filter: must have window title or significant memory (>50MB) or be a known game-related process
    const knownGameKeywords = [
        'game', 'unity', 'unreal', 'gaming', 'steam', 'epic', 'battle',
        'client', 'render', 'engine', 'play', 'world', 'dota', 'csgo',
        'valorant', 'lol', 'minecraft', 'roblox', 'client', 'renderer'
    ];

    return enriched.filter(p => {
        const nameLower = p.name.toLowerCase();
        const titleLower = p.windowTitle.toLowerCase();
        const cmdLower = p.cmd.toLowerCase();

        if (p.windowTitle && p.windowTitle.length > 2) return true;
        if (p.memoryMB > 100) return true;
        if (p.memoryMB > 50 && (p.cpu > 0 || nameLower.includes('game'))) return true;

        for (const kw of knownGameKeywords) {
            if (nameLower.includes(kw) || titleLower.includes(kw) || cmdLower.includes(kw)) {
                return true;
            }
        }

        return false;
    }).sort((a, b) => b.memoryMB - a.memoryMB);
}

/**
 * Check if a process is still running.
 */
function isProcessRunning(pid) {
    return new Promise((resolve) => {
        exec(`tasklist /fi "PID eq ${pid}" /fo csv /nh`, { timeout: 3000 }, (err, stdout) => {
            if (err) { resolve(false); return; }
            resolve(stdout.includes(pid.toString()));
        });
    });
}

module.exports = {
    listProcesses,
    listGameProcesses,
    getProcessMetrics,
    getWindowTitle,
    monitorProcess,
    stopAllMonitoring,
    isProcessRunning
};
