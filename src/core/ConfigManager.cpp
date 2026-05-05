#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>

namespace ProfilerCore {

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

/** Trim whitespace from both ends. */
std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/** Extract a JSON string value (assumes input starts with '"'). */
std::string ExtractJSONString(const std::string& json, size_t& pos) {
    std::string result;
    if (pos >= json.size() || json[pos] != '"') return result;
    pos++; // skip opening quote
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            char escaped = json[pos++];
            switch (escaped) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += escaped; break;
            }
        } else {
            result += c;
        }
    }
    return result;
}

/** Skip whitespace in JSON. */
void SkipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
           json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
}

/** Simple JSON object parser — extracts key-value pairs as strings. */
std::unordered_map<std::string, std::string> ParseFlatJSONObject(const std::string& json) {
    std::unordered_map<std::string, std::string> result;
    size_t pos = 0;
    SkipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '{') return result;
    pos++; // skip '{'

    while (pos < json.size()) {
        SkipWhitespace(json, pos);
        if (json[pos] == '}') break;
        if (json[pos] == ',') { pos++; continue; }

        // Parse key
        std::string key = ExtractJSONString(json, pos);
        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') pos++;
        SkipWhitespace(json, pos);

        // Parse value
        size_t valueStart = pos;
        if (pos < json.size() && json[pos] == '"') {
            // String value
            std::string strVal = ExtractJSONString(json, pos);
            result[key] = "\"" + strVal + "\"";
        } else if (pos < json.size() && (json[pos] == 't' || json[pos] == 'f')) {
            // Boolean
            size_t end = json.find_first_of(",} \t\r\n", pos);
            result[key] = json.substr(pos, end - pos);
            pos = end;
        } else if (pos < json.size() && (json[pos] == '-' || std::isdigit(json[pos]))) {
            // Number
            size_t end = json.find_first_of(",} \t\r\n", pos);
            result[key] = json.substr(pos, end - pos);
            pos = end;
        } else if (pos < json.size() && json[pos] == 'n') {
            // null — skip
            pos += 4;
        }
    }
    return result;
}

/** Check if a string represents an integer. */
bool IsIntegerString(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(s[i])) return false;
    }
    return true;
}

/** Check if a string represents a floating-point number. */
bool IsDoubleString(const std::string& s) {
    if (s.empty()) return false;
    try {
        size_t idx;
        std::stod(s, &idx);
        return idx == s.size();
    } catch (...) {
        return false;
    }
}

} // anonymous namespace

// ─── Singleton ────────────────────────────────────────────────────────────────

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() {
    InitializeDefaults();
    InitializePresets();
}

ConfigManager::~ConfigManager() = default;

// ─── Initialize Defaults ──────────────────────────────────────────────────────

