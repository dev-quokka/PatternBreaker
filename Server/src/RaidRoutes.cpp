#include "RaidRoutes.h"

#include "JwtTokens.h"
#include "RaidSessions.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <atomic>
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

std::optional<std::uint64_t> authorize(const JwtTokens& tokens, const drogon::HttpRequestPtr& request,
                                       const ResponseCallback& respond)
{
    if (!tokens.isConfigured())
    {
        respondStatus(respond, drogon::k503ServiceUnavailable, "auth_unavailable");
        return std::nullopt;
    }
    const auto userId = tokens.verifyBearer(request->getHeader("authorization"));
    if (!userId)
    {
        respondStatus(respond, drogon::k401Unauthorized, "invalid_token");
    }
    return userId;
}

struct FinishRequest
{
    std::string runId;
    bool clear;
};

std::optional<FinishRequest> readFinishRequest(const drogon::HttpRequestPtr& request)
{
    const auto body = request->body();
    if (request->contentType() != drogon::CT_APPLICATION_JSON || body.empty() || body.size() > 4096)
    {
        return std::nullopt;
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
            return std::nullopt;
        }
    }
    catch (const Json::Exception&)
    {
        return std::nullopt;
    }
    if (!data.isObject() || !data["run_id"].isString() || !data["result"].isString())
    {
        return std::nullopt;
    }

    auto runId = data["run_id"].asString();
    const auto result = data["result"].asString();
    if (runId.size() != 32 || !std::all_of(runId.begin(), runId.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }) || (result != "clear" && result != "dead"))
    {
        return std::nullopt;
    }
    return FinishRequest{std::move(runId), result == "clear"};
}

void saveClearRecord(const drogon::orm::DbClientPtr& client, const std::shared_ptr<RaidSessions>& sessions,
                     trantor::EventLoop& loop, const RaidSessions::SaveAttempt& attempt, ResponseCallback respond)
{
    const auto replied = std::make_shared<std::atomic_bool>(false);
    const auto reply = std::make_shared<ResponseCallback>(std::move(respond));
    const auto complete = [sessions, attempt, replied, reply](bool saved) {
        if (replied->exchange(true))
        {
            return;
        }

        const auto now = RaidSessions::Clock::now();
        const auto outcome = sessions->completeSave(attempt, saved, now);
        auto respondOnce = std::move(*reply);
        Json::Value body(Json::objectValue);
        body["run_id"] = attempt.record.runId;
        if (outcome == RaidSessions::SaveOutcome::Committed)
        {
            body["status"] = "clear";
            body["elapsed_ms"] = Json::UInt(attempt.record.elapsedMs);
            respondJson(respondOnce, drogon::k200OK, body);
        }
        else
        {
            body["status"] = "database_unavailable";
            body["retryable"] = outcome == RaidSessions::SaveOutcome::Retryable ||
                                (outcome == RaidSessions::SaveOutcome::Stale && now < attempt.retryUntil);
            respondJson(respondOnce, drogon::k503ServiceUnavailable, body);
        }
    };

    const auto remaining = attempt.deadline - RaidSessions::Clock::now();
    if (remaining <= RaidSessions::Clock::duration::zero() || !client || !client->hasAvailableConnections())
    {
        complete(false);
        return;
    }

    loop.runAfter(remaining, [complete] { complete(false); });
    const auto& record = attempt.record;
    // The procedure commits both writes before returning its only response.
    client->execSqlAsync(
        "CALL save_raid_clear(?, ?, ?, ?)",
        [complete](const drogon::orm::Result&) { complete(true); },
        [complete](const drogon::orm::DrogonDbException&) { complete(false); },
        record.runId, static_cast<unsigned long long>(record.userId), static_cast<unsigned int>(record.elapsedMs),
        static_cast<long long>(record.clearedAtUnixSeconds));
}
}

void registerRaidRoutes(drogon::HttpAppFramework& app, const drogon::orm::DbClientPtr& client,
                        const std::shared_ptr<const JwtTokens>& tokens)
{
    const auto sessions = std::make_shared<RaidSessions>();
    auto* const loop = app.getLoop();
    app.registerBeginningAdvice([sessions, loop] {
        loop->runEvery(30.0, [sessions] { sessions->sweep(RaidSessions::Clock::now()); });
    });

    app.registerHandler(
        "/raid/start",
        [tokens, sessions](const drogon::HttpRequestPtr& request, ResponseCallback&& respond) {
            const auto userId = authorize(*tokens, request, respond);
            if (!userId)
            {
                return;
            }
            const auto started = sessions->start(*userId, RaidSessions::Clock::now());
            switch (started.code)
            {
                case RaidSessions::StartCode::AlreadyActive:
                    respondStatus(respond, drogon::k409Conflict, "raid_already_active");
                    return;
                case RaidSessions::StartCode::Unavailable:
                    respondStatus(respond, drogon::k503ServiceUnavailable, "raid_unavailable");
                    return;
                case RaidSessions::StartCode::Started:
                    Json::Value body(Json::objectValue);
                    body["status"] = "started";
                    body["run_id"] = started.runId;
                    body["time_limit_seconds"] = static_cast<int>(RaidSessions::timeLimit.count());
                    respondJson(respond, drogon::k201Created, body);
                    return;
            }
        },
        {drogon::Post});

    app.registerHandler(
        "/raid/finish",
        [tokens, sessions, client, loop](const drogon::HttpRequestPtr& request, ResponseCallback&& respond) {
            const auto userId = authorize(*tokens, request, respond);
            if (!userId)
            {
                return;
            }
            const auto input = readFinishRequest(request);
            if (!input)
            {
                respondStatus(respond, drogon::k400BadRequest, "invalid_request");
                return;
            }

            const auto finished = sessions->finish(*userId, input->runId, input->clear, RaidSessions::Clock::now());
            switch (finished.code)
            {
                case RaidSessions::FinishCode::NotFound:
                    respondStatus(respond, drogon::k404NotFound, "raid_not_found");
                    return;
                case RaidSessions::FinishCode::Expired:
                    respondStatus(respond, drogon::k409Conflict, "raid_expired");
                    return;
                case RaidSessions::FinishCode::Saving:
                    respondStatus(respond, drogon::k409Conflict, "raid_saving");
                    return;
                case RaidSessions::FinishCode::AlreadyFinished:
                    respondStatus(respond, drogon::k409Conflict, "raid_finished");
                    return;
                case RaidSessions::FinishCode::SaveClear:
                    saveClearRecord(client, sessions, *loop, *finished.attempt, std::move(respond));
                    return;
                case RaidSessions::FinishCode::Dead:
                    Json::Value body(Json::objectValue);
                    body["status"] = "dead";
                    body["run_id"] = input->runId;
                    respondJson(respond, drogon::k200OK, body);
                    return;
            }
        },
        {drogon::Post});
}
