// Fill out your copyright notice in the Description page of Project Settings.

#include "BossCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PatternBreakerCharacter.h"
#include "PatternBreakerGameMode.h"

namespace
{
	constexpr float AttackUpdateInterval = 0.02f;
	constexpr float MinPhaseSeconds = 0.01f;
}

ABossCharacter::ABossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	ArenaCenter = GetActorLocation();
	CurrentHP = MaxHP;
	OnHPChanged.Broadcast(CurrentHP, MaxHP);
	if (IsBossDead())
	{
		StopRaidActionCycle();
	}
}

void ABossCharacter::StartRaidActionCycle()
{
	if (bCycleRunning)
	{
		return;
	}

	bCycleRunning = true;
	if (!CanRunCycle())
	{
		StopRaidActionCycle();
		return;
	}

	PreviousPattern.Reset();
	if (PatternSeed == INDEX_NONE)
	{
		PatternRandom.GenerateNewSeed();
	}
	else
	{
		PatternRandom.Initialize(PatternSeed);
	}
	SetActionState(EBossActionState::Preparing);
}

void ABossCharacter::StopRaidActionCycle()
{
	bCycleRunning = false;
	GetWorldTimerManager().ClearTimer(ActionTimerHandle);
	GetWorldTimerManager().ClearTimer(AttackUpdateTimerHandle);

	const EBossActionState StoppedState = IsBossDead() ? EBossActionState::Dead : EBossActionState::Idle;
	const bool bStateChanged = ActionState != StoppedState;
	ActionState = StoppedState;
	EndPattern(true);
	if (bStateChanged)
	{
		OnBossStateChanged(StoppedState);
	}
}

void ABossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRaidActionCycle();
	Super::EndPlay(EndPlayReason);
}

void ABossCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABossCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ABossCharacter::ApplyBossDamage(float Damage)
{
	if (Damage <= 0.0f || !CanRunCycle())
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
		StopRaidActionCycle();
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

bool ABossCharacter::CanRunCycle() const
{
	const UWorld* World = GetWorld();
	const APatternBreakerGameMode* RaidMode = World ? World->GetAuthGameMode<APatternBreakerGameMode>() : nullptr;
	return bCycleRunning && !IsBossDead() && RaidMode && RaidMode->IsRaidInProgress() && RaidMode->GetRaidRemainingTime() > 0.0f;
}

void ABossCharacter::PrepareAttack()
{
	constexpr EBossAttackPattern Patterns[] = {
		EBossAttackPattern::Circle, EBossAttackPattern::Dash, EBossAttackPattern::Shockwave, EBossAttackPattern::Fan
	};
	TArray<EBossAttackPattern, TInlineAllocator<UE_ARRAY_COUNT(Patterns)>> Candidates;
	for (const EBossAttackPattern Pattern : Patterns)
	{
		if (!PreviousPattern.IsSet() || Pattern != PreviousPattern.GetValue())
		{
			Candidates.Add(Pattern);
		}
	}

	// Use high bits of the stream and discard the uneven tail before indexing.
	constexpr uint32 SampleCount = 1u << 24;
	const uint32 CandidateCount = static_cast<uint32>(Candidates.Num());
	const uint32 Limit = SampleCount - SampleCount % CandidateCount;
	uint32 Sample;
	do
	{
		Sample = PatternRandom.GetUnsignedInt() >> 8;
	}
	while (Sample >= Limit);

	CurrentAttack = {};
	CurrentAttack.Pattern = Candidates[Sample % CandidateCount];
	PreviousPattern = CurrentAttack.Pattern;
	CurrentAttack.Origin = GetActorLocation();
	CurrentAttack.Direction = GetActorForwardVector().GetSafeNormal2D();
	CurrentAttack.DashEnd = CurrentAttack.Origin;
	CurrentAttack.HeightTolerance = FMath::Max(1.0f, AttackHeightTolerance);

	if (CurrentAttack.Pattern == EBossAttackPattern::Dash || CurrentAttack.Pattern == EBossAttackPattern::Fan)
	{
		double NearestDistanceSq = TNumericLimits<double>::Max();
		for (TActorIterator<APatternBreakerCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsPlayerControlled() || It->IsPlayerDead()) continue;
			const FVector Offset = It->GetActorLocation() - CurrentAttack.Origin;
			const double DistanceSq = Offset.SizeSquared2D();
			if (DistanceSq > UE_KINDA_SMALL_NUMBER && DistanceSq < NearestDistanceSq)
			{
				NearestDistanceSq = DistanceSq;
				CurrentAttack.Direction = Offset.GetSafeNormal2D();
			}
		}
		SetActorRotation(CurrentAttack.Direction.Rotation());
	}

	switch (CurrentAttack.Pattern)
	{
	case EBossAttackPattern::Circle:
		CurrentAttack.Range = FMath::Max(1.0f, AttackRadius);
		CurrentAttack.Timing = CircleSettings;
		break;
	case EBossAttackPattern::Dash:
		CurrentAttack.DashEnd = PlanDashEnd();
		CurrentAttack.Range = FVector::Dist2D(CurrentAttack.Origin, CurrentAttack.DashEnd);
		CurrentAttack.DashHalfWidth = FMath::Max(1.0f, DashHalfWidth);
		CurrentAttack.Timing = DashSettings;
		break;
	case EBossAttackPattern::Shockwave:
		CurrentAttack.Range = FMath::Max(1.0f, ShockwaveRadius);
		CurrentAttack.WaveThickness = FMath::Clamp(ShockwaveThickness, 1.0f, CurrentAttack.Range);
		CurrentAttack.Timing = ShockwaveSettings;
		break;
	case EBossAttackPattern::Fan:
		CurrentAttack.Range = FMath::Max(1.0f, FanRange);
		CurrentAttack.FanAngleDegrees = FMath::Clamp(FanAngleDegrees, 1.0f, 180.0f);
		CurrentAttack.Timing = FanSettings;
		break;
	}

	CurrentAttack.Timing.PrepareSeconds = FMath::Max(MinPhaseSeconds, CurrentAttack.Timing.PrepareSeconds);
	CurrentAttack.Timing.AttackSeconds = FMath::Max(MinPhaseSeconds, CurrentAttack.Timing.AttackSeconds);
	CurrentAttack.Timing.RecoverSeconds = FMath::Max(MinPhaseSeconds, CurrentAttack.Timing.RecoverSeconds);
	CurrentAttack.Timing.Damage = FMath::Max(0.0f, CurrentAttack.Timing.Damage);
	HitPlayers.Reset();
	CurrentWaveRadius = 0.0f;
	bDashBlocked = false;
	bPatternOpen = true;
}

