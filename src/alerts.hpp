// Detection of error bursts: GET /alerts.
// Pure logic, no HTTP, so it can be unit tested without starting a server.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "log_store.hpp"

namespace map_project {

/// An alert needs at least this many ERROR lines...
inline constexpr int kAlertMinErrors = 5;
/// ...inside a window of this many minutes.
inline constexpr int kAlertWindowMinutes = 10;

/// Formats epoch seconds as "YYYY-MM-DDTHH:MM:SS".
inline std::string FormatTimestamp(std::int64_t epoch) {
    std::int64_t days = epoch / 86400;
    std::int64_t seconds_of_day = epoch % 86400;
    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        --days;
    }

    // Inverse of DaysFromCivil.
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t day_of_era = days - era * 146097;
    const std::int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_from_march = (5 * day_of_year + 2) / 153;
    const int day = static_cast<int>(day_of_year - (153 * month_from_march + 2) / 5 + 1);
    const int month = static_cast<int>(month_from_march < 10 ? month_from_march + 3
                                                             : month_from_march - 9);
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
                  static_cast<int>(year), month, day, static_cast<int>(seconds_of_day / 3600),
                  static_cast<int>(seconds_of_day % 3600 / 60),
                  static_cast<int>(seconds_of_day % 60));
    return buffer;
}

struct Alert {
    std::int64_t start = 0;  // time of the first error in the window
    std::int64_t end = 0;    // end of the window: start plus the window length
    int error_count = 0;
};

/// Finds non-overlapping alert windows in a list of ERROR times, sorted ascending.
///
/// The window opens at an error and covers `window_seconds`, both ends included:
/// an error exactly `window_seconds` after the first one is inside. If the window
/// holds at least `min_errors` errors it is an alert, and the search resumes at the
/// first error after the window ends. Otherwise the window slides to the next error.
inline std::vector<Alert> FindAlerts(const std::vector<std::int64_t>& error_times,
                                     int min_errors, std::int64_t window_seconds) {
    std::vector<Alert> alerts;
    std::size_t i = 0;
    while (i < error_times.size()) {
        const std::int64_t start = error_times[i];
        const std::int64_t end = start + window_seconds;

        std::size_t j = i;
        while (j < error_times.size() && error_times[j] <= end) ++j;

        const int count = static_cast<int>(j - i);
        if (count >= min_errors) {
            alerts.push_back(Alert{start, end, count});
            i = j;
        } else {
            ++i;
        }
    }
    return alerts;
}

/// GET /alerts, with the fixed parameters K and T.
inline ApiResponse GetAlerts(const LogStore& store) {
    std::vector<std::int64_t> times;
    for (const auto& entry : store.Query(LogLevel::Error, std::nullopt, std::nullopt)) {
        times.push_back(entry.line.epoch);
    }

    nlohmann::json list = nlohmann::json::array();
    for (const auto& alert :
         FindAlerts(times, kAlertMinErrors, std::int64_t{kAlertWindowMinutes} * 60)) {
        list.push_back({{"start", FormatTimestamp(alert.start)},
                        {"end", FormatTimestamp(alert.end)},
                        {"error_count", alert.error_count}});
    }
    return {200, list};
}

}  // namespace map_project
