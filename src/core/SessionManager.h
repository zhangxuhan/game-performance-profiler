#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>

namespace ProfilerCore {

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Severity level for session events
 */
enum class SessionEventSeverity {
    Info = 0,
    Warning = 1,
    Critical = 2
};

/**
 * A timestamped event within a profiling session.
 * Used to mark significant moments (scene transitions, GC pauses, loading, etc.)
 */
struct SessionEvent {
    int64_t timestampUs;             // Microseconds since session start
    int64_t absoluteTimestampUs;     // Microseconds since epoch
    std::string tag;                 // Short tag (e.g., "SceneLoad", "GC", "OOM")
    std::string message;             // Human-readable description
    SessionEventSeverity severity;
    std::unordered_map<std::string, double> metrics; // Associated metrics snapshot

    std::string ToJSON() const;
};

/**
 * A user-placed bookmark for quick navigation in replay.
 */
struct SessionBookmark {
    int64_t timestampUs;             // Microseconds since session start
    int64_t frameNumber;
    std::string label;               // User label (e.g., "Before optimization")
    std::string color;               // Display color hint (hex, e.g., "#FF5733")
    std::string note;                // Optional longer note

    std::string ToJSON() const;
};

/**
 * A time range within a session, used for region analysis.
 */
struct SessionRegion {
    std::string id;                  // Unique region identifier
    std::string name;                // Human-readable name
    std::string category;            // Category (e.g., "Gameplay", "Loading", "Menu")
    int64_t startTimestampUs;        // Start time (relative to session)
    int64_t endTimestampUs;          // End time (0 = ongoing)
    int startFrame;
    int endFrame;                    // 0 = ongoing
    std::unordered_map<std::string, std::string> metadata;

    bool IsOngoing() const { return endTimestampUs == 0; }
    int64_t DurationUs() const { return endTimestampUs - startTimestampUs; }

    std::string ToJSON() const;
};

/**
 * Session status
 */
enum class SessionStatus {
    Created = 0,
    Running = 1,
    Paused = 2,
    Stopped = 3,
    Archived = 4
};

/**
 * Complete session metadata and summary.
 */
struct SessionInfo {
    std::string sessionId;
    std::string name;
    std::string description;
    std::vector<std::string> tags;   // User-defined tags for filtering
    SessionStatus status;

    int64_t createdAt;               // Epoch microseconds
    int64_t startedAt;
    int64_t stoppedAt;
    int64_t durationUs;              // Total profiling duration

    // Session metrics summary (populated on stop)
    double avgFps;
    double minFps;
    double maxFps;
    double avgFrameTimeMs;
    double peakMemoryMB;
    double overallScore;
    int totalFrames;
    int totalEvents;
    int totalBookmarks;
    int totalRegions;

    std::string gameVersion;
    std::string platformInfo;
    std::string hardwareInfo;