void ConfigManager::InitializeDefaults() {
    // FPS / Frame settings
    RegisterRanged("fps.target", 60.0, 1.0, 500.0,
                   "Target frames per second for estimation", "fps");
    RegisterRanged("fps.warningThreshold", 45.0, 1.0, 500.0,
                   "FPS below this triggers a warning alert", "fps");
    RegisterRanged("fps.criticalThreshold", 30.0, 1.0, 500.0,
                   "FPS below this triggers a critical alert", "fps");

    // Memory settings
    RegisterRanged("memory.warningMB", 256.0, 1.0, 65536.0,
                   "Memory usage (MB) warning threshold", "memory");
    RegisterRanged("memory.criticalMB", 512.0, 1.0, 65536.0,
                   "Memory usage (MB) critical threshold", "memory");
    RegisterRanged("memory.growthRateWarning", 1.0, 0.0, 100.0,
                   "Memory growth rate warning (MB/frame)", "memory");
    Register("memory.leakDetectionEnabled", true,
             "Enable automatic memory leak detection", "memory");

    // Frame time settings
    RegisterRanged("frameTime.spikeMinor", 2.0, 1.0, 10.0,
                   "Minor spike threshold (x average)", "frameTime");
    RegisterRanged("frameTime.spikeModerate", 3.0, 1.0, 10.0,
                   "Moderate spike threshold (x average)", "frameTime");
    RegisterRanged("frameTime.spikeSevere", 5.0, 1.0, 20.0,
                   "Severe spike threshold (x average)", "frameTime");

    // Analysis window
    RegisterRanged("analysis.windowSize", 300, 10, 10000,
                   "Rolling window size for statistics", "analysis");
    RegisterRanged("analysis.maxFrameHistory", 600, 60, 10000,
                   "Maximum frame history buffer size", "analysis");

    // Network
    RegisterRanged("network.targetRttMs", 50.0, 1.0, 1000.0,
                   "Target round-trip time in ms", "network");
    RegisterRanged("network.maxJitterMs", 30.0, 1.0, 500.0,
                   "Maximum acceptable jitter in ms", "network");
    Register("network.alertsEnabled", true,
             "Enable network performance alerts", "network");

    // Thermal
    RegisterRanged("thermal.cpuWarningTemp", 80.0, 30.0, 120.0,
                   "CPU temperature warning threshold (C)", "thermal");
    RegisterRanged("thermal.gpuWarningTemp", 85.0, 30.0, 120.0,
                   "GPU temperature warning threshold (C)", "thermal");
    Register("thermal.monitorEnabled", true,
             "Enable thermal monitoring", "thermal");

    // Scoring weights
    RegisterRanged("scoring.fpsWeight", 0.3, 0.0, 1.0,
                   "Weight of FPS in performance score", "scoring");
    RegisterRanged("scoring.stabilityWeight", 0.25, 0.0, 1.0,
                   "Weight of stability in performance score", "scoring");
    RegisterRanged("scoring.memoryWeight", 0.2, 0.0, 1.0,
                   "Weight of memory in performance score", "scoring");
    RegisterRanged("scoring.thermalWeight", 0.15, 0.0, 1.0,
                   "Weight of thermal in performance score", "scoring");
    RegisterRanged("scoring.networkWeight", 0.1, 0.0, 1.0,
                   "Weight of network in performance score", "scoring");

    // General
    Register("general.autoStartSampling", false,
             "Auto-start sampling when process is attached", "general");
    Register("general.exportFormat", std::string("json"),
             "Default export format", "general");
    Register("general.samplingIntervalMs", 16,
             "Sampling poll interval in milliseconds", "general");
}

// ─── Initialize Presets ───────────────────────────────────────────────────────

