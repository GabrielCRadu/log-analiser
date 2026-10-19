#include <doctest/doctest.h>

#include "admin_auth.hpp"
#include "alerts.hpp"

using namespace map_project;

namespace {

constexpr std::int64_t kWindow = 600;  // 10 minutes

std::string FiveErrorsLastAt(const std::string& last_time) {
    return std::string(
               "2026-03-14 09:00:00 ERROR one\n"
               "2026-03-14 09:02:00 ERROR two\n"
               "2026-03-14 09:05:00 ERROR three\n"
               "2026-03-14 09:07:00 ERROR four\n") +
           "2026-03-14 " + last_time + " ERROR five\n";
}

std::string Body(const std::string& content) {
    return nlohmann::json{{"content", content}}.dump();
}

std::string BasicHeader(const std::string& user_and_password) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int buffer = 0, bits = 0;
    for (const unsigned char c : user_and_password) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kAlphabet[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) out.push_back(kAlphabet[(buffer << (6 - bits)) & 0x3F]);
    while (out.size() % 4 != 0) out.push_back('=');
    return "Basic " + out;
}

}  // namespace

TEST_CASE("format timestamp is the inverse of the parser") {
    CHECK(FormatTimestamp(*ParseTimestamp("2026-03-14T09:03:00")) == "2026-03-14T09:03:00");
    CHECK(FormatTimestamp(*ParseTimestamp("2024-02-29T23:59:59")) == "2024-02-29T23:59:59");
    CHECK(FormatTimestamp(*ParseTimestamp("1970-01-01T00:00:00")) == "1970-01-01T00:00:00");
}

TEST_CASE("alerts: an error exactly T minutes after the first is inside the window") {
    LogStore store;
    PostLogs(store, Body(FiveErrorsLastAt("09:10:00")));

    const auto response = GetAlerts(store);

    REQUIRE(response.body.size() == 1);
    CHECK(response.body[0]["start"] == "2026-03-14T09:00:00");
    CHECK(response.body[0]["end"] == "2026-03-14T09:10:00");
    CHECK(response.body[0]["error_count"] == 5);
}

TEST_CASE("alerts: an error one second after the window is outside it") {
    LogStore store;
    PostLogs(store, Body(FiveErrorsLastAt("09:10:01")));

    CHECK(GetAlerts(store).body.empty());
}

TEST_CASE("alerts: four errors are not enough") {
    LogStore store;
    PostLogs(store, Body("2026-03-14 09:00:00 ERROR a\n2026-03-14 09:01:00 ERROR b\n"
                         "2026-03-14 09:02:00 ERROR c\n2026-03-14 09:03:00 ERROR d\n"));

    CHECK(GetAlerts(store).body.empty());
}

TEST_CASE("alerts: other levels do not count") {
    LogStore store;
    PostLogs(store, Body("2026-03-14 09:00:00 ERROR a\n2026-03-14 09:01:00 WARN b\n"
                         "2026-03-14 09:02:00 ERROR c\n2026-03-14 09:03:00 ERROR d\n"
                         "2026-03-14 09:04:00 INFO e\n2026-03-14 09:05:00 ERROR f\n"));

    CHECK(GetAlerts(store).body.empty());
}

TEST_CASE("alerts: an empty store has no alerts") {
    LogStore store;

    CHECK(GetAlerts(store).body.is_array());
    CHECK(GetAlerts(store).body.empty());
}

TEST_CASE("alerts: the window slides to the error that can still start an alert") {
    // The first error is too early to reach five; the window must restart at the second.
    const std::vector<std::int64_t> times = {0, 700, 800, 900, 1000, 1100};
    const auto alerts = FindAlerts(times, 5, kWindow);

    REQUIRE(alerts.size() == 1);
    CHECK(alerts[0].start == 700);
    CHECK(alerts[0].error_count == 5);
}

TEST_CASE("alerts: windows do not overlap, analysis resumes after the window ends") {
    // 5 errors in the first window, then 5 more starting after it: two alerts, not several.
    const std::vector<std::int64_t> times = {0, 100, 200, 300, 400,
                                             1000, 1100, 1200, 1300, 1400};
    const auto alerts = FindAlerts(times, 5, kWindow);

    REQUIRE(alerts.size() == 2);
    CHECK(alerts[0].start == 0);
    CHECK(alerts[1].start == 1000);
}

TEST_CASE("alerts: a long burst is reported once per window, not once per error") {
    std::vector<std::int64_t> times;
    for (int i = 0; i < 10; ++i) times.push_back(i * 60);  // 10 errors in 9 minutes

    const auto alerts = FindAlerts(times, 5, kWindow);

    REQUIRE(alerts.size() == 1);
    CHECK(alerts[0].error_count == 10);
}

TEST_CASE("alerts: the error right after the window end starts the next analysis") {
    // Window [0, 600]. The error at 601 is outside, so it is the next candidate start.
    const std::vector<std::int64_t> times = {0, 100, 200, 300, 400, 601, 700, 800, 900, 1000};
    const auto alerts = FindAlerts(times, 5, kWindow);

    REQUIRE(alerts.size() == 2);
    CHECK(alerts[1].start == 601);
}

TEST_CASE("base64 decoder handles padding and rejects garbage") {
    CHECK(DecodeBase64("YWRtaW46cGFzcw==") == "admin:pass");
    CHECK(DecodeBase64("YWRtaW46cGFzcz8=") == "admin:pass?");
    CHECK_FALSE(DecodeBase64("not base64!").has_value());
}

TEST_CASE("admin stats without credentials is 401 unauthorized") {
    LogStore store;
    const auto response = GetAdminStats(store, "", "admin", "secret");

    CHECK(response.status == 401);
    CHECK(response.body["error"] == "unauthorized");
}

TEST_CASE("admin stats with wrong credentials is 401") {
    LogStore store;

    CHECK(GetAdminStats(store, BasicHeader("admin:wrong"), "admin", "secret").status == 401);
    CHECK(GetAdminStats(store, BasicHeader("other:secret"), "admin", "secret").status == 401);
    CHECK(GetAdminStats(store, "Bearer abc", "admin", "secret").status == 401);
    CHECK(GetAdminStats(store, "Basic %%%", "admin", "secret").status == 401);
}

TEST_CASE("admin stats with valid credentials returns the statistics") {
    LogStore store;
    PostLogs(store, Body("2026-03-14 09:00:00 ERROR a"));

    const auto response = GetAdminStats(store, BasicHeader("admin:secret"), "admin", "secret");

    CHECK(response.status == 200);
    CHECK(response.body["total"] == 1);
}

TEST_CASE("admin password may contain a colon") {
    LogStore store;

    CHECK(GetAdminStats(store, BasicHeader("admin:se:cret"), "admin", "se:cret").status == 200);
}

TEST_CASE("admin stats is closed when the credentials are not configured") {
    LogStore store;

    CHECK(GetAdminStats(store, BasicHeader(":"), "", "").status == 401);
    CHECK(GetAdminStats(store, BasicHeader("admin:"), "admin", "").status == 401);
}
