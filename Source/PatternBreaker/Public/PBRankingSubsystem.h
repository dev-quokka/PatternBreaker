// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PBRankingSubsystem.generated.h"

USTRUCT(BlueprintType)
struct PATTERNBREAKER_API FPBRankingRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	int32 ClearTimeMs = 0;
};

UCLASS()
class PATTERNBREAKER_API UPBRankingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Returns local test records ordered from fastest to slowest. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Ranking")
	TArray<FPBRankingRecord> GetTestRankingData() const;
};
