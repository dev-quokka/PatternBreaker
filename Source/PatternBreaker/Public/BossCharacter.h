// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "BossCharacter.generated.h"

class APatternBreakerCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBossHPChanged, float, CurrentHP, float, MaxHP);

UENUM(BlueprintType)
enum class EBossActionState : uint8
{
	Preparing,
	Attacking,
	Recovering,
	Dead,
	Idle
};

UENUM(BlueprintType)
enum class EBossAttackPattern : uint8
{
	Circle,
	Dash,
	Shockwave,
	Fan
};

USTRUCT(BlueprintType)
struct FBossPatternSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pattern", meta = (ClampMin = "0.01", Units = "s"))
	float PrepareSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pattern", meta = (ClampMin = "0.01", Units = "s"))
	float AttackSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pattern", meta = (ClampMin = "0.01", Units = "s"))
	float RecoverSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pattern", meta = (ClampMin = "0.0"))
	float Damage = 50.0f;
};

USTRUCT(BlueprintType)
struct FBossAttackPreview
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	EBossAttackPattern Pattern = EBossAttackPattern::Circle;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	FVector Direction = FVector::ForwardVector;

	/** Radius, reach, or planned dash distance in cm, depending on the pattern. */
	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	float Range = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	FVector DashEnd = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	float DashHalfWidth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	float WaveThickness = 0.0f;

	/** Full angle of the fan in degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	float FanAngleDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	float HeightTolerance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pattern")
	FBossPatternSettings Timing;
};

UCLASS()
class PATTERNBREAKER_API ABossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABossCharacter();
	void StartRaidActionCycle();
	void StopRaidActionCycle();

	// 보스 최대 체력
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	float MaxHP = 300.0f;

	/*
		EditAnywhere: 에디터에서 값을 변경할 수 있음
		BlueprintReadOnly: 블루프린트에서 값을 읽을 수 있음
		Category(Boss): 에디터에서 Boss 항목으로 분류
	*/

	// 보스 현재 체력
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	float CurrentHP = 300.0f;

	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnBossHPChanged OnHPChanged;

	/*
		VisibleAnywhere: 에디터에서 값을 확인할 수 있지만 직접 수정할 수는 없음
	*/

	UFUNCTION(BlueprintCallable, Category = "Boss")
	void ApplyBossDamage(float Damage);

	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsBossDead() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Circle", meta = (ClampMin = "1.0", Units = "cm"))
	float AttackRadius = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Circle")
	FBossPatternSettings CircleSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Dash")
	FBossPatternSettings DashSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Dash", meta = (ClampMin = "1.0", ClampMax = "3000.0", Units = "cm"))
	float DashDistance = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHalfWidth = 110.0f;

	/** Additional horizontal limit measured from the boss's spawn position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DashArenaRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Shockwave")
	FBossPatternSettings ShockwaveSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Shockwave", meta = (ClampMin = "1.0", Units = "cm"))
	float ShockwaveRadius = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Shockwave", meta = (ClampMin = "1.0", Units = "cm"))
	float ShockwaveThickness = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Fan")
	FBossPatternSettings FanSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Fan", meta = (ClampMin = "1.0", Units = "cm"))
	float FanRange = 520.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns|Fan", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float FanAngleDegrees = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns", meta = (ClampMin = "1.0", Units = "cm"))
	float AttackHeightTolerance = 180.0f;

	/** -1 uses a fresh seed on each raid. A fixed seed reproduces the selection sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns", meta = (ClampMin = "-1"))
	int32 PatternSeed = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Patterns")
	FBossAttackPreview CurrentAttack;

	/** Current leading edge in cm while a shockwave is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Patterns")
	float CurrentWaveRadius = 0.0f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|Patterns")
	void OnBossAttackPreparing(const FBossAttackPreview& Preview);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|Patterns")
	void OnBossAttackStarted(const FBossAttackPreview& Preview);

	/** Also fires if a preparation or an active attack is cancelled. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|Patterns")
	void OnBossAttackEnded(const FBossAttackPreview& Preview, bool bCancelled);

	/** Visual notification only; damage and timing are handled in C++. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss")
	void OnBossStateChanged(EBossActionState NewState);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss", meta = (AllowPrivateAccess = "true"))
	EBossActionState ActionState = EBossActionState::Idle;

	FTimerHandle ActionTimerHandle;
	FTimerHandle AttackUpdateTimerHandle;
	FRandomStream PatternRandom;
	TOptional<EBossAttackPattern> PreviousPattern;
	TSet<TWeakObjectPtr<APatternBreakerCharacter>> HitPlayers;
	FVector ArenaCenter = FVector::ZeroVector;
	double AttackStartedAt = 0.0;
	bool bCycleRunning = false;
	bool bPatternOpen = false;
	bool bDashBlocked = false;

	bool CanRunCycle() const;
	void SetActionState(EBossActionState NewState);
	void AdvanceActionState();
	void PrepareAttack();
	FVector PlanDashEnd() const;
	void EndPattern(bool bCancelled);
	void TickActiveAttack();
	void UpdateActiveAttack(float Progress);
	void DamagePlayers(TFunctionRef<bool(const FVector&)> IsInRange);

};
