// Parsing of log lines: "YYYY-MM-DD HH:MM:SS LEVEL message".
// Pure logic, no HTTP, so it can be unit tested without starting a server.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace map_project {

enum class LogLevel { Debug, Info, Warn, Error };

inline const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

/// Returns the level for an exact, uppercase name, or nothing for anything else.
inline std::optional<LogLevel> LevelFromString(const std::string& name) {
    if (name == "DEBUG") return LogLevel::Debug;
    if (name == "INFO") return LogLevel::Info;
    if (name == "WARN") return LogLevel::Warn;
    if (name == "ERROR") return LogLevel::Error;
    return std::nullopt;
}

/// One successfully parsed line. The id is assigned later by the store.
struct ParsedLine {
    std::string timestamp;      // ISO 8601, e.g. "2026-03-14T09:31:07"
    std::int64_t epoch = 0;     // seconds, used for window arithmetic
    LogLevel level = LogLevel::Info;
    std::string message;
};

struct ParseResult {
    std::vector<ParsedLine> lines;
    int ignored = 0;
};

namespace detail {

inline bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

inline int DaysInMonth(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (month == 2 && IsLeapYear(year)) ? 29 : kDays[month - 1];
}

/// Days since 1970-01-01 for a valid civil date.
inline std::int64_t DaysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int year_of_era = year - era * 400;
    const int month_from_march = (month + 9) % 12;
    const int day_of_year = (153 * month_from_march + 2) / 5 + day - 1;
    const int day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
}

/// Reads exactly `count` digits starting at `pos`.
inline std::optional<int> Digits(const std::string& text, std::size_t pos, std::size_t count) {
    if (pos + count > text.size()) return std::nullopt;
    int value = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return std::nullopt;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

}  // namespace detail

/// Parses one line. Returns nothing when the line does not match the format
/// exactly: real calendar date, valid time, known level, non-empty message.
inline std::optional<ParsedLine> ParseLine(const std::string& line) {
    // "YYYY-MM-DD HH:MM:SS LEVEL message": the fixed part is 19 characters.
    if (line.size() < 20 || line[4] != '-' || line[7] != '-' || line[10] != ' ' ||
        line[13] != ':' || line[16] != ':' || line[19] != ' ') {
        return std::nullopt;
    }

    const auto year = detail::Digits(line, 0, 4);
    const auto month = detail::Digits(line, 5, 2);
    const auto day = detail::Digits(line, 8, 2);
    const auto hour = detail::Digits(line, 11, 2);
    const auto minute = detail::Digits(line, 14, 2);
    const auto second = detail::Digits(line, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;

    if (*month < 1 || *month > 12) return std::nullopt;
    if (*day < 1 || *day > detail::DaysInMonth(*year, *month)) return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 59) return std::nullopt;

    const std::size_t level_end = line.find(' ', 20);
    if (level_end == std::string::npos) return std::nullopt;
    const auto level = LevelFromString(line.substr(20, level_end - 20));
    if (!level) return std::nullopt;

    // Decision: a line with a level but no message is treated as invalid.
    std::string message = line.substr(level_end + 1);
    if (message.empty()) return std::nullopt;

    ParsedLine parsed;
    parsed.timestamp = line.substr(0, 10) + "T" + line.substr(11, 8);
    parsed.epoch = detail::DaysFromCivil(*year, *month, *day) * 86400 + *hour * 3600 +
                   *minute * 60 + *second;
    parsed.level = *level;
    parsed.message = std::move(message);
    return parsed;
}

/// Parses a whole log. Empty lines are skipped without counting; any other
/// line that does not match the format is counted in `ignored`.
inline ParseResult ParseLog(const std::string& content) {
    ParseResult result;
    std::size_t start = 0;
    while (start <= content.size()) {
        std::size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();

        std::string line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!line.empty()) {
            if (auto parsed = ParseLine(line)) {
                result.lines.push_back(std::move(*parsed));
            } else {
                ++result.ignored;
            }
        }
        start = end + 1;
    }
    return result;
}

}  // namespace map_project
