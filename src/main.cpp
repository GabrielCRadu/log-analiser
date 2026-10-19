// Wires the HTTP routes of the common contract.
// Add the routes required by your assigned theme below.

#include <chrono>
#include <iostream>

#include <httplib.h>

#include "app.hpp"
#include "log_store.hpp"

int main() {
    using namespace map_project;

    const auto started_at = std::chrono::steady_clock::now();
    Store store;
    LogStore logs;
    httplib::Server server;

    server.Get("/health", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(HealthBody(started_at).dump(), "application/json; charset=utf-8");
    });

    server.Get("/version", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(VersionBody().dump(), "application/json; charset=utf-8");
    });

    server.Post("/reset", [&](const httplib::Request&, httplib::Response& response) {
        store.Reset();
        logs.Reset();
        response.status = 204;
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(HomePage(), "text/html; charset=utf-8");
    });

    server.Post("/logs", [&](const httplib::Request& request, httplib::Response& response) {
        const ApiResponse result = PostLogs(logs, request.body);
        response.status = result.status;
        response.set_content(result.body.dump(), "application/json; charset=utf-8");
    });

    server.Get("/entries", [&](const httplib::Request& request, httplib::Response& response) {
        const auto param = [&](const char* name) -> std::optional<std::string> {
            if (!request.has_param(name)) return std::nullopt;
            return request.get_param_value(name);
        };
        const ApiResponse result = GetEntries(logs, param("level"), param("from"), param("to"));
        response.status = result.status;
        response.set_content(result.body.dump(), "application/json; charset=utf-8");
    });

    server.Get("/stats", [&](const httplib::Request&, httplib::Response& response) {
        const ApiResponse result = GetStats(logs);
        response.status = result.status;
        response.set_content(result.body.dump(), "application/json; charset=utf-8");
    });

    server.set_error_handler([](const httplib::Request&, httplib::Response& response) {
        if (response.status == 404) {
            response.set_content(ErrorBody("not_found", "route does not exist").dump(),
                                 "application/json; charset=utf-8");
        }
    });

    std::cout << kAppName << " " << kAppVersion << " starting on port 8080" << std::endl;
    if (!server.listen("0.0.0.0", 8080)) {
        std::cerr << "failed to bind port 8080" << std::endl;
        return 1;
    }
    return 0;
}