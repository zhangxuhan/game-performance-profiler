#include "SessionManager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <random>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

namespace ProfilerCore {

// ============================================================================
// JSON Helpers
// ============================================================================

static std::string EscapeJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string SeverityToString(SessionEventSeverity s) {
    switch (s) {
        case SessionEventSeverity::Info:     return "Info";
        case SessionEventSeverity::Warning:  return "Warning";
        case SessionEventSeverity::Critical: return "Critical";
    }
    return "Unknown";
}

static std::string StatusToString(SessionStatus s) {
    switch (s) {
        case SessionStatus::Created:  return "Created";
        case SessionStatus::Running:  return "Running";
        case SessionStatus::Paused:   return "Paused";
        case SessionStatus::Stopped:  return "Stopped";
        case SessionStatus::Archived: return "Archived";
    }
    return "Unknown";
}

// ============================================================================
// SessionEvent
// ============================================================================

std::string SessionEvent::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestampUs\":" << timestampUs << ",";
    oss << "\"absoluteTimestampUs\":" << absoluteTimestampUs << ",";
    oss << "\"tag\":\"" << EscapeJSON(tag) << "\",";
    oss << "\"message\":\"" << EscapeJSON(message) << "\",";
    oss << "\"severity\":\"" << SeverityToString(severity) << "\",";
    oss << "\"metrics\":{";
    bool first = true;
    for (const auto& kv : metrics) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << EscapeJSON(kv.first) << "\":" << std::fixed << std::setprecision(4) << kv.second;
    }
    oss << "}";
    oss << "}";
    return oss.str();
}

// ============================================================================
// SessionBookmark
// ============================================================================

std::string SessionBookmark::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"timestampUs\":" << timestampUs << ",";
    oss << "\"frameNumber\":" << frameNumber << ",";
    oss << "\"label\":\"" << EscapeJSON(label) << "\",";
    oss << "\"color\":\"" << EscapeJSON(color) << "\",";
    oss << "\"note\":\"" << EscapeJSON(note) << "\"";
    oss << "}";
    return oss.str();
}

// ============================================================================
// SessionRegion
// ============================================================================

std::string SessionRegion::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"id\":\"" << EscapeJSON(id) << "\",";
    oss << "\"name\":\"" << EscapeJSON(name) << "\",";
    oss << "\"category\":\"" << EscapeJSON(category) << "\",";
    oss << "\"startTimestampUs\":" << startTimestampUs << ",";
    oss << "\"endTimestampUs\":" << endTimestampUs << ",";
    oss << "\"startFrame\":" << startFrame << ",";
    oss << "\"endFrame\":" << endFrame << ",";
    oss << "\"ongoing\":" << (IsOngoing() ? "true" : "false") << ",";
    oss << "\"metadata\":{";
    bool first = true;
    for (const auto& kv : metadata) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << EscapeJSON(kv.first) << "\":\"" << EscapeJSON(kv.second) << "\"";
    }
    oss << "}";
    oss << "}";
    return oss.str();
}

// ============================================================================
// SessionInfo
// ============================================================================

std::string SessionInfo::ToJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"sessionId\":\"" << EscapeJSON(sessionId) << "\",";
    oss << "\"name\":\"" << EscapeJSON(name) << "\",";
    oss << "\"description\":\"" << EscapeJSON(description) << "\",";
    oss << "\"tags\":[";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << EscapeJSON(tags[i]) << "\"";
    }
    oss << "],";
    oss << "\"status\":\"" << StatusToString(status) << "\",";
    oss << "\"createdAt\":" << createdAt << ",";
    oss << "\"startedAt\":" << startedAt << ",";
    oss << "\"stoppedAt\":" << stoppedAt << ",";
    oss << "\"durationUs\":" << durationUs << ",";
    oss << "\"avgFps\":" << std::fixed << std::setprecision(2) << avgFps << ",";
    oss << "\"minFps\":" << minFps << ",";
    oss << "\"maxFps\":" << maxFps << ",";
    oss << "\"avgFrameTimeMs\":" << avgFrameTimeMs << ",";
    oss << "\"peakMemoryMB\":" << peakMemoryMB << ",";
    oss << "\"overallScore\":" << overallScore << ",";
    oss << "\"totalFrames\":" << totalFrames << ",";
    oss << "\"totalEvents\":" << totalEvents << ",";
    oss << "\"totalBookmarks\":" << totalBookmarks << ",";
    oss << "\"totalRegions\":" << totalRegions << ",";
    oss << "\"gameVersion\":\"" << EscapeJSON(gameVersion) << "\",";
    oss << "\"platformInfo\":\"" << EscapeJSON(platformInfo) << "\",";
    oss << "\"hardwareInfo\":\"" << EscapeJSON(hardwareInfo) << "\"";
    oss << "}";
    return oss.str();
}

