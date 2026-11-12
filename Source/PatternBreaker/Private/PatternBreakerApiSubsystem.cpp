#include "PatternBreakerApiSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if !UE_BUILD_SHIPPING
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogPatternBreakerApi, Log, All);

namespace
{
	constexpr float RequestTimeoutSeconds = 5.0f;
	using EAuthResult = EPatternBreakerAuthResult;

	void CancelPendingRequest(FHttpRequestPtr& PendingRequest)
	{
		if (FHttpRequestPtr Request = MoveTemp(PendingRequest))
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}

	EAuthResult GetAuthTransportError(EHttpFailureReason Reason)
	{
		switch (Reason)
		{
		case EHttpFailureReason::ConnectionError: return EAuthResult::ConnectionFailed;
		case EHttpFailureReason::TimedOut: return EAuthResult::TimedOut;
		default: return EAuthResult::TransportError;
		}
	}

	FString ReadServerError(const FJsonObject& Body)
	{
		FString Status;
		if (!Body.HasTypedField<EJson::String>(TEXT("status")) ||
			!Body.TryGetStringField(TEXT("status"), Status) || Status.IsEmpty())
		{
			return FString();
		}

		// Return only known API codes. A proxy or server error may contain sensitive text.
		for (const TCHAR* KnownStatus : { TEXT("invalid_request"), TEXT("username_taken"),
			TEXT("invalid_credentials"), TEXT("too_many_attempts"), TEXT("invalid_token"),
			TEXT("database_unavailable"), TEXT("auth_unavailable"), TEXT("auth_busy"),
			TEXT("registration_unavailable"), TEXT("registration_busy") })
		{
			if (Status == KnownStatus)
			{
				return Status;
			}
		}
		return TEXT("unknown_server_error");
	}

	bool ReadAuthUser(const FJsonObject& Body, FPatternBreakerAuthUser& User)
	{
		return Body.HasTypedField<EJson::Number>(TEXT("user_id")) &&
			Body.TryGetNumberField(TEXT("user_id"), User.UserId) && User.UserId > 0 &&
			Body.HasTypedField<EJson::String>(TEXT("username")) &&
			Body.TryGetStringField(TEXT("username"), User.Username) && !User.Username.IsEmpty();
	}

	bool IsSafeBearerToken(const FString& Token)
	{
		if (Token.IsEmpty() || Token.Len() > 2048)
		{
			return false;
		}
		int32 Separators = 0;
		bool bSegmentHasData = false;
		for (const TCHAR Ch : Token)
		{
			if (Ch == '.')
			{
				if (!bSegmentHasData || ++Separators > 2)
				{
					return false;
				}
				bSegmentHasData = false;
				continue;
			}
			if (!((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') ||
				Ch == '-' || Ch == '_'))
			{
				return false;
			}
			bSegmentHasData = true;
		}
		return Separators == 2 && bSegmentHasData;
	}

#if !UE_BUILD_SHIPPING
	FAutoConsoleCommandWithWorld CheckServerHealthCommand(
		TEXT("pb.CheckServerHealth"),
		TEXT("Checks the PatternBreaker HTTP server from the current game or PIE world."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (World && World->IsGameWorld())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (UPatternBreakerApiSubsystem* Api = GameInstance->GetSubsystem<UPatternBreakerApiSubsystem>())
					{
						Api->CheckServerHealth();
						return;
					}
				}
			}
			UE_LOG(LogPatternBreakerApi, Warning, TEXT("Run pb.CheckServerHealth in a game or PIE session."));
		}));
#endif
}

void UPatternBreakerApiSubsystem::CheckServerHealth()
{
	check(IsInGameThread());
	if (HealthRequest.IsValid())
	{
		return;
	}

	const FString Url = GetApiUrl(TEXT("/health"));
	if (Url.IsEmpty())
	{
		ReportHealthResult(EPatternBreakerHealthResult::InvalidConfiguration);
		return;
	}

	const FHttpRequestRef Request = CreateRequest(TEXT("GET"), Url);
	HealthRequest = Request;
	Request->OnProcessRequestComplete().BindUObject(this, &UPatternBreakerApiSubsystem::HandleHealthResponse);
	// UE invokes the completion delegate on startup errors as well as completed requests.
	Request->ProcessRequest();
}

FString UPatternBreakerApiSubsystem::GetApiUrl(const TCHAR* Path) const
{
	FString BaseUrl = ServerBaseUrl.TrimStartAndEnd();
	while (BaseUrl.RemoveFromEnd(TEXT("/")))
	{
	}
	return BaseUrl.IsEmpty() ? FString() : BaseUrl + Path;
}