void ConfigManager::InitializePresets() {
    // Low Overhead: minimal impact on game performance
    ConfigPreset lowOverhead;
    lowOverhead.name = "low-overhead";
    lowOverhead.description = "Minimal profiling overhead — reduced sampling, fewer alerts";
    lowOverhead.overrides = {
        {"analysis.windowSize", 100},
        {"analysis.maxFrameHistory", 120},
        {"general.samplingIntervalMs", 33},
        {"memory.leakDetectionEnabled", false},
        {"network.alertsEnabled", false},
        {"thermal.monitorEnabled", false},
    };
    m_presets[lowOverhead.name] = lowOverhead;

    // Verbose: detailed profiling for deep analysis
    ConfigPreset verbose;
    verbose.name = "verbose";
    verbose.description = "Maximum detail — high-frequency sampling, all features enabled";
    verbose.overrides = {
        {"analysis.windowSize", 1000},
        {"analysis.maxFrameHistory", 3000},
        {"general.samplingIntervalMs", 8},
        {"memory.leakDetectionEnabled", true},
        {"network.alertsEnabled", true},
        {"thermal.monitorEnabled", true},
    };
    m_presets[verbose.name] = verbose;

    // Network Focus: optimized for network-heavy games
    ConfigPreset networkFocus;
    networkFocus.name = "network-focus";
    networkFocus.description = "Optimized for online/multiplayer games — deep network analysis";
    networkFocus.overrides = {
        {"scoring.fpsWeight", 0.2},
        {"scoring.stabilityWeight", 0.15},
        {"scoring.networkWeight", 0.4},
        {"network.targetRttMs", 30.0},
        {"network.maxJitterMs", 15.0},
        {"network.alertsEnabled", true},
        {"analysis.windowSize", 500},
    };
    m_presets[networkFocus.name] = networkFocus;

    // Thermal Focus: watch for overheating
    ConfigPreset thermalFocus;
    thermalFocus.name = "thermal-focus";
    thermalFocus.description = "Focused on temperature and throttling detection";
    thermalFocus.overrides = {
        {"scoring.thermalWeight", 0.4},
        {"scoring.fpsWeight", 0.2},
        {"thermal.cpuWarningTemp", 70.0},
        {"thermal.gpuWarningTemp", 75.0},
        {"thermal.monitorEnabled", true},
    };
    m_presets[thermalFocus.name] = thermalFocus;

    // Competitive: for esports/competitive gaming
    ConfigPreset competitive;
    competitive.name = "competitive";
    competitive.description = "Competitive gaming — tight thresholds, frame-time focus";
    competitive.overrides = {
        {"fps.target", 144.0},
        {"fps.warningThreshold", 120.0},
        {"fps.criticalThreshold", 90.0},
        {"frameTime.spikeMinor", 1.5},
        {"frameTime.spikeModerate", 2.0},
        {"frameTime.spikeSevere", 3.0},
        {"scoring.fpsWeight", 0.4},
        {"scoring.stabilityWeight", 0.35},
    };
    m_presets[competitive.name] = competitive;
}

// ─── Registration ─────────────────────────────────────────────────────────────

void ConfigManager::Register(const std::string& key, ConfigValue defaultValue,
                              const std::string& description,
                              const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ConfigEntry entry;
    entry.value = defaultValue;
    entry.source = ConfigSource::Default;
    entry.description = description;
    entry.category = category;
    m_entries[key] = entry;
    m_defaults[key] = defaultValue;
}

void ConfigManager::RegisterRanged(const std::string& key, ConfigValue defaultValue,
                                    ConfigValue minVal, ConfigValue maxVal,
                                    const std::string& description,
                                    const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ConfigEntry entry;
    entry.value = defaultValue;
    entry.source = ConfigSource::Default;
    entry.description = description;
    entry.category = category;
    entry.minValue = minVal;
    entry.maxValue = maxVal;
    m_entries[key] = entry;
    m_defaults[key] = defaultValue;
}

void ConfigManager::RegisterEnum(const std::string& key, ConfigValue defaultValue,
                                  const std::vector<ConfigValue>& allowed,
                                  const std::string& description,
                                  const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ConfigEntry entry;
    entry.value = defaultValue;
    entry.source = ConfigSource::Default;
    entry.description = description;
    entry.category = category;
    entry.allowedValues = allowed;
    m_entries[key] = entry;
    m_defaults[key] = defaultValue;
}

void ConfigManager::Unregister(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(key);
    m_defaults.erase(key);
}

bool ConfigManager::HasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.find(key) != m_entries.end();
}

// ─── Getters ──────────────────────────────────────────────────────────────────

bool ConfigManager::GetBool(const std::string& key, bool fallback) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return fallback;
    const bool* val = std::get_if<bool>(&it->second.value);
    return val ? *val : fallback;
}

int ConfigManager::GetInt(const std::string& key, int fallback) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return fallback;
    const int* val = std::get_if<int>(&it->second.value);
    return val ? *val : fallback;
}

double ConfigManager::GetDouble(const std::string& key, double fallback) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return fallback;
    if (const double* val = std::get_if<double>(&it->second.value)) return *val;
    if (const int* val = std::get_if<int>(&it->second.value)) return static_cast<double>(*val);
    return fallback;
}

std::string ConfigManager::GetString(const std::string& key, const std::string& fallback) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return fallback;
    const std::string* val = std::get_if<std::string>(&it->second.value);
    return val ? *val : fallback;
}

