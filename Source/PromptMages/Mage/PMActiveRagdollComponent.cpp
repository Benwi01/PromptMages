// Prompt-Mages: the active-ragdoll body and two independent grabbing arms (GDD 2.1).

#include "PMActiveRagdollComponent.h"
#include "PMSpawnedObject.h"
#include "PMSpellcasterComponent.h"
#include "PMWandComponent.h"
#include "PMSpellSubsystem.h"
#include "PromptMages.h"
#include "PromptMagesCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

namespace PMRagdoll
{
	const FName Pelvis(TEXT("pelvis"));
	const FName Spine(TEXT("spine_02"));
	const FName Head(TEXT("head"));
	const FName ThighL(TEXT("thigh_l"));
	const FName ThighR(TEXT("thigh_r"));

	FPhysicalAnimationData Drive(float Orientation, float AngularVelocity)
	{
		FPhysicalAnimationData D;
		D.bIsLocalSimulation = true;
		D.OrientationStrength = Orientation;
		D.AngularVelocityStrength = AngularVelocity;
		D.PositionStrength = 0.f;
		D.VelocityStrength = 0.f;
		D.MaxLinearForce = 0.f;
		D.MaxAngularForce = 0.f;
		return D;
	}

	FPhysicalAnimationData Scaled(FPhysicalAnimationData D, float Scale)
	{
		D.OrientationStrength *= Scale;
		D.AngularVelocityStrength *= Scale;
		D.PositionStrength *= Scale;
		D.VelocityStrength *= Scale;
		return D;
	}
}

UPMActiveRagdollComponent::UPMActiveRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After CharacterMovement and physics: we read simulated bones, correct the capsule and place the camera.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	LegDrive = PMRagdoll::Drive(1500.f, 150.f);
	SpineDrive = PMRagdoll::Drive(3000.f, 300.f);
	ArmDrive = PMRagdoll::Drive(800.f, 80.f);
	ReachingArmDrive = PMRagdoll::Drive(80.f, 15.f);

	Arms[0].HandBone = TEXT("hand_l");
	Arms[0].UpperArmBone = TEXT("upperarm_l");
	Arms[0].ClavicleBone = TEXT("clavicle_l");
	Arms[0].Button = EKeys::LeftMouseButton;
	Arms[0].Side = -1.f;

	Arms[1].HandBone = TEXT("hand_r");
	Arms[1].UpperArmBone = TEXT("upperarm_r");
	Arms[1].ClavicleBone = TEXT("clavicle_r");
	Arms[1].Button = EKeys::RightMouseButton;
	Arms[1].Side = 1.f;
}

void UPMActiveRagdollComponent::BeginPlay()
{
	Super::BeginPlay();
	SetupBody();
	TryBindInput();
}

void UPMActiveRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FPMArmState& Arm : Arms)
	{
		ReleaseGrip(Arm, false);
		if (Arm.Reach)
		{
			Arm.Reach->ReleaseComponent();
		}
	}
	if (APlayerController* PC = BoundPC.Get())
	{
		if (ArmInput)
		{
			PC->PopInputComponent(ArmInput);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// --- Setup ---------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::SetupBody()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	Mesh = Character->GetMesh();
	Movement = Character->GetCharacterMovement();
	if (!Mesh || !Mesh->GetPhysicsAsset() || !Movement)
	{
		UE_LOG(LogPromptMages, Warning, TEXT("Active ragdoll: %s has no physics asset or movement component."), *GetNameSafe(GetOwner()));
		return;
	}

	// The visible body becomes a ragdoll that ignores its own capsule (Ragdoll profile ignores Pawn).
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));

	PhysicalAnimation = NewObject<UPhysicalAnimationComponent>(GetOwner(), TEXT("PMPhysicalAnimation"));
	PhysicalAnimation->RegisterComponent();
	PhysicalAnimation->SetSkeletalMeshComponent(Mesh);

	// Everything below the pelvis is simulated; the pelvis stays glued to the capsule.
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::Pelvis, LegDrive, false);
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::Spine, SpineDrive, true);
	Mesh->SetAllBodiesBelowSimulatePhysics(PMRagdoll::Pelvis, true, false);
	if (!bSimulateLegs)
	{
		Mesh->SetAllBodiesBelowSimulatePhysics(PMRagdoll::ThighL, false, true);
		Mesh->SetAllBodiesBelowSimulatePhysics(PMRagdoll::ThighR, false, true);
	}
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(PMRagdoll::Pelvis, 1.f, false, false);

	for (FPMArmState& Arm : Arms)
	{
		Arm.Reach = NewObject<UPhysicsHandleComponent>(GetOwner());
		Arm.Reach->RegisterComponent();
		Arm.Reach->SetLinearStiffness(ReachStiffness);
		Arm.Reach->SetLinearDamping(ReachDamping);
		Arm.Reach->SetInterpolationSpeed(25.f);
		ApplyArmDrive(Arm);
	}

	// Movement feel (GDD 2.1): 70 kg body, accel ramps over ~0.3 s, momentum carries.
	Movement->Mass = BodyMassKg;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxAcceleration = Acceleration;
	Movement->BrakingDecelerationWalking = BrakingDeceleration;
	Movement->GroundFriction = 3.f;

	// Crouch (Ctrl): the capsule shrinks and we drop the hips ourselves (see UpdateStoopAndCrouch).
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	DefaultMeshZ = Mesh->GetRelativeLocation().Z;
	StandingHalfHeight = Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	StandingEyeHeight = Character->BaseEyeHeight;

	bBodyReady = true;
}