FHttpRequestRef UPatternBreakerApiSubsystem::CreateRequest(const TCHAR* Verb, const FString& Url) const
{
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(Url);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	return Request;
}

void UPatternBreakerApiSubsystem::HandleHealthResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	if (!HealthRequest.IsValid() || Request != HealthRequest)
	{
		return;
	}
	HealthRequest.Reset();

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	if (!bSucceeded || !Response.IsValid())
	{
		const EHttpFailureReason Reason = Request->GetFailureReason();
		if (Reason == EHttpFailureReason::TimedOut)
		{
			ReportHealthResult(EPatternBreakerHealthResult::TimedOut, StatusCode);
		}
		else if (Reason == EHttpFailureReason::ConnectionError)
		{
			ReportHealthResult(EPatternBreakerHealthResult::ConnectionFailed, StatusCode);
		}
		else
		{
			ReportHealthResult(EPatternBreakerHealthResult::TransportError, StatusCode);
		}
		return;
	}

	if (StatusCode != EHttpResponseCodes::Ok)
	{
		ReportHealthResult(EPatternBreakerHealthResult::HttpError, StatusCode);
		return;
	}

	TSharedPtr<FJsonObject> Body;
	const FString Content = Response->GetContentAsString();
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Body) || !Body.IsValid())
	{
		ReportHealthResult(EPatternBreakerHealthResult::InvalidJson, StatusCode);
		return;
	}

	FString Status;
	if (!Body->TryGetStringField(TEXT("status"), Status) || Status != TEXT("ok"))
	{
		ReportHealthResult(EPatternBreakerHealthResult::UnexpectedStatus, StatusCode);
		return;
	}
	ReportHealthResult(EPatternBreakerHealthResult::Ok, StatusCode);
}

void UPatternBreakerApiSubsystem::ReportHealthResult(EPatternBreakerHealthResult Result, int32 HttpStatusCode)
{
	if (Result == EPatternBreakerHealthResult::Ok)
	{
		UE_LOG(LogPatternBreakerApi, Log, TEXT("PatternBreaker Server Health: OK"));
	}
	else
	{
		UE_LOG(LogPatternBreakerApi, Warning, TEXT("PatternBreaker Server Health: Failed (%s, HTTP %d)"),
			*StaticEnum<EPatternBreakerHealthResult>()->GetNameStringByValue(static_cast<int64>(Result)), HttpStatusCode);
	}
	OnServerHealthChecked.Broadcast(Result, HttpStatusCode);
}

void UPatternBreakerApiSubsystem::RegisterAccount(const FString& Username, const FString& Password)
{
	StartAuthRequest(EAuthOperation::Register, Username, Password);
}

void UPatternBreakerApiSubsystem::Login(const FString& Username, const FString& Password)
{
	StartAuthRequest(EAuthOperation::Login, Username, Password);
}

void UPatternBreakerApiSubsystem::GetCurrentUser()
{
	StartAuthRequest(EAuthOperation::CurrentUser);
}

void UPatternBreakerApiSubsystem::StartAuthRequest(EAuthOperation Operation, const FString& Username, const FString& Password)
{
	check(IsInGameThread());
	FHttpRequestPtr& PendingRequest = Operation == EAuthOperation::Register ? RegistrationRequest : SessionRequest;
	if (PendingRequest.IsValid())
	{
		ReportAuthResult(Operation, { EAuthResult::Busy });
		return;
	}

	const bool bCurrentUser = Operation == EAuthOperation::CurrentUser;
	if (bCurrentUser && (!AccessToken.IsEmpty() && FPlatformTime::Seconds() >= TokenExpiresAt))
	{
		ClearAuthSession(true);
	}
	if (bCurrentUser && AccessToken.IsEmpty())
	{
		ReportAuthResult(Operation, { bSessionExpired ? EAuthResult::TokenExpired : EAuthResult::NotLoggedIn });
		return;
	}

	const TCHAR* Path = TEXT("/auth/me");
	if (!bCurrentUser)
	{
		Path = Operation == EAuthOperation::Register ? TEXT("/auth/register") : TEXT("/auth/login");
	}
	const FString Url = GetApiUrl(Path);
	if (Url.IsEmpty())
	{
		ReportAuthResult(Operation, { EAuthResult::InvalidConfiguration });
		return;
	}

	const FHttpRequestRef Request = CreateRequest(bCurrentUser ? TEXT("GET") : TEXT("POST"), Url);
	if (bCurrentUser)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}
	else
	{
		const TSharedRef<FJsonObject> Credentials = MakeShared<FJsonObject>();
		Credentials->SetStringField(TEXT("username"), Username);
		Credentials->SetStringField(TEXT("password"), Password);
		FString Content;
		FJsonSerializer::Serialize(Credentials, TJsonWriterFactory<>::Create(&Content));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Content);
	}
	PendingRequest = Request;
	Request->OnProcessRequestComplete().BindUObject(this, &UPatternBreakerApiSubsystem::HandleAuthResponse, Operation);
	Request->ProcessRequest();
}