std::optional<ConfigEntry> ConfigManager::GetEntry(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return std::nullopt;
    return it->second;
}

ConfigSource ConfigManager::GetSource(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return ConfigSource::Default;
    return it->second.source;
}

// ─── Setters ──────────────────────────────────────────────────────────────────

bool ConfigManager::Set(const std::string& key, ConfigValue value, ConfigSource source) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return false;

    // Source priority check: only override if new source >= current source
    if (static_cast<int>(source) < static_cast<int>(it->second.source)) {
        return false;
    }

    // Validate before setting
    auto validation = ValidateProposed(key, value);
    if (!validation.valid) return false;

    ConfigValue oldValue = it->second.value;
    it->second.value = value;
    it->second.source = source;

    // Notify if value actually changed
    if (oldValue != value) {
        ConfigChangeEvent event{key, oldValue, value, source};
        // Unlock before notifying to avoid deadlock
        m_mutex.unlock();
        NotifyListeners(event);
        m_mutex.lock();
    }

    return true;
}

void ConfigManager::ForceSet(const std::string& key, ConfigValue value, ConfigSource source) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return;

    ConfigValue oldValue = it->second.value;
    it->second.value = value;
    it->second.source = source;

    if (oldValue != value) {
        ConfigChangeEvent event{key, oldValue, value, source};
        m_mutex.unlock();
        NotifyListeners(event);
        m_mutex.lock();
    }
}

void ConfigManager::ResetToDefault(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto defIt = m_defaults.find(key);
    if (defIt == m_defaults.end()) return;
    auto entryIt = m_entries.find(key);
    if (entryIt == m_entries.end()) return;

    ConfigValue oldValue = entryIt->second.value;
    entryIt->second.value = defIt->second;
    entryIt->second.source = ConfigSource::Default;

    if (oldValue != defIt->second) {
        ConfigChangeEvent event{key, oldValue, defIt->second, ConfigSource::Default};
        m_mutex.unlock();
        NotifyListeners(event);
        m_mutex.lock();
    }
}

void ConfigManager::ResetAllToDefaults() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, entry] : m_entries) {
        auto defIt = m_defaults.find(key);
        if (defIt != m_defaults.end()) {
            entry.value = defIt->second;
            entry.source = ConfigSource::Default;
        }
    }
}

void ConfigManager::ResetCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, entry] : m_entries) {
        if (entry.category != category) continue;
        auto defIt = m_defaults.find(key);
        if (defIt != m_defaults.end()) {
            entry.value = defIt->second;
            entry.source = ConfigSource::Default;
        }
    }
}

// ─── Bulk Operations ──────────────────────────────────────────────────────────

std::vector<std::string> ConfigManager::GetAllKeys() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> keys;
    keys.reserve(m_entries.size());
    for (const auto& [k, _] : m_entries) keys.push_back(k);
    return keys;
}

std::vector<std::string> ConfigManager::GetKeysByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> keys;
    for (const auto& [k, entry] : m_entries) {
        if (entry.category == category) keys.push_back(k);
    }
    return keys;
}

std::vector<std::string> ConfigManager::GetCategories() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> cats;
    for (const auto& [_, entry] : m_entries) {
        if (std::find(cats.begin(), cats.end(), entry.category) == cats.end()) {
            cats.push_back(entry.category);
        }
    }
    return cats;
}

bool ConfigManager::ApplyOverrides(const std::unordered_map<std::string, ConfigValue>& overrides,
                                    ConfigSource source) {
    // Validate all first
    for (const auto& [key, value] : overrides) {
        if (!HasKey(key)) return false;
        auto validation = ValidateProposed(key, value);
        if (!validation.valid) return false;
    }

    // Apply all
    for (const auto& [key, value] : overrides) {
        Set(key, value, source);
    }
    return true;
}

// ─── Validation ───────────────────────────────────────────────────────────────

