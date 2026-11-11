#include "Registration.h"
#include "JwtTokens.h"

#include <argon2.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;

class PasswordWorkQueue
{
public:
    bool tryEnqueue(std::function<void()> task)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (workers_.getTaskCount() >= 16)
        {
            return false;
        }
        workers_.runTaskInQueue(std::move(task));
        return true;
    }

private:
    std::mutex mutex_;
    trantor::ConcurrentTaskQueue workers_{2, "password-work"};
};

class LoginRateLimit
{
public:
    bool tryConsume()
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        while (!attempts_.empty() && now - attempts_.front() >= std::chrono::minutes(1))
        {
            attempts_.pop_front();
        }
        if (attempts_.size() >= 10)
        {
            return false;
        }
        attempts_.push_back(now);
        return true;
    }

private:
    std::mutex mutex_;
    std::deque<std::chrono::steady_clock::time_point> attempts_;
};

void respondWithJson(const ResponseCallback& respond, drogon::HttpStatusCode code, const Json::Value& body)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(code);
    response->addHeader("Cache-Control", "no-store");
    respond(response);
}

void respondWithStatus(const ResponseCallback& respond, drogon::HttpStatusCode code, const char* status)
{
    Json::Value body(Json::objectValue);
    body["status"] = status;
    respondWithJson(respond, code, body);
}

bool readCredentials(const drogon::HttpRequestPtr& request, std::string& username, std::string& password,
                     std::size_t minimumPasswordBytes)
{
    const auto body = request->body();
    if (request->contentType() != drogon::CT_APPLICATION_JSON || body.empty() || body.size() > 4096)
    {
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["stackLimit"] = 8;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value data;
    try
    {
        if (!reader->parse(body.data(), body.data() + body.size(), &data, nullptr))
        {
            return false;
        }
    }
    catch (const Json::Exception&)
    {
        return false;
    }
    if (!data.isObject() || !data["username"].isString() || !data["password"].isString())
    {
        return false;
    }

    username = data["username"].asString();
    password = data["password"].asString();
    return username.size() >= 3 && username.size() <= 32 &&
           std::all_of(username.begin(), username.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
           }) &&
           password.size() >= minimumPasswordBytes && password.size() <= 128 &&
           std::none_of(password.begin(), password.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

std::string hashPassword(std::string_view password)
{
    constexpr unsigned int iterations = 2;
    constexpr unsigned int memoryKiB = 19 * 1024;
    constexpr unsigned int parallelism = 1;
    constexpr unsigned int hashBytes = 32;
    std::array<unsigned char, 16> salt{};
    std::array<char, 255> encoded{};

    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
        argon2id_hash_encoded(iterations, memoryKiB, parallelism, password.data(), password.size(),
                              salt.data(), salt.size(), hashBytes, encoded.data(), encoded.size()) != ARGON2_OK)
    {
        return {};
    }
    return encoded.data();
}

void sendLoginToken(const ResponseCallback& respond, const JwtTokens& tokens,
                    std::uint64_t userId, const std::string& username)
{
    try
    {
        Json::Value body(Json::objectValue);
        body["user_id"] = Json::UInt64(userId);
        body["username"] = username;
        body["access_token"] = tokens.issue(userId);
        body["token_type"] = "Bearer";
        body["expires_in"] = JwtTokens::lifetimeSeconds;
        respondWithJson(respond, drogon::k200OK, body);
    }
    catch (const std::exception&)
    {
        respondWithStatus(respond, drogon::k503ServiceUnavailable, "auth_unavailable");
    }
}

}

