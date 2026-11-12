// Copyright Epic Games, Inc. All Rights Reserved.

#include "PatternBreakerGameMode.h"
#include "BossCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "PatternBreaker.h"
#include "PatternBreakerApiSubsystem.h"
#include "PatternBreakerCharacter.h"

#define LOCTEXT_NAMESPACE "PatternBreakerRaid"

namespace
{
	constexpr int32 MaxClearSaveAttempts = 3;

	FText DescribeStartFailure(const FPatternBreakerRaidResponse& Response)
	{
		if (Response.ServerStatus == TEXT("raid_already_active") || Response.Result == EPatternBreakerAuthResult::Busy)
		{
			return LOCTEXT("Active", "A previous raid is still active or saving. Try again after it closes.");
		}
		if (Response.HttpStatusCode == 401 || Response.Result == EPatternBreakerAuthResult::NotLoggedIn ||
			Response.Result == EPatternBreakerAuthResult::TokenExpired)
		{
			return LOCTEXT("SignIn", "Sign in again before starting a raid.");
		}
		return LOCTEXT("StartError", "The server could not start the raid. Check the connection and try again.");
	}
}

APatternBreakerGameMode::APatternBreakerGameMode()
{
	// stub
}

void APatternBreakerGameMode::StartPlay()
{
	Super::StartPlay();
	TryStartRaid();
}

void APatternBreakerGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	TryStartRaid();
}

void APatternBreakerGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(MainMenuReturnTimer);
	GetWorldTimerManager().ClearTimer(FinishRetryTimer);
	ReleaseWaitingInput();
	StopRaidTracking();
	if (UPatternBreakerApiSubsystem* Api = RaidApi.Get())
	{
		Api->OnRaidStartCompleted.RemoveDynamic(this, &ThisClass::HandleRaidStart);
		Api->OnRaidFinishCompleted.RemoveDynamic(this, &ThisClass::HandleRaidFinish);
		if (bRaidStartRequested && !bRaidFinished && !bRecoveringPreviousClear)
		{
			Api->AbandonRaid();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void APatternBreakerGameMode::TryStartRaid()
{
	UWorld* World = GetWorld();
	if (bRaidStartRequested || !World || !World->HasBegunPlay())
	{
		return;
	}

	APatternBreakerCharacter* Player = Cast<APatternBreakerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!IsValid(Player) || !Player->HasActorBegunPlay())
	{
		return;
	}

	ABossCharacter* Boss = nullptr;
	for (TActorIterator<ABossCharacter> It(World); It; ++It)
	{
		if (IsValid(*It) && It->HasActorBegunPlay())
		{
			Boss = *It;
			break;
		}
	}
	if (!Boss)
	{
		return;
	}

	RaidPlayer = Player;
	RaidBoss = Boss;
	bRaidStartRequested = true;
	WaitingController = Player->GetController();
	if (AController* Controller = WaitingController.Get()) Controller->SetIgnoreMoveInput(true);
	Player->StopJumping();
	Player->GetCharacterMovement()->StopMovementImmediately();
	ShowRaidServerMessage(LOCTEXT("Starting", "Waiting for the raid server..."));

	UGameInstance* Instance = GetGameInstance();
	UPatternBreakerApiSubsystem* Api = Instance ? Instance->GetSubsystem<UPatternBreakerApiSubsystem>() : nullptr;
	if (!Api)
	{
		FailRaidStart(LOCTEXT("MissingApi", "Raid connection is unavailable."));
		return;
	}
	const EPatternBreakerRaidState State = Api->GetRaidState();
	if (State == EPatternBreakerRaidState::Starting || State == EPatternBreakerRaidState::Active || State == EPatternBreakerRaidState::Finishing)
	{
		FailRaidStart(DescribeStartFailure({ EPatternBreakerAuthResult::Busy }));
		return;
	}
	RaidApi = Api;
	Api->OnRaidStartCompleted.AddUniqueDynamic(this, &ThisClass::HandleRaidStart);
	Api->OnRaidFinishCompleted.AddUniqueDynamic(this, &ThisClass::HandleRaidFinish);
	bRecoveringPreviousClear = State == EPatternBreakerRaidState::RetryPending;
	if (bRecoveringPreviousClear) Api->RetryRaidFinish();
	else Api->StartRaid();
}

void APatternBreakerGameMode::HandleRaidStart(const FPatternBreakerRaidResponse& Response)
{
	if (!IsRaidWaitingToStart() || bRaidFinished || bRecoveringPreviousClear) return;
	if (Response.Result != EPatternBreakerAuthResult::Ok)
	{
		UE_LOG(LogPatternBreaker, Warning, TEXT("Raid start failed (HTTP %d, %s)."), Response.HttpStatusCode, *Response.ServerStatus);
		FailRaidStart(DescribeStartFailure(Response));
		return;
	}

	APatternBreakerCharacter* Player = RaidPlayer.Get();
	ABossCharacter* Boss = RaidBoss.Get();
	UPatternBreakerApiSubsystem* Api = RaidApi.Get();
	if (!Player || !Boss || !Api || Player->IsPlayerDead() || Boss->IsBossDead())
	{
		if (Api) Api->AbandonRaid();
		FailRaidStart(LOCTEXT("MissingActors", "The raid could not start because the combatants are unavailable."));
		return;
	}

	Player->OnHPChanged.AddUniqueDynamic(this, &APatternBreakerGameMode::HandlePlayerHPChanged);
	Boss->OnHPChanged.AddUniqueDynamic(this, &APatternBreakerGameMode::HandleBossHPChanged);

	// The send time is the earliest possible server start and avoids granting extra time for the round trip.
	RaidStartSeconds = Api->GetRaidStartRequestSeconds();
	RaidTimeLimitSeconds = static_cast<float>(Response.TimeLimitSeconds);
	bRaidStarted = true;
	UE_LOG(LogPatternBreaker, Log, TEXT("Raid started: %.2f second limit."), RaidTimeLimitSeconds);
	ShowRaidServerMessage(FText());

	const float Remaining = GetRaidRemainingTime();
	if (Remaining <= 0.0f)
	{
		FinishRaid(ERaidResult::TimeOut);
		return;
	}
	ReleaseWaitingInput();
	Boss->StartRaidActionCycle();

	FTimerManagerTimerParameters UpdateParameters;
	UpdateParameters.bLoop = true;
	UpdateParameters.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(RemainingTimeTimer, this, &APatternBreakerGameMode::PublishRemainingTime, 1.0f, UpdateParameters);
	GetWorldTimerManager().SetTimer(RaidDeadlineTimer, this, &APatternBreakerGameMode::HandleRaidDeadline, Remaining);
	PublishRemainingTime();
}

bool APatternBreakerGameMode::IsRaidInProgress() const
{
	return bRaidStarted && !bRaidFinished;
}

float APatternBreakerGameMode::GetRaidRemainingTime() const
{
	if (!bRaidStarted)
	{
		return 0.0f;
	}

	const double ElapsedSeconds = bRaidFinished ? FinalElapsedSeconds : FPlatformTime::Seconds() - RaidStartSeconds;
	return static_cast<float>(FMath::Max(0.0, RaidTimeLimitSeconds - ElapsedSeconds));
}

bool APatternBreakerGameMode::TryGetRaidClearTime(float& ClearTimeSeconds) const
{
	const bool bHasClearTime = bResultPublished && FinalResult == ERaidResult::Victory;
	ClearTimeSeconds = bHasClearTime ? static_cast<float>(FinalElapsedSeconds) : 0.0f;
	return bHasClearTime;
}

void APatternBreakerGameMode::PublishRemainingTime()
{
	if (bRaidFinished)
	{
		return;
	}

	const float RemainingSeconds = GetRaidRemainingTime();
	if (RemainingSeconds <= 0.0f)
	{
		FinishRaid(ERaidResult::TimeOut);
		return;
	}

	OnRaidRemainingTimeChanged.Broadcast(RemainingSeconds);
}

void APatternBreakerGameMode::HandleRaidDeadline()
{
	const float RemainingSeconds = GetRaidRemainingTime();
	if (RemainingSeconds > 0.0f)
	{
		// The world timer can fire early relative to the monotonic clock.
		GetWorldTimerManager().SetTimer(RaidDeadlineTimer, this, &APatternBreakerGameMode::HandleRaidDeadline, RemainingSeconds);
		return;
	}

	FinishRaid(ERaidResult::TimeOut);
}

void APatternBreakerGameMode::HandlePlayerHPChanged(float CurrentHP, float MaxHP)
{
	if (CurrentHP <= 0.0f)
	{
		FinishRaid(ERaidResult::Defeat);
	}
}

void APatternBreakerGameMode::HandleBossHPChanged(float CurrentHP, float MaxHP)
{
	if (CurrentHP <= 0.0f)
	{
		FinishRaid(ERaidResult::Victory);
	}
}

void APatternBreakerGameMode::FinishRaid(ERaidResult Result)
{
	if (!IsRaidInProgress())
	{
		return;
	}

	FinalElapsedSeconds = FPlatformTime::Seconds() - RaidStartSeconds;
	// Honor the deadline even if an HP event arrives before the timer callback.
	FinalResult = FinalElapsedSeconds >= RaidTimeLimitSeconds ? ERaidResult::TimeOut : Result;
	if (FinalResult == ERaidResult::Victory && RaidPlayer.IsValid() && RaidPlayer->IsPlayerDead())
	{
		FinalResult = ERaidResult::Defeat;
	}
	bRaidFinished = true;
	StopRaidTracking();

	OnRaidRemainingTimeChanged.Broadcast(GetRaidRemainingTime());
	if (FinalResult == ERaidResult::Victory)
	{
		ShowRaidServerMessage(LOCTEXT("Saving", "Saving the raid clear..."));
		FinishAttempts = 1;
		if (UPatternBreakerApiSubsystem* Api = RaidApi.Get(); Api && Api->GetRaidState() == EPatternBreakerRaidState::Active)
		{
			Api->FinishRaid(EPatternBreakerRaidFinish::Clear);
		}
		else HandleRaidFinish({ EPatternBreakerAuthResult::TransportError });
	}
	else
	{
		if (UPatternBreakerApiSubsystem* Api = RaidApi.Get()) Api->FinishRaid(EPatternBreakerRaidFinish::Dead);
		PublishRaidResult();
	}
}

void APatternBreakerGameMode::HandleRaidFinish(const FPatternBreakerRaidResponse& Response)
{
	if (bRecoveringPreviousClear)
	{
		bRecoveringPreviousClear = false;
		UPatternBreakerApiSubsystem* Api = RaidApi.Get();
		if (Api && !Response.bRetryable) Api->StartRaid();
		else FailRaidStart(LOCTEXT("PreviousPending", "A previous raid clear is still waiting to be saved. Try again later."));
		return;
	}
	if (!bRaidFinished || FinalResult != ERaidResult::Victory || bResultPublished || bCanReturnToMenu) return;

	if (Response.Result == EPatternBreakerAuthResult::Ok && Response.ElapsedMs >= 0)
	{
		FinalElapsedSeconds = Response.ElapsedMs / 1000.0;
		PublishRaidResult();
		return;
	}
	if (Response.ServerStatus == TEXT("raid_expired"))
	{
		FinalResult = ERaidResult::TimeOut;
		FinalElapsedSeconds = RaidTimeLimitSeconds;
		OnRaidRemainingTimeChanged.Broadcast(0.0f);
		PublishRaidResult();
		return;
	}
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (Response.bRetryable && FinishAttempts < MaxClearSaveAttempts && RaidApi.IsValid() && RaidApi->GetCachedUser(User, Remaining))
	{
		ShowRaidServerMessage(LOCTEXT("Retrying", "Save not confirmed. Retrying the raid clear..."));
		GetWorldTimerManager().SetTimer(FinishRetryTimer, this, &ThisClass::RetryClearSave, 2.0f * FinishAttempts);
		return;
	}
	ShowRaidServerMessage(Response.bRetryable
		? LOCTEXT("SavePending", "Clear save not confirmed. Sign in and enter the arena again to retry within 10 minutes.")
		: LOCTEXT("SaveUnconfirmed", "The server did not confirm a saved clear for this raid."));
	UE_LOG(LogPatternBreaker, Warning, TEXT("Raid clear unconfirmed (HTTP %d, %s)."), Response.HttpStatusCode, *Response.ServerStatus);
	AllowMenuReturn();
}

void APatternBreakerGameMode::RetryClearSave()
{
	++FinishAttempts;
	if (UPatternBreakerApiSubsystem* Api = RaidApi.Get()) Api->RetryRaidFinish();
	else HandleRaidFinish({ EPatternBreakerAuthResult::TransportError });
}

void APatternBreakerGameMode::PublishRaidResult()
{
	if (FinalResult == ERaidResult::TimeOut)
	{
		if (APatternBreakerCharacter* Player = RaidPlayer.Get())
		{
			Player->StopJumping();
			Player->ConsumeMovementInputVector();
			UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
	}
	bResultPublished = true;
	UE_LOG(LogPatternBreaker, Log, TEXT("Raid ended: %s after %.2f seconds."),
		*UEnum::GetValueAsString(FinalResult), FinalElapsedSeconds);
	OnRaidEnded.Broadcast(FinalResult, static_cast<float>(FinalElapsedSeconds));
	AllowMenuReturn();
}

void APatternBreakerGameMode::FailRaidStart(const FText& Message)
{
	bRaidFinished = true;
	ShowRaidServerMessage(Message);
	AllowMenuReturn();
}

void APatternBreakerGameMode::ShowRaidServerMessage(const FText& Message)
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Widget : Widgets)
	{
		if (Widget->GetClass()->GetFName() != TEXT("WBP_CombatHUD_C")) continue;
		if (UTextBlock* Label = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("TXT_RaidResult"))))
		{
			Label->SetText(Message);
			Label->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
		}
	}
	if (!Message.IsEmpty()) UE_LOG(LogPatternBreaker, Log, TEXT("%s"), *Message.ToString());
}

