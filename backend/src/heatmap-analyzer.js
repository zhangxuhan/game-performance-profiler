/**
 * Heatmap Analyzer - Visual heatmap generation for performance data
 * 
 * Features:
 * - FPS heatmap: visualize FPS distribution over time across multiple sessions
 * - Frame time heatmap: color-coded frame time grid
 * - Memory heatmap: memory pressure visualization over time
 * - Thermal heatmap: CPU/GPU temperature heatmap
 * - Call stack heatmap: function call frequency heatmap
 * - Comparative heatmap: side-by-side session comparison
 * - Custom metric heatmap: user-defined metric heatmap
 */

const express = require('express');

// ─── Helper Functions ─────────────────────────────────────────────────────────

/**
 * Calculate percentiles for coloring
 */
function calculatePercentile(values, p) {
    if (!values || values.length === 0) return 0;
    const sorted = [...values].sort((a, b) => a - b);
    const idx = Math.floor((p / 100) * (sorted.length - 1));
    return sorted[idx] || 0;
}

/**
 * Interpolate between two colors based on value
 */
function interpolateColor(value, min, max, colorLow, colorMid, colorHigh) {
    const ratio = (value - min) / (max - min);
    if (ratio <= 0.5) {
        return blendColors(colorLow, colorMid, ratio * 2);
    } else {
        return blendColors(colorMid, colorHigh, (ratio - 0.5) * 2);
    }
}

/**
 * Blend two RGB colors
 */
function blendColors(c1, c2, ratio) {
    const r = Math.round(c1[0] + (c2[0] - c1[0]) * ratio);
    const g = Math.round(c1[1] + (c2[1] - c1[1]) * ratio);
    const b = Math.round(c1[2] + (c2[2] - c1[2]) * ratio);
    return [r, g, b];
}

/**
 * Format color for output
 */
function formatColor(rgb) {
    return `#${rgb.map(v => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('')}`;
}

// Default color palettes for different metrics
const COLOR_PALETTES = {
    fps: {
        low: [0, 176, 80],      // Green
        mid: [255, 192, 0],     // Yellow
        high: [255, 0, 0]       // Red
    },
    frameTime: {
        low: [0, 176, 80],      // Green
        mid: [255, 192, 0],     // Yellow
        high: [255, 0, 0]       // Red
    },
    memory: {
        low: [0, 176, 240],     // Blue
        mid: [255, 192, 0],     // Yellow
        high: [255, 0, 0]       // Red
    },
    temperature: {
        low: [0, 176, 80],      // Green (cool)
        mid: [255, 192, 0],     // Yellow (warm)
        high: [255, 0, 0]       // Red (hot)
    },
    custom: {
        low: [100, 100, 180],   // Purple
        mid: [50, 200, 200],    // Cyan
        high: [255, 100, 100]   // Coral
    }
};

// ─── Core Heatmap Generation ──────────────────────────────────────────────────

/**
 * Generate a heatmap data structure from frame data
 * @param {Array} frames - Array of frame data
 * @param {Object} options - Configuration options
 * @returns {Object} Heatmap data with grid, legend, and statistics
 */