FVector ABossCharacter::PlanDashEnd() const
{
	const FVector Origin = CurrentAttack.Origin;
	const FVector Direction = CurrentAttack.Direction;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float FloorReach = Movement->MaxStepHeight + 5.0f;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(BossDashPath), false, this);
	// Use the capsule's blocking responses so dynamic floors can support the dash too.
	FCollisionResponseParams FloorResponses(Capsule->GetCollisionResponseToChannels());
	FloorResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);

	FCollisionObjectQueryParams Obstacles(ECC_WorldStatic);
	Obstacles.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		FMath::Max(1.0f, Radius - 2.0f), FMath::Max(1.0f, HalfHeight - 2.0f));
	float MaxTravel = FMath::Clamp(DashDistance, 0.0f, 3000.0f);
	const float RequestedTravel = MaxTravel;
	FHitResult Wall;
	const bool bSweepHit = GetWorld()->SweepSingleByObjectType(Wall, Origin, Origin + Direction * MaxTravel,
		FQuat::Identity, Obstacles, Shape, Query);
	if (bSweepHit)
	{
		MaxTravel = FMath::Max(0.0f, static_cast<float>(Wall.Distance) - 1.0f);
	}

	const float Stride = FMath::Max(5.0f, Radius * 0.5f);
	const double ArenaLimitSq = FMath::Square(FMath::Max(0.0f, DashArenaRadius - Radius));
	const int32 Steps = FMath::CeilToInt(MaxTravel / Stride);
	FVector End = Origin;
	const TCHAR* StopReason = RequestedTravel <= 0.0f ? TEXT("DashDistanceZero") :
		bSweepHit ? (Wall.bStartPenetrating ? TEXT("SweepStartPenetrating") : TEXT("SweepHit")) : TEXT("FullDistance");
	FHitResult Floor;
	FVector LastFeet = Origin - FVector::UpVector * HalfHeight;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector Candidate = Origin + Direction * FMath::Min(MaxTravel, Step * Stride);
		if (FVector::DistSquared2D(Candidate, ArenaCenter) > ArenaLimitSq)
		{
			StopReason = TEXT("ArenaRadius");
			break;
		}

		// Check ahead of the capsule center so the dash stops before reaching a ledge.
		LastFeet = Candidate + Direction * Radius - FVector::UpVector * HalfHeight;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, LastFeet + FVector::UpVector * FloorReach,
			LastFeet - FVector::UpVector * FloorReach, Capsule->GetCollisionObjectType(), Query, FloorResponses))
		{
			StopReason = TEXT("FloorTraceMiss");
			break;
		}
		if (Floor.ImpactNormal.Z < Movement->GetWalkableFloorZ())
		{
			StopReason = TEXT("FloorTooSteep");
			break;
		}
		End = Candidate;
	}
	const UPrimitiveComponent* FloorComponent = Floor.GetComponent();
	UE_LOG(LogTemp, Log, TEXT("Boss [%s] dash plan: reason=%s range=%.2f requested=%.2f start=%s end=%s capsule=(%.2f, %.2f) "
		"arenaRadius=%.2f startFromCenter=%.2f sweepHit=%d startPenetrating=%d hitDistance=%.2f penetrationDepth=%.2f hit=%s hitNormal=%s "
		"floorProbe=%s floorReach=%.2f floor=%s floorType=%s floorNormalZ=%.2f"),
		*GetName(), StopReason, FVector::Dist2D(Origin, End), RequestedTravel, *Origin.ToCompactString(), *End.ToCompactString(),
		Radius, HalfHeight, DashArenaRadius, FVector::Dist2D(Origin, ArenaCenter), bSweepHit, Wall.bStartPenetrating,
		Wall.Distance, Wall.PenetrationDepth, *GetNameSafe(Wall.GetComponent()), *Wall.ImpactNormal.ToCompactString(),
		*LastFeet.ToCompactString(), FloorReach, *GetNameSafe(FloorComponent),
		FloorComponent ? *UEnum::GetValueAsString(FloorComponent->GetCollisionObjectType()) : TEXT("None"), Floor.ImpactNormal.Z);
	return End;
}

