/**
 * Dashboard API - Advanced analytics and data aggregation
 * 
 * Provides:
 * - Aggregated time-series metrics (1s/5s/30s buckets)
 * - Trend analysis (FPS, memory, frame time trends)
 * - Performance scoring and summary
 * - Data export (CSV, JSON, PDF-ready formats)
 * - Recording session management
 * - Anomaly detection and alerts summary
 * - Real-time metric streaming
 */

const express = require('express');

// ─── Time-series aggregator ───────────────────────────────────────────────────

/**
 * Aggregate frame data into time buckets
 * @param {Array} frames - Array of frame data objects
 * @param {number} bucketSizeMs - Bucket size in milliseconds
 * @returns {Array} Aggregated buckets
 */
function aggregateFrames(frames, bucketSizeMs = 5000) {
    if (!frames || frames.length === 0) return [];

    const buckets = new Map();
    
    frames.forEach((frame) => {
        const ts = frame.timestamp || frame.receivedAt || Date.now();
        const bucketKey = Math.floor(ts / bucketSizeMs) * bucketSizeMs;
        
        if (!buckets.has(bucketKey)) {
            buckets.set(bucketKey, {
                timestamp: bucketKey,
                fps: [],
                frameTime: [],
                memory: [],
                frames: [],
                profileData: {}  // Sum of profile durations per function
            });
        }
        
        const bucket = buckets.get(bucketKey);
        if (frame.fps !== undefined) bucket.fps.push(frame.fps);
        if (frame.frameTime !== undefined) bucket.frameTime.push(frame.frameTime);
        if (frame.memory !== undefined) {
            const mem = typeof frame.memory === 'object' 
                ? (frame.memory.currentUsage || 0) 
                : frame.memory;
            bucket.memory.push(mem);
        }
        bucket.frames.push(frame);
        
        // Aggregate profile data
        if (frame.profiles && Array.isArray(frame.profiles)) {
            frame.profiles.forEach(p => {
                if (!bucket.profileData[p.name]) {
                    bucket.profileData[p.name] = { total: 0, count: 0 };
                }
                bucket.profileData[p.name].total += p.duration || 0;
                bucket.profileData[p.name].count += 1;
            });
        }
    });
    
    // Compute aggregates for each bucket
    return Array.from(buckets.values()).map(bucket => {
        const agg = {
            timestamp: bucket.timestamp,
            frameCount: bucket.frames.length,
            fps: computeAggregates(bucket.fps),
            frameTime: computeAggregates(bucket.frameTime),
            memory: computeAggregates(bucket.memory),
            profileFunctions: {}
        };
        
        // Average profile times per function
        Object.entries(bucket.profileData).forEach(([fn, data]) => {
            agg.profileFunctions[fn] = parseFloat((data.total / data.count).toFixed(2));
        });
        
        return agg;
    }).sort((a, b) => a.timestamp - b.timestamp);
}

/**
 * Compute min/max/avg/pXX for an array of numbers
 */
function computeAggregates(values) {
    if (!values || values.length === 0) return null;
    
    const sorted = [...values].sort((a, b) => a - b);
    const sum = sorted.reduce((a, b) => a + b, 0);
    const avg = sum / sorted.length;
    
    return {
        min: parseFloat(sorted[0].toFixed(2)),
        max: parseFloat(sorted[sorted.length - 1].toFixed(2)),
        avg: parseFloat(avg.toFixed(2)),
        median: parseFloat(sorted[Math.floor(sorted.length / 2)].toFixed(2)),
        p90: parseFloat(sorted[Math.min(Math.floor(sorted.length * 0.9), sorted.length - 1)].toFixed(2)),
        p95: parseFloat(sorted[Math.min(Math.floor(sorted.length * 0.95), sorted.length - 1)].toFixed(2)),
        p99: parseFloat(sorted[Math.min(Math.floor(sorted.length * 0.99), sorted.length - 1)].toFixed(2)),
        stdDev: parseFloat(Math.sqrt(
            sorted.reduce((acc, v) => acc + Math.pow(v - avg, 2), 0) / sorted.length
        ).toFixed(2))
    };
}