// ============================================================================
// SessionManager
// ============================================================================

SessionManager::SessionManager() {
}

SessionManager::~SessionManager() {
}

void SessionManager::SetConfig(const SessionConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void SessionManager::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.clear();
    m_sessionOrder.clear();
    m_activeSessionId.clear();
}

// ============================================================================
// Timestamp Utilities
// ============================================================================

int64_t SessionManager::GetTimestampNow() const {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    int64_t now = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return now / 10; // Convert 100ns intervals to microseconds
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
#endif
}

std::string SessionManager::GenerateSessionId() const {
    // Format: sess_<timestamp>_<random4hex>
    auto now = GetTimestampNow();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 0xFFFF);
    int randPart = dist(gen);

    std::ostringstream oss;
    oss << "sess_" << std::hex << now << "_" << std::setw(4) << std::setfill('0') << randPart;
    return oss.str();
}

int64_t SessionManager::GetSessionRelativeTime(const SessionData& session) const {
    if (session.info.startedAt == 0) return 0;
    return GetTimestampNow() - session.info.startedAt;
}

// ============================================================================
// Session Lookup
// ============================================================================

SessionManager::SessionData* SessionManager::FindSession(const std::string& sessionId) {
    auto it = m_sessions.find(sessionId);
    return (it != m_sessions.end()) ? it->second.get() : nullptr;
}

const SessionManager::SessionData* SessionManager::FindSession(const std::string& sessionId) const {
    auto it = m_sessions.find(sessionId);
    return (it != m_sessions.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// Session Lifecycle
// ============================================================================

std::string SessionManager::StartSession(const std::string& name,
                                           const std::string& description,
                                           const std::string& gameVersion,
                                           const std::string& platformInfo,
                                           const std::string& hardwareInfo) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string id = GenerateSessionId();
    auto data = std::make_unique<SessionData>();

    int64_t now = GetTimestampNow();
    data->info.sessionId = id;
    data->info.name = name;
    data->info.description = description;
    data->info.status = SessionStatus::Running;
    data->info.createdAt = now;
    data->info.startedAt = now;
    data->info.stoppedAt = 0;
    data->info.durationUs = 0;
    data->info.avgFps = 0;
    data->info.minFps = 0;
    data->info.maxFps = 0;
    data->info.avgFrameTimeMs = 0;
    data->info.peakMemoryMB = 0;
    data->info.overallScore = 0;
    data->info.totalFrames = 0;
    data->info.totalEvents = 0;
    data->info.totalBookmarks = 0;
    data->info.totalRegions = 0;
    data->info.gameVersion = gameVersion;
    data->info.platformInfo = platformInfo;
    data->info.hardwareInfo = hardwareInfo;

    // Log session start event
    SessionEvent startEvent;
    startEvent.timestampUs = 0;
    startEvent.absoluteTimestampUs = now;
    startEvent.tag = "SessionStart";
    startEvent.message = "Session \"" + name + "\" started";
    startEvent.severity = SessionEventSeverity::Info;
    data->events.push_back(startEvent);

    m_sessions[id] = std::move(data);
    m_sessionOrder.push_back(id);
    m_activeSessionId = id;

    return id;
}

bool SessionManager::StopSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionData* session = FindSession(sessionId);
    if (!session) return false;
    if (session->info.status != SessionStatus::Running &&
        session->info.status != SessionStatus::Paused) {
        return false;
    }

    SessionStatus oldStatus = session->info.status;
    int64_t now = GetTimestampNow();
    session->info.status = SessionStatus::Stopped;
    session->info.stoppedAt = now;
    session->info.durationUs = now - session->info.startedAt;
    session->info.totalEvents = static_cast<int>(session->events.size());
    session->info.totalBookmarks = static_cast<int>(session->bookmarks.size());
    session->info.totalRegions = static_cast<int>(session->regions.size());

    // Log session end event
    SessionEvent endEvent;
    endEvent.timestampUs = GetSessionRelativeTime(*session);
    endEvent.absoluteTimestampUs = now;
    endEvent.tag = "SessionStop";
    endEvent.message = "Session \"" + session->info.name + "\" stopped after " +
                       std::to_string(session->info.durationUs / 1000000) + "s";
    endEvent.severity = SessionEventSeverity::Info;
    session->events.push_back(endEvent);

    if (m_activeSessionId == sessionId) {
        m_activeSessionId.clear();
    }

    NotifyStatusChange(sessionId, oldStatus, SessionStatus::Stopped);
    return true;
}