void UPMActiveRagdollComponent::SetupLocalView()
{
	APromptMagesCharacter* Character = Cast<APromptMagesCharacter>(GetOwner());
	if (!Character || !Mesh)
	{
		return;
	}

	// Show our own full body instead of the floating template arms.
	Mesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
	Mesh->SetOwnerNoSee(false);
	Mesh->HideBoneByName(PMRagdoll::Head, PBO_None);

	if (USkeletalMeshComponent* Arms1P = Character->GetFirstPersonMesh())
	{
		Arms1P->SetVisibility(false, true);
	}

	// The camera follows the physical head ourselves (see UpdateCamera).
	if (UCameraComponent* Camera = Character->GetFirstPersonCameraComponent())
	{
		Camera->SetUsingAbsoluteLocation(true);
	}
}

APlayerController* UPMActiveRagdollComponent::GetLocalPC() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? PC : nullptr;
}

void UPMActiveRagdollComponent::TryBindInput()
{
	APlayerController* PC = GetLocalPC();
	if (!PC || BoundPC.Get() == PC || !bBodyReady)
	{
		return;
	}
	BoundPC = PC;

	ArmInput = NewObject<UInputComponent>(GetOwner(), TEXT("PMArmInput"));
	ArmInput->RegisterComponent();
	ArmInput->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &UPMActiveRagdollComponent::OnLeftPressed);
	ArmInput->BindKey(EKeys::LeftMouseButton, IE_Released, this, &UPMActiveRagdollComponent::OnLeftReleased);
	// RMB is shared with the wand (it casts while the wand is drawn), so don't consume it.
	ArmInput->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &UPMActiveRagdollComponent::OnRightPressed).bConsumeInput = false;
	ArmInput->BindKey(EKeys::RightMouseButton, IE_Released, this, &UPMActiveRagdollComponent::OnRightReleased).bConsumeInput = false;
	ArmInput->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &UPMActiveRagdollComponent::OnReachOut);
	ArmInput->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &UPMActiveRagdollComponent::OnReachIn);
	ArmInput->BindKey(EKeys::LeftShift, IE_Pressed, this, &UPMActiveRagdollComponent::OnSprintPressed);
	ArmInput->BindKey(EKeys::LeftShift, IE_Released, this, &UPMActiveRagdollComponent::OnSprintReleased);
	ArmInput->BindKey(EKeys::LeftControl, IE_Pressed, this, &UPMActiveRagdollComponent::OnCrouchPressed);
	ArmInput->BindKey(EKeys::LeftControl, IE_Released, this, &UPMActiveRagdollComponent::OnCrouchReleased);
	PC->PushInputComponent(ArmInput);

	SetupLocalView();
}

bool UPMActiveRagdollComponent::GetView(FVector& OutLocation, FRotator& OutRotation) const
{
	if (APlayerController* PC = BoundPC.Get())
	{
		PC->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}
	return false;
}