function generateHeatmap(frames, options = {}) {
    const {
        rows = 60,                    // Time axis resolution (e.g., 60 rows = 1 minute at 1 row/sec)
        cols = 20,                    // Value axis resolution
        metric = 'fps',               // Which metric to visualize
        palette = null,               // Custom color palette
        showLabels = true,            // Include axis labels
        normalize = true             // Normalize values to 0-100
    } = options;

    if (!frames || frames.length === 0) {
        return { error: 'No frames provided', grid: [], stats: {} };
    }

    // Determine time range
    const timestamps = frames.map(f => f.timestamp || f.receivedAt || Date.now());
    const minTime = Math.min(...timestamps);
    const maxTime = Math.max(...timestamps);
    const timeRange = maxTime - minTime || 1;

    // Extract metric values
    const values = frames.map(f => extractMetricValue(f, metric));
    const validValues = values.filter(v => v !== null && !isNaN(v));
    
    if (validValues.length === 0) {
        return { error: `No valid values for metric: ${metric}`, grid: [], stats: {} };
    }

    // Calculate value range for coloring
    const minVal = Math.min(...validValues);
    const maxVal = Math.max(...validValues);
    const medianVal = calculatePercentile(validValues, 50);

    // Get color palette
    const colorPalette = palette || COLOR_PALETTES[metric] || COLOR_PALETTES.custom;

    // Build 2D grid
    const grid = [];
    const bucketSize = timeRange / rows;
    const valueBucketSize = (maxVal - minVal) / cols || 1;

    for (let row = 0; row < rows; row++) {
        const rowData = [];
        const timeStart = minTime + row * bucketSize;
        const timeEnd = timeStart + bucketSize;

        // Find frames in this time bucket
        const bucketFrames = frames.filter(f => {
            const t = f.timestamp || f.receivedAt || Date.now();
            return t >= timeStart && t < timeEnd;
        });

        // Determine column values based on aggregation
        const aggregated = aggregateBucketValues(bucketFrames, metric, cols);

        for (let col = 0; col < cols; col++) {
            const value = aggregated[col];
            let intensity;

            if (normalize && maxVal !== minVal) {
                intensity = (value - minVal) / (maxVal - minVal);
            } else {
                intensity = value / (maxVal || 1);
            }

            const color = interpolateColor(
                value,
                minVal,
                maxVal,
                colorPalette.low,
                colorPalette.mid,
                colorPalette.high
            );

            rowData.push({
                row,
                col,
                value: Math.round(value * 100) / 100,
                intensity: Math.round(intensity * 100) / 100,
                color: formatColor(color),
                frameCount: bucketFrames.length,
                timeStart,
                timeEnd
            });
        }

        grid.push(rowData);
    }

    // Build legend data
    const legend = buildLegend(colorPalette, minVal, maxVal, metric);

    // Calculate statistics
    const stats = calculateHeatmapStats(frames, values, metric);

    return {
        metric,
        rows,
        cols,
        minTime,
        maxTime,
        timeRange,
        grid,
        legend,
        stats,
        palette: colorPalette,
        options
    };
}

/**
 * Extract metric value from a frame
 */
function extractMetricValue(frame, metric) {
    switch (metric) {
        case 'fps':
            return frame.fps;
        case 'frameTime':
            return frame.frameTime;
        case 'memory':
            if (typeof frame.memory === 'object') {
                return frame.memory.currentUsage || frame.memory.heapUsed || 0;
            }
            return frame.memory;
        case 'temperature':
            if (typeof frame.temperature === 'object') {
                return frame.temperature.cpu || frame.temperature.gpu || null;
            }
            return frame.temperature;
        case 'power':
            if (typeof frame.power === 'object') {
                return frame.power.cpu || frame.power.gpu || null;
            }
            return frame.power;
        case 'gcPause':
            if (typeof frame.gc === 'object') {
                return frame.gc.pauseTime || 0;
            }
            return 0;
        default:
            // Try to get value directly or from nested object
            if (frame[metric] !== undefined) {
                if (typeof frame[metric] === 'object') {
                    return frame[metric].value || frame[metric].current || 
                           Object.values(frame[metric])[0] || null;
                }
                return frame[metric];
            }
            return null;
    }
}

/**
 * Aggregate values in a time bucket across columns
 */
function aggregateBucketValues(bucketFrames, metric, cols) {
    if (bucketFrames.length === 0) {
        return Array(cols).fill(0);
    }

    const values = bucketFrames.map(f => extractMetricValue(f, metric)).filter(v => v !== null);
    if (values.length === 0) {
        return Array(cols).fill(0);
    }

    // Create histogram-style distribution
    const minVal = Math.min(...values);
    const maxVal = Math.max(...values);
    const bucketSize = (maxVal - minVal) / cols || 1;

    const histogram = Array(cols).fill(0);
    values.forEach(v => {
        const idx = Math.min(cols - 1, Math.floor((v - minVal) / bucketSize));
        histogram[idx]++;
    });

    // Normalize histogram to 0-100 scale for visualization
    const maxCount = Math.max(...histogram);
    if (maxCount > 0) {
        return histogram.map(count => (count / maxCount) * (maxVal - minVal) + minVal);
    }

    return histogram.map(() => minVal);
}