bool SessionManager::PauseSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionData* session = FindSession(sessionId);
    if (!session || session->info.status != SessionStatus::Running) return false;

    SessionStatus oldStatus = session->info.status;
    session->info.status = SessionStatus::Paused;

    SessionEvent pauseEvent;
    pauseEvent.timestampUs = GetSessionRelativeTime(*session);
    pauseEvent.absoluteTimestampUs = GetTimestampNow();
    pauseEvent.tag = "SessionPause";
    pauseEvent.message = "Session paused";
    pauseEvent.severity = SessionEventSeverity::Info;
    session->events.push_back(pauseEvent);

    NotifyStatusChange(sessionId, oldStatus, SessionStatus::Paused);
    return true;
}

bool SessionManager::ResumeSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionData* session = FindSession(sessionId);
    if (!session || session->info.status != SessionStatus::Paused) return false;

    SessionStatus oldStatus = session->info.status;
    session->info.status = SessionStatus::Running;
    m_activeSessionId = sessionId;

    SessionEvent resumeEvent;
    resumeEvent.timestampUs = GetSessionRelativeTime(*session);
    resumeEvent.absoluteTimestampUs = GetTimestampNow();
    resumeEvent.tag = "SessionResume";
    resumeEvent.message = "Session resumed";
    resumeEvent.severity = SessionEventSeverity::Info;
    session->events.push_back(resumeEvent);

    NotifyStatusChange(sessionId, oldStatus, SessionStatus::Running);
    return true;
}

bool SessionManager::ArchiveSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionData* session = FindSession(sessionId);
    if (!session || session->info.status != SessionStatus::Stopped) return false;

    SessionStatus oldStatus = session->info.status;
    session->info.status = SessionStatus::Archived;
    NotifyStatusChange(sessionId, oldStatus, SessionStatus::Archived);
    return true;
}

bool SessionManager::DeleteSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return false;

    // Remove from order list
    m_sessionOrder.erase(
        std::remove(m_sessionOrder.begin(), m_sessionOrder.end(), sessionId),
        m_sessionOrder.end()
    );

    m_sessions.erase(it);

    if (m_activeSessionId == sessionId) {
        m_activeSessionId.clear();
    }
    return true;
}

// ============================================================================
// Session Queries
// ============================================================================

SessionInfo SessionManager::GetSessionInfo(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    return session ? session->info : SessionInfo();
}

std::vector<SessionInfo> SessionManager::GetActiveSessions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> result;
    for (const auto& id : m_sessionOrder) {
        auto it = m_sessions.find(id);
        if (it != m_sessions.end()) {
            const auto& info = it->second->info;
            if (info.status == SessionStatus::Running || info.status == SessionStatus::Paused) {
                result.push_back(info);
            }
        }
    }
    return result;
}

std::vector<SessionInfo> SessionManager::GetAllSessions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> result;
    for (const auto& id : m_sessionOrder) {
        auto it = m_sessions.find(id);
        if (it != m_sessions.end()) {
            result.push_back(it->second->info);
        }
    }
    return result;
}

std::vector<SessionInfo> SessionManager::GetSessionsByTag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> result;
    for (const auto& id : m_sessionOrder) {
        auto it = m_sessions.find(id);
        if (it != m_sessions.end()) {
            const auto& tags = it->second->info.tags;
            if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                result.push_back(it->second->info);
            }
        }
    }
    return result;
}

