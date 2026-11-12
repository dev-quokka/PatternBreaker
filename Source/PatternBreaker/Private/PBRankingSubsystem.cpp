#include "PBRankingSubsystem.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	using EResult = EPatternBreakerAuthResult;
	void Cancel(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Pending)
	{
		if (auto Request = MoveTemp(Pending))
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}

	TSharedPtr<FJsonObject> ReadObject(const FString& Payload)
	{
		TSharedPtr<FJsonObject> Object;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Object,
			FJsonSerializer::EFlags::StoreNumbersAsStrings) ? Object : nullptr;
	}

	bool ReadIdentity(const FJsonObject& Object, FPBRankingRecord& Record)
	{
		return Object.HasTypedField<EJson::Number>(TEXT("user_id")) &&
			Object.TryGetNumberField(TEXT("user_id"), Record.UserId) && Record.UserId > 0 &&
			Object.HasTypedField<EJson::String>(TEXT("username")) &&
			Object.TryGetStringField(TEXT("username"), Record.PlayerId) && !Record.PlayerId.IsEmpty();
	}

	bool ReadClear(const FJsonObject& Object, FPBRankingRecord& Record)
	{
		FDateTime ParsedTime;
		if (!Object.HasTypedField<EJson::Number>(TEXT("elapsed_ms")) ||
			!Object.TryGetNumberField(TEXT("elapsed_ms"), Record.ClearTimeMs) || Record.ClearTimeMs < 0 ||
			!Object.TryGetStringField(TEXT("cleared_at"), Record.ClearedAt) ||
			!FDateTime::ParseIso8601(*Record.ClearedAt, ParsedTime))
		{
			return false;
		}
		const int32 Minutes = Record.ClearTimeMs / 60000;
		const int32 Seconds = Record.ClearTimeMs / 1000 % 60;
		const int32 Milliseconds = Record.ClearTimeMs % 1000;
		Record.ClearTimeText = FText::FromString(Minutes > 0
			? FString::Printf(TEXT("%dm %02d.%03ds"), Minutes, Seconds, Milliseconds)
			: FString::Printf(TEXT("%d.%03ds"), Seconds, Milliseconds));
		return true;
	}
}

void UPBRankingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Api = Collection.InitializeDependency<UPatternBreakerApiSubsystem>();
	if (Api) Api->OnAuthSessionChanged.AddUObject(this, &ThisClass::ClearPersonalOnAuthChange);
}

void UPBRankingSubsystem::Deinitialize()
{
	if (Api) Api->OnAuthSessionChanged.RemoveAll(this);
	CloseRanking();
	Api = nullptr;
	Super::Deinitialize();
}

void UPBRankingSubsystem::RefreshRanking()
{
	CloseRanking();
	const uint64 Top = TopGeneration;
	const uint64 Personal = PersonalGeneration;
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	const bool bSignedIn = Api && Api->GetCachedUser(User, Remaining);
	TopState = Api ? EPBRankingState::Loading : EPBRankingState::Error;
	PersonalState = bSignedIn ? EPBRankingState::Loading : EPBRankingState::Idle;
	if (!Api) TopResponse.Result = EResult::InvalidConfiguration;
	OnRankingDataChanged.Broadcast();
	if (!Api || Top != TopGeneration) return;

	const TWeakObjectPtr<UPBRankingSubsystem> Self(this);
	Api->RequestRanking(false, TopRequest, [Self, Top](const FPatternBreakerAuthResponse& Result, const FString& Body)
	{
		if (auto* Ranking = Self.Get()) Ranking->HandleTop(Top, Result, Body);
	});
	if (bSignedIn && Personal == PersonalGeneration)
	{
		Api->RequestRanking(true, PersonalRequest, [Self, Personal, UserId = User.UserId](const FPatternBreakerAuthResponse& Result, const FString& Body)
		{
			if (auto* Ranking = Self.Get()) Ranking->HandlePersonal(Personal, UserId, Result, Body);
		});
	}
}

void UPBRankingSubsystem::CloseRanking()
{
	++TopGeneration;
	++PersonalGeneration;
	Cancel(TopRequest);
	Cancel(PersonalRequest);
	Records.Reset();
	PersonalBest = {};
	TopResponse = {};
	PersonalResponse = {};
	TopState = EPBRankingState::Idle;
	PersonalState = EPBRankingState::Idle;
}