// ─── Trend analysis ───────────────────────────────────────────────────────────

/**
 * Analyze FPS trend over time
 * @param {Array} frames - Frame data array
 * @param {number} windowSize - Number of frames for trend window
 * @returns {Object} Trend analysis result
 */
function analyzeFPSTrend(frames, windowSize = 60) {
    if (frames.length < windowSize * 2) {
        return { trend: 'insufficient_data', slope: 0, direction: 'neutral' };
    }
    
    // Split into two halves
    const halfLen = Math.floor(frames.length / 2);
    const firstHalf = frames.slice(0, halfLen);
    const secondHalf = frames.slice(-halfLen);
    
    const avgFirst = firstHalf.reduce((s, f) => s + (f.fps || 0), 0) / firstHalf.length;
    const avgSecond = secondHalf.reduce((s, f) => s + (f.fps || 0), 0) / secondHalf.length;
    
    const delta = avgSecond - avgFirst;
    const deltaPercent = avgFirst > 0 ? (delta / avgFirst) * 100 : 0;
    
    let direction = 'stable';
    if (deltaPercent > 2) direction = 'improving';
    else if (deltaPercent < -2) direction = 'degrading';
    
    // Linear regression for slope
    const fpsValues = frames.map(f => f.fps || 0);
    const slope = computeLinearRegressionSlope(fpsValues);
    
    return {
        direction,
        slope: parseFloat(slope.toFixed(4)),
        deltaFps: parseFloat(delta.toFixed(2)),
        deltaPercent: parseFloat(deltaPercent.toFixed(2)),
        avgFirstHalf: parseFloat(avgFirst.toFixed(2)),
        avgSecondHalf: parseFloat(avgSecond.toFixed(2))
    };
}

function computeLinearRegressionSlope(values) {
    const n = values.length;
    if (n < 2) return 0;
    
    let sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    for (let i = 0; i < n; i++) {
        sumX += i;
        sumY += values[i];
        sumXY += i * values[i];
        sumX2 += i * i;
    }
    
    const denom = n * sumX2 - sumX * sumX;
    if (Math.abs(denom) < 0.0001) return 0;
    
    return (n * sumXY - sumX * sumY) / denom;
}

/**
 * Analyze memory trend
 */
function analyzeMemoryTrend(frames, windowSize = 60) {
    if (frames.length < windowSize * 2) {
        return { trend: 'insufficient_data', slope: 0 };
    }
    
    const memValues = frames.map(f => {
        const mem = typeof f.memory === 'object' 
            ? (f.memory.currentUsage || 0) 
            : (f.memory || 0);
        return mem / (1024 * 1024); // Convert to MB
    });
    
    const slope = computeLinearRegressionSlope(memValues);
    const avgFirst = memValues.slice(0, windowSize).reduce((a, b) => a + b, 0) / windowSize;
    const avgSecond = memValues.slice(-windowSize).reduce((a, b) => a + b, 0) / windowSize;
    const growthRateMBPerFrame = slope;
    const growthRateMBPerMin = slope * 60 * 60; // Assuming ~60fps
    
    let trend = 'stable';
    if (growthRateMBPerMin > 10) trend = 'leak_suspected';
    else if (growthRateMBPerMin < -5) trend = 'decreasing';
    
    return {
        trend,
        slope: parseFloat(slope.toFixed(4)),
        growthRateMBPerFrame: parseFloat(growthRateMBPerFrame.toFixed(4)),
        growthRateMBPerMin: parseFloat(growthRateMBPerMin.toFixed(2)),
        avgFirstHalfMB: parseFloat(avgFirst.toFixed(2)),
        avgSecondHalfMB: parseFloat(avgSecond.toFixed(2)),
        currentMB: parseFloat(memValues[memValues.length - 1].toFixed(2)),
        peakMB: parseFloat(Math.max(...memValues).toFixed(2))
    };
}

