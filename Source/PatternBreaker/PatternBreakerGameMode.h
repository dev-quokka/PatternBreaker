// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "PatternBreakerGameMode.generated.h"

class ABossCharacter;
class APatternBreakerCharacter;

UENUM(BlueprintType)
enum class ERaidResult : uint8
{
	Victory,
	Defeat,
	TimeOut
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRaidRemainingTimeChanged, float, RemainingSeconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRaidEnded, ERaidResult, Result, float, ElapsedSeconds);

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class APatternBreakerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	APatternBreakerGameMode();

	UPROPERTY(BlueprintAssignable, Category = "Raid")
	FOnRaidRemainingTimeChanged OnRaidRemainingTimeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Raid")
	FOnRaidEnded OnRaidEnded;

	UFUNCTION(BlueprintPure, Category = "Raid")
	bool IsRaidInProgress() const;

	UFUNCTION(BlueprintPure, Category = "Raid")
	float GetRaidRemainingTime() const;

	/** Returns true and the recorded clear time only after Victory. */
	UFUNCTION(BlueprintPure, Category = "Raid")
	bool TryGetRaidClearTime(float& ClearTimeSeconds) const;

	void ReturnToMainMenu();

	virtual void StartPlay() override;

protected:
	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Raid", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", Units = "s"))
	float RaidTimeLimitSeconds = 60.0f;

	UPROPERTY()
	TSoftObjectPtr<UWorld> MainMenuLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/L_MainMenu.L_MainMenu")));

	TWeakObjectPtr<APatternBreakerCharacter> RaidPlayer;
	TWeakObjectPtr<ABossCharacter> RaidBoss;
	FTimerHandle RemainingTimeTimer;
	FTimerHandle RaidDeadlineTimer;
	FTimerHandle MainMenuReturnTimer;
	double RaidStartSeconds = 0.0;
	double FinalElapsedSeconds = 0.0;
	ERaidResult FinalResult = ERaidResult::TimeOut;
	bool bRaidStarted = false;
	bool bRaidFinished = false;
	bool bMainMenuTravelRequested = false;

	void TryStartRaid();
	void PublishRemainingTime();
	void HandleRaidDeadline();
	void FinishRaid(ERaidResult Result);
	void StopRaidTracking();

	UFUNCTION()
	void HandlePlayerHPChanged(float CurrentHP, float MaxHP);

	UFUNCTION()
	void HandleBossHPChanged(float CurrentHP, float MaxHP);
};