/**
 * Build legend for the heatmap
 */
function buildLegend(palette, minVal, maxVal, metric) {
    const steps = 10;
    const legendItems = [];

    for (let i = 0; i <= steps; i++) {
        const ratio = i / steps;
        const value = minVal + (maxVal - minVal) * ratio;
        const color = interpolateColor(value, minVal, maxVal, palette.low, palette.mid, palette.high);

        legendItems.push({
            value: Math.round(value * 100) / 100,
            color: formatColor(color),
            label: formatMetricLabel(value, metric)
        });
    }

    return {
        metric,
        minValue: minVal,
        maxValue: maxVal,
        steps,
        items: legendItems
    };
}

/**
 * Format metric label for legend
 */
function formatMetricLabel(value, metric) {
    switch (metric) {
        case 'fps':
            return `${Math.round(value)} FPS`;
        case 'frameTime':
            return `${Math.round(value * 10) / 10} ms`;
        case 'memory':
            return formatBytes(value);
        case 'temperature':
            return `${Math.round(value)}°C`;
        case 'power':
            return `${Math.round(value)} W`;
        case 'gcPause':
            return `${Math.round(value)} ms`;
        default:
            return `${Math.round(value * 100) / 100}`;
    }
}

/**
 * Format bytes to human readable
 */
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

/**
 * Calculate heatmap statistics
 */
function calculateHeatmapStats(frames, values, metric) {
    const validValues = values.filter(v => v !== null && !isNaN(v));
    
    if (validValues.length === 0) {
        return {};
    }

    const sorted = [...validValues].sort((a, b) => a - b);
    const sum = validValues.reduce((a, b) => a + b, 0);
    const mean = sum / validValues.length;
    const variance = validValues.reduce((acc, v) => acc + Math.pow(v - mean, 2), 0) / validValues.length;
    const stdDev = Math.sqrt(variance);

    return {
        count: validValues.length,
        min: sorted[0],
        max: sorted[sorted.length - 1],
        mean: Math.round(mean * 100) / 100,
        median: sorted[Math.floor(sorted.length / 2)],
        stdDev: Math.round(stdDev * 100) / 100,
        p5: calculatePercentile(validValues, 5),
        p95: calculatePercentile(validValues, 95),
        p99: calculatePercentile(validValues, 99),
        metric,
        label: formatMetricLabel(mean, metric)
    };
}

// ─── Specialized Heatmaps ─────────────────────────────────────────────────────

/**
 * Generate FPS heatmap with 1-second resolution
 */
function generateFPSHeatmap(frames, options = {}) {
    return generateHeatmap(frames, {
        rows: 60,
        cols: 20,
        metric: 'fps',
        ...options
    });
}

/**
 * Generate frame time heatmap with spike highlighting
 */
function generateFrameTimeHeatmap(frames, options = {}) {
    const heatmap = generateHeatmap(frames, {
        rows: 60,
        cols: 20,
        metric: 'frameTime',
        ...options
    });

    // Add spike detection to each cell
    if (heatmap.grid) {
        const avgFrameTime = heatmap.stats?.mean || 16.67;
        const threshold = avgFrameTime * 2;

        heatmap.grid.forEach(row => {
            row.forEach(cell => {
                cell.isSpike = cell.value > threshold;
                cell.severity = cell.value > threshold * 3 ? 'critical' : 
                               cell.value > threshold * 2 ? 'warning' : 'normal';
            });
        });
    }

    return heatmap;
}

/**
 * Generate memory heatmap with leak detection
 */
function generateMemoryHeatmap(frames, options = {}) {
    const heatmap = generateHeatmap(frames, {
        rows: 60,
        cols: 20,
        metric: 'memory',
        ...options
    });

    // Detect potential memory leak pattern
    if (frames.length > 30) {
        const memoryValues = frames.map(f => extractMetricValue(f, 'memory')).filter(v => v !== null);
        const trend = calculateLinearTrend(memoryValues);

        heatmap.leakDetection = {
            trendSlope: trend.slope,
            trendDirection: trend.slope > 0.01 ? 'increasing' : 
                           trend.slope < -0.01 ? 'decreasing' : 'stable',
            projectedLeakMbPerMin: trend.slope * 60000 / (1024 * 1024),
            isPotentialLeak: trend.slope > 0.1 && memoryValues.length > 60
        };
    }

    return heatmap;
}

