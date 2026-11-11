#include "JwtTokens.h"

#include <jwt-cpp/jwt.h>
#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <stdexcept>

namespace
{
constexpr char issuer[] = "patternbreaker-server";
constexpr char audience[] = "patternbreaker-api";
}

JwtTokens::JwtTokens(const std::string& secretHex)
{
    std::array<unsigned char, 32> bytes{};
    if (secretHex.size() != bytes.size() * 2 ||
        !std::all_of(secretHex.begin(), secretHex.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }))
    {
        return;
    }

    std::size_t length = 0;
    if (OPENSSL_hexstr2buf_ex(bytes.data(), bytes.size(), &length, secretHex.c_str(), '\0') == 1 &&
        length == bytes.size() &&
        !std::all_of(bytes.begin(), bytes.end(), [&](unsigned char byte) { return byte == bytes.front(); }))
    {
        key_.assign(reinterpret_cast<const char*>(bytes.data()), length);
    }
    OPENSSL_cleanse(bytes.data(), bytes.size());
}

bool JwtTokens::isConfigured() const noexcept
{
    return !key_.empty();
}

std::string JwtTokens::issue(std::uint64_t userId) const
{
    if (!isConfigured() || userId == 0)
    {
        throw std::invalid_argument("JWT issuance unavailable");
    }

    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    return jwt::create()
        .set_type("JWT")
        .set_subject(std::to_string(userId))
        .set_issuer(issuer)
        .set_audience(audience)
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(lifetimeSeconds))
        .sign(jwt::algorithm::hs256{key_});
}

std::optional<std::uint64_t> JwtTokens::verify(std::string_view token) const
{
    if (!isConfigured() || token.empty() || token.size() > 2048)
    {
        return std::nullopt;
    }

    try
    {
        const auto decoded = jwt::decode(std::string(token));
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{key_})
            .with_issuer(issuer)
            .with_audience(audience)
            .verify(decoded);

        const auto now = std::chrono::system_clock::now();
        if (decoded.get_type() != "JWT" || !decoded.has_issued_at() || !decoded.has_expires_at() ||
            decoded.get_issued_at() > now || decoded.get_expires_at() <= now ||
            decoded.get_expires_at() <= decoded.get_issued_at())
        {
            return std::nullopt;
        }

        const auto subject = decoded.get_subject();
        std::uint64_t userId = 0;
        const auto end = subject.data() + subject.size();
        const auto parsed = std::from_chars(subject.data(), end, userId);
        if (parsed.ec == std::errc{} && parsed.ptr == end && userId != 0)
        {
            return userId;
        }
    }
    catch (const std::exception&)
    {
        // Malformed tokens and signature errors have the same response.
    }
    return std::nullopt;
}

std::optional<std::uint64_t> JwtTokens::verifyBearer(std::string_view authorization) const
{
    constexpr std::string_view scheme = "bearer ";
    if (authorization.size() <= scheme.size() ||
        !std::equal(scheme.begin(), scheme.end(), authorization.begin(), [](char expected, unsigned char actual) {
            return expected == std::tolower(actual);
        }))
    {
        return std::nullopt;
    }
    return verify(authorization.substr(scheme.size()));
}