bool UPMActiveRagdollComponent::IsWandHand(const FPMArmState& Arm) const
{
	if (Arm.Side < 0.f)
	{
		return false;
	}
	const UPMWandComponent* Wand = GetOwner()->FindComponentByClass<UPMWandComponent>();
	return Wand && Wand->IsWandOut();
}

bool UPMActiveRagdollComponent::IsSpellbookOpen() const
{
	const UPMSpellcasterComponent* Caster = GetOwner()->FindComponentByClass<UPMSpellcasterComponent>();
	return Caster && Caster->IsSpellbookOpen();
}

void UPMActiveRagdollComponent::Toast(const FString& Message) const
{
	if (UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>())
	{
		Spells->Toast(Message);
	}
}

// --- Input ---------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::OnReachOut()
{
	ReachDistance = FMath::Clamp(ReachDistance + 10.f, ReachDistanceRange.X, ReachDistanceRange.Y);
}

void UPMActiveRagdollComponent::OnReachIn()
{
	ReachDistance = FMath::Clamp(ReachDistance - 10.f, ReachDistanceRange.X, ReachDistanceRange.Y);
}

void UPMActiveRagdollComponent::OnSprintPressed()
{
	bSprinting = true;
	if (Movement)
	{
		Movement->MaxWalkSpeed = SprintSpeed;
	}
	for (const FPMArmState& Arm : Arms)
	{
		ApplyArmDrive(Arm);
	}
}

void UPMActiveRagdollComponent::OnSprintReleased()
{
	bSprinting = false;
	if (Movement)
	{
		Movement->MaxWalkSpeed = WalkSpeed;
	}
	for (const FPMArmState& Arm : Arms)
	{
		ApplyArmDrive(Arm);
	}
}

void UPMActiveRagdollComponent::OnCrouchPressed()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->Crouch();
	}
}

void UPMActiveRagdollComponent::OnCrouchReleased()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->UnCrouch();
	}
}

// --- Tick ----------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bBodyReady)
	{
		return;
	}
	if (!BoundPC.IsValid())
	{
		TryBindInput();
	}

	// While typing, arms lock to their current grip (GDD 2.3). When the book closes, re-read the buttons.
	const bool bBookOpen = IsSpellbookOpen();
	if (bArmsLocked && !bBookOpen)
	{
		if (APlayerController* PC = BoundPC.Get())
		{
			for (FPMArmState& Arm : Arms)
			{
				Arm.bButtonDown = PC->IsInputKeyDown(Arm.Button);
			}
		}
	}
	bArmsLocked = bBookOpen;

	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetView(ViewLocation, ViewRotation))
	{
		ViewLocation = GetOwner()->GetActorLocation();
		ViewRotation = GetOwner()->GetActorRotation();
	}

	if (GFrameCounter % 60 == 0)
	{
		const FVector Base = GetOwner()->GetActorLocation();
		UE_LOG(LogPromptMages, Verbose, TEXT("Body (relative to capsule): pelvis %s head %s hand_l %s foot_l %s | visible %d sim %d"),
			*(Mesh->GetBoneLocation(PMRagdoll::Pelvis) - Base).ToCompactString(),
			*(Mesh->GetBoneLocation(PMRagdoll::Head) - Base).ToCompactString(),
			*(Mesh->GetBoneLocation(TEXT("hand_l")) - Base).ToCompactString(),
			*(Mesh->GetBoneLocation(TEXT("foot_l")) - Base).ToCompactString(),
			Mesh->IsVisible() ? 1 : 0, Mesh->IsSimulatingPhysics(TEXT("spine_03")) ? 1 : 0);
	}

	UpdateMotors(DeltaTime);
	UpdateStoopAndCrouch(DeltaTime);
	for (FPMArmState& Arm : Arms)
	{
		UpdateArm(Arm, DeltaTime, ViewLocation, ViewRotation);
	}
	UpdateClimb(DeltaTime);
	UpdateForcesAndLeash(DeltaTime);
	UpdateCamera(DeltaTime);
}

void UPMActiveRagdollComponent::UpdateMotors(float DeltaTime)
{
	// Motors are strong on the ground and drop in the air (GDD 2.1).
	const float Target = (Movement->IsFalling() && !bClimbing) ? AirborneMotorScale : 1.f;
	MotorScale = FMath::FInterpTo(MotorScale, Target, DeltaTime, 6.f);
	PhysicalAnimation->SetStrengthMultiplyer(MotorScale);
}