/**
 * Generate temperature heatmap with throttle warnings
 */
function generateThermalHeatmap(frames, options = {}) {
    const heatmap = generateHeatmap(frames, {
        rows: 60,
        cols: 20,
        metric: 'temperature',
        ...options
    });

    // Add throttle detection
    if (heatmap.grid) {
        heatmap.throttleEvents = [];
        
        heatmap.grid.forEach((row, rowIdx) => {
            const highTempCells = row.filter(c => c.value > 85);
            if (highTempCells.length > row.length * 0.3) {
                heatmap.throttleEvents.push({
                    timeStart: row[0].timeStart,
                    timeEnd: row[0].timeEnd,
                    severity: 'warning',
                    message: 'Elevated temperature detected'
                });
            }

            const criticalCells = row.filter(c => c.value > 95);
            if (criticalCells.length > 0) {
                heatmap.throttleEvents.push({
                    timeStart: row[0].timeStart,
                    timeEnd: row[0].timeEnd,
                    severity: 'critical',
                    message: 'Critical temperature - thermal throttling likely'
                });
            }
        });
    }

    return heatmap;
}

/**
 * Generate call stack heatmap from profile data
 */
function generateCallStackHeatmap(frames, options = {}) {
    const {
        maxFunctions = 15,
        timeResolution = 60
    } = options;

    // Extract function call frequencies
    const functionCalls = new Map();
    const timestamps = frames.map(f => f.timestamp || f.receivedAt || Date.now());
    const minTime = Math.min(...timestamps);
    const maxTime = Math.max(...timestamps);

    frames.forEach(frame => {
        if (frame.profiles && Array.isArray(frame.profiles)) {
            frame.profiles.forEach(p => {
                if (!functionCalls.has(p.name)) {
                    functionCalls.set(p.name, []);
                }
                functionCalls.get(p.name).push({
                    duration: p.duration,
                    timestamp: frame.timestamp || frame.receivedAt || Date.now()
                });
            });
        }
    });

    // Get top N functions by total duration
    const topFunctions = Array.from(functionCalls.entries())
        .map(([name, calls]) => ({
            name,
            totalDuration: calls.reduce((sum, c) => sum + c.duration, 0),
            callCount: calls.length,
            avgDuration: calls.reduce((sum, c) => sum + c.duration, 0) / calls.length
        }))
        .sort((a, b) => b.totalDuration - a.totalDuration)
        .slice(0, maxFunctions);

    // Build time-resolved grid
    const bucketSize = (maxTime - minTime) / timeResolution;
    const grid = [];

    topFunctions.forEach((func, funcIdx) => {
        const row = [];
        const calls = functionCalls.get(func.name);

        for (let t = 0; t < timeResolution; t++) {
            const timeStart = minTime + t * bucketSize;
            const timeEnd = timeStart + bucketSize;
            
            const bucketCalls = calls.filter(c => c.timestamp >= timeStart && c.timestamp < timeEnd);
            const totalDuration = bucketCalls.reduce((sum, c) => sum + c.duration, 0);
            const avgDuration = bucketCalls.length > 0 ? totalDuration / bucketCalls.length : 0;

            row.push({
                funcIndex: funcIdx,
                funcName: func.name,
                timeSlice: t,
                timeStart,
                timeEnd,
                callCount: bucketCalls.length,
                totalDuration: Math.round(totalDuration * 100) / 100,
                avgDuration: Math.round(avgDuration * 100) / 100,
                intensity: avgDuration / (func.avgDuration || 1)
            });
        }

        grid.push(row);
    });

    return {
        type: 'callStack',
        functions: topFunctions,
        rows: topFunctions.length,
        cols: timeResolution,
        minTime,
        maxTime,
        grid,
        legend: {
            metric: 'duration',
            unit: 'ms',
            functions: topFunctions.map((f, i) => ({
                index: i,
                name: f.name,
                totalDuration: Math.round(f.totalDuration * 100) / 100,
                callCount: f.callCount
            }))
        }
    };
}

