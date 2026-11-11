#include "RankingRoutes.h"

#include "JwtTokens.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <utility>

namespace
{
using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;

void respondJson(const ResponseCallback& respond, drogon::HttpStatusCode code, const Json::Value& body)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(code);
    response->addHeader("Cache-Control", "no-store");
    respond(response);
}

void respondStatus(const ResponseCallback& respond, drogon::HttpStatusCode code, const char* status)
{
    Json::Value body(Json::objectValue);
    body["status"] = status;
    respondJson(respond, code, body);
}

void respondDatabaseUnavailable(const ResponseCallback& respond)
{
    respondStatus(respond, drogon::k503ServiceUnavailable, "database_unavailable");
}

Json::Value bestRecordJson(const drogon::orm::Row& row)
{
    Json::Value body(Json::objectValue);
    body["elapsed_ms"] = Json::UInt(row["elapsed_ms"].as<unsigned int>());
    body["cleared_at"] = row["cleared_at"].as<std::string>();
    return body;
}
}

void registerRankingRoutes(drogon::HttpAppFramework& app, const drogon::orm::DbClientPtr& client,
                           const std::shared_ptr<const JwtTokens>& tokens)
{
    app.registerHandler(
        "/ranking/top",
        [client](const drogon::HttpRequestPtr&, ResponseCallback&& respond) {
            if (!client || !client->hasAvailableConnections())
            {
                respondDatabaseUnavailable(respond);
                return;
            }

            client->execSqlAsync(
                "SELECT best.user_id, u.username, best.elapsed_ms, "
                "DATE_FORMAT(best.cleared_at, '%Y-%m-%dT%H:%i:%sZ') AS cleared_at "
                "FROM (SELECT user_id, elapsed_ms, cleared_at, clear_record_id FROM raid_best_records "
                "      ORDER BY elapsed_ms, cleared_at, clear_record_id LIMIT 100) AS best "
                "JOIN users AS u ON u.id = best.user_id "
                "ORDER BY best.elapsed_ms, best.cleared_at, best.clear_record_id",
                [respond](const drogon::orm::Result& result) {
                    Json::Value rankings(Json::arrayValue);
                    Json::UInt rank = 1;
                    for (const auto& row : result)
                    {
                        auto entry = bestRecordJson(row);
                        entry["rank"] = rank++;
                        entry["user_id"] = Json::UInt64(row["user_id"].as<unsigned long long>());
                        entry["username"] = row["username"].as<std::string>();
                        rankings.append(std::move(entry));
                    }
                    Json::Value body(Json::objectValue);
                    body["rankings"] = std::move(rankings);
                    respondJson(respond, drogon::k200OK, body);
                },
                [respond](const drogon::orm::DrogonDbException&) { respondDatabaseUnavailable(respond); });
        },
        {drogon::Get});

    app.registerHandler(
        "/ranking/me",
        [tokens, client](const drogon::HttpRequestPtr& request, ResponseCallback&& respond) {
            if (!tokens->isConfigured())
            {
                respondStatus(respond, drogon::k503ServiceUnavailable, "auth_unavailable");
                return;
            }
            const auto userId = tokens->verifyBearer(request->getHeader("authorization"));
            if (!userId)
            {
                respondStatus(respond, drogon::k401Unauthorized, "invalid_token");
                return;
            }
            if (!client || !client->hasAvailableConnections())
            {
                respondDatabaseUnavailable(respond);
                return;
            }

            client->execSqlAsync(
                "SELECT u.username, best.elapsed_ms, "
                "DATE_FORMAT(best.cleared_at, '%Y-%m-%dT%H:%i:%sZ') AS cleared_at "
                "FROM users AS u LEFT JOIN raid_best_records AS best ON best.user_id = u.id WHERE u.id = ?",
                [respond, userId = *userId](const drogon::orm::Result& result) {
                    if (result.empty())
                    {
                        respondStatus(respond, drogon::k401Unauthorized, "invalid_token");
                        return;
                    }
                    const auto row = result[0];
                    Json::Value body(Json::objectValue);
                    body["user_id"] = Json::UInt64(userId);
                    body["username"] = row["username"].as<std::string>();
                    body["best_record"] = row["elapsed_ms"].isNull() ? Json::Value{} : bestRecordJson(row);
                    respondJson(respond, drogon::k200OK, body);
                },
                [respond](const drogon::orm::DrogonDbException&) { respondDatabaseUnavailable(respond); },
                static_cast<unsigned long long>(*userId));
        },
        {drogon::Get});
}