    std::string ToJSON() const;
};

/**
 * Configuration for session management.
 */
struct SessionConfig {
    size_t maxEventHistory = 10000;        // Max events per session
    size_t maxBookmarkCount = 500;          // Max bookmarks per session
    size_t maxRegionCount = 200;            // Max regions per session
    size_t maxSessionHistory = 100;         // Max stored sessions
    bool autoTagGCEvents = true;            // Auto-tag detected GC-like pauses
    bool autoTagSceneLoads = true;          // Auto-tag detected scene load spikes
    double gcPauseThresholdMs = 50.0;       // Frame time above this = potential GC
    double sceneLoadThresholdMs = 100.0;    // Frame time above this = potential load
    bool persistSessionsToFile = false;      // Auto-save sessions to disk
    std::string persistDirectory = "";       // Directory for session files
};

// ============================================================================
// SessionManager
// ============================================================================

/**
 * SessionManager - Manages profiling session lifecycle, events, bookmarks,
 * regions, and session history.
 *
 * Features:
 * - Start/stop/pause named profiling sessions with metadata
 * - Tag sessions for organization and filtering
 * - Log timestamped events (scene loads, GC pauses, user annotations)
 * - Place bookmarks for quick navigation during replay
 * - Define time regions for focused analysis
 * - Auto-detect GC pauses and scene load events from frame data
 * - Search and filter events by tag, severity, or time range
 * - Export session data to JSON for frontend consumption
 * - Maintain session history with summaries
 *
 * Usage:
 *   SessionManager mgr;
 *   auto id = mgr.StartSession("Level1-Profiling", "Baseline run");
 *   mgr.AddTag(id, "baseline");
 *   mgr.LogEvent(id, "SceneLoad", "Entering dungeon");
 *   mgr.AddBookmark(id, 1234, "Before Boss Fight", "#FF0000");
 *   mgr.BeginRegion(id, "boss-fight", "Boss Fight", "Gameplay");
 *   // ... profiling happens ...
 *   mgr.EndRegion(id, "boss-fight");
 *   mgr.StopSession(id);
 */
class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    // ─── Configuration ─────────────────────────────────────────────────────

    void SetConfig(const SessionConfig& config);
    const SessionConfig& GetConfig() const { return m_config; }
    void Reset();

    // ─── Session Lifecycle ─────────────────────────────────────────────────

    /** Create and start a new profiling session. Returns session ID. */
    std::string StartSession(const std::string& name,
                              const std::string& description = "",
                              const std::string& gameVersion = "",
                              const std::string& platformInfo = "",
                              const std::string& hardwareInfo = "");

    /** Stop an active session. Computes final summary metrics. */
    bool StopSession(const std::string& sessionId);

    /** Pause a running session (stops event auto-detection). */
    bool PauseSession(const std::string& sessionId);

    /** Resume a paused session. */
    bool ResumeSession(const std::string& sessionId);

    /** Archive a stopped session (removes from active list, keeps in history). */
    bool ArchiveSession(const std::string& sessionId);

    /** Delete a session and all its data. */
    bool DeleteSession(const std::string& sessionId);

    // ─── Session Queries ───────────────────────────────────────────────────

    SessionInfo GetSessionInfo(const std::string& sessionId) const;
    std::vector<SessionInfo> GetActiveSessions() const;
    std::vector<SessionInfo> GetAllSessions() const;
    std::vector<SessionInfo> GetSessionsByTag(const std::string& tag) const;
    std::vector<SessionInfo> GetSessionsByName(const std::string& name) const;
    std::string GetActiveSessionId() const;

    // ─── Tagging ───────────────────────────────────────────────────────────

    bool AddTag(const std::string& sessionId, const std::string& tag);
    bool RemoveTag(const std::string& sessionId, const std::string& tag);
    std::vector<std::string> GetTags(const std::string& sessionId) const;

    // ─── Events ────────────────────────────────────────────────────────────

    /** Log a custom event in the session. */
    bool LogEvent(const std::string& sessionId,
                  const std::string& tag,
                  const std::string& message,
                  SessionEventSeverity severity = SessionEventSeverity::Info,
                  const std::unordered_map<std::string, double>& metrics = {});

    /** Auto-detect events from frame data. Called by ProfilerCore each frame. */
    void ProcessFrameData(const std::string& sessionId,
                          int frameNumber,
                          double fps,
                          double frameTimeMs,
                          size_t memoryBytes);

    /** Get events for a session, optionally filtered. */
    std::vector<SessionEvent> GetEvents(
        const std::string& sessionId,
        const std::string& tagFilter = "",
        SessionEventSeverity minSeverity = SessionEventSeverity::Info,
        int64_t startUs = 0,
        int64_t endUs = 0) const;

    /** Get events count. */
    size_t GetEventCount(const std::string& sessionId) const;

    /** Clear events for a session. */
    void ClearEvents(const std::string& sessionId);