void UPatternBreakerApiSubsystem::HandleAuthResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bSucceeded, EAuthOperation Operation)
{
	FHttpRequestPtr& PendingRequest = Operation == EAuthOperation::Register ? RegistrationRequest : SessionRequest;
	if (!PendingRequest.IsValid() || Request != PendingRequest)
	{
		return;
	}
	PendingRequest.Reset();

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	if (!bSucceeded || !Response.IsValid())
	{
		ReportAuthResult(Operation, { GetAuthTransportError(Request->GetFailureReason()), StatusCode });
		return;
	}

	if (Operation == EAuthOperation::CurrentUser && StatusCode == EHttpResponseCodes::Denied)
	{
		ClearAuthSession();
	}

	const FString Content = Response->GetContentAsString();
	TSharedPtr<FJsonObject> Body;
	const bool bParsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Body,
		FJsonSerializer::EFlags::StoreNumbersAsStrings) && Body.IsValid();
	const int32 ExpectedStatus = Operation == EAuthOperation::Register ? EHttpResponseCodes::Created : EHttpResponseCodes::Ok;
	if (StatusCode != ExpectedStatus)
	{
		const FString ServerStatus = bParsed ? ReadServerError(*Body) : FString();
		ReportAuthResult(Operation, { ServerStatus.IsEmpty() ? EAuthResult::HttpError : EAuthResult::ServerError,
			StatusCode, ServerStatus });
		return;
	}
	if (!bParsed)
	{
		ReportAuthResult(Operation, { EAuthResult::InvalidJson, StatusCode });
		return;
	}

	if (Operation == EAuthOperation::Register)
	{
		FString Status;
		const bool bRegistered = Body->TryGetStringField(TEXT("status"), Status) && Status == TEXT("registered");
		ReportAuthResult(Operation, { bRegistered ? EAuthResult::Ok : EAuthResult::InvalidResponse, StatusCode });
		return;
	}

	FPatternBreakerAuthUser User;
	if (!ReadAuthUser(*Body, User))
	{
		ReportAuthResult(Operation, { EAuthResult::InvalidResponse, StatusCode });
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Operation == EAuthOperation::Login)
	{
		FString Token;
		FString TokenType;
		int32 ExpiresIn = 0;
		if (!Body->HasTypedField<EJson::String>(TEXT("access_token")) ||
			!Body->TryGetStringField(TEXT("access_token"), Token) || !IsSafeBearerToken(Token) ||
			!Body->TryGetStringField(TEXT("token_type"), TokenType) || TokenType != TEXT("Bearer") ||
			!Body->HasTypedField<EJson::Number>(TEXT("expires_in")) ||
			!Body->TryGetNumberField(TEXT("expires_in"), ExpiresIn) || ExpiresIn <= 0)
		{
			ReportAuthResult(Operation, { EAuthResult::InvalidResponse, StatusCode });
			return;
		}
		AccessToken = MoveTemp(Token);
		TokenExpiresAt = Now + ExpiresIn;
		bSessionExpired = false;
	}
	else if (Now >= TokenExpiresAt)
	{
		ClearAuthSession(true);
		ReportAuthResult(Operation, { EAuthResult::TokenExpired, StatusCode });
		return;
	}
	else if (User.UserId != CachedUser.UserId)
	{
		ClearAuthSession();
		ReportAuthResult(Operation, { EAuthResult::InvalidResponse, StatusCode });
		return;
	}
	CachedUser = User;
	if (Operation == EAuthOperation::Login) OnAuthSessionChanged.Broadcast();
	ReportAuthResult(Operation, { EAuthResult::Ok, StatusCode }, User);
}