ValidationResult ConfigManager::ValidateKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return {false, {"Key not found: " + key}, {}};
    }
    return ValidateProposed(key, it->second.value);
}

ValidationResult ConfigManager::ValidateAll() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    ValidationResult result{true, {}, {}};
    for (const auto& [key, entry] : m_entries) {
        auto r = ValidateProposed(key, entry.value);
        if (!r.valid) {
            result.valid = false;
            for (auto& err : r.errors) result.errors.push_back(key + ": " + err);
        }
        for (auto& warn : r.warnings) result.warnings.push_back(key + ": " + warn);
    }

    // Cross-key validation: scoring weights should sum to ~1.0
    double weightSum = 0.0;
    std::vector<std::string> weightKeys = {
        "scoring.fpsWeight", "scoring.stabilityWeight",
        "scoring.memoryWeight", "scoring.thermalWeight", "scoring.networkWeight"
    };
    for (const auto& wk : weightKeys) {
        auto it = m_entries.find(wk);
        if (it != m_entries.end()) {
            if (const double* v = std::get_if<double>(&it->second.value)) {
                weightSum += *v;
            }
        }
    }
    if (std::abs(weightSum - 1.0) > 0.05) {
        result.warnings.push_back("Scoring weights sum to " +
            std::to_string(weightSum) + " (expected ~1.0)");
    }

    return result;
}

ValidationResult ConfigManager::ValidateProposed(const std::string& key,
                                                  const ConfigValue& value) const {
    // Note: caller must hold m_mutex or this is a const read operation
    ValidationResult result{true, {}, {}};

    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        result.valid = false;
        result.errors.push_back("Key not registered: " + key);
        return result;
    }

    const ConfigEntry& entry = it->second;

    // Type check: value must match entry type
    if (value.index() != entry.value.index()) {
        // Allow int→double promotion
        if (!(std::holds_alternative<int>(value) && std::holds_alternative<double>(entry.value))) {
            result.valid = false;
            result.errors.push_back("Type mismatch for key: " + key);
            return result;
        }
    }

    // Range check
    if (entry.minValue.has_value()) {
        if (const double* minD = std::get_if<double>(&entry.minValue.value())) {
            double valD = 0.0;
            if (const double* v = std::get_if<double>(&value)) valD = *v;
            else if (const int* v = std::get_if<int>(&value)) valD = static_cast<double>(*v);
            if (valD < *minD) {
                result.valid = false;
                result.errors.push_back("Value below minimum (" + std::to_string(valD) +
                    " < " + std::to_string(*minD) + ")");
            }
        }
        if (const int* minI = std::get_if<int>(&entry.minValue.value())) {
            if (const int* v = std::get_if<int>(&value)) {
                if (*v < *minI) {
                    result.valid = false;
                    result.errors.push_back("Value below minimum (" + std::to_string(*v) +
                        " < " + std::to_string(*minI) + ")");
                }
            }
        }
    }

    if (entry.maxValue.has_value()) {
        if (const double* maxD = std::get_if<double>(&entry.maxValue.value())) {
            double valD = 0.0;
            if (const double* v = std::get_if<double>(&value)) valD = *v;
            else if (const int* v = std::get_if<int>(&value)) valD = static_cast<double>(*v);
            if (valD > *maxD) {
                result.valid = false;
                result.errors.push_back("Value above maximum (" + std::to_string(valD) +
                    " > " + std::to_string(*maxD) + ")");
            }
        }
        if (const int* maxI = std::get_if<int>(&entry.maxValue.value())) {
            if (const int* v = std::get_if<int>(&value)) {
                if (*v > *maxI) {
                    result.valid = false;
                    result.errors.push_back("Value above maximum (" + std::to_string(*v) +
                        " > " + std::to_string(*maxI) + ")");
                }
            }
        }
    }

    // Allowed values check
    if (!entry.allowedValues.empty()) {
        bool found = false;
        for (const auto& allowed : entry.allowedValues) {
            if (value == allowed) { found = true; break; }
        }
        if (!found) {
            result.valid = false;
            result.errors.push_back("Value not in allowed set for key: " + key);
        }
    }

    return result;
}