/**
 * Analyze frame time trend
 */
function analyzeFrameTimeTrend(frames, windowSize = 60) {
    if (frames.length < windowSize * 2) {
        return { trend: 'insufficient_data' };
    }
    
    const ftValues = frames.map(f => f.frameTime || 0);
    const slope = computeLinearRegressionSlope(ftValues);
    
    const avgFirst = ftValues.slice(0, windowSize).reduce((a, b) => a + b, 0) / windowSize;
    const avgSecond = ftValues.slice(-windowSize).reduce((a, b) => a + b, 0) / windowSize;
    
    let trend = 'stable';
    if (slope > 0.05) trend = 'degrading';     // Frame time increasing = bad
    else if (slope < -0.05) trend = 'improving'; // Frame time decreasing = good
    
    const spikeCount = ftValues.filter(v => v > 20).length;
    
    return {
        trend,
        slope: parseFloat(slope.toFixed(4)),
        avgFirstMs: parseFloat(avgFirst.toFixed(2)),
        avgSecondMs: parseFloat(avgSecond.toFixed(2)),
        spikeCount,
        spikePercent: parseFloat(((spikeCount / ftValues.length) * 100).toFixed(2))
    };
}

// ─── Performance scoring ───────────────────────────────────────────────────────

/**
 * Compute a comprehensive performance score (0-100)
 * Based on FPS stability, frame time consistency, and memory health
 */
function computePerformanceScore(frames, windowSize = 300) {
    const recentFrames = frames.slice(-windowSize);
    if (recentFrames.length < 10) return 0;
    
    const fpsValues = recentFrames.map(f => f.fps || 0);
    const ftValues = recentFrames.map(f => f.frameTime || 0);
    const memValues = recentFrames.map(f => {
        const mem = typeof f.memory === 'object' 
            ? (f.memory.currentUsage || 0) 
            : (f.memory || 0);
        return mem / (1024 * 1024 * 1024); // GB
    });
    
    // FPS component (40%)
    const avgFps = fpsValues.reduce((a, b) => a + b, 0) / fpsValues.length;
    const fpsVariance = fpsValues.reduce((acc, v) => acc + Math.pow(v - avgFps, 2), 0) / fpsValues.length;
    const fpsStdDev = Math.sqrt(fpsVariance);
    const fpsStability = Math.max(0, 100 - (fpsStdDev / avgFps) * 100);
    const fpsScore = Math.min(100, (avgFps / 60) * 100) * 0.6 + fpsStability * 0.4;
    
    // Frame time component (35%)
    const avgFt = ftValues.reduce((a, b) => a + b, 0) / ftValues.length;
    const ftVariance = ftValues.reduce((acc, v) => acc + Math.pow(v - avgFt, 2), 0) / ftValues.length;
    const ftStdDev = Math.sqrt(ftVariance);
    const ftStability = Math.max(0, 100 - (ftStdDev / avgFt) * 100);
    const ftScore = Math.min(100, (16.67 / avgFt) * 100) * 0.5 + ftStability * 0.5;
    
    // Memory component (25%)
    const peakMem = Math.max(...memValues);
    const memScore = Math.max(0, 100 - (peakMem / 8) * 100); // Assume 8GB as baseline
    
    // Combined score
    const overall = fpsScore * 0.4 + ftScore * 0.35 + memScore * 0.25;
    
    // Grading
    let grade = 'F';
    if (overall >= 90) grade = 'A+';
    else if (overall >= 85) grade = 'A';
    else if (overall >= 80) grade = 'A-';
    else if (overall >= 75) grade = 'B+';
    else if (overall >= 70) grade = 'B';
    else if (overall >= 65) grade = 'B-';
    else if (overall >= 60) grade = 'C+';
    else if (overall >= 55) grade = 'C';
    else if (overall >= 50) grade = 'C-';
    else if (overall >= 40) grade = 'D';
    
    return {
        overall: parseFloat(overall.toFixed(1)),
        grade,
        fpsScore: parseFloat(fpsScore.toFixed(1)),
        frameTimeScore: parseFloat(ftScore.toFixed(1)),
        memoryScore: parseFloat(memScore.toFixed(1)),
        stability: parseFloat(fpsStability.toFixed(1)),
        recommendations: generateScoreRecommendations(fpsScore, ftScore, memScore)
    };
}