void ABossCharacter::SetActionState(EBossActionState NewState)
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(ActionTimerHandle);
	Timers.ClearTimer(AttackUpdateTimerHandle);
	ActionState = NewState;

	if (NewState == EBossActionState::Preparing) PrepareAttack();
	if (NewState == EBossActionState::Attacking) AttackStartedAt = GetWorld()->GetTimeSeconds();
	OnBossStateChanged(NewState);
	if (!CanRunCycle() || ActionState != NewState) return;

	float Duration;
	switch (NewState)
	{
	case EBossActionState::Preparing:
		Duration = CurrentAttack.Timing.PrepareSeconds;
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: preparing %s."), *GetName(), *UEnum::GetValueAsString(CurrentAttack.Pattern));
		OnBossAttackPreparing(CurrentAttack);
		break;
	case EBossActionState::Attacking:
		Duration = CurrentAttack.Timing.AttackSeconds;
		OnBossAttackStarted(CurrentAttack);
		if (!CanRunCycle() || ActionState != NewState) return;
		UpdateActiveAttack(0.0f);
		if (CanRunCycle() && (CurrentAttack.Pattern == EBossAttackPattern::Dash || CurrentAttack.Pattern == EBossAttackPattern::Shockwave))
		{
			FTimerManagerTimerParameters UpdateParameters;
			UpdateParameters.bLoop = true;
			UpdateParameters.bMaxOncePerFrame = true;
			Timers.SetTimer(AttackUpdateTimerHandle, this, &ThisClass::TickActiveAttack, AttackUpdateInterval, UpdateParameters);
		}
		break;
	case EBossActionState::Recovering:
		Duration = CurrentAttack.Timing.RecoverSeconds;
		EndPattern(false);
		break;
	default:
		return;
	}

	if (CanRunCycle() && ActionState == NewState)
	{
		Timers.SetTimer(ActionTimerHandle, this, &ThisClass::AdvanceActionState, Duration, false);
	}
}

void ABossCharacter::AdvanceActionState()
{
	if (!CanRunCycle())
	{
		StopRaidActionCycle();
		return;
	}

	switch (ActionState)
	{
	case EBossActionState::Preparing:
		SetActionState(EBossActionState::Attacking);
		break;
	case EBossActionState::Attacking:
		if (CurrentAttack.Pattern == EBossAttackPattern::Dash || CurrentAttack.Pattern == EBossAttackPattern::Shockwave)
		{
			UpdateActiveAttack(1.0f);
		}
		if (CanRunCycle()) SetActionState(EBossActionState::Recovering);
		break;
	case EBossActionState::Recovering:
		SetActionState(EBossActionState::Preparing);
		break;
	default:
		break;
	}
}

void ABossCharacter::EndPattern(bool bCancelled)
{
	CurrentWaveRadius = 0.0f;
	HitPlayers.Reset();
	if (bPatternOpen)
	{
		bPatternOpen = false;
		OnBossAttackEnded(CurrentAttack, bCancelled);
	}
}

void ABossCharacter::TickActiveAttack()
{
	if (!CanRunCycle())
	{
		StopRaidActionCycle();
		return;
	}
	const double Elapsed = GetWorld()->GetTimeSeconds() - AttackStartedAt;
	UpdateActiveAttack(FMath::Clamp(static_cast<float>(Elapsed) / CurrentAttack.Timing.AttackSeconds, 0.0f, 1.0f));
}