// --- Arms ----------------------------------------------------------------------------------------

bool UPMActiveRagdollComponent::IsReaching(EPMArm Arm) const
{
	const FPMArmState& A = GetArm(Arm);
	return A.Reach && A.Reach->GetGrabbedComponent() != nullptr;
}

void UPMActiveRagdollComponent::ApplyArmDrive(const FPMArmState& Arm)
{
	if (!PhysicalAnimation)
	{
		return;
	}
	const bool bActive = Arm.IsGripping() || (Arm.Reach && Arm.Reach->GetGrabbedComponent());
	FPhysicalAnimationData Data = bActive ? ReachingArmDrive : ArmDrive;
	if (bSprinting)
	{
		Data = PMRagdoll::Scaled(Data, SprintArmScale);
	}
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Arm.ClavicleBone, Data, true);
}

FVector UPMActiveRagdollComponent::GetShoulderLocation(const FPMArmState& Arm) const
{
	// Taken from the capsule rather than the floppy bone, so reach and leash feel stable.
	// Measured from the floor so crouching (smaller capsule + dropped hips) lowers it correctly.
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Shoulder = Character->GetActorLocation() + Character->GetActorRightVector() * Arm.Side * 18.f;
	Shoulder.Z = FloorZ + StandingHalfHeight * 1.5f - HipDrop;
	return Shoulder;
}

FVector UPMActiveRagdollComponent::GetReachTarget(const FPMArmState& Arm, const FVector& ViewLocation, const FRotator& ViewRotation) const
{
	const FVector Dir = ViewRotation.Vector();
	const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
	const float MaxReach = ArmLength + StoopReach * Stoop;

	// Aim at whatever the crosshair is on, if it's within reach (things on the floor, a ledge, a crate).
	// Otherwise hold the hand at the wheel-controlled distance, which is what you swing and throw with.
	FVector Target = ViewLocation + Dir * ReachDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMReachAim), false, GetOwner());
	for (const FPMArmState& Other : Arms)
	{
		if (const UPrimitiveComponent* Held = Other.GripTarget.Get())
		{
			Params.AddIgnoredActor(Held->GetOwner()); // Don't aim at what we're already carrying.
		}
	}
	FHitResult Hit;
	const float TraceLength = ReachDistance + StoopReach + ArmLength;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + Dir * TraceLength, ECC_Visibility, Params))
	{
		if (Hit.Distance < ReachDistance || Stoop > 0.2f)
		{
			Target = Hit.ImpactPoint - Dir * 4.f;
		}
	}
	Target += Right * Arm.Side * 12.f;

	const FVector Shoulder = GetShoulderLocation(Arm);
	const FVector ToTarget = Target - Shoulder;
	if (ToTarget.SizeSquared() > FMath::Square(MaxReach))
	{
		Target = Shoulder + ToTarget.GetSafeNormal() * MaxReach;
	}
	return Target;
}

void UPMActiveRagdollComponent::UpdateStoopAndCrouch(float DeltaTime)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());

	// Bend over when reaching while looking down, so hands can get to things on the floor.
	const bool bReachingByHand = !bArmsLocked && (Arms[0].bButtonDown || (Arms[1].bButtonDown && !IsWandHand(Arms[1])));
	const float LookDown = -FRotator::NormalizeAxis(Character->GetControlRotation().Pitch);
	float Target = bReachingByHand
		? FMath::Clamp((LookDown - StoopStartPitch) / FMath::Max(StoopFullPitch - StoopStartPitch, 1.f), 0.f, 1.f)
		: 0.f;
	if (bClimbing || Movement->IsFalling())
	{
		Target = 0.f;
	}
	Stoop = FMath::FInterpTo(Stoop, Target, DeltaTime, 4.f);

	// Crouch: the capsule shrinks (CharacterMovement) and we drop the hips; the relaxed legs fold under them.
	const bool bCrouched = Character->bIsCrouched;
	HipDrop = FMath::FInterpTo(HipDrop, bCrouched ? CrouchHipDrop : 0.f, DeltaTime, 6.f);
	const float CapsuleShift = StandingHalfHeight - Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	FVector MeshLocation = Mesh->GetRelativeLocation();
	MeshLocation.Z = DefaultMeshZ + CapsuleShift - HipDrop;
	Mesh->SetRelativeLocation(MeshLocation);

	if (FMath::Abs(Stoop - AppliedStoop) > 0.05f || bCrouched != bLegsRelaxed)
	{
		bLegsRelaxed = bCrouched;
		AppliedStoop = Stoop;
		ApplySpineAndLegDrives();
	}
}