void registerAuthRoutes(drogon::HttpAppFramework& app, const drogon::orm::DbClientPtr& client,
                        const std::shared_ptr<const JwtTokens>& tokens)
{
    const auto queue = std::make_shared<PasswordWorkQueue>();
    const auto limiter = std::make_shared<LoginRateLimit>();
    const auto dummyHash = std::make_shared<const std::string>(
        tokens->isConfigured() ? hashPassword("patternbreaker-absent-user-check") : std::string{});
    app.registerHandler(
        "/auth/register",
        [client, queue](const drogon::HttpRequestPtr& request, ResponseCallback&& respond)
        {
            std::string username;
            std::string password;
            if (!readCredentials(request, username, password, 12))
            {
                respondWithStatus(respond, drogon::k400BadRequest, "invalid_request");
                return;
            }
            if (!client || !client->hasAvailableConnections())
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                return;
            }

            const bool queued = queue->tryEnqueue(
                [client, respond, username = std::move(username), password = std::move(password)]() mutable
                {
                    auto hash = hashPassword(password);
                    OPENSSL_cleanse(password.data(), password.size());
                    if (hash.empty())
                    {
                        respondWithStatus(respond, drogon::k503ServiceUnavailable, "registration_unavailable");
                        return;
                    }

                    client->execSqlAsync(
                        "INSERT INTO users (username, password_hash) VALUES (?, ?)",
                        [respond](const drogon::orm::Result&) {
                            respondWithStatus(respond, drogon::k201Created, "registered");
                        },
                        [respond](const drogon::orm::DrogonDbException& error) {
                            const auto* sqlError = dynamic_cast<const drogon::orm::SqlError*>(&error.base());
                            if (sqlError && sqlError->errcode() == 1062)
                            {
                                respondWithStatus(respond, drogon::k409Conflict, "username_taken");
                                return;
                            }
                            LOG_WARN << "Registration database operation failed, code "
                                     << (sqlError ? sqlError->errcode() : 0);
                            respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                        },
                        std::move(username), std::move(hash));
                });
            if (!queued)
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "registration_busy");
            }
        },
        {drogon::Post});

    app.registerHandler(
        "/auth/login",
        [client, queue, tokens, limiter, dummyHash](const drogon::HttpRequestPtr& request, ResponseCallback&& respond)
        {
            std::string username;
            std::string password;
            if (!readCredentials(request, username, password, 1))
            {
                respondWithStatus(respond, drogon::k400BadRequest, "invalid_request");
                return;
            }
            if (!tokens->isConfigured() || dummyHash->empty())
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "auth_unavailable");
                return;
            }
            if (!client || !client->hasAvailableConnections())
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                return;
            }
            if (!limiter->tryConsume())
            {
                respondWithStatus(respond, drogon::k429TooManyRequests, "too_many_attempts");
                return;
            }

            client->execSqlAsync(
                "SELECT id, username, password_hash FROM users WHERE username = ? LIMIT 1",
                [queue, tokens, dummyHash, respond, password = std::move(password)](const drogon::orm::Result& result) mutable {
                    const auto userId = result.empty() ? 0ULL : result[0]["id"].as<unsigned long long>();
                    auto canonicalUsername = result.empty() ? std::string{} : result[0]["username"].as<std::string>();
                    auto hash = result.empty() ? *dummyHash : result[0]["password_hash"].as<std::string>();
                    const bool queued = queue->tryEnqueue(
                        [tokens, respond, userId, username = std::move(canonicalUsername),
                         hash = std::move(hash), password = std::move(password)]() mutable {
                            const bool matches = argon2id_verify(hash.c_str(), password.data(), password.size()) == ARGON2_OK;
                            OPENSSL_cleanse(password.data(), password.size());
                            if (!matches || userId == 0)
                            {
                                respondWithStatus(respond, drogon::k401Unauthorized, "invalid_credentials");
                                return;
                            }
                            sendLoginToken(respond, *tokens, userId, username);
                        });
                    if (!queued)
                    {
                        respondWithStatus(respond, drogon::k503ServiceUnavailable, "auth_busy");
                    }
                },
                [respond](const drogon::orm::DrogonDbException&) {
                    respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                },
                std::move(username));
        },
        {drogon::Post});

    app.registerHandler(
        "/auth/me",
        [client, tokens](const drogon::HttpRequestPtr& request, ResponseCallback&& respond)
        {
            if (!tokens->isConfigured())
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "auth_unavailable");
                return;
            }
            const auto userId = tokens->verifyBearer(request->getHeader("authorization"));
            if (!userId)
            {
                respondWithStatus(respond, drogon::k401Unauthorized, "invalid_token");
                return;
            }
            if (!client || !client->hasAvailableConnections())
            {
                respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                return;
            }

            client->execSqlAsync(
                "SELECT username FROM users WHERE id = ? LIMIT 1",
                [respond, userId = *userId](const drogon::orm::Result& result) {
                    if (result.empty())
                    {
                        respondWithStatus(respond, drogon::k401Unauthorized, "invalid_token");
                        return;
                    }
                    Json::Value body(Json::objectValue);
                    body["user_id"] = Json::UInt64(userId);
                    body["username"] = result[0]["username"].as<std::string>();
                    respondWithJson(respond, drogon::k200OK, body);
                },
                [respond](const drogon::orm::DrogonDbException&) {
                    respondWithStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
                },
                static_cast<unsigned long long>(*userId));
        },
        {drogon::Get});
}