void UPatternBreakerApiSubsystem::ReportAuthResult(EAuthOperation Operation,
	const FPatternBreakerAuthResponse& Response, const FPatternBreakerAuthUser& User)
{
	switch (Operation)
	{
	case EAuthOperation::Register: OnRegistrationCompleted.Broadcast(Response); break;
	case EAuthOperation::Login: OnLoginCompleted.Broadcast(Response, User); break;
	case EAuthOperation::CurrentUser: OnCurrentUserCompleted.Broadcast(Response, User); break;
	}
}

bool UPatternBreakerApiSubsystem::GetCachedUser(FPatternBreakerAuthUser& OutUser, float& OutTokenSecondsRemaining) const
{
	const double Remaining = TokenExpiresAt - FPlatformTime::Seconds();
	const bool bHasSession = !AccessToken.IsEmpty() && Remaining > 0.0;
	OutUser = bHasSession ? CachedUser : FPatternBreakerAuthUser();
	OutTokenSecondsRemaining = bHasSession ? static_cast<float>(Remaining) : 0.0f;
	return bHasSession;
}

void UPatternBreakerApiSubsystem::Logout()
{
	check(IsInGameThread());
	ClearAuthSession();
	CancelPendingRequest(SessionRequest);
}

void UPatternBreakerApiSubsystem::ClearAuthSession(bool bExpired)
{
	const bool bHadSession = !AccessToken.IsEmpty();
	AccessToken.Reset();
	CachedUser = FPatternBreakerAuthUser();
	TokenExpiresAt = 0.0;
	bSessionExpired = bExpired;
	if (bHadSession) OnAuthSessionChanged.Broadcast();
}

void UPatternBreakerApiSubsystem::RequestRanking(bool bPersonal, FHttpRequestPtr& OutRequest,
	TFunction<void(const FPatternBreakerAuthResponse&, const FString&)> Completion)
{
	check(IsInGameThread());
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (bPersonal && !GetCachedUser(User, Remaining))
	{
		Completion({ EAuthResult::NotLoggedIn }, FString());
		return;
	}
	const FString Url = GetApiUrl(bPersonal ? TEXT("/ranking/me") : TEXT("/ranking/top"));
	if (Url.IsEmpty())
	{
		Completion({ EAuthResult::InvalidConfiguration }, FString());
		return;
	}
	const FHttpRequestRef Request = CreateRequest(TEXT("GET"), Url);
	if (bPersonal) Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	OutRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, bPersonal, Completion = MoveTemp(Completion)](FHttpRequestPtr Sent, FHttpResponsePtr Response, bool bSucceeded)
		{
			const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
			if (!bSucceeded || !Response.IsValid())
			{
				Completion({ GetAuthTransportError(Sent->GetFailureReason()), Code }, FString());
				return;
			}
			const FString Payload = Response->GetContentAsString();
			if (Code == EHttpResponseCodes::Ok)
			{
				Completion({ EAuthResult::Ok, Code }, Payload);
				return;
			}
			FString Status;
			TSharedPtr<FJsonObject> Body;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Body) && Body.IsValid())
			{
				Status = ReadServerError(*Body);
			}
			Completion({ Status.IsEmpty() ? EAuthResult::HttpError : EAuthResult::ServerError, Code, Status }, FString());
			if (bPersonal && Code == EHttpResponseCodes::Denied && !AccessToken.IsEmpty() &&
				Sent->GetHeader(TEXT("Authorization")).Equals(TEXT("Bearer ") + AccessToken, ESearchCase::CaseSensitive))
			{
				ClearAuthSession();
			}
		});
	Request->ProcessRequest();
}

void UPatternBreakerApiSubsystem::Deinitialize()
{
	CancelPendingRequest(RaidRequest);
	Logout();
	CancelPendingRequest(RegistrationRequest);
	CancelPendingRequest(HealthRequest);
	Super::Deinitialize();
}

void UPatternBreakerApiSubsystem::StartRaid()
{
	check(IsInGameThread());
	if (RaidState == EPatternBreakerRaidState::RetryPending && FPlatformTime::Seconds() >= RaidRetryDeadline)
	{
		RaidState = EPatternBreakerRaidState::Failed;
	}
	if (RaidState == EPatternBreakerRaidState::Starting || RaidState == EPatternBreakerRaidState::Active ||
		RaidState == EPatternBreakerRaidState::Finishing || RaidState == EPatternBreakerRaidState::RetryPending)
	{
		return;
	}

	RaidRunId.Reset();
	RaidOwnerId = 0;
	RaidServerTimeLimit = 0;
	ConfirmedClearMs = -1;
	bAbandonStartingRaid = false;
	RaidState = EPatternBreakerRaidState::Starting;
	SendRaidRequest(true);
}

