#include <doctest/doctest.h>

#include "log_parser.hpp"

using namespace map_project;

TEST_CASE("parse line accepts the documented format") {
    const auto parsed = ParseLine("2026-03-14 09:31:07 ERROR Connection refused");

    REQUIRE(parsed.has_value());
    CHECK(parsed->timestamp == "2026-03-14T09:31:07");
    CHECK(parsed->level == LogLevel::Error);
    CHECK(parsed->message == "Connection refused");
}

TEST_CASE("parse line accepts all four levels") {
    CHECK(ParseLine("2026-03-14 09:00:00 DEBUG a")->level == LogLevel::Debug);
    CHECK(ParseLine("2026-03-14 09:00:00 INFO a")->level == LogLevel::Info);
    CHECK(ParseLine("2026-03-14 09:00:00 WARN a")->level == LogLevel::Warn);
    CHECK(ParseLine("2026-03-14 09:00:00 ERROR a")->level == LogLevel::Error);
}

TEST_CASE("parse line rejects an unknown or lowercase level") {
    CHECK_FALSE(ParseLine("2026-03-14 09:00:00 FATAL a").has_value());
    CHECK_FALSE(ParseLine("2026-03-14 09:00:00 error a").has_value());
}

TEST_CASE("parse line rejects an impossible calendar date") {
    CHECK_FALSE(ParseLine("2026-02-30 09:00:00 INFO a").has_value());
    CHECK_FALSE(ParseLine("2026-13-01 09:00:00 INFO a").has_value());
    CHECK_FALSE(ParseLine("2026-00-10 09:00:00 INFO a").has_value());
}

TEST_CASE("parse line handles leap years") {
    CHECK(ParseLine("2024-02-29 09:00:00 INFO a").has_value());
    CHECK_FALSE(ParseLine("2026-02-29 09:00:00 INFO a").has_value());
}

TEST_CASE("parse line rejects an invalid time") {
    CHECK_FALSE(ParseLine("2026-03-14 24:00:00 INFO a").has_value());
    CHECK_FALSE(ParseLine("2026-03-14 09:60:00 INFO a").has_value());
    CHECK_FALSE(ParseLine("2026-03-14 09:00:60 INFO a").has_value());
}

TEST_CASE("parse line rejects a line without a message") {
    CHECK_FALSE(ParseLine("2026-03-14 09:00:00 INFO").has_value());
    CHECK_FALSE(ParseLine("2026-03-14 09:00:00 INFO ").has_value());
}

TEST_CASE("parse line rejects garbage") {
    CHECK_FALSE(ParseLine("").has_value());
    CHECK_FALSE(ParseLine("this line does not match the format").has_value());
}

TEST_CASE("parse log counts accepted and ignored lines") {
    const auto result = ParseLog(
        "2026-03-14 09:00:00 ERROR one\n"
        "garbage\n"
        "2026-03-14 09:01:00 INFO two\n");

    CHECK(result.lines.size() == 2);
    CHECK(result.ignored == 1);
}

TEST_CASE("parse log does not count the empty line after the last newline") {
    const auto result = ParseLog("2026-03-14 09:00:00 INFO one\nnot a log line\n");

    CHECK(result.lines.size() == 1);
    CHECK(result.ignored == 1);
}

TEST_CASE("parse log handles windows line endings") {
    const auto result = ParseLog("2026-03-14 09:00:00 INFO one\r\n2026-03-14 09:01:00 INFO two\r\n");

    CHECK(result.lines.size() == 2);
    CHECK(result.lines[0].message == "one");
    CHECK(result.ignored == 0);
}

TEST_CASE("parse log of empty content is empty") {
    const auto result = ParseLog("");

    CHECK(result.lines.empty());
    CHECK(result.ignored == 0);
}

TEST_CASE("epoch differs by exactly the number of seconds between lines") {
    const auto first = ParseLine("2026-03-14 09:00:00 ERROR a");
    const auto second = ParseLine("2026-03-14 09:10:00 ERROR b");
    const auto next_day = ParseLine("2026-03-15 09:00:00 ERROR c");

    CHECK(second->epoch - first->epoch == 600);
    CHECK(next_day->epoch - first->epoch == 86400);
}