std::vector<SessionInfo> SessionManager::GetSessionsByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> result;
    for (const auto& id : m_sessionOrder) {
        auto it = m_sessions.find(id);
        if (it != m_sessions.end() && it->second->info.name == name) {
            result.push_back(it->second->info);
        }
    }
    return result;
}

std::string SessionManager::GetActiveSessionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeSessionId;
}

// ============================================================================
// Tagging
// ============================================================================

bool SessionManager::AddTag(const std::string& sessionId, const std::string& tag) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;

    auto& tags = session->info.tags;
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        tags.push_back(tag);
    }
    return true;
}

bool SessionManager::RemoveTag(const std::string& sessionId, const std::string& tag) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;

    auto& tags = session->info.tags;
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    return true;
}

std::vector<std::string> SessionManager::GetTags(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    return session ? session->info.tags : std::vector<std::string>();
}

// ============================================================================
// Events
// ============================================================================

bool SessionManager::LogEvent(const std::string& sessionId,
                                const std::string& tag,
                                const std::string& message,
                                SessionEventSeverity severity,
                                const std::unordered_map<std::string, double>& metrics) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;
    if (session->info.status != SessionStatus::Running) return false;

    SessionEvent event;
    event.timestampUs = GetSessionRelativeTime(*session);
    event.absoluteTimestampUs = GetTimestampNow();
    event.tag = tag;
    event.message = message;
    event.severity = severity;
    event.metrics = metrics;

    session->events.push_back(event);
    TrimSessionData(*session);

    if (m_eventCallback) {
        m_eventCallback(sessionId, event);
    }

    return true;
}

void SessionManager::ProcessFrameData(const std::string& sessionId,
                                        int frameNumber,
                                        double fps,
                                        double frameTimeMs,
                                        size_t memoryBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session || session->info.status != SessionStatus::Running) return;

    // Update running session metrics
    if (fps > 0) {
        if (session->info.totalFrames == 0) {
            session->info.minFps = fps;
            session->info.maxFps = fps;
            session->info.avgFps = fps;
        } else {
            session->info.minFps = std::min(session->info.minFps, fps);
            session->info.maxFps = std::max(session->info.maxFps, fps);
            // Running average
            int n = session->info.totalFrames + 1;
            session->info.avgFps = session->info.avgFps * (n - 1) / n + fps / n;
        }
    }

    if (frameTimeMs > 0) {
        int n = session->info.totalFrames + 1;
        session->info.avgFrameTimeMs = session->info.avgFrameTimeMs * (n - 1) / n + frameTimeMs / n;
    }

    double memMB = static_cast<double>(memoryBytes) / (1024.0 * 1024.0);
    session->info.peakMemoryMB = std::max(session->info.peakMemoryMB, memMB);

    session->info.totalFrames = frameNumber;

    // Auto-detect GC pause
    if (m_config.autoTagGCEvents && frameTimeMs > m_config.gcPauseThresholdMs) {
        SessionEvent gcEvent;
        gcEvent.timestampUs = GetSessionRelativeTime(*session);
        gcEvent.absoluteTimestampUs = GetTimestampNow();
        gcEvent.tag = "GCPause";
        gcEvent.message = "Possible GC pause detected: " +
                          std::to_string(static_cast<int>(frameTimeMs)) + "ms frame at frame " +
                          std::to_string(frameNumber);
        gcEvent.severity = SessionEventSeverity::Warning;
        gcEvent.metrics["frameTimeMs"] = frameTimeMs;
        gcEvent.metrics["fps"] = fps;
        gcEvent.metrics["memoryMB"] = memMB;
        session->events.push_back(gcEvent);
        session->gcEventCount++;
    }

    // Auto-detect scene load
    if (m_config.autoTagSceneLoads && frameTimeMs > m_config.sceneLoadThresholdMs) {
        SessionEvent loadEvent;
        loadEvent.timestampUs = GetSessionRelativeTime(*session);
        loadEvent.absoluteTimestampUs = GetTimestampNow();
        loadEvent.tag = "SceneLoad";
        loadEvent.message = "Possible scene load: " +
                            std::to_string(static_cast<int>(frameTimeMs)) + "ms frame at frame " +
                            std::to_string(frameNumber);
        loadEvent.severity = SessionEventSeverity::Info;
        loadEvent.metrics["frameTimeMs"] = frameTimeMs;
        loadEvent.metrics["fps"] = fps;
        loadEvent.metrics["memoryMB"] = memMB;
        session->events.push_back(loadEvent);
        session->sceneLoadEventCount++;
    }

    session->lastFrameTimeMs = frameTimeMs;
    session->lastFrameNumber = frameNumber;

    TrimSessionData(*session);
}

