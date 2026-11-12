// Copyright Epic Games, Inc. All Rights Reserved.

#include "PatternBreakerCharacter.h"
#include "BossCharacter.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "PatternBreaker.h"
#include "PatternBreakerGameMode.h"

APatternBreakerCharacter::APatternBreakerCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void APatternBreakerCharacter::BeginPlay()
{
	Super::BeginPlay();

	CurrentPlayerHP = FMath::Max(0.0f, MaxPlayerHP);
	OnHPChanged.Broadcast(CurrentPlayerHP, MaxPlayerHP);
	UE_LOG(LogPatternBreaker, Log, TEXT("Player [%s] HP: %.2f / %.2f, Dead: %s"),
		*GetName(), CurrentPlayerHP, MaxPlayerHP, IsPlayerDead() ? TEXT("true") : TEXT("false"));
}

void APatternBreakerCharacter::ApplyPlayerDamage(float Damage)
{
	if (Damage <= 0.0f || IsPlayerDead())
	{
		return;
	}

	const float PreviousHP = CurrentPlayerHP;
	CurrentPlayerHP = FMath::Max(0.0f, CurrentPlayerHP - Damage);
	UE_LOG(LogPatternBreaker, Log, TEXT("Player [%s] HP: %.2f / %.2f, Dead: %s"),
		*GetName(), CurrentPlayerHP, MaxPlayerHP, IsPlayerDead() ? TEXT("true") : TEXT("false"));

	if (IsPlayerDead())
	{
		StopJumping();
		ConsumeMovementInputVector();
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		UE_LOG(LogPatternBreaker, Log, TEXT("Player [%s] died."), *GetName());
	}

	if (CurrentPlayerHP != PreviousHP)
	{
		OnHPChanged.Broadcast(CurrentPlayerHP, MaxPlayerHP);
	}
}

bool APatternBreakerCharacter::IsPlayerDead() const
{
	return CurrentPlayerHP <= 0.0f;
}

void APatternBreakerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &APatternBreakerCharacter::AttackBossForTest);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &APatternBreakerCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &APatternBreakerCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APatternBreakerCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &APatternBreakerCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APatternBreakerCharacter::Look);
	}
	else
	{
		UE_LOG(LogPatternBreaker, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void APatternBreakerCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void APatternBreakerCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void APatternBreakerCharacter::DoMove(float Right, float Forward)
{
	if (IsPlayerDead())
	{
		return;
	}

	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void APatternBreakerCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void APatternBreakerCharacter::DoJumpStart()
{
	if (IsPlayerDead())
	{
		return;
	}
	const APatternBreakerGameMode* RaidMode = GetWorld() ? GetWorld()->GetAuthGameMode<APatternBreakerGameMode>() : nullptr;
	if (RaidMode && RaidMode->IsRaidWaitingToStart())
	{
		return;
	}

	// signal the character to jump
	Jump();
}

void APatternBreakerCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void APatternBreakerCharacter::AttackBossForTest()
{
	if (IsPlayerDead())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APatternBreakerGameMode* RaidMode = World->GetAuthGameMode<APatternBreakerGameMode>();
	if (RaidMode && !RaidMode->IsRaidInProgress())
	{
		return;
	}

	ABossCharacter* NearestBoss = nullptr;
	double NearestDistanceSquared = FMath::Square(300.0);
	const FVector PlayerLocation = GetActorLocation();

	for (TActorIterator<ABossCharacter> It(World); It; ++It)
	{
		ABossCharacter* Boss = *It;
		if (!IsValid(Boss) || Boss->IsBossDead())
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(PlayerLocation, Boss->GetActorLocation());
		if (DistanceSquared <= NearestDistanceSquared)
		{
			NearestBoss = Boss;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	if (NearestBoss)
	{
		NearestBoss->ApplyBossDamage(30.0f);
	}
}