void UPMActiveRagdollComponent::ApplySpineAndLegDrives()
{
	// Weaker spine motors let the reaching hands pull the torso down into a bend.
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::Spine,
		PMRagdoll::Scaled(SpineDrive, FMath::Lerp(1.f, StoopSpineScale, AppliedStoop)), true);

	const FPhysicalAnimationData Legs = bLegsRelaxed ? PMRagdoll::Scaled(LegDrive, CrouchLegScale) : LegDrive;
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::ThighL, Legs, true);
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::ThighR, Legs, true);

	// The spine call also covered the arms; put their own drives back.
	for (const FPMArmState& Arm : Arms)
	{
		ApplyArmDrive(Arm);
	}
}

FVector UPMActiveRagdollComponent::GetGripAnchor(const FPMArmState& Arm) const
{
	const UPrimitiveComponent* Target = Arm.GripTarget.Get();
	return Target ? Target->GetComponentTransform().TransformPosition(Arm.GripLocalPoint) : FVector::ZeroVector;
}

void UPMActiveRagdollComponent::StartReach(FPMArmState& Arm)
{
	Arm.Reach->GrabComponentAtLocation(Mesh, Arm.HandBone, Mesh->GetBoneLocation(Arm.HandBone));
	ApplyArmDrive(Arm);
}

void UPMActiveRagdollComponent::StopReach(FPMArmState& Arm)
{
	Arm.Reach->ReleaseComponent();
	ApplyArmDrive(Arm);
}

void UPMActiveRagdollComponent::UpdateArm(FPMArmState& Arm, float DeltaTime, const FVector& ViewLocation, const FRotator& ViewRotation)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bReaching = Arm.Reach->GetGrabbedComponent() != nullptr;

	// With the wand drawn, the right arm holds it out toward the crosshair instead of grabbing.
	if (IsWandHand(Arm) && !bArmsLocked)
	{
		if (Arm.IsGripping())
		{
			ReleaseGrip(Arm, false);
		}
		if (!bReaching)
		{
			StartReach(Arm);
		}
		const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
		FVector Target = ViewLocation + ViewRotation.Vector() * 55.f + Right * 16.f - FVector(0.f, 0.f, 12.f);
		const FVector Shoulder = GetShoulderLocation(Arm);
		if (FVector::DistSquared(Target, Shoulder) > FMath::Square(ArmLength))
		{
			Target = Shoulder + (Target - Shoulder).GetSafeNormal() * ArmLength;
		}
		Arm.Reach->SetTargetLocation(Target);
		Arm.Load = 0.f;
		Arm.Stamina = FMath::Min(1.f, Arm.Stamina + DeltaTime * GripRegenPerSecond);
		return;
	}

	// While the Spellbook is open the arm keeps doing exactly what it was doing.
	if (!bArmsLocked)
	{
		if (Arm.bButtonDown)
		{
			if (!bReaching)
			{
				StartReach(Arm);
			}

			Arm.Reach->SetTargetLocation(GetReachTarget(Arm, ViewLocation, ViewRotation));

			if (!Arm.IsGripping() && Now >= Arm.RegripBlockedUntil && Arm.Stamina > 0.15f)
			{
				TryGrip(Arm);
			}
		}
		else
		{
			if (bReaching)
			{
				StopReach(Arm);
			}
			if (Arm.IsGripping())
			{
				ReleaseGrip(Arm, false);
			}
		}
	}

	// Grip stamina drains by the load the hand is actually carrying.
	if (Arm.IsGripping())
	{
		if (!Arm.GripTarget.IsValid())
		{
			ReleaseGrip(Arm, false);
			return;
		}

		// Constraint forces come back in Newtons. Smooth them so a single jolt doesn't empty the grip.
		FVector LinearForce, AngularForce;
		Arm.Grip->GetConstraintForce(LinearForce, AngularForce);
		const float RawLoad = LinearForce.Size() / (MaxLoadPerArmKg * 9.81f);
		float Load = FMath::FInterpTo(Arm.Load, RawLoad, DeltaTime, 3.f);

		// Hanging from the world: the capsule isn't physics, so count body weight per hand ourselves.
		if (Arm.bGripStatic && !Movement->IsMovingOnGround())
		{
			int32 StaticHands = 0;
			for (const FPMArmState& Other : Arms)
			{
				StaticHands += (Other.IsGripping() && Other.bGripStatic) ? 1 : 0;
			}
			Load = FMath::Max(Load, 1.f / FMath::Max(StaticHands, 1));
		}

		Arm.Load = FMath::Clamp(Load, 0.05f, 50.f);
		if (GFrameCounter % 30 == 0)
		{
			UE_LOG(LogPromptMages, Verbose, TEXT("%s grip: force %s, load %.2f, stamina %.2f"), Arm.Side < 0.f ? TEXT("Left") : TEXT("Right"),
				*LinearForce.ToCompactString(), Arm.Load, Arm.Stamina);
		}
		const float Drain = Arm.Load / GripSecondsAtMaxLoad * (bSprinting ? 1.5f : 1.f);
		Arm.Stamina -= DeltaTime * Drain;
		if (Arm.Stamina <= 0.f)
		{
			Arm.Stamina = 0.f;
			ReleaseGrip(Arm, true);
			Arm.RegripBlockedUntil = Now + 1.0;
		}
	}
	else
	{
		Arm.Load = 0.f;
		Arm.Stamina = FMath::Min(1.f, Arm.Stamina + DeltaTime * GripRegenPerSecond);
	}
}

