// HTTP Basic authentication for GET /admin/stats.
// Credentials come from ADMIN_USER and ADMIN_PASSWORD, never from the code.
#pragma once

#include <optional>
#include <string>

#include "app.hpp"
#include "log_store.hpp"

namespace map_project {

/// Decodes standard base64. Returns nothing if the text is not valid base64.
inline std::optional<std::string> DecodeBase64(const std::string& text) {
    std::string output;
    int buffer = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (const char c : text) {
        int value;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+') value = 62;
        else if (c == '/') value = 63;
        else if (c == '=') { ++padding; continue; }
        else return std::nullopt;
        if (padding > 0) return std::nullopt;  // data after padding
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return output;
}

/// Compares two strings without stopping at the first difference.
inline bool SameText(const std::string& a, const std::string& b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

/// True only when the header is "Basic <base64 of user:password>" and both match.
/// If the expected user or password is empty, nobody gets in.
inline bool CheckBasicAuth(const std::string& header, const std::string& expected_user,
                           const std::string& expected_password) {
    if (expected_user.empty() || expected_password.empty()) return false;

    const std::string prefix = "Basic ";
    if (header.compare(0, prefix.size(), prefix) != 0) return false;

    const auto decoded = DecodeBase64(header.substr(prefix.size()));
    if (!decoded) return false;

    // The password may contain ':', so split on the first one only.
    const std::size_t colon = decoded->find(':');
    if (colon == std::string::npos) return false;

    const bool user_ok = SameText(decoded->substr(0, colon), expected_user);
    const bool password_ok = SameText(decoded->substr(colon + 1), expected_password);
    return user_ok && password_ok;
}

/// GET /admin/stats: the global statistics, but only with valid credentials.
inline ApiResponse GetAdminStats(const LogStore& store, const std::string& authorization,
                                 const std::string& expected_user,
                                 const std::string& expected_password) {
    if (!CheckBasicAuth(authorization, expected_user, expected_password)) {
        return {401, ErrorBody("unauthorized", "valid credentials are required")};
    }
    return GetStats(store);
}

}  // namespace map_project