void UPatternBreakerApiSubsystem::FinishRaid(EPatternBreakerRaidFinish Outcome)
{
	check(IsInGameThread());
	if (RaidState != EPatternBreakerRaidState::Active)
	{
		return;
	}
	RaidOutcome = Outcome;
	RaidRetryDeadline = FPlatformTime::Seconds() + 600.0;
	RaidState = EPatternBreakerRaidState::Finishing;
	SendRaidRequest(false);
}

void UPatternBreakerApiSubsystem::RetryRaidFinish()
{
	check(IsInGameThread());
	if (RaidState != EPatternBreakerRaidState::RetryPending)
	{
		return;
	}
	RaidState = EPatternBreakerRaidState::Finishing;
	if (FPlatformTime::Seconds() >= RaidRetryDeadline)
	{
		CompleteRaidRequest(false, { EAuthResult::ServerError, 0, TEXT("raid_retry_expired") });
		return;
	}
	SendRaidRequest(false);
}

void UPatternBreakerApiSubsystem::AbandonRaid()
{
	if (RaidState == EPatternBreakerRaidState::Starting)
	{
		bAbandonStartingRaid = true;
	}
	else if (RaidState == EPatternBreakerRaidState::Active)
	{
		FinishRaid(EPatternBreakerRaidFinish::Dead);
	}
}

void UPatternBreakerApiSubsystem::SendRaidRequest(bool bStart)
{
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (!GetCachedUser(User, Remaining) || (!bStart && User.UserId != RaidOwnerId))
	{
		const bool bExpired = bSessionExpired || (!AccessToken.IsEmpty() && FPlatformTime::Seconds() >= TokenExpiresAt);
		CompleteRaidRequest(bStart, { bExpired ? EAuthResult::TokenExpired : EAuthResult::NotLoggedIn });
		return;
	}
	const FString Url = GetApiUrl(bStart ? TEXT("/raid/start") : TEXT("/raid/finish"));
	if (Url.IsEmpty())
	{
		CompleteRaidRequest(bStart, { EAuthResult::InvalidConfiguration });
		return;
	}

	const FHttpRequestRef Request = CreateRequest(TEXT("POST"), Url);
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	if (bStart)
	{
		RaidOwnerId = User.UserId;
		RaidStartRequestSeconds = FPlatformTime::Seconds();
	}
	else
	{
		// The server can wait up to eight seconds for a save attempt.
		Request->SetTimeout(12.0f);
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("run_id"), RaidRunId);
		Body->SetStringField(TEXT("result"), RaidOutcome == EPatternBreakerRaidFinish::Clear ? TEXT("clear") : TEXT("dead"));
		FString Content;
		FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Content));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Content);
	}
	RaidRequest = Request;
	Request->OnProcessRequestComplete().BindUObject(this, &ThisClass::HandleRaidResponse, bStart);
	Request->ProcessRequest();
}