void UPMActiveRagdollComponent::TryGrip(FPMArmState& Arm)
{
	// Grip from the palm (between the wrist and the knuckles), not the wrist: that's what touches things.
	FVector Hand = Mesh->GetBoneLocation(Arm.HandBone);
	const FName Knuckle(Arm.Side < 0.f ? TEXT("middle_01_l") : TEXT("middle_01_r"));
	if (Mesh->GetBoneIndex(Knuckle) != INDEX_NONE)
	{
		Hand = (Hand + Mesh->GetBoneLocation(Knuckle)) * 0.5f;
	}

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_Destructible);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMGrip), false, GetOwner());

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Hand, FQuat::Identity, Objects, FCollisionShape::MakeSphere(GripRadius), Params);

	UPrimitiveComponent* Best = nullptr;
	int32 BestItem = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Comp = Overlap.GetComponent();
		if (!Comp || Comp->GetOwner() == GetOwner())
		{
			continue;
		}
		const ECollisionEnabled::Type Collision = Comp->GetCollisionEnabled();
		if (Collision != ECollisionEnabled::QueryAndPhysics && Collision != ECollisionEnabled::PhysicsOnly)
		{
			continue; // Triggers and query-only shapes can't be held.
		}
		FVector Closest;
		const float Distance = Comp->GetClosestPointOnCollision(Hand, Closest);
		if (Distance < BestDistance)
		{
			Best = Comp;
			BestItem = Overlap.ItemIndex;
			BestDistance = Distance;
		}
	}
	if (!Best)
	{
		return;
	}

	FName TargetBone = NAME_None;
	if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Best->GetOwner()))
	{
		// Mystery Crates open when grabbed.
		if (Spawned->IsMystery())
		{
			Spawned->RevealMystery();
			Arm.RegripBlockedUntil = GetWorld()->GetTimeSeconds() + 0.5;
			return;
		}
		Best = Spawned->GetBody(); // The welded root, not a decorative sub-part.
	}
	else if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Best))
	{
		if (Skeletal->Bodies.IsValidIndex(BestItem) && Skeletal->Bodies[BestItem] && Skeletal->Bodies[BestItem]->BodySetup.IsValid())
		{
			TargetBone = Skeletal->Bodies[BestItem]->BodySetup->BoneName;
		}
	}

	// A ball joint at the hand: position locked, rotation free, like holding a handle.
	UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(GetOwner());
	Constraint->SetWorldLocation(Hand);
	Constraint->RegisterComponent();
	Constraint->SetLinearXLimit(LCM_Locked, 0.f);
	Constraint->SetLinearYLimit(LCM_Locked, 0.f);
	Constraint->SetLinearZLimit(LCM_Locked, 0.f);
	Constraint->SetAngularSwing1Limit(ACM_Free, 0.f);
	Constraint->SetAngularSwing2Limit(ACM_Free, 0.f);
	Constraint->SetAngularTwistLimit(ACM_Free, 0.f);
	Constraint->SetDisableCollision(true);
	Constraint->SetConstrainedComponents(Mesh, Arm.HandBone, Best, TargetBone);

	Arm.Grip = Constraint;
	Arm.GripTarget = Best;
	Arm.bGripStatic = !Best->IsSimulatingPhysics(TargetBone);
	Arm.GripLocalPoint = Best->GetComponentTransform().InverseTransformPosition(Hand);
	ApplyArmDrive(Arm);

	UE_LOG(LogPromptMages, Verbose, TEXT("%s hand gripped %s (%s)"), Arm.Side < 0.f ? TEXT("Left") : TEXT("Right"),
		*GetNameSafe(Best->GetOwner()), Arm.bGripStatic ? TEXT("static") : TEXT("dynamic"));
}

