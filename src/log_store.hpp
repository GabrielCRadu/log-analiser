// In-memory storage for parsed log lines, plus the logic behind
// POST /logs and GET /entries. No HTTP types here, so it can be unit tested.
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app.hpp"
#include "log_parser.hpp"

namespace map_project {

/// A stored line: the parsed data plus the id assigned by the server.
struct LogEntry {
    int id = 0;
    ParsedLine line;
};

/// A status code and a JSON body, ready to be sent by the HTTP layer.
struct ApiResponse {
    int status = 200;
    nlohmann::json body;
};

/// Parses "YYYY-MM-DDTHH:MM:SS" (optionally followed by "Z") into epoch seconds.
/// Returns nothing for any other shape or for an impossible date or time.
inline std::optional<std::int64_t> ParseTimestamp(const std::string& text) {
    if (text.size() == 20 && text.back() == 'Z') {
        return ParseTimestamp(text.substr(0, 19));
    }
    if (text.size() != 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }

    const auto year = detail::Digits(text, 0, 4);
    const auto month = detail::Digits(text, 5, 2);
    const auto day = detail::Digits(text, 8, 2);
    const auto hour = detail::Digits(text, 11, 2);
    const auto minute = detail::Digits(text, 14, 2);
    const auto second = detail::Digits(text, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;

    if (*month < 1 || *month > 12) return std::nullopt;
    if (*day < 1 || *day > detail::DaysInMonth(*year, *month)) return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 59) return std::nullopt;

    return detail::DaysFromCivil(*year, *month, *day) * 86400 + *hour * 3600 + *minute * 60 +
           *second;
}

/// Holds every accepted log line. Everything is lost on restart or /reset.
class LogStore {
public:
    void Reset() {
        std::lock_guard<std::mutex> guard(mutex_);
        entries_.clear();
        next_id_ = 1;
    }

    /// Stores the lines in the given order. Ids are consecutive, starting at 1.
    void AddAll(const std::vector<ParsedLine>& lines) {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& line : lines) {
            entries_.push_back(LogEntry{next_id_++, line});
        }
    }

    /// Entries matching the filters, in chronological order. Both ends of the
    /// time interval are inclusive. When two lines have the same time, the one
    /// with the smaller id comes first.
    std::vector<LogEntry> Query(std::optional<LogLevel> level,
                                std::optional<std::int64_t> from,
                                std::optional<std::int64_t> to) const {
        std::vector<LogEntry> result;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (const auto& entry : entries_) {
                if (level && entry.line.level != *level) continue;
                if (from && entry.line.epoch < *from) continue;
                if (to && entry.line.epoch > *to) continue;
                result.push_back(entry);
            }
        }
        std::stable_sort(result.begin(), result.end(), [](const LogEntry& a, const LogEntry& b) {
            if (a.line.epoch != b.line.epoch) return a.line.epoch < b.line.epoch;
            return a.id < b.id;
        });
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
    int next_id_ = 1;
};

inline nlohmann::json EntryToJson(const LogEntry& entry) {
    return {{"id", entry.id},
            {"timestamp", entry.line.timestamp},
            {"level", LevelToString(entry.line.level)},
            {"message", entry.line.message}};
}

/// POST /logs. The body must be a JSON object with a string field "content".
inline ApiResponse PostLogs(LogStore& store, const std::string& raw_body) {
    const auto body = nlohmann::json::parse(raw_body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return {400, ErrorBody("validation", "body must be a JSON object")};
    }
    const auto content = body.find("content");
    if (content == body.end() || !content->is_string()) {
        return {400, ErrorBody("validation", "content is required and must be a string")};
    }

    const ParseResult parsed = ParseLog(content->get<std::string>());
    store.AddAll(parsed.lines);
    return {201, {{"accepted", static_cast<int>(parsed.lines.size())},
                  {"ignored", parsed.ignored}}};
}

/// GET /entries. Each filter is optional; a filter that is present but invalid is a 400.
inline ApiResponse GetEntries(const LogStore& store,
                              const std::optional<std::string>& level_param,
                              const std::optional<std::string>& from_param,
                              const std::optional<std::string>& to_param) {
    std::optional<LogLevel> level;
    if (level_param) {
        level = LevelFromString(*level_param);
        if (!level) {
            return {400, ErrorBody("validation", "level must be DEBUG, INFO, WARN or ERROR")};
        }
    }

    std::optional<std::int64_t> from;
    if (from_param) {
        from = ParseTimestamp(*from_param);
        if (!from) return {400, ErrorBody("validation", "from must be an ISO 8601 timestamp")};
    }

    std::optional<std::int64_t> to;
    if (to_param) {
        to = ParseTimestamp(*to_param);
        if (!to) return {400, ErrorBody("validation", "to must be an ISO 8601 timestamp")};
    }

    nlohmann::json list = nlohmann::json::array();
    for (const auto& entry : store.Query(level, from, to)) {
        list.push_back(EntryToJson(entry));
    }
    return {200, list};
}

}  // namespace map_project