    // ─── Bookmarks ─────────────────────────────────────────────────────────

    bool AddBookmark(const std::string& sessionId,
                     int64_t timestampUs,
                     int frameNumber,
                     const std::string& label,
                     const std::string& color = "#FFD700",
                     const std::string& note = "");

    bool RemoveBookmark(const std::string& sessionId, int64_t timestampUs);

    std::vector<SessionBookmark> GetBookmarks(
        const std::string& sessionId,
        int64_t startUs = 0,
        int64_t endUs = 0) const;

    // ─── Regions ───────────────────────────────────────────────────────────

    /** Begin a named region. Returns region ID. */
    std::string BeginRegion(const std::string& sessionId,
                             const std::string& name,
                             const std::string& category = "",
                             int startFrame = 0);

    /** End a previously started region. */
    bool EndRegion(const std::string& sessionId,
                   const std::string& regionId,
                   int endFrame = 0);

    /** Get all regions for a session. */
    std::vector<SessionRegion> GetRegions(
        const std::string& sessionId,
        const std::string& categoryFilter = "") const;

    /** Get a specific region by ID. */
    std::optional<SessionRegion> GetRegion(const std::string& sessionId,
                                            const std::string& regionId) const;

    // ─── Session Comparison ────────────────────────────────────────────────

    struct SessionComparison {
        std::string baselineId;
        std::string comparisonId;
        double fpsDelta;
        double fpsDeltaPercent;
        double frameTimeDeltaMs;
        double memoryDeltaMB;
        double scoreDelta;
        std::string assessment; // "Improved", "Degraded", "Similar"
        std::vector<std::string> notableDifferences;
    };

    SessionComparison CompareSessions(const std::string& baselineId,
                                       const std::string& comparisonId) const;

    // ─── Export ─────────────────────────────────────────────────────────────

    std::string ExportSessionToJSON(const std::string& sessionId) const;
    std::string ExportSessionEventsToJSON(const std::string& sessionId) const;
    std::string ExportSessionRegionsToJSON(const std::string& sessionId) const;
    std::string ExportSessionHistoryToJSON() const;

    // ─── Callbacks ─────────────────────────────────────────────────────────

    using EventCallback = std::function<void(const std::string& sessionId,
                                              const SessionEvent&)>;
    using StatusCallback = std::function<void(const std::string& sessionId,
                                               SessionStatus oldStatus,
                                               SessionStatus newStatus)>;

    void SetEventCallback(EventCallback cb) { m_eventCallback = std::move(cb); }
    void SetStatusCallback(StatusCallback cb) { m_statusCallback = std::move(cb); }

private:
    // Internal session data storage
    struct SessionData {
        SessionInfo info;
        std::vector<SessionEvent> events;
        std::vector<SessionBookmark> bookmarks;
        std::vector<SessionRegion> regions;

        // For auto-detection
        double lastFrameTimeMs = 0.0;
        int lastFrameNumber = 0;
        int gcEventCount = 0;
        int sceneLoadEventCount = 0;
        int regionCounter = 0;
    };

    std::string GenerateSessionId() const;
    int64_t GetTimestampNow() const;
    int64_t GetSessionRelativeTime(const SessionData& session) const;
    SessionData* FindSession(const std::string& sessionId);
    const SessionData* FindSession(const std::string& sessionId) const;
    void TrimSessionData(SessionData& session);
    void UpdateSessionSummary(SessionData& session);
    void NotifyStatusChange(const std::string& sessionId,
                             SessionStatus oldStatus, SessionStatus newStatus);

    SessionConfig m_config;
    std::unordered_map<std::string, std::unique_ptr<SessionData>> m_sessions;
    std::vector<std::string> m_sessionOrder; // Maintain insertion order
    std::string m_activeSessionId;

    mutable std::mutex m_mutex;

    EventCallback m_eventCallback;
    StatusCallback m_statusCallback;
};

} // namespace ProfilerCore
