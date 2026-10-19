#include <doctest/doctest.h>

#include "log_store.hpp"

using namespace map_project;

namespace {

std::vector<ParsedLine> Lines(const std::string& content) {
    return ParseLog(content).lines;
}

}  // namespace

TEST_CASE("timestamp parser accepts the iso format with and without Z") {
    const auto plain = ParseTimestamp("2026-03-14T09:03:00");
    const auto zulu = ParseTimestamp("2026-03-14T09:03:00Z");

    REQUIRE(plain.has_value());
    REQUIRE(zulu.has_value());
    CHECK(*plain == *zulu);
}

TEST_CASE("timestamp parser agrees with the log line epoch") {
    const auto line = ParseLine("2026-03-14 09:03:00 INFO a");

    CHECK(ParseTimestamp("2026-03-14T09:03:00") == line->epoch);
}

TEST_CASE("timestamp parser rejects bad shapes and impossible dates") {
    CHECK_FALSE(ParseTimestamp("").has_value());
    CHECK_FALSE(ParseTimestamp("2026-03-14").has_value());
    CHECK_FALSE(ParseTimestamp("2026-03-14 09:03:00").has_value());
    CHECK_FALSE(ParseTimestamp("2026-02-30T09:03:00").has_value());
    CHECK_FALSE(ParseTimestamp("2026-03-14T25:00:00").has_value());
}

TEST_CASE("store ids start at one and are consecutive") {
    LogStore store;
    store.AddAll(Lines("2026-03-14 09:00:00 INFO a\n2026-03-14 09:01:00 INFO b\n"));

    const auto all = store.Query(std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(all.size() == 2);
    CHECK(all[0].id == 1);
    CHECK(all[1].id == 2);
}

TEST_CASE("reset empties the store and restarts the ids") {
    LogStore store;
    store.AddAll(Lines("2026-03-14 09:00:00 INFO a\n"));

    store.Reset();
    store.AddAll(Lines("2026-03-14 09:00:00 INFO b\n"));

    const auto all = store.Query(std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(all.size() == 1);
    CHECK(all[0].id == 1);
    CHECK(all[0].line.message == "b");
}

TEST_CASE("query filters by level") {
    LogStore store;
    store.AddAll(Lines("2026-03-14 09:00:00 INFO a\n2026-03-14 09:01:00 ERROR b\n"));

    const auto errors = store.Query(LogLevel::Error, std::nullopt, std::nullopt);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].line.message == "b");
}

TEST_CASE("query interval includes both ends") {
    LogStore store;
    store.AddAll(Lines(
        "2026-03-14 09:00:00 INFO a\n"
        "2026-03-14 09:05:00 INFO b\n"
        "2026-03-14 09:10:00 INFO c\n"
        "2026-03-14 09:10:01 INFO d\n"));

    const auto from = ParseTimestamp("2026-03-14T09:05:00");
    const auto to = ParseTimestamp("2026-03-14T09:10:00");
    const auto result = store.Query(std::nullopt, from, to);

    REQUIRE(result.size() == 2);
    CHECK(result[0].line.message == "b");
    CHECK(result[1].line.message == "c");
}

TEST_CASE("query returns chronological order even when lines arrive shuffled") {
    LogStore store;
    store.AddAll(Lines("2026-03-14 09:10:00 INFO late\n2026-03-14 09:00:00 INFO early\n"));

    const auto result = store.Query(std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(result.size() == 2);
    CHECK(result[0].line.message == "early");
    CHECK(result[1].line.message == "late");
}

TEST_CASE("query breaks ties by ascending id") {
    LogStore store;
    store.AddAll(Lines("2026-03-14 09:00:00 INFO second\n2026-03-14 09:00:00 INFO third\n"));

    const auto result = store.Query(std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(result.size() == 2);
    CHECK(result[0].id < result[1].id);
}

TEST_CASE("post logs reports accepted and ignored and returns 201") {
    LogStore store;
    const auto response = PostLogs(
        store, R"({"content":"2026-03-14 09:00:00 ERROR one\ngarbage\n2026-03-14 09:01:00 INFO two\n"})");

    CHECK(response.status == 201);
    CHECK(response.body["accepted"] == 2);
    CHECK(response.body["ignored"] == 1);
}

TEST_CASE("post logs ignored lines do not consume ids") {
    LogStore store;
    PostLogs(store, R"({"content":"garbage\n2026-03-14 09:00:00 INFO one\n"})");

    const auto all = store.Query(std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(all.size() == 1);
    CHECK(all[0].id == 1);
}

TEST_CASE("post logs rejects a missing content field") {
    LogStore store;
    const auto response = PostLogs(store, R"({"text":"hello"})");

    CHECK(response.status == 400);
    CHECK(response.body["error"] == "validation");
}

TEST_CASE("post logs rejects content that is not a string") {
    LogStore store;

    CHECK(PostLogs(store, R"({"content":5})").status == 400);
    CHECK(PostLogs(store, R"({"content":null})").status == 400);
}

TEST_CASE("post logs rejects a body that is not a json object") {
    LogStore store;

    CHECK(PostLogs(store, "not json").status == 400);
    CHECK(PostLogs(store, "[1,2]").status == 400);
    CHECK(PostLogs(store, "").status == 400);
}

TEST_CASE("post logs accepts empty content with nothing accepted") {
    LogStore store;
    const auto response = PostLogs(store, R"({"content":""})");

    CHECK(response.status == 201);
    CHECK(response.body["accepted"] == 0);
    CHECK(response.body["ignored"] == 0);
}

TEST_CASE("get entries rejects an unknown level") {
    LogStore store;
    const auto response = GetEntries(store, std::string("FATAL"), std::nullopt, std::nullopt);

    CHECK(response.status == 400);
    CHECK(response.body["error"] == "validation");
}

TEST_CASE("get entries rejects an invalid from or to") {
    LogStore store;

    CHECK(GetEntries(store, std::nullopt, std::string("yesterday"), std::nullopt).status == 400);
    CHECK(GetEntries(store, std::nullopt, std::nullopt, std::string("2026-13-01T00:00:00")).status == 400);
}

TEST_CASE("get entries on an empty store is an empty list") {
    LogStore store;
    const auto response = GetEntries(store, std::nullopt, std::nullopt, std::nullopt);

    CHECK(response.status == 200);
    CHECK(response.body.is_array());
    CHECK(response.body.empty());
}

TEST_CASE("get entries entry has id timestamp level and message") {
    LogStore store;
    PostLogs(store, R"({"content":"2026-03-14 09:31:07 ERROR Connection refused"})");

    const auto response = GetEntries(store, std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(response.body.size() == 1);
    CHECK(response.body[0]["id"] == 1);
    CHECK(response.body[0]["timestamp"] == "2026-03-14T09:31:07");
    CHECK(response.body[0]["level"] == "ERROR");
    CHECK(response.body[0]["message"] == "Connection refused");
}
