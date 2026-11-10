// Fill out your copyright notice in the Description page of Project Settings.


#include "PBRankingSubsystem.h"

TArray<FPBRankingRecord> UPBRankingSubsystem::GetTestRankingData() const
{
	constexpr int32 TestRecordCount = 25;
	constexpr int32 FirstClearTimeMs = 35420;
	constexpr int32 ClearTimeStepMs = 1137;

	TArray<FPBRankingRecord> Records;
	Records.Reserve(TestRecordCount);
	for (int32 Index = 0; Index < TestRecordCount; ++Index)
	{
		FPBRankingRecord& Record = Records.AddDefaulted_GetRef();
		Record.PlayerId = FString::Printf(TEXT("TestPlayer%02d"), Index + 1);
		Record.ClearTimeMs = FirstClearTimeMs + Index * ClearTimeStepMs;
	}

	return Records;
}