std::vector<SessionEvent> SessionManager::GetEvents(
    const std::string& sessionId,
    const std::string& tagFilter,
    SessionEventSeverity minSeverity,
    int64_t startUs,
    int64_t endUs) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return {};

    std::vector<SessionEvent> result;
    int minSevInt = static_cast<int>(minSeverity);

    for (const auto& event : session->events) {
        // Filter by tag
        if (!tagFilter.empty() && event.tag != tagFilter) continue;
        // Filter by severity
        if (static_cast<int>(event.severity) < minSevInt) continue;
        // Filter by time range
        if (startUs > 0 && event.timestampUs < startUs) continue;
        if (endUs > 0 && event.timestampUs > endUs) continue;

        result.push_back(event);
    }

    return result;
}

size_t SessionManager::GetEventCount(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    return session ? session->events.size() : 0;
}

void SessionManager::ClearEvents(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (session) {
        session->events.clear();
    }
}

// ============================================================================
// Bookmarks
// ============================================================================

bool SessionManager::AddBookmark(const std::string& sessionId,
                                   int64_t timestampUs,
                                   int frameNumber,
                                   const std::string& label,
                                   const std::string& color,
                                   const std::string& note) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;

    if (session->bookmarks.size() >= m_config.maxBookmarkCount) return false;

    SessionBookmark bm;
    bm.timestampUs = timestampUs;
    bm.frameNumber = frameNumber;
    bm.label = label;
    bm.color = color;
    bm.note = note;

    session->bookmarks.push_back(bm);
    // Keep sorted by timestamp
    std::sort(session->bookmarks.begin(), session->bookmarks.end(),
              [](const SessionBookmark& a, const SessionBookmark& b) {
                  return a.timestampUs < b.timestampUs;
              });

    session->info.totalBookmarks = static_cast<int>(session->bookmarks.size());
    return true;
}

bool SessionManager::RemoveBookmark(const std::string& sessionId, int64_t timestampUs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;

    auto it = std::remove_if(session->bookmarks.begin(), session->bookmarks.end(),
                              [timestampUs](const SessionBookmark& bm) {
                                  return bm.timestampUs == timestampUs;
                              });
    if (it == session->bookmarks.end()) return false;

    session->bookmarks.erase(it, session->bookmarks.end());
    session->info.totalBookmarks = static_cast<int>(session->bookmarks.size());
    return true;
}

std::vector<SessionBookmark> SessionManager::GetBookmarks(
    const std::string& sessionId,
    int64_t startUs,
    int64_t endUs) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return {};

    std::vector<SessionBookmark> result;
    for (const auto& bm : session->bookmarks) {
        if (startUs > 0 && bm.timestampUs < startUs) continue;
        if (endUs > 0 && bm.timestampUs > endUs) continue;
        result.push_back(bm);
    }
    return result;
}

// ============================================================================
// Regions
// ============================================================================

std::string SessionManager::BeginRegion(const std::string& sessionId,
                                          const std::string& name,
                                          const std::string& category,
                                          int startFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session || session->info.status != SessionStatus::Running) return "";

    if (session->regions.size() >= m_config.maxRegionCount) return "";

    session->regionCounter++;
    std::string regionId = "region_" + std::to_string(session->regionCounter);

    SessionRegion region;
    region.id = regionId;
    region.name = name;
    region.category = category;
    region.startTimestampUs = GetSessionRelativeTime(*session);
    region.endTimestampUs = 0; // Ongoing
    region.startFrame = startFrame;
    region.endFrame = 0;

    session->regions.push_back(region);
    session->info.totalRegions = static_cast<int>(session->regions.size());

    // Log region start event
    SessionEvent regionEvent;
    regionEvent.timestampUs = region.startTimestampUs;
    regionEvent.absoluteTimestampUs = GetTimestampNow();
    regionEvent.tag = "RegionStart";
    regionEvent.message = "Region \"" + name + "\" started";
    regionEvent.severity = SessionEventSeverity::Info;
    regionEvent.metrics["regionId"] = static_cast<double>(session->regionCounter);
    session->events.push_back(regionEvent);

    return regionId;
}

