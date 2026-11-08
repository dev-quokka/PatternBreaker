// Fill out your copyright notice in the Description page of Project Settings.


#include "BossCharacter.h"
#include "PatternBreakerCharacter.h"
#include "EngineUtils.h"

// Sets default values
ABossCharacter::ABossCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

}

// Called when the game starts or when spawned
void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	CurrentHP = MaxHP;
	OnHPChanged.Broadcast(CurrentHP, MaxHP);
	SetActionState(IsBossDead() ? EBossActionState::Dead : EBossActionState::Preparing);
}

void ABossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ActionTimerHandle);
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABossCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ABossCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void ABossCharacter::ApplyBossDamage(float Damage)
{
	if (Damage <= 0.0f || IsBossDead())
	{
		return;
	}

	if (ActionState != EBossActionState::Recovering)
	{
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: damage ignored (not recovering)."), *GetName());
		return;
	}

	const float PreviousHP = CurrentHP;
	CurrentHP = FMath::Max(0.0f, CurrentHP - Damage);
	UE_LOG(LogTemp, Log, TEXT("Boss HP: %.2f / %.2f"), CurrentHP, MaxHP);

	if (IsBossDead())
	{
		SetActionState(EBossActionState::Dead);
	}

	if (CurrentHP != PreviousHP)
	{
		OnHPChanged.Broadcast(CurrentHP, MaxHP);
	}
}

bool ABossCharacter::IsBossDead() const
{
	return CurrentHP <= 0.0f;
}

void ABossCharacter::SetActionState(EBossActionState NewState)
{
	GetWorldTimerManager().ClearTimer(ActionTimerHandle);
	ActionState = NewState;

	float Duration = 0.0f;
	switch (ActionState)
	{
	case EBossActionState::Preparing:
		Duration = 2.0f;
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: Preparing (2s)."), *GetName());
		break;
	case EBossActionState::Attacking:
		Duration = 1.0f;
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: Attacking (1s)."), *GetName());
		PerformAreaAttack();
		break;
	case EBossActionState::Recovering:
		Duration = 2.0f;
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: Recovering (2s, vulnerable)."), *GetName());
		break;
	case EBossActionState::Dead:
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: Dead. Action cycle stopped."), *GetName());
		break;
	}

	if (Duration > 0.0f)
	{
		GetWorldTimerManager().SetTimer(ActionTimerHandle, this, &ABossCharacter::AdvanceActionState, Duration, false);
	}

	OnBossStateChanged(ActionState);
}

void ABossCharacter::AdvanceActionState()
{
	if (IsBossDead())
	{
		SetActionState(EBossActionState::Dead);
		return;
	}

	switch (ActionState)
	{
	case EBossActionState::Preparing:
		SetActionState(EBossActionState::Attacking);
		break;
	case EBossActionState::Attacking:
		SetActionState(EBossActionState::Recovering);
		break;
	case EBossActionState::Recovering:
		SetActionState(EBossActionState::Preparing);
		break;
	case EBossActionState::Dead:
		break;
	}
}

void ABossCharacter::PerformAreaAttack()
{
	UWorld* World = GetWorld();
	if (!World || IsBossDead())
	{
		return;
	}

	int32 HitCount = 0;
	const FVector BossLocation = GetActorLocation();
	const double RadiusSquared = FMath::Square(static_cast<double>(AttackRadius));
	for (TActorIterator<APatternBreakerCharacter> It(World); It; ++It)
	{
		APatternBreakerCharacter* Player = *It;
		if (!IsValid(Player) || !Player->IsPlayerControlled())
		{
			continue;
		}

		if (Player->IsPlayerDead())
		{
			UE_LOG(LogTemp, Log, TEXT("Boss [%s] area attack: player [%s] skipped (dead)."), *GetName(), *Player->GetName());
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(BossLocation, Player->GetActorLocation());
		if (DistanceSquared > RadiusSquared)
		{
			UE_LOG(LogTemp, Log, TEXT("Boss [%s] area attack: player [%s] missed (%.1f cm, radius %.1f cm)."),
				*GetName(), *Player->GetName(), FMath::Sqrt(DistanceSquared), AttackRadius);
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("Boss [%s] area attack: player [%s] hit (20 damage, %.1f cm)."),
			*GetName(), *Player->GetName(), FMath::Sqrt(DistanceSquared));
		Player->ApplyPlayerDamage(20.0f);
		++HitCount;
	}

	UE_LOG(LogTemp, Log, TEXT("Boss [%s] area attack complete: %d player(s) hit."), *GetName(), HitCount);
}
