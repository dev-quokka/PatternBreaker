#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class JwtTokens
{
public:
    static constexpr int lifetimeSeconds = 3600;

    explicit JwtTokens(const std::string& secretHex);
    bool isConfigured() const noexcept;
    std::string issue(std::uint64_t userId) const;
    std::optional<std::uint64_t> verify(std::string_view token) const;
    std::optional<std::uint64_t> verifyBearer(std::string_view authorization) const;

private:
    std::string key_;
};