void UPMActiveRagdollComponent::ReleaseGrip(FPMArmState& Arm, bool bSlipped)
{
	if (!Arm.Grip)
	{
		return;
	}
	Arm.Grip->BreakConstraint();
	Arm.Grip->DestroyComponent();
	Arm.Grip = nullptr;
	Arm.GripTarget = nullptr;
	Arm.Load = 0.f;
	ApplyArmDrive(Arm);

	if (bSlipped)
	{
		Toast(FString::Printf(TEXT("*fwip* - your %s hand slipped."), Arm.Side < 0.f ? TEXT("left") : TEXT("right")));
	}
}

// --- Body-level forces ---------------------------------------------------------------------------

void UPMActiveRagdollComponent::UpdateForcesAndLeash(float DeltaTime)
{
	for (FPMArmState& Arm : Arms)
	{
		if (!Arm.IsGripping())
		{
			continue;
		}

		if (!Arm.bGripStatic)
		{
			// Two-way forces: whatever the hand holds pulls the body (a rolling turnip drags you along).
			// The pelvis is kinematic, so the physics engine can't do this for us.
			// The force (Newtons) points from the hand toward what it holds. Only the part beyond what an arm can
			// comfortably hold drags the body, so normal carrying doesn't tug you around.
			FVector LinearForce, AngularForce;
			Arm.Grip->GetConstraintForce(LinearForce, AngularForce);
			const FVector Horizontal(LinearForce.X, LinearForce.Y, 0.f);
			const float Magnitude = Horizontal.Size();
			const float Comfortable = MaxLoadPerArmKg * 9.81f;
			if (Magnitude > Comfortable)
			{
				// x100: Newtons to Unreal's kg*cm/s^2.
				Movement->AddForce(Horizontal / Magnitude * (Magnitude - Comfortable) * 100.f * ForceTransfer);
			}
			continue;
		}

		if (bClimbing)
		{
			continue;
		}

		// Holding the world works like a rope: you can't move further than an arm's length from the grip.
		const FVector Anchor = GetGripAnchor(Arm);
		const FVector Shoulder = GetShoulderLocation(Arm);
		const FVector ToAnchor = Anchor - Shoulder;
		const float Distance = ToAnchor.Size();
		const float MaxDistance = ArmLength + 15.f;
		if (Distance > MaxDistance)
		{
			const FVector Dir = ToAnchor / Distance;
			GetOwner()->AddActorWorldOffset(Dir * (Distance - MaxDistance), true);
			const float Away = FVector::DotProduct(Movement->Velocity, Dir);
			if (Away < 0.f)
			{
				Movement->Velocity -= Dir * Away; // Keep the swing, drop the stretch.
			}
		}
	}
}

