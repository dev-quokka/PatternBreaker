#include "PBRankingWidget.h"

#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "PBRankingWidget"

void UPBRankingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RankingTitle = Cast<UTextBlock>(GetWidgetFromName(TEXT("TXT_RankingTitle")));
	EmptyMessage = Cast<UTextBlock>(GetWidgetFromName(TEXT("TXT_EmptyRanking")));
	if (!RankingTitle || !EmptyMessage) return;
	OriginalTitle = RankingTitle->GetText();
	OriginalEmptyMessage = EmptyMessage->GetText();

	UGameInstance* Instance = GetGameInstance();
	Ranking = Instance ? Instance->GetSubsystem<UPBRankingSubsystem>() : nullptr;
	if (!Ranking)
	{
		EmptyMessage->SetText(LOCTEXT("Unavailable", "Rankings are unavailable."));
		EmptyMessage->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}
	LastTopState = EPBRankingState::Idle;
	Ranking->OnRankingDataChanged.AddUniqueDynamic(this, &ThisClass::UpdateRanking);
	Ranking->RefreshRanking();
}

void UPBRankingWidget::RemoveFromParent()
{
	DisconnectRanking();
	Super::RemoveFromParent();
}

void UPBRankingWidget::NativeDestruct()
{
	DisconnectRanking();
	Super::NativeDestruct();
}

void UPBRankingWidget::DisconnectRanking()
{
	if (Ranking)
	{
		Ranking->OnRankingDataChanged.RemoveDynamic(this, &ThisClass::UpdateRanking);
		Ranking->CloseRanking();
		Ranking = nullptr;
	}
	if (RankingTitle) RankingTitle->SetText(OriginalTitle);
	if (EmptyMessage) EmptyMessage->SetText(OriginalEmptyMessage);
}

int32 UPBRankingWidget::GetServerRank(int32 Index) const
{
	return Ranking ? Ranking->GetRankAtIndex(Index) : 0;
}

void UPBRankingWidget::UpdateRanking()
{
	if (!Ranking) return;
	const EPBRankingState Top = Ranking->GetTopState();
	if (Top != LastTopState)
	{
		LastTopState = Top;
		ApplyServerRanking();
		if (Top == EPBRankingState::Loading)
		{
			EmptyMessage->SetText(LOCTEXT("Loading", "Loading rankings..."));
		}
		else if (Top == EPBRankingState::Error)
		{
			EmptyMessage->SetText(LOCTEXT("LoadFailed", "Could not load rankings. Check the connection and reopen this screen."));
		}
		else if (Top == EPBRankingState::Empty)
		{
			EmptyMessage->SetText(OriginalEmptyMessage.IsEmpty() ? LOCTEXT("Empty", "No ranking records yet.") : OriginalEmptyMessage);
		}
	}

	FText Personal;
	switch (Ranking->GetPersonalState())
	{
	case EPBRankingState::Loading: Personal = LOCTEXT("LoadingBest", "loading..."); break;
	case EPBRankingState::Empty: Personal = LOCTEXT("NoBest", "no record"); break;
	case EPBRankingState::Error: Personal = LOCTEXT("BestFailed", "unavailable"); break;
	case EPBRankingState::Ready:
	{
		const FPBRankingRecord Best = Ranking->GetPersonalBest();
		Personal = Best.Rank > 0
			? FText::Format(LOCTEXT("BestWithRank", "{0} (#{1})"), Best.ClearTimeText, FText::AsNumber(Best.Rank))
			: Best.ClearTimeText;
		break;
	}
	default: break;
	}
	RankingTitle->SetText(Personal.IsEmpty() ? OriginalTitle
		: FText::Format(LOCTEXT("TitleWithBest", "{0}  |  My best: {1}"), OriginalTitle, Personal));
}

#undef LOCTEXT_NAMESPACE
