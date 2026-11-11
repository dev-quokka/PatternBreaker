#include "RaidSessions.h"

#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
constexpr std::size_t maxSessions = 4096;
constexpr std::size_t maxClosedRaids = 4096;
constexpr std::chrono::minutes closedRetention{5};

std::string generateRunId()
{
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        return {};
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string runId;
    runId.reserve(bytes.size() * 2);
    for (const auto byte : bytes)
    {
        runId.push_back(hex[byte >> 4]);
        runId.push_back(hex[byte & 0x0f]);
    }
    return runId;
}
}

RaidSessions::StartResult RaidSessions::start(std::uint64_t userId, Clock::time_point now)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto active = activeByUser_.find(userId); active != activeByUser_.end())
    {
        const auto session = sessions_.find(active->second);
        recoverTimedOutSave(session->second, now);
        if (!isExpired(session->second, now))
        {
            return {StartCode::AlreadyActive, {}};
        }
        close(session, true, now);
    }
    if (sessions_.size() >= maxSessions)
    {
        return {StartCode::Unavailable, {}};
    }

    auto runId = generateRunId();
    if (runId.empty() || sessions_.count(runId) || closed_.count(runId))
    {
        return {StartCode::Unavailable, {}};
    }
    sessions_.emplace(runId, RaidSession{runId, userId, now});
    activeByUser_.emplace(userId, runId);
    return {StartCode::Started, std::move(runId)};
}

RaidSessions::FinishResult RaidSessions::finish(std::uint64_t userId, const std::string& runId,
                                                bool clear, Clock::time_point now)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(runId);
    if (found == sessions_.end())
    {
        pruneClosed(now);
        const auto closed = closed_.find(runId);
        if (closed != closed_.end() && closed->second.userId == userId && closed->second.retainUntil > now)
        {
            return {closed->second.expired ? FinishCode::Expired : FinishCode::AlreadyFinished, {}};
        }
        return {FinishCode::NotFound, {}};
    }

    auto& session = found->second;
    if (session.userId != userId)
    {
        return {FinishCode::NotFound, {}};
    }
    recoverTimedOutSave(session, now);
    if (isExpired(session, now))
    {
        close(found, true, now);
        return {FinishCode::Expired, {}};
    }
    if (session.status == RaidStatus::Saving)
    {
        return {FinishCode::Saving, {}};
    }
    if (!clear)
    {
        if (session.status == RaidStatus::AwaitingRetry)
        {
            return {FinishCode::AlreadyFinished, {}};
        }
        close(found, false, now);
        return {FinishCode::Dead, {}};
    }

    if (session.status == RaidStatus::Active)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.startTime);
        const auto clearedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());
        session.clearRecord = RaidClearRecord{runId, userId, static_cast<std::uint32_t>(elapsed.count()), clearedAt.count()};
        session.retryUntil = now + retryRetention;
    }
    session.status = RaidStatus::Saving;
    session.saveUntil = (std::min)(now + saveTimeout, session.retryUntil);
    return {FinishCode::SaveClear,
            SaveAttempt{*session.clearRecord, ++session.saveAttempt, session.saveUntil, session.retryUntil}};
}

RaidSessions::SaveOutcome RaidSessions::completeSave(const SaveAttempt& attempt, bool saved, Clock::time_point now)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(attempt.record.runId);
    if (found == sessions_.end() || found->second.status != RaidStatus::Saving ||
        found->second.saveAttempt != attempt.generation)
    {
        return SaveOutcome::Stale;
    }
    if (saved && now < found->second.saveUntil)
    {
        close(found, false, now);
        return SaveOutcome::Committed;
    }
    if (now >= found->second.retryUntil)
    {
        close(found, true, now);
        return SaveOutcome::Failed;
    }
    found->second.status = RaidStatus::AwaitingRetry;
    return SaveOutcome::Retryable;
}

void RaidSessions::sweep(Clock::time_point now)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    pruneClosed(now);
    for (auto it = sessions_.begin(); it != sessions_.end();)
    {
        const auto session = it++;
        recoverTimedOutSave(session->second, now);
        if (isExpired(session->second, now))
        {
            close(session, true, now);
        }
    }
}

void RaidSessions::recoverTimedOutSave(RaidSession& session, Clock::time_point now)
{
    if (session.status == RaidStatus::Saving && now >= session.saveUntil)
    {
        session.status = RaidStatus::AwaitingRetry;
    }
}

bool RaidSessions::isExpired(const RaidSession& session, Clock::time_point now)
{
    return (session.status == RaidStatus::Active && now - session.startTime >= timeLimit) ||
           (session.status == RaidStatus::AwaitingRetry && now >= session.retryUntil);
}

void RaidSessions::pruneClosed(Clock::time_point now)
{
    while (!closedOrder_.empty() && closed_.at(closedOrder_.front()).retainUntil <= now)
    {
        closed_.erase(closedOrder_.front());
        closedOrder_.pop_front();
    }
}

void RaidSessions::close(SessionMap::iterator session, bool expired, Clock::time_point now)
{
    pruneClosed(now);
    if (closedOrder_.size() >= maxClosedRaids)
    {
        closed_.erase(closedOrder_.front());
        closedOrder_.pop_front();
    }
    const auto& raid = session->second;
    closed_.emplace(raid.runId, ClosedRaid{raid.userId, expired, now + closedRetention});
    closedOrder_.push_back(raid.runId);
    activeByUser_.erase(raid.userId);
    sessions_.erase(session);
}