void UPatternBreakerApiSubsystem::HandleRaidResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bSucceeded, bool bStart)
{
	if (!RaidRequest.IsValid() || Request != RaidRequest)
	{
		return;
	}
	RaidRequest.Reset();
	const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
	if (!bSucceeded || !Response.IsValid())
	{
		CompleteRaidRequest(bStart, { GetAuthTransportError(Request->GetFailureReason()), Code });
		return;
	}
	if (Code == EHttpResponseCodes::Denied && !AccessToken.IsEmpty() &&
		Request->GetHeader(TEXT("Authorization")).Equals(TEXT("Bearer ") + AccessToken, ESearchCase::CaseSensitive))
	{
		ClearAuthSession();
	}

	FPatternBreakerRaidResponse Result;
	Result.HttpStatusCode = Code;
	const FString Content = Response->GetContentAsString();
	TSharedPtr<FJsonObject> Body;
	const bool bParsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Body) && Body.IsValid();
	FString Status;
	if (bParsed) Body->TryGetStringField(TEXT("status"), Status);
	if (Code != (bStart ? EHttpResponseCodes::Created : EHttpResponseCodes::Ok))
	{
		Result.Result = Status.IsEmpty() ? EAuthResult::HttpError : EAuthResult::ServerError;
		for (const TCHAR* Known : { TEXT("raid_already_active"), TEXT("raid_finished"), TEXT("raid_expired"),
			TEXT("raid_saving"), TEXT("raid_not_found"), TEXT("raid_unavailable"), TEXT("invalid_request"),
			TEXT("invalid_token"), TEXT("auth_unavailable"), TEXT("database_unavailable") })
		{
			if (Status == Known) { Result.ServerStatus = Known; break; }
		}
		if (Result.ServerStatus == TEXT("database_unavailable"))
		{
			FString FailedRunId;
			bool bServerAllowsRetry = false;
			Result.bRetryable = Body->TryGetStringField(TEXT("run_id"), FailedRunId) &&
				FailedRunId.Equals(RaidRunId, ESearchCase::CaseSensitive) &&
				Body->TryGetBoolField(TEXT("retryable"), bServerAllowsRetry) && bServerAllowsRetry;
		}
	}
	else if (!bParsed)
	{
		Result.Result = EAuthResult::InvalidJson;
	}
	else if (bStart)
	{
		bool bValidId = Body->TryGetStringField(TEXT("run_id"), Result.RunId) && Result.RunId.Len() == 32;
		for (const TCHAR Ch : Result.RunId)
		{
			bValidId &= (Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f');
		}
		if (Status != TEXT("started") || !bValidId ||
			!Body->TryGetNumberField(TEXT("time_limit_seconds"), Result.TimeLimitSeconds) || Result.TimeLimitSeconds <= 0)
		{
			Result.Result = EAuthResult::InvalidResponse;
			Result.RunId.Reset();
		}
		else Result.ServerStatus = TEXT("started");
	}
	else
	{
		const bool bClear = RaidOutcome == EPatternBreakerRaidFinish::Clear;
		FString ReturnedId;
		if (Status != (bClear ? TEXT("clear") : TEXT("dead")) ||
			!Body->TryGetStringField(TEXT("run_id"), ReturnedId) || !ReturnedId.Equals(RaidRunId, ESearchCase::CaseSensitive) ||
			(bClear && (!Body->TryGetNumberField(TEXT("elapsed_ms"), Result.ElapsedMs) || Result.ElapsedMs < 0 ||
				Result.ElapsedMs >= static_cast<int64>(RaidServerTimeLimit) * 1000)))
		{
			Result.Result = EAuthResult::InvalidResponse;
			Result.ElapsedMs = -1;
		}
		else Result.ServerStatus = bClear ? TEXT("clear") : TEXT("dead");
	}
	CompleteRaidRequest(bStart, MoveTemp(Result));
}

void UPatternBreakerApiSubsystem::CompleteRaidRequest(bool bStart, FPatternBreakerRaidResponse Response)
{
	if (bStart)
	{
		RaidState = EPatternBreakerRaidState::Failed;
		if (Response.Result == EAuthResult::Ok)
		{
			RaidRunId = Response.RunId;
			RaidServerTimeLimit = Response.TimeLimitSeconds;
			FPatternBreakerAuthUser User;
			float Remaining = 0.0f;
			if (GetCachedUser(User, Remaining) && User.UserId == RaidOwnerId)
			{
				RaidState = EPatternBreakerRaidState::Active;
				if (bAbandonStartingRaid)
				{
					FinishRaid(EPatternBreakerRaidFinish::Dead);
					return;
				}
			}
			else Response.Result = EAuthResult::NotLoggedIn;
		}
		OnRaidStartCompleted.Broadcast(Response);
		return;
	}

	Response.RunId = RaidRunId;
	if (Response.Result == EAuthResult::Ok ||
		(RaidOutcome == EPatternBreakerRaidFinish::Dead && Response.ServerStatus == TEXT("raid_expired")))
	{
		RaidState = EPatternBreakerRaidState::Closed;
		ConfirmedClearMs = Response.ElapsedMs;
	}
	else
	{
		const bool bUncertain = Response.Result == EAuthResult::ConnectionFailed || Response.Result == EAuthResult::TimedOut ||
			Response.Result == EAuthResult::TransportError || Response.Result == EAuthResult::InvalidJson ||
			Response.Result == EAuthResult::InvalidResponse;
		Response.bRetryable = RaidOutcome == EPatternBreakerRaidFinish::Clear && FPlatformTime::Seconds() < RaidRetryDeadline &&
			(Response.bRetryable || bUncertain || Response.HttpStatusCode == 401 || Response.ServerStatus == TEXT("raid_saving") ||
				Response.Result == EAuthResult::TokenExpired || Response.Result == EAuthResult::NotLoggedIn);
		RaidState = Response.bRetryable ? EPatternBreakerRaidState::RetryPending : EPatternBreakerRaidState::Failed;
	}
	OnRaidFinishCompleted.Broadcast(Response);
}
