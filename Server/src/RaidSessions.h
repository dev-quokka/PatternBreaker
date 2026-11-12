#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

enum class RaidStatus { Active, Saving, AwaitingRetry };

struct RaidClearRecord
{
    std::string runId;
    std::uint64_t userId;
    std::uint32_t elapsedMs;
    std::int64_t clearedAtUnixSeconds;
};

struct RaidSession
{
    std::string runId;
    std::uint64_t userId;
    std::chrono::steady_clock::time_point startTime;
    RaidStatus status = RaidStatus::Active;
    std::optional<RaidClearRecord> clearRecord;
    std::chrono::steady_clock::time_point retryUntil{};
    std::chrono::steady_clock::time_point saveUntil{};
    std::uint64_t saveAttempt = 0;
};

class RaidSessions
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds timeLimit{120};
    static constexpr std::chrono::minutes retryRetention{10};
    static constexpr std::chrono::seconds saveTimeout{8};

    enum class StartCode { Started, AlreadyActive, Unavailable };
    enum class FinishCode { SaveClear, ClearAlreadySaved, Dead, NotFound, Expired, Saving, AlreadyFinished };
    enum class SaveOutcome { Committed, Retryable, Failed, Stale };

    struct StartResult { StartCode code; std::string runId; };
    struct SaveAttempt
    {
        RaidClearRecord record;
        std::uint64_t generation;
        Clock::time_point deadline;
        Clock::time_point retryUntil;
    };
    struct FinishResult
    {
        FinishCode code;
        std::optional<SaveAttempt> attempt;
        std::optional<std::uint32_t> savedElapsedMs = std::nullopt;
    };

    StartResult start(std::uint64_t userId, Clock::time_point now);
    FinishResult finish(std::uint64_t userId, const std::string& runId, bool clear, Clock::time_point now);
    SaveOutcome completeSave(const SaveAttempt& attempt, bool saved, Clock::time_point now);
    void sweep(Clock::time_point now);

private:
    struct ClosedRaid
    {
        std::uint64_t userId;
        bool expired;
        Clock::time_point retainUntil;
        std::optional<std::uint32_t> savedElapsedMs;
    };

    using SessionMap = std::unordered_map<std::string, RaidSession>;
    static bool isExpired(const RaidSession& session, Clock::time_point now);
    static void recoverTimedOutSave(RaidSession& session, Clock::time_point now);
    void pruneClosed(Clock::time_point now);
    void close(SessionMap::iterator session, bool expired, Clock::time_point now,
               std::optional<std::uint32_t> savedElapsedMs = std::nullopt);

    std::mutex mutex_;
    SessionMap sessions_;
    std::unordered_map<std::uint64_t, std::string> activeByUser_;
    std::unordered_map<std::string, ClosedRaid> closed_;
    std::deque<std::string> closedOrder_;
};