bool SessionManager::EndRegion(const std::string& sessionId,
                                 const std::string& regionId,
                                 int endFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionData* session = FindSession(sessionId);
    if (!session) return false;

    for (auto& region : session->regions) {
        if (region.id == regionId && region.IsOngoing()) {
            region.endTimestampUs = GetSessionRelativeTime(*session);
            region.endFrame = endFrame;

            // Log region end event
            SessionEvent regionEvent;
            regionEvent.timestampUs = region.endTimestampUs;
            regionEvent.absoluteTimestampUs = GetTimestampNow();
            regionEvent.tag = "RegionEnd";
            regionEvent.message = "Region \"" + region.name + "\" ended (duration: " +
                                  std::to_string(region.DurationUs() / 1000) + "ms)";
            regionEvent.severity = SessionEventSeverity::Info;
            session->events.push_back(regionEvent);
            return true;
        }
    }
    return false;
}

std::vector<SessionRegion> SessionManager::GetRegions(
    const std::string& sessionId,
    const std::string& categoryFilter) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return {};

    std::vector<SessionRegion> result;
    for (const auto& region : session->regions) {
        if (!categoryFilter.empty() && region.category != categoryFilter) continue;
        result.push_back(region);
    }
    return result;
}

std::optional<SessionRegion> SessionManager::GetRegion(
    const std::string& sessionId,
    const std::string& regionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return std::nullopt;

    for (const auto& region : session->regions) {
        if (region.id == regionId) return region;
    }
    return std::nullopt;
}

// ============================================================================
// Session Comparison
// ============================================================================

SessionManager::SessionComparison SessionManager::CompareSessions(
    const std::string& baselineId,
    const std::string& comparisonId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionComparison comp;
    comp.baselineId = baselineId;
    comp.comparisonId = comparisonId;

    const SessionData* baseline = FindSession(baselineId);
    const SessionData* comparison = FindSession(comparisonId);

    if (!baseline || !comparison) return comp;

    const auto& b = baseline->info;
    const auto& c = comparison->info;

    // FPS comparison
    comp.fpsDelta = c.avgFps - b.avgFps;
    comp.fpsDeltaPercent = (b.avgFps > 0) ? (comp.fpsDelta / b.avgFps * 100.0) : 0.0;
    comp.frameTimeDeltaMs = c.avgFrameTimeMs - b.avgFrameTimeMs;
    comp.memoryDeltaMB = c.peakMemoryMB - b.peakMemoryMB;
    comp.scoreDelta = c.overallScore - b.overallScore;

    // Notable differences
    if (std::abs(comp.fpsDeltaPercent) > 5.0) {
        std::string direction = comp.fpsDelta > 0 ? "higher" : "lower";
        comp.notableDifferences.push_back(
            "FPS is " + std::to_string(static_cast<int>(std::abs(comp.fpsDeltaPercent))) +
            "% " + direction);
    }

    if (std::abs(comp.frameTimeDeltaMs) > 2.0) {
        std::string direction = comp.frameTimeDeltaMs > 0 ? "worse" : "better";
        comp.notableDifferences.push_back(
            "Frame time is " + std::to_string(static_cast<int>(std::abs(comp.frameTimeDeltaMs))) +
            "ms " + direction);
    }

    if (std::abs(comp.memoryDeltaMB) > 100.0) {
        std::string direction = comp.memoryDeltaMB > 0 ? "higher" : "lower";
        comp.notableDifferences.push_back(
            "Peak memory is " + std::to_string(static_cast<int>(std::abs(comp.memoryDeltaMB))) +
            "MB " + direction);
    }

    if (std::abs(comp.scoreDelta) > 5.0) {
        std::string direction = comp.scoreDelta > 0 ? "better" : "worse";
        comp.notableDifferences.push_back(
            "Overall score is " + std::to_string(static_cast<int>(std::abs(comp.scoreDelta))) +
            " points " + direction);
    }

    // Overall assessment
    double netImprovement = 0.0;
    netImprovement += comp.fpsDeltaPercent * 0.3;           // FPS improvement weight
    netImprovement -= comp.frameTimeDeltaMs * 2.0;          // Frame time penalty
    netImprovement -= comp.memoryDeltaMB * 0.01;            // Memory penalty
    netImprovement += comp.scoreDelta * 0.5;                // Score improvement weight

    if (netImprovement > 5.0) {
        comp.assessment = "Improved";
    } else if (netImprovement < -5.0) {
        comp.assessment = "Degraded";
    } else {
        comp.assessment = "Similar";
    }

    return comp;
}