void APatternBreakerGameMode::ReleaseWaitingInput()
{
	if (AController* Controller = WaitingController.Get()) Controller->SetIgnoreMoveInput(false);
	WaitingController.Reset();
}

void APatternBreakerGameMode::AllowMenuReturn()
{
	bCanReturnToMenu = true;
	if (!bMainMenuTravelRequested)
	{
		GetWorldTimerManager().SetTimer(MainMenuReturnTimer, this, &APatternBreakerGameMode::ReturnToMainMenu, 5.0f);
	}
}

void APatternBreakerGameMode::ReturnToMainMenu()
{
	if (!bCanReturnToMenu || bMainMenuTravelRequested)
	{
		return;
	}

	bMainMenuTravelRequested = true;
	GetWorldTimerManager().ClearTimer(MainMenuReturnTimer);
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
}

#undef LOCTEXT_NAMESPACE

void APatternBreakerGameMode::StopRaidTracking()
{
	GetWorldTimerManager().ClearTimer(RemainingTimeTimer);
	GetWorldTimerManager().ClearTimer(RaidDeadlineTimer);

	if (APatternBreakerCharacter* Player = RaidPlayer.Get())
	{
		Player->OnHPChanged.RemoveDynamic(this, &APatternBreakerGameMode::HandlePlayerHPChanged);
	}
	if (ABossCharacter* Boss = RaidBoss.Get())
	{
		Boss->OnHPChanged.RemoveDynamic(this, &APatternBreakerGameMode::HandleBossHPChanged);
		Boss->StopRaidActionCycle();
	}
}