function generateScoreRecommendations(fpsScore, ftScore, memScore) {
    const recs = [];
    if (fpsScore < 60) recs.push({ area: 'FPS', severity: 'high', message: 'Average FPS is below target. Consider reducing graphics settings.' });
    if (ftScore < 60) recs.push({ area: 'FrameTime', severity: 'high', message: 'Frame time variability is high. Check for background processes or shader complexity.' });
    if (memScore < 60) recs.push({ area: 'Memory', severity: 'medium', message: 'Memory usage is elevated. Monitor for potential memory leaks.' });
    return recs;
}

// ─── Data export ──────────────────────────────────────────────────────────────

/**
 * Export frames to CSV format
 */
function exportToCSV(frames, includeProfiles = true) {
    if (!frames || frames.length === 0) return '';
    
    const headers = ['frame', 'timestamp', 'fps', 'frameTime', 'memory'];
    const profileFunctions = new Set();
    
    if (includeProfiles) {
        frames.forEach(f => {
            if (f.profiles) f.profiles.forEach(p => profileFunctions.add(p.name));
        });
        profileFunctions.forEach(fn => headers.push(`profile_${fn}`));
    }
    
    let csv = headers.join(',') + '\n';
    
    frames.forEach(frame => {
        const row = [
            frame.frame || 0,
            frame.timestamp || frame.receivedAt || Date.now(),
            (frame.fps || 0).toFixed(2),
            (frame.frameTime || 0).toFixed(2),
            (typeof frame.memory === 'object' ? (frame.memory.currentUsage || 0) : (frame.memory || 0))
        ];
        
        if (includeProfiles) {
            profileFunctions.forEach(fn => {
                const p = (frame.profiles || []).find(pp => pp.name === fn);
                row.push(p ? p.duration.toFixed(2) : '');
            });
        }
        
        csv += row.join(',') + '\n';
    });
    
    return csv;
}

/**
 * Export aggregated data to JSON format (for chart libraries)
 */
function exportToChartJSON(frames, bucketSizeMs = 5000) {
    const aggregated = aggregateFrames(frames, bucketSizeMs);
    
    return JSON.stringify({
        generatedAt: new Date().toISOString(),
        frameCount: frames.length,
        bucketSizeMs,
        buckets: aggregated,
        summary: {
            fps: computeAggregates(frames.map(f => f.fps || 0)),
            frameTime: computeAggregates(frames.map(f => f.frameTime || 0)),
            memory: computeAggregates(frames.map(f => {
                const mem = typeof f.memory === 'object' 
                    ? (f.memory.currentUsage || 0) 
                    : (f.memory || 0);
                return mem / (1024 * 1024);
            }))
        }
    }, null, 2);
}

// ─── Recording sessions ────────────────────────────────────────────────────────