// ============================================================================
// Internal Helpers
// ============================================================================

void SessionManager::TrimSessionData(SessionData& session) {
    // Trim events if exceeding max
    while (session.events.size() > m_config.maxEventHistory) {
        // Keep the first event (SessionStart) and remove the second oldest
        if (session.events.size() > 1) {
            session.events.erase(session.events.begin() + 1);
        } else {
            break;
        }
    }

    // Trim session history if exceeding max
    while (m_sessionOrder.size() > m_config.maxSessionHistory) {
        std::string oldestId = m_sessionOrder.front();
        // Don't delete active sessions
        auto it = m_sessions.find(oldestId);
        if (it != m_sessions.end() &&
            (it->second->info.status == SessionStatus::Running ||
             it->second->info.status == SessionStatus::Paused)) {
            break;
        }
        m_sessions.erase(oldestId);
        m_sessionOrder.erase(m_sessionOrder.begin());
    }
}

void SessionManager::UpdateSessionSummary(SessionData& session) {
    session.info.totalEvents = static_cast<int>(session.events.size());
    session.info.totalBookmarks = static_cast<int>(session.bookmarks.size());
    session.info.totalRegions = static_cast<int>(session.regions.size());
}

void SessionManager::NotifyStatusChange(const std::string& sessionId,
                                          SessionStatus oldStatus,
                                          SessionStatus newStatus) {
    if (m_statusCallback && oldStatus != newStatus) {
        m_statusCallback(sessionId, oldStatus, newStatus);
    }
}

// ============================================================================
// Export
// ============================================================================

std::string SessionManager::ExportSessionToJSON(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return "{}";

    std::ostringstream oss;
    oss << "{";
    oss << "\"info\":" << session->info.ToJSON() << ",";

    // Events
    oss << "\"events\":[";
    for (size_t i = 0; i < session->events.size(); ++i) {
        if (i > 0) oss << ",";
        oss << session->events[i].ToJSON();
    }
    oss << "],";

    // Bookmarks
    oss << "\"bookmarks\":[";
    for (size_t i = 0; i < session->bookmarks.size(); ++i) {
        if (i > 0) oss << ",";
        oss << session->bookmarks[i].ToJSON();
    }
    oss << "],";

    // Regions
    oss << "\"regions\":[";
    for (size_t i = 0; i < session->regions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << session->regions[i].ToJSON();
    }
    oss << "]";

    oss << "}";
    return oss.str();
}

std::string SessionManager::ExportSessionEventsToJSON(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return "[]";

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < session->events.size(); ++i) {
        if (i > 0) oss << ",";
        oss << session->events[i].ToJSON();
    }
    oss << "]";
    return oss.str();
}

std::string SessionManager::ExportSessionRegionsToJSON(const std::string& sessionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SessionData* session = FindSession(sessionId);
    if (!session) return "[]";

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < session->regions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << session->regions[i].ToJSON();
    }
    oss << "]";
    return oss.str();
}

std::string SessionManager::ExportSessionHistoryToJSON() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream oss;
    oss << "{\"sessions\":[";
    for (size_t i = 0; i < m_sessionOrder.size(); ++i) {
        auto it = m_sessions.find(m_sessionOrder[i]);
        if (it != m_sessions.end()) {
            if (i > 0) oss << ",";
            oss << it->second->info.ToJSON();
        }
    }
    oss << "],";
    oss << "\"totalSessions\":" << m_sessions.size() << ",";
    oss << "\"activeSessionId\":\"" << EscapeJSON(m_activeSessionId) << "\"";
    oss << "}";
    return oss.str();
}

} // namespace ProfilerCore
