// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PatternBreakerApiSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PBRankingSubsystem.generated.h"

USTRUCT(BlueprintType)
struct PATTERNBREAKER_API FPBRankingRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	FString PlayerId;
	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	int32 Rank = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	int64 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	int32 ClearTimeMs = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	FString ClearedAt;
	UPROPERTY(BlueprintReadOnly, Category = "Ranking")
	FText ClearTimeText;
};

UENUM(BlueprintType)
enum class EPBRankingState : uint8 { Idle, Loading, Ready, Empty, Error };

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPBRankingDataChanged);

UCLASS()
class PATTERNBREAKER_API UPBRankingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Ranking")
	void RefreshRanking();
	UFUNCTION(BlueprintCallable, Category = "Ranking")
	void CloseRanking();
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Ranking")
	TArray<FPBRankingRecord> GetRankingData() const { return Records; }
	UFUNCTION(BlueprintPure, Category = "Ranking")
	int32 GetRankAtIndex(int32 Index) const;
	UFUNCTION(BlueprintPure, Category = "Ranking")
	EPBRankingState GetTopState() const { return TopState; }
	UFUNCTION(BlueprintPure, Category = "Ranking")
	EPBRankingState GetPersonalState() const { return PersonalState; }
	UFUNCTION(BlueprintPure, Category = "Ranking")
	FPBRankingRecord GetPersonalBest() const;
	UFUNCTION(BlueprintPure, Category = "Ranking")
	FPatternBreakerAuthResponse GetTopResponse() const { return TopResponse; }
	UFUNCTION(BlueprintPure, Category = "Ranking")
	FPatternBreakerAuthResponse GetPersonalResponse() const { return PersonalResponse; }
	UPROPERTY(BlueprintAssignable, Category = "Ranking")
	FPBRankingDataChanged OnRankingDataChanged;

private:
	void HandleTop(uint64 Generation, const FPatternBreakerAuthResponse& Response, const FString& Payload);
	void HandlePersonal(uint64 Generation, int64 RequestedUser, const FPatternBreakerAuthResponse& Response, const FString& Payload);
	void ClearPersonalOnAuthChange();

	UPROPERTY(Transient) TObjectPtr<UPatternBreakerApiSubsystem> Api;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> TopRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PersonalRequest;
	TArray<FPBRankingRecord> Records;
	FPBRankingRecord PersonalBest;
	FPatternBreakerAuthResponse TopResponse;
	FPatternBreakerAuthResponse PersonalResponse;
	EPBRankingState TopState = EPBRankingState::Idle;
	EPBRankingState PersonalState = EPBRankingState::Idle;
	uint64 TopGeneration = 0;
	uint64 PersonalGeneration = 0;
};
