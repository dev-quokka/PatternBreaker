// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "BossCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBossHPChanged, float, CurrentHP, float, MaxHP);

UENUM(BlueprintType)
enum class EBossActionState : uint8
{
	Preparing,
	Attacking,
	Recovering,
	Dead
};

UCLASS()
class PATTERNBREAKER_API ABossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABossCharacter();

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	float AttackRadius = 350.0f;

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
	EBossActionState ActionState = EBossActionState::Preparing;

	FTimerHandle ActionTimerHandle;

	void SetActionState(EBossActionState NewState);
	void AdvanceActionState();
	void PerformAreaAttack();

};
