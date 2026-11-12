#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PatternBreakerApiSubsystem.generated.h"

class IHttpRequest;
class IHttpResponse;

UENUM(BlueprintType)
enum class EPatternBreakerHealthResult : uint8
{
	Ok,
	InvalidConfiguration,
	TransportError,
	ConnectionFailed,
	TimedOut,
	HttpError,
	InvalidJson,
	UnexpectedStatus
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPatternBreakerHealthChecked,
	EPatternBreakerHealthResult, Result, int32, HttpStatusCode);

UENUM(BlueprintType)
enum class EPatternBreakerAuthResult : uint8
{
	Ok,
	Busy,
	NotLoggedIn,
	TokenExpired,
	InvalidConfiguration,
	ConnectionFailed,
	TimedOut,
	TransportError,
	HttpError,
	InvalidJson,
	InvalidResponse,
	ServerError
};

USTRUCT(BlueprintType)
struct PATTERNBREAKER_API FPatternBreakerAuthResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Auth")
	EPatternBreakerAuthResult Result = EPatternBreakerAuthResult::Ok;

	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Auth")
	int32 HttpStatusCode = 0;

	/** Known API error code, or unknown_server_error for an unrecognized code. */
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Auth")
	FString ServerStatus;
};

USTRUCT(BlueprintType)
struct PATTERNBREAKER_API FPatternBreakerAuthUser
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Auth")
	int64 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Auth")
	FString Username;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPatternBreakerRegistrationCompleted,
	const FPatternBreakerAuthResponse&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPatternBreakerAuthCompleted,
	const FPatternBreakerAuthResponse&, Response, const FPatternBreakerAuthUser&, User);

UENUM(BlueprintType)
enum class EPatternBreakerRaidState : uint8
{
	Idle, Starting, Active, Finishing, RetryPending, Closed, Failed
};

UENUM(BlueprintType)
enum class EPatternBreakerRaidFinish : uint8
{
	Clear, Dead
};

USTRUCT(BlueprintType)
struct PATTERNBREAKER_API FPatternBreakerRaidResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	EPatternBreakerAuthResult Result = EPatternBreakerAuthResult::Ok;
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	int32 HttpStatusCode = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	FString ServerStatus;
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	FString RunId;
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	int32 TimeLimitSeconds = 0;
	/** -1 until the server confirms a saved clear. */
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	int32 ElapsedMs = -1;
	UPROPERTY(BlueprintReadOnly, Category = "PatternBreaker|Raid")
	bool bRetryable = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPatternBreakerRaidCompleted, const FPatternBreakerRaidResponse&, Response);

UCLASS(Config = Game, DefaultConfig)
class PATTERNBREAKER_API UPatternBreakerApiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Starts a health check unless one is already in flight. */
	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|API")
	void CheckServerHealth();

	/** Called on the game thread. The HTTP status code is zero if no response was received. */
	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|API")
	FPatternBreakerHealthChecked OnServerHealthChecked;

	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Auth")
	void RegisterAccount(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Auth")
	void Login(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Auth")
	void GetCurrentUser();

	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Auth")
	void Logout();

	UFUNCTION(BlueprintPure, Category = "PatternBreaker|Auth")
	bool GetCachedUser(FPatternBreakerAuthUser& OutUser, float& OutTokenSecondsRemaining) const;

	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|Auth")
	FPatternBreakerRegistrationCompleted OnRegistrationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|Auth")
	FPatternBreakerAuthCompleted OnLoginCompleted;

	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|Auth")
	FPatternBreakerAuthCompleted OnCurrentUserCompleted;

	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Raid")
	void StartRaid();
	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Raid")
	void FinishRaid(EPatternBreakerRaidFinish Outcome);
	UFUNCTION(BlueprintCallable, Category = "PatternBreaker|Raid")
	void RetryRaidFinish();
	UFUNCTION(BlueprintPure, Category = "PatternBreaker|Raid")
	EPatternBreakerRaidState GetRaidState() const { return RaidState; }
	UFUNCTION(BlueprintPure, Category = "PatternBreaker|Raid")
	FString GetRaidRunId() const { return RaidRunId; }
	UFUNCTION(BlueprintPure, Category = "PatternBreaker|Raid")
	int32 GetConfirmedRaidClearMilliseconds() const { return ConfirmedClearMs; }
	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|Raid")
	FPatternBreakerRaidCompleted OnRaidStartCompleted;
	UPROPERTY(BlueprintAssignable, Category = "PatternBreaker|Raid")
	FPatternBreakerRaidCompleted OnRaidFinishCompleted;

	double GetRaidStartRequestSeconds() const { return RaidStartRequestSeconds; }
	void AbandonRaid();

	/** Ranking owns the request lifetime. This helper supplies transport and authentication. */
	void RequestRanking(bool bPersonal, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& OutRequest,
		TFunction<void(const FPatternBreakerAuthResponse&, const FString&)> Completion);
	FSimpleMulticastDelegate OnAuthSessionChanged;

	virtual void Deinitialize() override;

private:
	enum class EAuthOperation : uint8 { Register, Login, CurrentUser };

	FString GetApiUrl(const TCHAR* Path) const;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateRequest(const TCHAR* Verb, const FString& Url) const;
	void HandleHealthResponse(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bSucceeded);
	void ReportHealthResult(EPatternBreakerHealthResult Result, int32 HttpStatusCode = 0);
	void StartAuthRequest(EAuthOperation Operation, const FString& Username = FString(), const FString& Password = FString());
	void HandleAuthResponse(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bSucceeded, EAuthOperation Operation);
	void ReportAuthResult(EAuthOperation Operation, const FPatternBreakerAuthResponse& Response,
		const FPatternBreakerAuthUser& User = FPatternBreakerAuthUser());
	void ClearAuthSession(bool bExpired = false);
	void SendRaidRequest(bool bStart);
	void HandleRaidResponse(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bSucceeded, bool bStart);
	void CompleteRaidRequest(bool bStart, FPatternBreakerRaidResponse Response);

	UPROPERTY(Config)
	FString ServerBaseUrl = TEXT("http://127.0.0.1:18080");

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HealthRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> RegistrationRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> SessionRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> RaidRequest;

	FString AccessToken;
	FPatternBreakerAuthUser CachedUser;
	double TokenExpiresAt = 0.0;
	bool bSessionExpired = false;

	EPatternBreakerRaidState RaidState = EPatternBreakerRaidState::Idle;
	EPatternBreakerRaidFinish RaidOutcome = EPatternBreakerRaidFinish::Dead;
	FString RaidRunId;
	int64 RaidOwnerId = 0;
	double RaidStartRequestSeconds = 0.0;
	double RaidRetryDeadline = 0.0;
	int32 RaidServerTimeLimit = 0;
	int32 ConfirmedClearMs = -1;
	bool bAbandonStartingRaid = false;
};
