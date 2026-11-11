#include "JwtTokens.h"
#include "RaidRoutes.h"
#include "RankingRoutes.h"
#include "Registration.h"

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <trantor/utils/Logger.h>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;

std::string readEnvironmentVariable(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    const auto result = _dupenv_s(&value, nullptr, name);
    const std::unique_ptr<char, decltype(&std::free)> ownedValue(value, &std::free);
    return result == 0 && value ? std::string(value) : std::string{};
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

bool appendConnectionParameter(std::string& connectionInfo, std::string_view key, std::string_view value)
{
    if (value.empty() || value.find_first_of("\r\n") != std::string_view::npos)
    {
        return false;
    }

    connectionInfo.append(key);
    connectionInfo += "='";
    for (const char character : value)
    {
        if (character == '\'' || character == '\\')
        {
            connectionInfo += '\\';
        }
        connectionInfo += character;
    }
    // Drogon 1.9.13's MySQL connection parser matches quoted values on one line.
    connectionInfo += "'\n";
    return true;
}

drogon::orm::DbClientPtr createDatabaseClient()
{
    const auto portText = readEnvironmentVariable("PB_DB_PORT");
    int port = 0;
    const auto portEnd = portText.data() + portText.size();
    const auto parsedPort = std::from_chars(portText.data(), portEnd, port);

    std::string connectionInfo;
    if (parsedPort.ec != std::errc{} || parsedPort.ptr != portEnd || port < 1 || port > 65535 ||
        !appendConnectionParameter(connectionInfo, "host", readEnvironmentVariable("PB_DB_HOST")) ||
        !appendConnectionParameter(connectionInfo, "port", portText) ||
        !appendConnectionParameter(connectionInfo, "dbname", readEnvironmentVariable("PB_DB_NAME")) ||
        !appendConnectionParameter(connectionInfo, "user", readEnvironmentVariable("PB_DB_USER")) ||
        !appendConnectionParameter(connectionInfo, "password", readEnvironmentVariable("PB_DB_PASSWORD")))
    {
        LOG_WARN << "MySQL configuration is missing or invalid";
        return nullptr;
    }

    try
    {
        auto client = drogon::orm::DbClient::newMysqlClient(connectionInfo, 1);
        client->setTimeout(2.0);
        return client;
    }
    catch (const std::exception&)
    {
        LOG_WARN << "MySQL client initialization failed";
        return nullptr;
    }
}

drogon::HttpResponsePtr makeDatabaseHealthResponse(bool available)
{
    Json::Value body(Json::objectValue);
    body["database"] = available ? "ok" : "unavailable";
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(available ? drogon::k200OK : drogon::k503ServiceUnavailable);
    return response;
}
}

int main()
{
    // Drogon 1.9.13 can log expanded MySQL SQL and error text containing bound values.
    trantor::Logger::setOutputFunction(
        [](const char* message, const uint64_t length) {
            const std::string_view line(message, static_cast<std::size_t>(length));
            if (line.find("MysqlConnection.cc:") == std::string_view::npos)
            {
                std::fwrite(message, 1, line.size(), stdout);
            }
        },
        [] { std::fflush(stdout); });

    auto& app = drogon::app();
    const auto client = createDatabaseClient();
    app.registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr&,
           ResponseCallback&& respond)
        {
            Json::Value body(Json::objectValue);
            body["status"] = "ok";
            respond(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        {drogon::Get});

    app.registerHandler(
        "/health/db",
        [client](const drogon::HttpRequestPtr&, ResponseCallback&& respond)
        {
            if (!client || !client->hasAvailableConnections())
            {
                respond(makeDatabaseHealthResponse(false));
                return;
            }

            const auto reply = [respond = std::move(respond)](bool available)
            {
                respond(makeDatabaseHealthResponse(available));
            };
            client->execSqlAsync(
                "SELECT 1",
                [reply](const drogon::orm::Result&) { reply(true); },
                [reply](const drogon::orm::DrogonDbException&) { reply(false); });
        },
        {drogon::Get});

    const auto tokens = std::make_shared<const JwtTokens>(readEnvironmentVariable("PB_JWT_SECRET"));
    registerAuthRoutes(app, client, tokens);
    registerRaidRoutes(app, client, tokens);
    registerRankingRoutes(app, client, tokens);
    app.addListener("127.0.0.1", 18080).run();
}