void ABossCharacter::UpdateActiveAttack(float Progress)
{
	const FVector Origin = CurrentAttack.Origin;
	const double Range = CurrentAttack.Range;
	const double HeightTolerance = CurrentAttack.HeightTolerance;
	switch (CurrentAttack.Pattern)
	{
	case EBossAttackPattern::Circle:
		DamagePlayers([Origin, Range](const FVector& Location)
		{
			return FVector::DistSquared(Origin, Location) <= FMath::Square(Range);
		});
		break;
	case EBossAttackPattern::Dash:
	{
		if (bDashBlocked) return;
		const FVector Start = GetActorLocation();
		FVector Target = FMath::Lerp(Origin, CurrentAttack.DashEnd, Progress);
		Target.Z = Start.Z;
		if (FVector::DotProduct(Target - Start, CurrentAttack.Direction) <= UE_KINDA_SMALL_NUMBER) return;

		FHitResult Hit;
		SetActorLocation(Target, true, &Hit);
		bDashBlocked = Hit.bBlockingHit;
		const FVector End = GetActorLocation();
		if (bDashBlocked)
		{
			UE_LOG(LogTemp, Log, TEXT("Boss [%s] dash movement blocked: planned=%.2f moved=%.2f startPenetrating=%d hitDistance=%.2f hit=%s"),
				*GetName(), Range, FVector::Dist2D(Origin, End), Hit.bStartPenetrating, Hit.Distance, *GetNameSafe(Hit.GetComponent()));
		}
		DamagePlayers([Start, End, HeightTolerance, Width = CurrentAttack.DashHalfWidth](const FVector& Location)
		{
			return FMath::Abs(Location.Z - Start.Z) <= HeightTolerance &&
				FMath::PointDistToSegmentSquared(FVector(Location.X, Location.Y, 0.0),
					FVector(Start.X, Start.Y, 0.0), FVector(End.X, End.Y, 0.0)) <= FMath::Square(Width);
		});
		break;
	}
	case EBossAttackPattern::Shockwave:
	{
		const float Outer = CurrentAttack.Range * Progress;
		if (Outer <= CurrentWaveRadius) return;
		// Sweep the moving band between updates so a long frame cannot skip a stationary player.
		const float Inner = FMath::Max(0.0f, CurrentWaveRadius - CurrentAttack.WaveThickness);
		CurrentWaveRadius = Outer;
		DamagePlayers([Origin, Inner, Outer, HeightTolerance](const FVector& Location)
		{
			const double DistanceSq = FVector::DistSquared2D(Origin, Location);
			return FMath::Abs(Location.Z - Origin.Z) <= HeightTolerance &&
				DistanceSq >= FMath::Square(Inner) && DistanceSq <= FMath::Square(Outer);
		});
		break;
	}
	case EBossAttackPattern::Fan:
	{
		const double MinDot = FMath::Cos(FMath::DegreesToRadians(CurrentAttack.FanAngleDegrees * 0.5));
		DamagePlayers([Origin, Range, HeightTolerance, MinDot, Direction = CurrentAttack.Direction](const FVector& Location)
		{
			const FVector Offset = Location - Origin;
			return FMath::Abs(Offset.Z) <= HeightTolerance && Offset.SizeSquared2D() <= FMath::Square(Range) &&
				(Offset.SizeSquared2D() <= UE_KINDA_SMALL_NUMBER || FVector::DotProduct(Direction, Offset.GetSafeNormal2D()) >= MinDot);
		});
		break;
	}
	}
}

void ABossCharacter::DamagePlayers(TFunctionRef<bool(const FVector&)> IsInRange)
{
	if (!CanRunCycle() || ActionState != EBossActionState::Attacking || CurrentAttack.Timing.Damage <= 0.0f) return;
	for (TActorIterator<APatternBreakerCharacter> It(GetWorld()); It && CanRunCycle(); ++It)
	{
		APatternBreakerCharacter* Player = *It;
		if (!Player->IsPlayerControlled() || Player->IsPlayerDead() || HitPlayers.Contains(Player) ||
			!IsInRange(Player->GetActorLocation()))
		{
			continue;
		}

		HitPlayers.Add(Player);
		UE_LOG(LogTemp, Log, TEXT("Boss [%s]: %s hit [%s] for %.1f."), *GetName(),
			*UEnum::GetValueAsString(CurrentAttack.Pattern), *Player->GetName(), CurrentAttack.Timing.Damage);
		Player->ApplyPlayerDamage(CurrentAttack.Timing.Damage);
	}
}