void UPBRankingSubsystem::ClearPersonalOnAuthChange()
{
	++PersonalGeneration;
	Cancel(PersonalRequest);
	PersonalBest = {};
	PersonalResponse = {};
	PersonalState = EPBRankingState::Idle;
	if (TopState != EPBRankingState::Idle) OnRankingDataChanged.Broadcast();
}

void UPBRankingSubsystem::HandleTop(uint64 Generation, const FPatternBreakerAuthResponse& Response, const FString& Payload)
{
	if (Generation != TopGeneration) return;
	TopRequest.Reset();
	TopResponse = Response;
	TopState = EPBRankingState::Error;
	if (Response.Result == EResult::Ok)
	{
		const TSharedPtr<FJsonObject> Object = ReadObject(Payload);
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		TopResponse.Result = Object ? EResult::InvalidResponse : EResult::InvalidJson;
		if (Object && Object->TryGetArrayField(TEXT("rankings"), Entries) && Entries && Entries->Num() <= 100)
		{
			TArray<FPBRankingRecord> Parsed;
			Parsed.Reserve(Entries->Num());
			for (const auto& Entry : *Entries)
			{
				const TSharedPtr<FJsonObject>* Row = nullptr;
				FPBRankingRecord Record;
				if (!Entry || !Entry->TryGetObject(Row) || !Row || !Row->IsValid() ||
					!(*Row)->HasTypedField<EJson::Number>(TEXT("rank")) ||
					!(*Row)->TryGetNumberField(TEXT("rank"), Record.Rank) || Record.Rank <= 0 ||
					!ReadIdentity(**Row, Record) || !ReadClear(**Row, Record)) break;
				Parsed.Add(MoveTemp(Record));
			}
			if (Parsed.Num() == Entries->Num())
			{
				Records = MoveTemp(Parsed);
				TopResponse.Result = EResult::Ok;
				TopState = Records.IsEmpty() ? EPBRankingState::Empty : EPBRankingState::Ready;
			}
		}
	}
	OnRankingDataChanged.Broadcast();
}

void UPBRankingSubsystem::HandlePersonal(uint64 Generation, int64 RequestedUser,
	const FPatternBreakerAuthResponse& Response, const FString& Payload)
{
	if (Generation != PersonalGeneration) return;
	PersonalRequest.Reset();
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (!Api || !Api->GetCachedUser(User, Remaining) || User.UserId != RequestedUser)
	{
		ClearPersonalOnAuthChange();
		return;
	}
	PersonalResponse = Response;
	PersonalState = EPBRankingState::Error;
	if (Response.Result == EResult::Ok)
	{
		const TSharedPtr<FJsonObject> Object = ReadObject(Payload);
		FPBRankingRecord Record;
		PersonalResponse.Result = Object ? EResult::InvalidResponse : EResult::InvalidJson;
		if (Object && ReadIdentity(*Object, Record) && Record.UserId == RequestedUser)
		{
			const TSharedPtr<FJsonValue> Best = Object->TryGetField(TEXT("best_record"));
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (Best && Best->IsNull())
			{
				PersonalState = EPBRankingState::Empty;
				PersonalResponse.Result = EResult::Ok;
			}
			else if (Best && Best->TryGetObject(Row) && Row && Row->IsValid() && ReadClear(**Row, Record))
			{
				PersonalBest = MoveTemp(Record);
				PersonalState = EPBRankingState::Ready;
				PersonalResponse.Result = EResult::Ok;
			}
		}
	}
	OnRankingDataChanged.Broadcast();
}

int32 UPBRankingSubsystem::GetRankAtIndex(int32 Index) const
{
	return Records.IsValidIndex(Index) ? Records[Index].Rank : 0;
}

FPBRankingRecord UPBRankingSubsystem::GetPersonalBest() const
{
	FPBRankingRecord Best = PersonalBest;
	if (Best.UserId != 0)
	{
		const FPBRankingRecord* Match = Records.FindByPredicate([Id = Best.UserId](const FPBRankingRecord& Record)
		{
			return Record.UserId == Id;
		});
		if (Match) Best.Rank = Match->Rank;
	}
	return Best;
}