/**
 * Calculate linear regression trend
 */
function calculateLinearTrend(values) {
    if (values.length < 2) {
        return { slope: 0, intercept: values[0] || 0, direction: 'stable' };
    }

    const n = values.length;
    let sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;

    for (let i = 0; i < n; i++) {
        sumX += i;
        sumY += values[i];
        sumXY += i * values[i];
        sumXX += i * i;
    }

    const slope = (n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX);
    const intercept = (sumY - slope * sumX) / n;

    return {
        slope: Math.round(slope * 10000) / 10000,
        intercept: Math.round(intercept * 100) / 100,
        direction: slope > 0.01 ? 'increasing' : slope < -0.01 ? 'decreasing' : 'stable'
    };
}

/**
 * Generate comparative heatmap between two sessions
 */
function generateComparativeHeatmap(session1, session2, options = {}) {
    const {
        rows = 60,
        cols = 20,
        metric = 'fps'
    } = options;

    // Generate individual heatmaps
    const heatmap1 = generateHeatmap(session1, { rows, cols, metric });
    const heatmap2 = generateHeatmap(session2, { rows, cols, metric });

    // Build comparison grid
    const comparisonGrid = [];

    for (let r = 0; r < rows; r++) {
        const row = [];
        for (let c = 0; c < cols; c++) {
            const cell1 = heatmap1.grid?.[r]?.[c] || { value: 0 };
            const cell2 = heatmap2.grid?.[r]?.[c] || { value: 0 };

            const diff = cell2.value - cell1.value;
            const pctDiff = cell1.value !== 0 ? (diff / cell1.value) * 100 : 0;

            // Color based on improvement/degradation
            let color;
            if (diff > 0) {
                // Better (green shades)
                const intensity = Math.min(1, diff / Math.abs(cell1.value || 1));
                color = formatColor(blendColors([200, 200, 200], [0, 176, 80], intensity));
            } else if (diff < 0) {
                // Worse (red shades)
                const intensity = Math.min(1, Math.abs(diff) / Math.abs(cell1.value || 1));
                color = formatColor(blendColors([200, 200, 200], [255, 0, 0], intensity));
            } else {
                color = '#C8C8C8'; // Neutral
            }

            row.push({
                row: r,
                col: c,
                session1Value: cell1.value,
                session2Value: cell2.value,
                difference: Math.round(diff * 100) / 100,
                percentDifference: Math.round(pctDiff * 10) / 10,
                color,
                status: diff > 0 ? 'improved' : diff < 0 ? 'degraded' : 'same'
            });
        }
        comparisonGrid.push(row);
    }

    // Calculate overall comparison stats
    const overallDiff = (heatmap2.stats?.mean || 0) - (heatmap1.stats?.mean || 0);
    const overallPct = heatmap1.stats?.mean ? (overallDiff / heatmap1.stats.mean) * 100 : 0;

    return {
        type: 'comparison',
        metric,
        rows,
        cols,
        session1: {
            stats: heatmap1.stats,
            dataPoints: session1.length
        },
        session2: {
            stats: heatmap2.stats,
            dataPoints: session2.length
        },
        comparison: {
            overallDifference: Math.round(overallDiff * 100) / 100,
            overallPercentChange: Math.round(overallPct * 10) / 10,
            improvedCells: comparisonGrid.flat().filter(c => c.status === 'improved').length,
            degradedCells: comparisonGrid.flat().filter(c => c.status === 'degraded').length,
            sameCells: comparisonGrid.flat().filter(c => c.status === 'same').length
        },
        grid: comparisonGrid
    };
}

// ─── REST API Routes ───────────────────────────────────────────────────────────

/**
 * Setup heatmap analyzer API routes
 */