void UPMActiveRagdollComponent::UpdateClimb(float DeltaTime)
{
	int32 StaticHands = 0;
	float TopZ = -TNumericLimits<float>::Max();
	FVector AnchorSum = FVector::ZeroVector;
	for (const FPMArmState& Arm : Arms)
	{
		if (Arm.IsGripping() && Arm.bGripStatic)
		{
			++StaticHands;
			const FVector Anchor = GetGripAnchor(Arm);
			TopZ = FMath::Max(TopZ, Anchor.Z);
			AnchorSum += Anchor;
		}
	}

	const APawn* Pawn = CastChecked<APawn>(GetOwner());
	const float Pitch = FRotator::NormalizeAxis(Pawn->GetControlRotation().Pitch);
	const bool bLookingUp = Pitch > ClimbLookUpPitch;

	// Both hands on an edge + look up = pull yourself up (GDD 2.1).
	if (!bClimbing)
	{
		if (StaticHands == 2 && bLookingUp && !bArmsLocked)
		{
			bClimbing = true;
			Movement->SetMovementMode(MOVE_Flying);
		}
		return;
	}

	if (StaticHands < 2 || !bLookingUp)
	{
		bClimbing = false;
		Movement->SetMovementMode(MOVE_Falling);
		return;
	}

	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const float FeetZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (FeetZ < TopZ + 5.f)
	{
		const FVector ToAnchors = (AnchorSum / StaticHands - Character->GetActorLocation()).GetSafeNormal2D();
		Movement->Velocity = FVector(0.f, 0.f, ClimbSpeed) + ToAnchors * 60.f;
		return;
	}

	// Over the top: let go and tumble forward onto the ledge.
	const double Now = GetWorld()->GetTimeSeconds();
	for (FPMArmState& Arm : Arms)
	{
		ReleaseGrip(Arm, false);
		Arm.RegripBlockedUntil = Now + 0.8;
	}
	bClimbing = false;
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = Character->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 120.f);
}

// --- Camera --------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::UpdateCamera(float DeltaTime)
{
	APromptMagesCharacter* Character = Cast<APromptMagesCharacter>(GetOwner());
	UCameraComponent* Camera = Character ? Character->GetFirstPersonCameraComponent() : nullptr;
	if (!Camera || !BoundPC.IsValid())
	{
		return;
	}

	// Heavy smoothing on position, and only ~30% of the physical head's motion reaches the camera (GDD 2.1).
	const FVector Head = Mesh->GetBoneLocation(PMRagdoll::Head);
	if (!bCameraInitialised)
	{
		SmoothedHead = Head;
		bCameraInitialised = true;
	}
	SmoothedHead = FMath::VInterpTo(SmoothedHead, Head, DeltaTime, CameraSmoothSpeed);

	// Eye height measured from the floor, so crouching lowers the view by the hip drop.
	const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Eye = Character->GetActorLocation();
	Eye.Z = FloorZ + StandingHalfHeight + StandingEyeHeight - HipDrop;
	const FVector Forward = FRotator(0.f, Character->GetControlRotation().Yaw, 0.f).Vector();
	// While bending over, ride the head further down so you look over your hands, not at your own back.
	const float Follow = FMath::Lerp(CameraHeadFollow, 0.8f, Stoop);
	Camera->SetWorldLocation(FMath::Lerp(Eye, SmoothedHead, Follow) + Forward * CameraForwardOffset);
}

// --- Console commands for testing ----------------------------------------------------------------

namespace PMRagdollConsole
{
	APlayerController* LocalPC(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	static FAutoConsoleCommandWithWorldAndArgs Hand(TEXT("pm.Hand"), TEXT("pm.Hand L|R 1|0 - hold or release a hand as if its mouse button were held."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = LocalPC(World);
			UPMActiveRagdollComponent* Body = (PC && PC->GetPawn()) ? PC->GetPawn()->FindComponentByClass<UPMActiveRagdollComponent>() : nullptr;
			if (!Body || Args.Num() < 1)
			{
				return;
			}
			const bool bHeld = Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0;
			const FString Which = Args[0].ToUpper();
			if (Which.Contains(TEXT("L")) || Which == TEXT("B"))
			{
				Body->SetHandHeld(EPMArm::Left, bHeld);
			}
			if (Which.Contains(TEXT("R")) || Which == TEXT("B"))
			{
				Body->SetHandHeld(EPMArm::Right, bHeld);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs Look(TEXT("pm.Look"), TEXT("pm.Look <pitch> [yaw] - set the local player's view rotation."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (APlayerController* PC = LocalPC(World))
			{
				FRotator Rotation = PC->GetControlRotation();
				if (Args.Num() > 0) { Rotation.Pitch = FCString::Atof(*Args[0]); }
				if (Args.Num() > 1) { Rotation.Yaw = FCString::Atof(*Args[1]); }
				PC->SetControlRotation(Rotation);
			}
		}));
}
