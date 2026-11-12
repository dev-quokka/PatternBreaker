#pragma once

#include "Blueprint/UserWidget.h"
#include "PBRankingSubsystem.h"
#include "PBRankingWidget.generated.h"

class UTextBlock;

/** Connects the existing ranking Widget Blueprint to the current server snapshot. */
UCLASS(Abstract, Blueprintable)
class PATTERNBREAKER_API UPBRankingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void RemoveFromParent() override;

	UFUNCTION(BlueprintPure, Category = "Ranking")
	int32 GetServerRank(int32 Index) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** The Blueprint converts server records to ST_RankingEntry and calls ApplyRankingData. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ranking")
	void ApplyServerRanking();

private:
	void DisconnectRanking();

	UFUNCTION()
	void UpdateRanking();

	UPROPERTY(Transient) TObjectPtr<UPBRankingSubsystem> Ranking;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RankingTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EmptyMessage;
	FText OriginalTitle;
	FText OriginalEmptyMessage;
	EPBRankingState LastTopState = EPBRankingState::Idle;
};