class RecordingSession {
    constructor(name, description = '') {
        this.id = `rec_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
        this.name = name;
        this.description = description;
        this.startTime = Date.now();
        this.endTime = null;
        this.frameCount = 0;
        this.frames = [];
        this.tags = [];
        this.markers = [];
        this.status = 'recording'; // recording, paused, stopped
        this.tags = [];
    }
    
    addFrame(frameData) {
        if (this.status !== 'recording') return;
        this.frames.push({
            ...frameData,
            _recordedAt: Date.now()
        });
        this.frameCount++;
    }
    
    pause() { this.status = 'paused'; }
    resume() { this.status = 'recording'; }
    
    stop() {
        this.status = 'stopped';
        this.endTime = Date.now();
    }
    
    addTag(tag) {
        this.tags.push({ tag, timestamp: Date.now() });
    }
    
    addMarker(label, description = '') {
        this.markers.push({
            label,
            description,
            timestamp: Date.now(),
            frameIndex: this.frameCount
        });
    }
    
    getMetadata() {
        return {
            id: this.id,
            name: this.name,
            description: this.description,
            duration: this.endTime ? this.endTime - this.startTime : Date.now() - this.startTime,
            frameCount: this.frameCount,
            status: this.status,
            tags: this.tags,
            markers: this.markers,
            avgFps: this.frameCount > 0 
                ? this.frames.reduce((s, f) => s + (f.fps || 0), 0) / this.frames.length 
                : 0
        };
    }
    
    exportFrames() {
        return this.frames.map(f => {
            const { _recordedAt, ...rest } = f;
            return rest;
        });
    }
}

// ─── API Router setup ─────────────────────────────────────────────────────────

let frameHistory = []; // Shared reference to server's frameHistory

function setupDashboardAPI(app, getFrameHistory) {
    // Recording sessions store
    const recordingSessions = new Map();
    let activeRecording = null;
    
    // Helper to get frame history
    const getFrames = () => getFrameHistory ? getFrameHistory() : frameHistory;
    
    // ─── Aggregated metrics ─────────────────────────────────────────────────
    
    /**
     * GET /api/dashboard/aggregated
     * Get aggregated metrics in time buckets
     * Query params: bucketSize (ms, default 5000), limit (buckets, default 100)
     */
    app.get('/api/dashboard/aggregated', (req, res) => {
        const frames = getFrames();
        const bucketSize = parseInt(req.query.bucketSize) || 5000;
        const limit = parseInt(req.query.limit) || 100;
        
        if (frames.length === 0) {
            res.json({ buckets: [], summary: null });
            return;
        }
        
        const aggregated = aggregateFrames(frames, bucketSize);
        const limited = aggregated.slice(-limit);
        
        res.json({
            bucketSizeMs: bucketSize,
            bucketCount: limited.length,
            totalFrames: frames.length,
            buckets: limited,
            summary: {
                fps: computeAggregates(frames.map(f => f.fps || 0)),
                frameTime: computeAggregates(frames.map(f => f.frameTime || 0)),
                memory: computeAggregates(frames.map(f => {
                    const mem = typeof f.memory === 'object' 
                        ? (f.memory.currentUsage || 0) 
                        : (f.memory || 0);
                    return mem / (1024 * 1024);
                }))
            }
        });
    });
    
    /**
     * GET /api/dashboard/trend
     * Get trend analysis for FPS, memory, and frame time
     */
    app.get('/api/dashboard/trend', (req, res) => {
        const frames = getFrames();
        const windowSize = parseInt(req.query.window) || 60;
        
        if (frames.length < windowSize * 2) {
            res.status(400).json({ error: `Need at least ${windowSize * 2} frames for trend analysis` });
            return;
        }
        
        const fpsTrend = analyzeFPSTrend(frames, windowSize);
        const memTrend = analyzeMemoryTrend(frames, windowSize);
        const ftTrend = analyzeFrameTimeTrend(frames, windowSize);
        
        res.json({
            windowSize,
            fpsTrend,
            memoryTrend: memTrend,
            frameTimeTrend: ftTrend,
            timestamp: new Date().toISOString()
        });
    });
    
    /**
     * GET /api/dashboard/score
     * Get comprehensive performance score
     */
    app.get('/api/dashboard/score', (req, res) => {
        const frames = getFrames();
        const windowSize = parseInt(req.query.window) || 300;
        
        if (frames.length < 10) {
            res.status(400).json({ error: 'Not enough frame data for scoring' });
            return;
        }
        
        const score = computePerformanceScore(frames, windowSize);
        res.json({
            ...score,
            frameCount: frames.length,
            windowSize,
            timestamp: new Date().toISOString()
        });
    });
    
    /**
     * GET /api/dashboard/summary
     * Get comprehensive dashboard summary in one call
     */
    app.get('/api/dashboard/summary', (req, res) => {
        const frames = getFrames();
        
        if (frames.length === 0) {
            res.json({ 
                status: 'no_data',
                frameCount: 0,
                message: 'Waiting for frame data...'
            });
            return;
        }
        
        const fpsValues = frames.map(f => f.fps || 0);
        const ftValues = frames.map(f => f.frameTime || 0);
        const memValues = frames.map(f => {
            const mem = typeof f.memory === 'object' 
                ? (f.memory.currentUsage || 0) 
                : (f.memory || 0);
            return mem / (1024 * 1024);
        });
        
        res.json({
            status: 'ok',
            frameCount: frames.length,
            duration: frames.length > 1 
                ? (frames[frames.length - 1].timestamp - frames[0].timestamp) / 1000 
                : 0,
            fps: computeAggregates(fpsValues),
            frameTime: computeAggregates(ftValues),
            memory: computeAggregates(memValues),
            score: computePerformanceScore(frames),
            trends: {
                fps: analyzeFPSTrend(frames),
                memory: analyzeMemoryTrend(frames),
                frameTime: analyzeFrameTimeTrend(frames)
            },
            timestamp: new Date().toISOString()
        });
    });
    
    // ─── Data export ─────────────────────────────────────────────────────────
    
    /**
     * GET /api/dashboard/export/csv
     * Export frame data as CSV
     */
    app.get('/api/dashboard/export/csv', (req, res) => {
        const frames = getFrames();
        const limit = parseInt(req.query.limit) || 10000;
        const recentFrames = frames.slice(-limit);
        
        if (recentFrames.length === 0) {
            res.status(400).json({ error: 'No data to export' });
            return;
        }
        
        const csv = exportToCSV(recentFrames, req.query.profiles !== 'false');
        
        res.setHeader('Content-Type', 'text/csv');
        res.setHeader('Content-Disposition', `attachment; filename="profiler_export_${Date.now()}.csv"`);
        res.send(csv);
    });
    
    /**
     * GET /api/dashboard/export/json
     * Export aggregated data as JSON for charts
     */
    app.get('/api/dashboard/export/json', (req, res) => {
        const frames = getFrames();
        const bucketSize = parseInt(req.query.bucketSize) || 5000;
        
        if (frames.length === 0) {
            res.status(400).json({ error: 'No data to export' });
            return;
        }
        
        res.setHeader('Content-Type', 'application/json');
        res.setHeader('Content-Disposition', `attachment; filename="profiler_export_${Date.now()}.json"`);
        res.send(exportToChartJSON(frames, bucketSize));
    });
    
    /**
     * GET /api/dashboard/export/prof
     * Re-export in .prof (JSON lines) format
     */
    app.get('/api/dashboard/export/prof', (req, res) => {
        const frames = getFrames();
        const limit = parseInt(req.query.limit) || 10000;
        const recentFrames = frames.slice(-limit);
        
        if (recentFrames.length === 0) {
            res.status(400).json({ error: 'No data to export' });
            return;
        }
        
        const lines = recentFrames.map(f => {
            const { receivedAt, _source, _playbackIndex, _originalFrameTime, ...rest } = f;
            return JSON.stringify(rest);
        }).join('\n');
        
        res.setHeader('Content-Type', 'application/json');
        res.setHeader('Content-Disposition', `attachment; filename="profiler_export_${Date.now()}.prof"`);
        res.send(lines);
    });
    
    // ─── Recording sessions ──────────────────────────────────────────────────
    
    /**
     * POST /api/recordings/start
     * Start a new recording session
     */
    app.post('/api/recordings/start', (req, res) => {
        const { name, description } = req.body;
        
        if (activeRecording) {
            res.status(400).json({ error: 'A recording is already in progress. Stop it first.' });
            return;
        }
        
        const session = new RecordingSession(name || `Recording ${Date.now()}`, description || '');
        recordingSessions.set(session.id, session);
        activeRecording = session;
        
        res.json({ 
            success: true, 
            session: session.getMetadata()
        });
    });
    
    /**
     * POST /api/recordings/stop
     * Stop the current recording session
     */
    app.post('/api/recordings/stop', (req, res) => {
        if (!activeRecording) {
            res.status(400).json({ error: 'No active recording session' });
            return;
        }
        
        activeRecording.stop();
        const metadata = activeRecording.getMetadata();
        activeRecording = null;
        
        res.json({ 
            success: true, 
            session: metadata
        });
    });
    
    /**
     * POST /api/recordings/pause
     * Pause the current recording
     */
    app.post('/api/recordings/pause', (req, res) => {
        if (!activeRecording) {
            res.status(400).json({ error: 'No active recording session' });
            return;
        }
        
        activeRecording.pause();
        res.json({ success: true, status: 'paused' });
    });
    
    /**
     * POST /api/recordings/resume
     * Resume the current recording
     */
    app.post('/api/recordings/resume', (req, res) => {
        if (!activeRecording) {
            res.status(400).json({ error: 'No active recording session' });
            return;
        }
        
        activeRecording.resume();
        res.json({ success: true, status: 'recording' });
    });
    
    /**
     * POST /api/recordings/tag
     * Add a tag to the current recording
     */
    app.post('/api/recordings/tag', (req, res) => {
        const { tag } = req.body;
        if (!activeRecording) {
            res.status(400).json({ error: 'No active recording session' });
            return;
        }
        
        activeRecording.addTag(tag || 'untagged');
        res.json({ success: true, tags: activeRecording.tags });
    });
    
    /**
     * POST /api/recordings/marker
     * Add a marker to the current recording
     */
    app.post('/api/recordings/marker', (req, res) => {
        const { label, description } = req.body;
        if (!activeRecording) {
            res.status(400).json({ error: 'No active recording session' });
            return;
        }
        
        activeRecording.addMarker(label || 'Marker', description || '');
        res.json({ success: true, markers: activeRecording.markers });
    });
    
    /**
     * GET /api/recordings
     * List all recording sessions
     */
    app.get('/api/recordings', (req, res) => {
        const sessions = Array.from(recordingSessions.values())
            .map(s => s.getMetadata())
            .sort((a, b) => b.startTime - a.startTime);
        
        res.json({ 
            sessions,
            activeSession: activeRecording ? activeRecording.getMetadata() : null,
            count: sessions.length
        });
    });
    
    /**
     * GET /api/recordings/:id
     * Get a specific recording session details
     */
    app.get('/api/recordings/:id', (req, res) => {
        const session = recordingSessions.get(req.params.id);
        if (!session) {
            res.status(404).json({ error: 'Recording not found' });
            return;
        }
        
        res.json({ 
            ...session.getMetadata(),
            frames: session.exportFrames()
        });
    });
    
    /**
     * GET /api/recordings/:id/export
     * Export a recording as CSV
     */
    app.get('/api/recordings/:id/export', (req, res) => {
        const session = recordingSessions.get(req.params.id);
        if (!session) {
            res.status(404).json({ error: 'Recording not found' });
            return;
        }
        
        const csv = exportToCSV(session.exportFrames());
        res.setHeader('Content-Type', 'text/csv');
        res.setHeader('Content-Disposition', `attachment; filename="${session.name}_${session.id}.csv"`);
        res.send(csv);
    });
    
    /**
     * DELETE /api/recordings/:id
     * Delete a recording session
     */
    app.delete('/api/recordings/:id', (req, res) => {
        if (activeRecording && activeRecording.id === req.params.id) {
            res.status(400).json({ error: 'Cannot delete active recording. Stop it first.' });
            return;
        }
        
        const deleted = recordingSessions.delete(req.params.id);
        res.json({ success: deleted });
    });
    
    // ─── Anomaly detection ───────────────────────────────────────────────────
    
    /**
     * GET /api/dashboard/anomalies
     * Detect anomalies in the frame data
     */
    app.get('/api/dashboard/anomalies', (req, res) => {
        const frames = getFrames();
        const threshold = parseFloat(req.query.threshold) || 2.0; // std dev threshold
        
        if (frames.length < 30) {
            res.status(400).json({ error: 'Need at least 30 frames for anomaly detection' });
            return;
        }
        
        const fpsValues = frames.map(f => f.fps || 0);
        const ftValues = frames.map(f => f.frameTime || 0);
        
        const avgFps = fpsValues.reduce((a, b) => a + b, 0) / fpsValues.length;
        const fpsVariance = fpsValues.reduce((acc, v) => acc + Math.pow(v - avgFps, 2), 0) / fpsValues.length;
        const fpsStdDev = Math.sqrt(fpsVariance);
        
        const avgFt = ftValues.reduce((a, b) => a + b, 0) / ftValues.length;
        const ftVariance = ftValues.reduce((acc, v) => acc + Math.pow(v - avgFt, 2), 0) / ftValues.length;
        const ftStdDev = Math.sqrt(ftVariance);
        
        const anomalies = [];
        
        frames.forEach((frame, idx) => {
            const fpsZ = Math.abs((frame.fps - avgFps) / fpsStdDev);
            const ftZ = Math.abs((frame.frameTime - avgFt) / ftStdDev);
            
            if (fpsZ > threshold || ftZ > threshold) {
                anomalies.push({
                    frameIndex: idx,
                    timestamp: frame.timestamp || frame.receivedAt,
                    type: fpsZ > threshold && ftZ > threshold ? 'both' : fpsZ > threshold ? 'fps' : 'frametime',
                    fps: frame.fps,
                    fpsZScore: parseFloat(fpsZ.toFixed(2)),
                    frameTime: frame.frameTime,
                    ftZScore: parseFloat(ftZ.toFixed(2))
                });
            }
        });
        
        res.json({
            threshold,
            totalAnomalies: anomalies.length,
            anomalyPercent: parseFloat(((anomalies.length / frames.length) * 100).toFixed(2)),
            anomalies: anomalies.slice(-100), // Return last 100
            stats: {
                avgFps: parseFloat(avgFps.toFixed(2)),
                fpsStdDev: parseFloat(fpsStdDev.toFixed(2)),
                avgFrameTime: parseFloat(avgFt.toFixed(2)),
                ftStdDev: parseFloat(ftStdDev.toFixed(2))
            }
        });
    });
    
    // ─── Alerts summary ──────────────────────────────────────────────────────
    
    /**
     * GET /api/dashboard/alerts-summary
     * Get alerts summary with grouped statistics
     */
    app.get('/api/dashboard/alerts-summary', (req, res) => {
        const frames = getFrames();
        
        // This would integrate with the existing alert system
        // For now, generate a summary based on frame data patterns
        const fpsValues = frames.map(f => f.fps || 0);
        const ftValues = frames.map(f => f.frameTime || 0);
        
        const avgFps = fpsValues.reduce((a, b) => a + b, 0) / fpsValues.length;
        const avgFt = ftValues.reduce((a, b) => a + b, 0) / ftValues.length;
        
        const summary = {
            fpsStatus: avgFps >= 60 ? 'good' : avgFps >= 45 ? 'warning' : 'critical',
            frameTimeStatus: avgFt <= 16.67 ? 'good' : avgFt <= 25 ? 'warning' : 'critical',
            spikeCount: ftValues.filter(v => v > 20).length,
            lowFpsCount: fpsValues.filter(v => v < 30).length,
            totalFrames: frames.length,
            timestamp: new Date().toISOString()
        };
        
        res.json(summary);
    });
    
    console.log('[Dashboard API] Registered dashboard endpoints');
}

module.exports = {
    setupDashboardAPI,
    aggregateFrames,
    analyzeFPSTrend,
    analyzeMemoryTrend,
    analyzeFrameTimeTrend,
    computePerformanceScore,
    exportToCSV,
    exportToChartJSON,
    RecordingSession
};