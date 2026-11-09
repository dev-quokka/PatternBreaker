// Copyright Epic Games, Inc. All Rights Reserved.

#include "PatternBreakerGameMode.h"
#include "BossCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "PatternBreaker.h"
#include "PatternBreakerCharacter.h"

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
	StopRaidTracking();
	Super::EndPlay(EndPlayReason);
}

void APatternBreakerGameMode::TryStartRaid()
{
	UWorld* World = GetWorld();
	if (bRaidStarted || !World || !World->HasBegunPlay())
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
	Player->OnHPChanged.AddUniqueDynamic(this, &APatternBreakerGameMode::HandlePlayerHPChanged);
	Boss->OnHPChanged.AddUniqueDynamic(this, &APatternBreakerGameMode::HandleBossHPChanged);

	RaidStartSeconds = FPlatformTime::Seconds();
	bRaidStarted = true;
	UE_LOG(LogPatternBreaker, Log, TEXT("Raid started: %.2f second limit."), RaidTimeLimitSeconds);

	// HP initialization can occur before the GameMode binds the delegates.
	if (Player->IsPlayerDead())
	{
		FinishRaid(ERaidResult::Defeat);
		return;
	}
	if (Boss->IsBossDead())
	{
		FinishRaid(ERaidResult::Victory);
		return;
	}

	FTimerManagerTimerParameters UpdateParameters;
	UpdateParameters.bLoop = true;
	UpdateParameters.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(RemainingTimeTimer, this, &APatternBreakerGameMode::PublishRemainingTime, 1.0f, UpdateParameters);
	GetWorldTimerManager().SetTimer(RaidDeadlineTimer, this, &APatternBreakerGameMode::HandleRaidDeadline, RaidTimeLimitSeconds);
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
	const bool bHasClearTime = bRaidFinished && FinalResult == ERaidResult::Victory;
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
	bRaidFinished = true;
	StopRaidTracking();

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

	UE_LOG(LogPatternBreaker, Log, TEXT("Raid ended: %s after %.2f seconds."),
		*UEnum::GetValueAsString(FinalResult), FinalElapsedSeconds);
	OnRaidRemainingTimeChanged.Broadcast(GetRaidRemainingTime());
	OnRaidEnded.Broadcast(FinalResult, static_cast<float>(FinalElapsedSeconds));

	if (!bMainMenuTravelRequested)
	{
		GetWorldTimerManager().SetTimer(MainMenuReturnTimer, this, &APatternBreakerGameMode::ReturnToMainMenu, 5.0f);
	}
}

void APatternBreakerGameMode::ReturnToMainMenu()
{
	if (!bRaidFinished || bMainMenuTravelRequested)
	{
		return;
	}

	bMainMenuTravelRequested = true;
	GetWorldTimerManager().ClearTimer(MainMenuReturnTimer);
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
}

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
	}
}