// ─── Observers ────────────────────────────────────────────────────────────────

int ConfigManager::AddChangeListener(ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextListenerId++;
    m_listeners[id] = std::move(callback);
    return id;
}

void ConfigManager::RemoveChangeListener(int listenerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.erase(listenerId);
}

int ConfigManager::AddKeyListener(const std::string& key, ConfigChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextListenerId++;
    m_keyListeners[key][id] = std::move(callback);
    return id;
}

void ConfigManager::RemoveKeyListener(int listenerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, listeners] : m_keyListeners) {
        listeners.erase(listenerId);
    }
}

void ConfigManager::NotifyListeners(const ConfigChangeEvent& event) {
    // Global listeners
    for (auto& [id, cb] : m_listeners) {
        try { cb(event); } catch (...) {}
    }
    // Key-specific listeners
    auto kit = m_keyListeners.find(event.key);
    if (kit != m_keyListeners.end()) {
        for (auto& [id, cb] : kit->second) {
            try { cb(event); } catch (...) {}
        }
    }
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

bool ConfigManager::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    return ImportFromJSON(content, ConfigSource::File);
}

bool ConfigManager::SaveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << ExportToJSON();
    file.close();
    return true;
}

bool ConfigManager::SaveOverridesToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << ExportOverridesToJSON();
    file.close();
    return true;
}

// ─── Presets ──────────────────────────────────────────────────────────────────

bool ConfigManager::ApplyPreset(const std::string& presetName) {
    auto it = m_presets.find(presetName);
    if (it == m_presets.end()) return false;

    // Reset to defaults first, then apply preset overrides
    ResetAllToDefaults();

    for (const auto& [key, value] : it->second.overrides) {
        ForceSet(key, value, ConfigSource::Runtime);
    }
    return true;
}

std::vector<std::string> ConfigManager::GetPresetNames() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : m_presets) names.push_back(name);
    return names;
}

std::optional<ConfigPreset> ConfigManager::GetPreset(const std::string& name) const {
    auto it = m_presets.find(name);
    if (it == m_presets.end()) return std::nullopt;
    return it->second;
}

void ConfigManager::RegisterPreset(const ConfigPreset& preset) {
    m_presets[preset.name] = preset;
}

// ─── Diff & Export ────────────────────────────────────────────────────────────

std::vector<std::string> ConfigManager::GetNonDefaultKeys() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (const auto& [key, entry] : m_entries) {
        auto defIt = m_defaults.find(key);
        if (defIt != m_defaults.end() && entry.value != defIt->second) {
            result.push_back(key);
        }
    }
    return result;
}

std::string ConfigManager::ExportToJSON() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "{\n";
    bool first = true;
    for (const auto& [key, entry] : m_entries) {
        if (!first) ss << ",\n";
        first = false;
        ss << "  \"" << EscapeJSONString(key) << "\": " << ValueToJSON(entry.value);
    }
    ss << "\n}\n";
    return ss.str();
}

std::string ConfigManager::ExportOverridesToJSON() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "{\n";
    bool first = true;
    for (const auto& [key, entry] : m_entries) {
        auto defIt = m_defaults.find(key);
        if (defIt == m_defaults.end() || entry.value == defIt->second) continue;
        if (!first) ss << ",\n";
        first = false;
        ss << "  \"" << EscapeJSONString(key) << "\": " << ValueToJSON(entry.value);
    }
    ss << "\n}\n";
    return ss.str();
}

