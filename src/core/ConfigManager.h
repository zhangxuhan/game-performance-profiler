#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <variant>
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace ProfilerCore {

// ─── Configuration Value Type ─────────────────────────────────────────────────

/**
 * A configuration value that can hold different types.
 * Supports: bool, int, double, string
 */
using ConfigValue = std::variant<bool, int, double, std::string>;

/**
 * Source of a configuration value, in priority order (highest last).
 */
enum class ConfigSource {
    Default = 0,     // Built-in default
    File = 1,        // Loaded from JSON config file
    Runtime = 2      // Set programmatically at runtime (highest priority)
};

/**
 * An entry in the config registry with its source tracking.
 */
struct ConfigEntry {
    ConfigValue value;
    ConfigSource source;
    std::string description;      // Human-readable description
    std::string category;         // Grouping category (e.g., "fps", "memory", "network")
    std::optional<ConfigValue> minValue;  // Optional range constraint
    std::optional<ConfigValue> maxValue;
    std::vector<ConfigValue> allowedValues; // Optional enum-like constraint
};

// ─── Config Change Event ──────────────────────────────────────────────────────

struct ConfigChangeEvent {
    std::string key;
    ConfigValue oldValue;
    ConfigValue newValue;
    ConfigSource source;
};

using ConfigChangeCallback = std::function<void(const ConfigChangeEvent&)>;

// ─── Config Preset ────────────────────────────────────────────────────────────

struct ConfigPreset {
    std::string name;
    std::string description;
    std::unordered_map<std::string, ConfigValue> overrides;
};

// ─── Validation Result ────────────────────────────────────────────────────────

struct ValidationResult {
    bool valid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// ─── Main Class ───────────────────────────────────────────────────────────────

/**
 * ConfigManager — centralized configuration management for the profiler.
 *
 * Features:
 * - Hierarchical config: defaults < file < runtime overrides
 * - Type-safe value access with optional range/enum validation
 * - Load/save JSON config files
 * - Observer pattern for config changes
 * - Built-in presets for common profiling scenarios
 * - Per-category bulk operations
 * - Config diff and export
 *
 * Usage:
 *   auto& cfg = ConfigManager::GetInstance();
 *   cfg.Register("fps.target", 60.0, "Target FPS", "fps");
 *   double targetFps = cfg.GetDouble("fps.target");
 *   cfg.Set("fps.target", 120.0, ConfigSource::Runtime);
 */
class ConfigManager {
public:
    static ConfigManager& GetInstance();

    // ─── Registration ────────────────────────────────────────────────────────

    /**
     * Register a configuration key with default value and metadata.
     * Calling Register on an existing key resets it to the given default.
     */
    void Register(const std::string& key, ConfigValue defaultValue,
                  const std::string& description = "",
                  const std::string& category = "general");

    /** Register with numeric range constraints. */
    void RegisterRanged(const std::string& key, ConfigValue defaultValue,
                        ConfigValue minVal, ConfigValue maxVal,
                        const std::string& description = "",
                        const std::string& category = "general");

    /** Register with allowed-values constraint (enum-like). */
    void RegisterEnum(const std::string& key, ConfigValue defaultValue,
                      const std::vector<ConfigValue>& allowed,
                      const std::string& description = "",
                      const std::string& category = "general");

    /** Unregister a config key. */
    void Unregister(const std::string& key);

    /** Check if a key is registered. */
    bool HasKey(const std::string& key) const;

    // ─── Getters ─────────────────────────────────────────────────────────────

    bool        GetBool(const std::string& key, bool fallback = false) const;
    int         GetInt(const std::string& key, int fallback = 0) const;
    double      GetDouble(const std::string& key, double fallback = 0.0) const;
    std::string GetString(const std::string& key, const std::string& fallback = "") const;

    /** Get the full entry including source and metadata. */
    std::optional<ConfigEntry> GetEntry(const std::string& key) const;

    /** Get the source of a config value. */
    ConfigSource GetSource(const std::string& key) const;

    // ─── Setters ─────────────────────────────────────────────────────────────

    /**
     * Set a config value with the given source.
     * Only applies if the new source has >= priority than the current source.
     * Returns true if the value was actually changed.
     */
    bool Set(const std::string& key, ConfigValue value,
             ConfigSource source = ConfigSource::Runtime);

    /**
     * Force-set a value regardless of source priority.
     * Use with caution — mainly for file loading.
     */
    void ForceSet(const std::string& key, ConfigValue value, ConfigSource source);

    /** Reset a key to its default value. */
    void ResetToDefault(const std::string& key);

    /** Reset all keys to defaults. */
    void ResetAllToDefaults();

    /** Reset all keys in a category to defaults. */
    void ResetCategory(const std::string& category);

    // ─── Bulk Operations ─────────────────────────────────────────────────────

    /** Get all keys. */
    std::vector<std::string> GetAllKeys() const;

    /** Get all keys in a category. */
    std::vector<std::string> GetKeysByCategory(const std::string& category) const;

    /** Get all categories. */
    std::vector<std::string> GetCategories() const;

    /** Apply multiple overrides at once (transactional — all or nothing if validation fails). */
    bool ApplyOverrides(const std::unordered_map<std::string, ConfigValue>& overrides,
                        ConfigSource source = ConfigSource::Runtime);

    // ─── Validation ──────────────────────────────────────────────────────────

    /** Validate a single key's current value. */
    ValidationResult ValidateKey(const std::string& key) const;

    /** Validate all registered keys. */
    ValidationResult ValidateAll() const;

    /** Validate a proposed value for a key without applying it. */
    ValidationResult ValidateProposed(const std::string& key, const ConfigValue& value) const;

    // ─── Observers ───────────────────────────────────────────────────────────

    /**
     * Register a callback for config changes.
     * Returns a listener ID that can be used to remove it.
     */
    int AddChangeListener(ConfigChangeCallback callback);

    /** Remove a listener by ID. */
    void RemoveChangeListener(int listenerId);

    /** Register a callback for changes to a specific key. */
    int AddKeyListener(const std::string& key, ConfigChangeCallback callback);

    /** Remove a key listener by ID. */
    void RemoveKeyListener(int listenerId);

    // ─── File I/O ────────────────────────────────────────────────────────────

    /** Load config from a JSON file. Merges with current (file source). */
    bool LoadFromFile(const std::string& filepath);

    /** Save current config to a JSON file. */
    bool SaveToFile(const std::string& filepath) const;

    /** Export only non-default values (useful for portable config). */
    bool SaveOverridesToFile(const std::string& filepath) const;

    // ─── Presets ─────────────────────────────────────────────────────────────

    /** Apply a built-in preset by name. */
    bool ApplyPreset(const std::string& presetName);

    /** Get list of available preset names. */
    std::vector<std::string> GetPresetNames() const;

    /** Get preset details. */
    std::optional<ConfigPreset> GetPreset(const std::string& name) const;

    /** Register a custom preset. */
    void RegisterPreset(const ConfigPreset& preset);

    // ─── Diff & Export ───────────────────────────────────────────────────────

    /** Get all keys whose current value differs from default. */
    std::vector<std::string> GetNonDefaultKeys() const;

    /** Export current config as JSON string. */
    std::string ExportToJSON() const;

    /** Export only non-default values as JSON string. */
    std::string ExportOverridesToJSON() const;

    /** Import config from a JSON string. */
    bool ImportFromJSON(const std::string& json, ConfigSource source = ConfigSource::File);

    // ─── Debug ───────────────────────────────────────────────────────────────

    /** Dump all config entries as a human-readable string. */
    std::string Dump() const;

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void InitializeDefaults();
    void InitializePresets();
    void NotifyListeners(const ConfigChangeEvent& event);

    // Simple JSON parser helpers (no external dependency)
    std::string ValueToJSON(const ConfigValue& val) const;
    ConfigValue JSONToValue(const std::string& jsonStr, const ConfigValue& typeHint) const;
    std::string EscapeJSONString(const std::string& str) const;

    mutable std::mutex m_mutex;

    // Config registry: key → entry
    std::unordered_map<std::string, ConfigEntry> m_entries;

    // Default values (kept separate for reset)
    std::unordered_map<std::string, ConfigValue> m_defaults;

    // Change listeners: id → callback
    std::unordered_map<int, ConfigChangeCallback> m_listeners;
    int m_nextListenerId = 1;

    // Key-specific listeners: key → {id → callback}
    std::unordered_map<std::string, std::unordered_map<int, ConfigChangeCallback>> m_keyListeners;

    // Presets: name → preset
    std::unordered_map<std::string, ConfigPreset> m_presets;
};

} // namespace ProfilerCore
