#include "PBRankingWidget.h"

#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "PBRankingWidget"

void UPBRankingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UTextBlock* Title = Cast<UTextBlock>(GetWidgetFromName(TEXT("TXT_RankingTitle"))))
	{
		Title->SetText(LOCTEXT("RankingTitle", "RANKING"));
	}
	UpdatePersonalRecord(EPBRankingState::Idle);
	EmptyMessage = Cast<UTextBlock>(GetWidgetFromName(TEXT("TXT_EmptyRanking")));
	if (!EmptyMessage) return;
	OriginalEmptyMessage = EmptyMessage->GetText();

	UGameInstance* Instance = GetGameInstance();
	Ranking = Instance ? Instance->GetSubsystem<UPBRankingSubsystem>() : nullptr;
	if (!Ranking)
	{
		UpdatePersonalRecord(EPBRankingState::Error);
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
	UpdatePersonalRecord(EPBRankingState::Idle);
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

	if (Ranking) UpdatePersonalRecord(Ranking->GetPersonalState(), Ranking->GetPersonalBest());
}

void UPBRankingWidget::UpdatePersonalRecord(EPBRankingState State, const FPBRankingRecord& Best)
{
	FText Rank = LOCTEXT("NoRank", "—");
	FText ClearTime = Rank;
	switch (State)
	{
	case EPBRankingState::Loading: ClearTime = LOCTEXT("LoadingBest", "Loading..."); break;
	case EPBRankingState::Empty: ClearTime = LOCTEXT("NoBest", "No record"); break;
	case EPBRankingState::Error: ClearTime = LOCTEXT("BestFailed", "Unavailable"); break;
	case EPBRankingState::Ready:
		if (Best.Rank > 0) Rank = FText::Format(LOCTEXT("PersonalRank", "#{0}"), FText::AsNumber(Best.Rank));
		ClearTime = Best.ClearTimeText;
		break;
	default: break;
	}
	if (TXT_MyRank) TXT_MyRank->SetText(Rank);
	if (TXT_MyBest) TXT_MyBest->SetText(ClearTime);
}

#undef LOCTEXT_NAMESPACE