bool ConfigManager::ImportFromJSON(const std::string& json, ConfigSource source) {
    auto parsed = ParseFlatJSONObject(json);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& [key, jsonValue] : parsed) {
        auto it = m_entries.find(key);
        if (it == m_entries.end()) continue; // Skip unknown keys

        // Determine type from existing entry
        ConfigValue converted;
        const ConfigValue& typeHint = it->second.value;

        if (std::holds_alternative<bool>(typeHint)) {
            converted = (jsonValue == "true");
        } else if (std::holds_alternative<int>(typeHint)) {
            if (IsIntegerString(jsonValue)) {
                converted = std::stoi(jsonValue);
            } else if (IsDoubleString(jsonValue)) {
                converted = static_cast<int>(std::round(std::stod(jsonValue)));
            } else {
                continue;
            }
        } else if (std::holds_alternative<double>(typeHint)) {
            if (IsDoubleString(jsonValue) || IsIntegerString(jsonValue)) {
                converted = std::stod(jsonValue);
            } else {
                continue;
            }
        } else if (std::holds_alternative<std::string>(typeHint)) {
            // Strip surrounding quotes
            if (jsonValue.size() >= 2 && jsonValue.front() == '"' && jsonValue.back() == '"') {
                converted = jsonValue.substr(1, jsonValue.size() - 2);
            } else {
                converted = jsonValue;
            }
        } else {
            continue;
        }

        ForceSet(key, converted, source);
    }
    return true;
}

// ─── Debug ────────────────────────────────────────────────────────────────────

std::string ConfigManager::Dump() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "=== ConfigManager Dump ===\n\n";

    // Group by category
    std::unordered_map<std::string, std::vector<std::pair<std::string, const ConfigEntry*>>> byCategory;
    for (const auto& [key, entry] : m_entries) {
        byCategory[entry.category].emplace_back(key, &entry);
    }

    for (const auto& [cat, items] : byCategory) {
        ss << "--- [" << cat << "] ---\n";
        for (const auto& [key, entry] : items) {
            ss << "  " << key << " = ";

            // Value
            std::visit([&ss](const auto& v) { ss << v; }, entry->value);

            // Source
            const char* sourceStr = "default";
            if (entry->source == ConfigSource::File) sourceStr = "file";
            else if (entry->source == ConfigSource::Runtime) sourceStr = "runtime";
            ss << "  (source: " << sourceStr << ")";

            // Description
            if (!entry->description.empty()) {
                ss << "  // " << entry->description;
            }
            ss << "\n";
        }
        ss << "\n";
    }

    // Presets
    ss << "--- Presets ---\n";
    for (const auto& [name, preset] : m_presets) {
        ss << "  " << name << ": " << preset.description
           << " (" << preset.overrides.size() << " overrides)\n";
    }

    return ss.str();
}

// ─── JSON Helpers ─────────────────────────────────────────────────────────────

std::string ConfigManager::ValueToJSON(const ConfigValue& val) const {
    if (const bool* v = std::get_if<bool>(&val)) return *v ? "true" : "false";
    if (const int* v = std::get_if<int>(&val)) return std::to_string(*v);
    if (const double* v = std::get_if<double>(&val)) {
        // Avoid unnecessary decimal places for round numbers
        std::ostringstream ss;
        ss << *v;
        return ss.str();
    }
    if (const std::string* v = std::get_if<std::string>(&val)) {
        return "\"" + EscapeJSONString(*v) + "\"";
    }
    return "null";
}

ConfigValue ConfigManager::JSONToValue(const std::string& jsonStr,
                                        const ConfigValue& typeHint) const {
    std::string trimmed = Trim(jsonStr);

    if (std::holds_alternative<bool>(typeHint)) {
        return trimmed == "true";
    } else if (std::holds_alternative<int>(typeHint)) {
        return std::stoi(trimmed);
    } else if (std::holds_alternative<double>(typeHint)) {
        return std::stod(trimmed);
    } else if (std::holds_alternative<std::string>(typeHint)) {
        if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
            return trimmed.substr(1, trimmed.size() - 2);
        }
        return trimmed;
    }
    return trimmed;
}

std::string ConfigManager::EscapeJSONString(const std::string& str) const {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

} // namespace ProfilerCore