function setupHeatmapAPI(app, getFrameHistory) {
    /**
     * GET /api/heatmap/fps
     * Generate FPS heatmap
     */
    app.get('/api/heatmap/fps', (req, res) => {
        try {
            const frames = getFrameHistory();
            const options = {
                rows: parseInt(req.query.rows) || 60,
                cols: parseInt(req.query.cols) || 20
            };
            const heatmap = generateFPSHeatmap(frames, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/frameTime
     * Generate frame time heatmap with spike highlighting
     */
    app.get('/api/heatmap/frameTime', (req, res) => {
        try {
            const frames = getFrameHistory();
            const options = {
                rows: parseInt(req.query.rows) || 60,
                cols: parseInt(req.query.cols) || 20
            };
            const heatmap = generateFrameTimeHeatmap(frames, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/memory
     * Generate memory heatmap with leak detection
     */
    app.get('/api/heatmap/memory', (req, res) => {
        try {
            const frames = getFrameHistory();
            const options = {
                rows: parseInt(req.query.rows) || 60,
                cols: parseInt(req.query.cols) || 20
            };
            const heatmap = generateMemoryHeatmap(frames, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/thermal
     * Generate thermal heatmap with throttle warnings
     */
    app.get('/api/heatmap/thermal', (req, res) => {
        try {
            const frames = getFrameHistory();
            const options = {
                rows: parseInt(req.query.rows) || 60,
                cols: parseInt(req.query.cols) || 20
            };
            const heatmap = generateThermalHeatmap(frames, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/callstack
     * Generate call stack heatmap
     */
    app.get('/api/heatmap/callstack', (req, res) => {
        try {
            const frames = getFrameHistory();
            const options = {
                maxFunctions: parseInt(req.query.maxFunctions) || 15,
                timeResolution: parseInt(req.query.resolution) || 60
            };
            const heatmap = generateCallStackHeatmap(frames, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/comparison
     * Generate comparative heatmap between two sessions
     */
    app.get('/api/heatmap/comparison', (req, res) => {
        try {
            const { sessionId1, sessionId2, metric } = req.query;
            
            // This would normally fetch from session storage
            // For now, use current frame history split in half
            const frames = getFrameHistory();
            const mid = Math.floor(frames.length / 2);
            const session1 = frames.slice(0, mid);
            const session2 = frames.slice(mid);

            const options = {
                rows: parseInt(req.query.rows) || 60,
                cols: parseInt(req.query.cols) || 20,
                metric: metric || 'fps'
            };

            const heatmap = generateComparativeHeatmap(session1, session2, options);
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * POST /api/heatmap/custom
     * Generate custom metric heatmap
     */
    app.post('/api/heatmap/custom', (req, res) => {
        try {
            const { metric, rows = 60, cols = 20, palette } = req.body;
            const frames = getFrameHistory();

            if (!metric) {
                return res.status(400).json({ error: 'Metric is required' });
            }

            const options = { rows, cols, palette };
            const heatmap = generateHeatmap(frames, { ...options, metric });
            res.json(heatmap);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    /**
     * GET /api/heatmap/export/png
     * Export heatmap as base64 PNG (using canvas-style data)
     */
    app.get('/api/heatmap/export/png', (req, res) => {
        try {
            const { metric, width = 800, height = 600 } = req.query;
            const frames = getFrameHistory();
            
            const heatmap = generateHeatmap(frames, {
                rows: Math.min(100, parseInt(req.query.rows) || 60),
                cols: Math.min(50, parseInt(req.query.cols) || 20),
                metric: metric || 'fps'
            });

            // Generate SVG representation (which can be converted to PNG client-side)
            const svg = generateHeatmapSVG(heatmap, parseInt(width), parseInt(height));
            
            res.json({
                format: 'svg',
                data: svg,
                heatmap: heatmap
            });
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    console.log('[HeatmapAPI] Heatmap analyzer routes registered');
}

module.exports = {
    setupHeatmapAPI,
    generateHeatmap,
    generateFPSHeatmap,
    generateFrameTimeHeatmap,
    generateMemoryHeatmap,
    generateThermalHeatmap,
    generateCallStackHeatmap,
    generateComparativeHeatmap
};