// Prompt-Mages: the active-ragdoll body, its arms and E-to-pick-up (GDD 2.1).

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
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

namespace PMRagdoll
{
	TAutoConsoleVariable<float> CVarThirdPerson(TEXT("pm.ThirdPerson"), 0.f,
		TEXT("Camera distance behind the local player (0 = first person). Shows your own head."));
	TAutoConsoleVariable<float> CVarThirdPersonYaw(TEXT("pm.ThirdPersonYaw"), 0.f,
		TEXT("Orbit the third-person camera around the mage, in degrees (180 = front view)."));
	TAutoConsoleVariable<float> CVarNeckDrive(TEXT("pm.NeckDrive"), 2.f,
		TEXT("Neck and head motor strength, as a multiple of the spine's. Higher = the head stays up instead of drooping."));
	TAutoConsoleVariable<float> CVarBodyStiffness(TEXT("pm.BodyStiffness"), 1.f,
		TEXT("Multiplier on every body motor. Lower = floppier ragdoll, higher = steadier."));

	const FName Pelvis(TEXT("pelvis"));
	const FName Spine(TEXT("spine_02"));
	const FName Neck(TEXT("neck_01"));
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

	/** Name shown in prompts: a spawned object's prompt name, else the actor's label. */
	FString NameOf(const UPrimitiveComponent* Comp)
	{
		const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
		if (const APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Owner))
		{
			return Spawned->GetDisplayName();
		}
		return Owner ? Owner->GetActorNameOrLabel() : FString();
	}
}

UPMActiveRagdollComponent::UPMActiveRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After CharacterMovement and physics: we read simulated bones, place the hands and the camera.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Strong enough that the body mostly holds its pose; it still sways and stumbles a bit.
	LegDrive = PMRagdoll::Drive(4000.f, 400.f);
	SpineDrive = PMRagdoll::Drive(8000.f, 800.f);
	ArmDrive = PMRagdoll::Drive(2500.f, 250.f);
	PlacedArmDrive = PMRagdoll::Drive(150.f, 20.f);

	Arms[0].HandBone = TEXT("hand_l");
	Arms[0].ClavicleBone = TEXT("clavicle_l");
	Arms[0].Side = -1.f;

	Arms[1].HandBone = TEXT("hand_r");
	Arms[1].ClavicleBone = TEXT("clavicle_r");
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
	Drop();
	for (FPMArmState& Arm : Arms)
	{
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
		Arm.Reach->SetLinearStiffness(HandStiffness);
		Arm.Reach->SetLinearDamping(HandDamping);
		Arm.Reach->SetInterpolationSpeed(40.f);
	}
	HoldHandle = NewObject<UPhysicsHandleComponent>(GetOwner(), TEXT("PMHoldHandle"));
	HoldHandle->RegisterComponent();
	ApplyBodyDrives();

	// Movement feel (GDD 2.1): 70 kg body, accel ramps over ~0.3 s, momentum carries.
	Movement->Mass = BodyMassKg;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxAcceleration = Acceleration;
	Movement->BrakingDecelerationWalking = BrakingDeceleration;
	Movement->GroundFriction = 3.f;

	// Crouch (Ctrl): the capsule shrinks and we drop the hips ourselves (see UpdateCrouch).
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
	ArmInput->BindKey(EKeys::E, IE_Pressed, this, &UPMActiveRagdollComponent::OnInteractPressed);
	ArmInput->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &UPMActiveRagdollComponent::OnThrowPressed).bConsumeInput = false;
	ArmInput->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &UPMActiveRagdollComponent::OnHoldFarther);
	ArmInput->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &UPMActiveRagdollComponent::OnHoldCloser);
	ArmInput->BindKey(EKeys::LeftShift, IE_Pressed, this, &UPMActiveRagdollComponent::OnSprintPressed);
	ArmInput->BindKey(EKeys::LeftShift, IE_Released, this, &UPMActiveRagdollComponent::OnSprintReleased);
	ArmInput->BindKey(EKeys::LeftControl, IE_Pressed, this, &UPMActiveRagdollComponent::OnCrouchPressed);
	ArmInput->BindKey(EKeys::LeftControl, IE_Released, this, &UPMActiveRagdollComponent::OnCrouchReleased);

	// V: switch between first and third person (remembers the third-person distance).
	FInputKeyBinding ViewToggle(FInputChord(EKeys::V), IE_Pressed);
	ViewToggle.KeyDelegate.GetDelegateForManualSet().BindLambda([this]()
	{
		if (IsSpellbookOpen())
		{
			return;
		}
		static float LastDistance = 250.f;
		IConsoleVariable* ThirdPerson = PMRagdoll::CVarThirdPerson.AsVariable();
		const float Current = ThirdPerson->GetFloat();
		if (Current > 0.f)
		{
			LastDistance = Current;
		}
		ThirdPerson->Set(Current > 0.f ? 0.f : LastDistance, ECVF_SetByConsole);
	});
	ArmInput->KeyBindings.Add(ViewToggle);
	PC->PushInputComponent(ArmInput);

	SetupLocalView();
}

bool UPMActiveRagdollComponent::IsWandOut() const
{
	const UPMWandComponent* Wand = GetOwner()->FindComponentByClass<UPMWandComponent>();
	return Wand && Wand->IsWandOut();
}

bool UPMActiveRagdollComponent::IsSpellbookOpen() const
{
	const UPMSpellcasterComponent* Caster = GetOwner()->FindComponentByClass<UPMSpellcasterComponent>();
	return Caster && Caster->IsSpellbookOpen();
}

bool UPMActiveRagdollComponent::IsLocalFirstPerson() const
{
	return GetLocalPC() != nullptr && PMRagdoll::CVarThirdPerson.GetValueOnGameThread() <= 0.f;
}

void UPMActiveRagdollComponent::Toast(const FString& Message) const
{
	if (UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>())
	{
		Spells->Toast(Message);
	}
}

// --- Input ---------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::OnThrowPressed()
{
	if (IsSpellbookOpen())
	{
		return;
	}
	if (Held.IsValid())
	{
		Throw();
	}
	else
	{
		Punch();
	}
}

void UPMActiveRagdollComponent::OnHoldFarther()
{
	HoldDistance = FMath::Clamp(HoldDistance + 10.f, HoldDistanceRange.X, HoldDistanceRange.Y);
}

void UPMActiveRagdollComponent::OnHoldCloser()
{
	HoldDistance = FMath::Clamp(HoldDistance - 10.f, HoldDistanceRange.X, HoldDistanceRange.Y);
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

	UpdateMotors(DeltaTime);
	UpdateCrouch(DeltaTime);
	UpdateHeld(DeltaTime);
	UpdateMoveSpeed();
	UpdateInteractPrompt();
	UpdatePunch(DeltaTime);
	UpdateArms();
	UpdateCamera(DeltaTime);
}

// --- Body ----------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::UpdateMotors(float DeltaTime)
{
	// Tuning from the console takes effect right away.
	const float Stiffness = PMRagdoll::CVarBodyStiffness.GetValueOnGameThread();
	const float NeckDrive = PMRagdoll::CVarNeckDrive.GetValueOnGameThread();
	if (Stiffness != AppliedStiffness || NeckDrive != AppliedNeckDrive)
	{
		ApplyBodyDrives();
	}

	// Motors are strong on the ground and drop in the air (GDD 2.1).
	const float Target = Movement->IsFalling() ? AirborneMotorScale : 1.f;
	MotorScale = FMath::FInterpTo(MotorScale, Target, DeltaTime, 6.f);
	PhysicalAnimation->SetStrengthMultiplyer(MotorScale);
}

void UPMActiveRagdollComponent::ApplyBodyDrives()
{
	AppliedStiffness = PMRagdoll::CVarBodyStiffness.GetValueOnGameThread();
	AppliedNeckDrive = PMRagdoll::CVarNeckDrive.GetValueOnGameThread();

	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::Spine, PMRagdoll::Scaled(SpineDrive, AppliedStiffness), true);

	// The head is heavy on a thin neck: with only the spine's motors it droops, chin on chest.
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::Neck,
		PMRagdoll::Scaled(SpineDrive, AppliedStiffness * AppliedNeckDrive), true);

	const FPhysicalAnimationData Legs = PMRagdoll::Scaled(LegDrive, AppliedStiffness * (bLegsRelaxed ? CrouchLegScale : 1.f));
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::ThighL, Legs, true);
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PMRagdoll::ThighR, Legs, true);

	// The spine call also covered the arms; put their own drives back.
	for (const FPMArmState& Arm : Arms)
	{
		ApplyArmDrive(Arm);
	}
}

void UPMActiveRagdollComponent::ApplyArmDrive(const FPMArmState& Arm)
{
	if (!PhysicalAnimation)
	{
		return;
	}
	const bool bPlaced = Arm.Reach && Arm.Reach->GetGrabbedComponent() && Arm.Reach->LinearStiffness > 0.f;
	const float Stiffness = FMath::Max(AppliedStiffness, 0.f);
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Arm.ClavicleBone,
		PMRagdoll::Scaled(bPlaced ? PlacedArmDrive : ArmDrive, Stiffness), true);
}

void UPMActiveRagdollComponent::UpdateCrouch(float DeltaTime)
{
	ACharacter* Character = CastChecked<ACharacter>(GetOwner());

	// Crouch: the capsule shrinks (CharacterMovement) and we drop the hips; the relaxed legs fold under them.
	const bool bCrouched = Character->bIsCrouched;
	HipDrop = FMath::FInterpTo(HipDrop, bCrouched ? CrouchHipDrop : 0.f, DeltaTime, 6.f);
	const float CapsuleShift = StandingHalfHeight - Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	FVector MeshLocation = Mesh->GetRelativeLocation();
	MeshLocation.Z = DefaultMeshZ + CapsuleShift - HipDrop;
	Mesh->SetRelativeLocation(MeshLocation);

	if (bCrouched != bLegsRelaxed)
	{
		bLegsRelaxed = bCrouched;
		ApplyBodyDrives();
	}
}

FVector UPMActiveRagdollComponent::GetEyeLocation() const
{
	// Eye height measured from the floor, so crouching lowers it by the hip drop.
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Eye = Character->GetActorLocation();
	Eye.Z = FloorZ + StandingHalfHeight + StandingEyeHeight - HipDrop;
	return Eye;
}

FRotator UPMActiveRagdollComponent::GetViewRotation() const
{
	const APawn* Pawn = CastChecked<APawn>(GetOwner());
	return Pawn->GetBaseAimRotation();
}

FVector UPMActiveRagdollComponent::GetShoulderLocation(const FPMArmState& Arm) const
{
	// Taken from the capsule rather than the floppy bone, so hand placement feels stable.
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Shoulder = Character->GetActorLocation() + Character->GetActorRightVector() * Arm.Side * 18.f;
	Shoulder.Z = FloorZ + StandingHalfHeight * 1.5f - HipDrop;
	return Shoulder;
}

void UPMActiveRagdollComponent::UpdateMoveSpeed()
{
	float Speed = bSprinting ? SprintSpeed : WalkSpeed;
	if (Held.IsValid())
	{
		// Carrying slows you a little; dragging something heavy slows you a lot.
		const float Alpha = bDragging
			? FMath::Clamp((HeldMass - CarryMaxKg) / FMath::Max(DragMaxKg - CarryMaxKg, 1.f), 0.f, 1.f)
			: 0.f;
		Speed *= bDragging ? FMath::Lerp(0.8f, HeaviestDragSpeedScale, Alpha)
			: FMath::Lerp(1.f, 0.85f, FMath::Clamp(HeldMass / CarryMaxKg, 0.f, 1.f));
	}
	Movement->MaxWalkSpeed = Speed;
}

// --- Picking things up ---------------------------------------------------------------------------

UPrimitiveComponent* UPMActiveRagdollComponent::FindInteractTarget(FVector& OutHitPoint) const
{
	const FVector Eye = GetEyeLocation();
	const FVector Dir = GetViewRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMInteract), false, GetOwner());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * InteractRange, ECC_Visibility, Params))
	{
		return nullptr;
	}
	OutHitPoint = Hit.ImpactPoint;

	UPrimitiveComponent* Comp = Hit.GetComponent();
	if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Hit.GetActor()))
	{
		Comp = Spawned->GetBody(); // The welded root, not a decorative sub-part.
	}
	if (!Comp || !Comp->IsSimulatingPhysics())
	{
		return nullptr; // Walls, floors and anything bolted down.
	}
	return Comp;
}

void UPMActiveRagdollComponent::Interact()
{
	if (!bBodyReady || IsSpellbookOpen())
	{
		return;
	}
	if (Held.IsValid())
	{
		Drop();
		return;
	}

	FVector HitPoint;
	UPrimitiveComponent* Target = FindInteractTarget(HitPoint);
	if (!Target)
	{
		return;
	}
	if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Target->GetOwner()))
	{
		if (Spawned->IsMystery())
		{
			Spawned->RevealMystery(); // Mystery Crates open when you grab them.
			return;
		}
	}
	const float Mass = Target->GetMass();
	if (Mass > DragMaxKg)
	{
		Toast(FString::Printf(TEXT("%s won't budge (%.0f kg). Get some help!"), *PMRagdoll::NameOf(Target), Mass));
		return;
	}
	PickUp(Target, HitPoint);
}

void UPMActiveRagdollComponent::PickUp(UPrimitiveComponent* Target, const FVector& HitPoint)
{
	Held = Target;
	HeldMass = Target->GetMass();
	HeldName = PMRagdoll::NameOf(Target);
	bDragging = HeldMass > CarryMaxKg;

	const FRotator ViewYaw(0.f, GetViewRotation().Yaw, 0.f);
	if (!bDragging)
	{
		// Carry: hold it in front of your face, keeping how it was turned relative to you.
		HeldRadius = FMath::Clamp(Target->Bounds.SphereRadius, 5.f, 80.f);
		HeldRelativeRotation = ViewYaw.Quaternion().Inverse() * Target->GetComponentQuat();
		HoldHandle->SetLinearStiffness(1500.f);
		HoldHandle->SetLinearDamping(200.f);
		HoldHandle->SetAngularStiffness(1500.f);
		HoldHandle->SetAngularDamping(500.f);
		HoldHandle->SetInterpolationSpeed(30.f);
		HoldHandle->GrabComponentAtLocationWithRotation(Target, NAME_None, Target->Bounds.Origin, Target->GetComponentRotation());
	}
	else
	{
		// Drag: hang on to the spot you grabbed and pull it along behind you, at the same height.
		const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		DragLocalPoint = Target->GetComponentTransform().InverseTransformPosition(HitPoint);
		DragDistance = FMath::Clamp(FVector::Dist2D(HitPoint, Character->GetActorLocation()), 70.f, 170.f);
		DragHeight = FMath::Clamp(HitPoint.Z - FloorZ, 10.f, 150.f);
		HoldHandle->SetLinearStiffness(400.f);
		HoldHandle->SetLinearDamping(150.f);
		HoldHandle->SetInterpolationSpeed(10.f);
		HoldHandle->GrabComponentAtLocation(Target, NAME_None, HitPoint);
	}
	UE_LOG(LogPromptMages, Verbose, TEXT("%s %s (%.0f kg)"), bDragging ? TEXT("Dragging") : TEXT("Carrying"), *HeldName, HeldMass);
}

void UPMActiveRagdollComponent::Drop()
{
	if (HoldHandle)
	{
		HoldHandle->ReleaseComponent();
	}
	Held = nullptr;
	HeldName.Empty();
	HeldMass = 0.f;
	bDragging = false;
}

void UPMActiveRagdollComponent::Throw()
{
	UPrimitiveComponent* Target = Held.Get();
	if (!Target || bDragging)
	{
		return;
	}
	const float Mass = HeldMass;
	Drop();

	// Light things fly, heavy things lob.
	const float Speed = ThrowSpeed * FMath::Lerp(1.f, 0.35f, FMath::Clamp(Mass / CarryMaxKg, 0.f, 1.f));
	const FVector Dir = (GetViewRotation().Vector() + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
	Target->SetPhysicsLinearVelocity(Dir * Speed + Movement->Velocity * 0.5f);
}

void UPMActiveRagdollComponent::UpdateHeld(float DeltaTime)
{
	UPrimitiveComponent* Target = Held.Get();
	if (!Target)
	{
		if (HoldHandle && HoldHandle->GetGrabbedComponent())
		{
			Drop(); // It burned up, got cleared, or something else took it.
		}
		return;
	}
	if (!Target->IsSimulatingPhysics() || HoldHandle->GetGrabbedComponent() != Target)
	{
		Drop();
		return;
	}

	const FVector Eye = GetEyeLocation();
	const FRotator View = GetViewRotation();
	const FRotator ViewYaw(0.f, View.Yaw, 0.f);
	FVector Goal;
	FVector Current;
	if (!bDragging)
	{
		// A little below the line of sight, so you can still see past it.
		Goal = Eye + View.Vector() * (HoldDistance + HeldRadius) - FRotationMatrix(View).GetUnitAxis(EAxis::Z) * (HeldRadius * 0.6f);
		Current = Target->Bounds.Origin;
		HoldHandle->SetTargetLocationAndRotation(Goal, (ViewYaw.Quaternion() * HeldRelativeRotation).Rotator());
	}
	else
	{
		const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Goal = Character->GetActorLocation() + ViewYaw.Vector() * DragDistance;
		Goal.Z = FloorZ + DragHeight;
		Current = Target->GetComponentTransform().TransformPosition(DragLocalPoint);
		HoldHandle->SetTargetLocation(Goal);
	}

	// Snagged on something and left behind: let go.
	if (FVector::DistSquared(Goal, Current) > FMath::Square(bDragging ? 220.f : 160.f))
	{
		Toast(FString::Printf(TEXT("You lost hold of the %s."), *HeldName));
		Drop();
	}
}

void UPMActiveRagdollComponent::UpdateInteractPrompt()
{
	InteractPrompt.Reset();
	if (!GetLocalPC())
	{
		return;
	}
	if (Held.IsValid())
	{
		InteractPrompt = bDragging
			? FString::Printf(TEXT("Dragging %s (%.0f kg)    [E] let go"), *HeldName, HeldMass)
			: FString::Printf(TEXT("Holding %s    [E] drop    [LMB] throw"), *HeldName);
		return;
	}

	FVector HitPoint;
	const UPrimitiveComponent* Target = FindInteractTarget(HitPoint);
	if (!Target)
	{
		return;
	}
	const APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Target->GetOwner());
	const FString Name = PMRagdoll::NameOf(Target);
	const float Mass = Target->GetMass();
	if (Spawned && Spawned->IsMystery())
	{
		InteractPrompt = FString::Printf(TEXT("[E] Open %s"), *Name);
	}
	else if (Mass > DragMaxKg)
	{
		InteractPrompt = FString::Printf(TEXT("%s (%.0f kg) - too heavy alone"), *Name, Mass);
	}
	else if (Mass > CarryMaxKg)
	{
		InteractPrompt = FString::Printf(TEXT("[E] Drag %s (%.0f kg)"), *Name, Mass);
	}
	else
	{
		InteractPrompt = FString::Printf(TEXT("[E] Pick up %s"), *Name);
	}
}

// --- Punching ------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::Punch()
{
	if (!bBodyReady || IsSpellbookOpen() || Held.IsValid())
	{
		return;
	}
	if (PunchTime >= 0.f && PunchTime < PunchCooldown)
	{
		return; // Still recovering from the last one.
	}
	// Alternate fists; with the wand out the right hand is busy, so it's all left.
	PunchArm = IsWandOut() ? 0 : 1 - PunchArm;
	PunchTime = 0.f;
	bPunchLanded = false;
}

void UPMActiveRagdollComponent::UpdatePunch(float DeltaTime)
{
	if (PunchTime < 0.f)
	{
		return;
	}
	PunchTime += DeltaTime;
	// The hit lands when the fist is most of the way out.
	if (!bPunchLanded && PunchTime >= PunchDuration * 0.3f)
	{
		bPunchLanded = true;
		PunchHit();
	}
	if (PunchTime >= PunchCooldown)
	{
		PunchTime = -1.f;
	}
}

void UPMActiveRagdollComponent::PunchHit()
{
	const FVector Eye = GetEyeLocation();
	const FVector Dir = GetViewRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMPunch), false, GetOwner());
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByChannel(Hit, Eye, Eye + Dir * PunchReach, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(18.f), Params))
	{
		return;
	}

	// Another mage (or anything that walks): shove them back and up a little.
	if (ACharacter* Victim = Cast<ACharacter>(Hit.GetActor()))
	{
		const FVector Shove = Dir.GetSafeNormal2D() * PunchKnockback + FVector(0.f, 0.f, PunchKnockback * 0.35f);
		Victim->LaunchCharacter(Shove, true, false);
		UE_LOG(LogPromptMages, Verbose, TEXT("Punched %s"), *Victim->GetName());
		return;
	}

	UPrimitiveComponent* Comp = Hit.GetComponent();
	if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Hit.GetActor()))
	{
		if (Spawned->IsMystery())
		{
			Spawned->RevealMystery(); // Punching a Mystery Crate opens it too.
			return;
		}
		Comp = Spawned->GetBody();
	}
	if (!Comp || !Comp->IsSimulatingPhysics())
	{
		return; // Punched a wall. Ouch.
	}

	// Light things fly, heavy things barely budge. Knock it mostly forward and a bit up, even when
	// punching down at something, so it doesn't just get pressed into the floor.
	const float Mass = FMath::Max(Comp->GetMass(), 0.1f);
	const float Speed = PunchSpeed * FMath::Min(1.f, PunchFullSpeedKg / Mass);
	const FVector Push = (Dir.GetSafeNormal2D() + FVector(0.f, 0.f, FMath::Max(Dir.Z, 0.f) + 0.35f)).GetSafeNormal();
	Comp->AddImpulseAtLocation(Push * Speed * Mass, Hit.ImpactPoint);
	UE_LOG(LogPromptMages, Verbose, TEXT("Punched %s (%.0f kg) at %.0f cm/s"), *PMRagdoll::NameOf(Comp), Mass, Speed);
}

// --- Hands ---------------------------------------------------------------------------------------

void UPMActiveRagdollComponent::PlaceHand(FPMArmState& Arm, bool bPlace, const FVector& Target)
{
	// The handle holds the hand all the time; a relaxed arm just has a zero-strength spring.
	// (Releasing and re-grabbing the same hand doesn't take: the hand stayed limp after switching views.)
	if (!Arm.Reach->GetGrabbedComponent())
	{
		Arm.Reach->GrabComponentAtLocation(Mesh, Arm.HandBone, Mesh->GetBoneLocation(Arm.HandBone));
	}
	const bool bPlaced = Arm.Reach->LinearStiffness > 0.f;
	if (bPlace)
	{
		if (!bPlaced)
		{
			Arm.Reach->SetLinearStiffness(HandStiffness);
			Arm.Reach->SetLinearDamping(HandDamping);
			ApplyArmDrive(Arm);
		}
		// Never ask for more than an arm's length from the shoulder.
		const FVector Shoulder = GetShoulderLocation(Arm);
		FVector Goal = Target;
		if (FVector::DistSquared(Goal, Shoulder) > FMath::Square(ArmLength))
		{
			Goal = Shoulder + (Goal - Shoulder).GetSafeNormal() * ArmLength;
		}
		Arm.Reach->SetTargetLocation(Goal);
	}
	else if (bPlaced)
	{
		Arm.Reach->SetLinearStiffness(0.f);
		Arm.Reach->SetLinearDamping(0.f);
		Arm.Reach->SetTargetLocation(Mesh->GetBoneLocation(Arm.HandBone));
		ApplyArmDrive(Arm);
	}
}

void UPMActiveRagdollComponent::UpdateArms()
{
	const FVector Eye = GetEyeLocation();
	const FRotationMatrix View(GetViewRotation());
	const FVector Forward = View.GetUnitAxis(EAxis::X);
	const FVector Right = View.GetUnitAxis(EAxis::Y);
	const FVector Up = View.GetUnitAxis(EAxis::Z);
	const bool bFirstPerson = IsLocalFirstPerson();
	const bool bWand = IsWandOut();
	const UPrimitiveComponent* Target = Held.Get();

	for (int32 i = 0; i < 2; ++i)
	{
		FPMArmState& Arm = Arms[i];

		// Punching: the fist shoots out from the guard to the crosshair and comes back.
		if (IsPunching() && i == PunchArm)
		{
			const float T = PunchTime / PunchDuration;
			const float Out = T < 0.4f ? FMath::InterpEaseOut(0.f, 1.f, T / 0.4f, 2.f) : 1.f - (T - 0.4f) / 0.6f;
			const FVector Guard = Eye + Forward * 35.f + Right * Arm.Side * 18.f - Up * 16.f;
			const FVector Strike = Eye + Forward * PunchReach + Right * Arm.Side * 4.f - Up * 6.f;
			PlaceHand(Arm, true, FMath::Lerp(Guard, Strike, Out));
			continue;
		}

		// The wand hand holds the wand out toward the crosshair.
		if (bWand && Arm.Side > 0.f)
		{
			PlaceHand(Arm, true, Eye + Forward * 55.f + Right * 16.f - Up * 12.f);
			continue;
		}
		// Holding something: hands on it (either side of a carried thing, together on a dragged one).
		if (Target)
		{
			const FVector Grip = bDragging
				? Target->GetComponentTransform().TransformPosition(DragLocalPoint) + Right * Arm.Side * 12.f
				: Target->Bounds.Origin + Right * Arm.Side * (HeldRadius + 6.f);
			PlaceHand(Arm, true, Grip);
			continue;
		}
		// First person: hands up in view, ready. Third person (and how others see you): arms hang.
		if (bFirstPerson)
		{
			const FVector& O = FirstPersonHandOffset;
			PlaceHand(Arm, true, Eye + Forward * O.X + Right * Arm.Side * O.Y + Up * O.Z);
			continue;
		}
		PlaceHand(Arm, false, FVector::ZeroVector);
	}
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

	// Heavy smoothing on position, and only a little of the physical head's motion reaches the camera.
	const FVector Head = Mesh->GetBoneLocation(PMRagdoll::Head);
	if (!bCameraInitialised)
	{
		SmoothedHead = Head;
		bCameraInitialised = true;
	}
	SmoothedHead = FMath::VInterpTo(SmoothedHead, Head, DeltaTime, CameraSmoothSpeed);

	const FVector Eye = GetEyeLocation();
	const FVector Forward = FRotator(0.f, Character->GetControlRotation().Yaw, 0.f).Vector();
	FVector CameraLocation = FMath::Lerp(Eye, SmoothedHead, CameraHeadFollow) + Forward * CameraForwardOffset;

	// pm.ThirdPerson <distance>: look at your own mage from behind (debugging and fun).
	const float ThirdPerson = PMRagdoll::CVarThirdPerson.GetValueOnGameThread();
	if (ThirdPerson > 0.f)
	{
		// pm.ThirdPersonYaw orbits the camera around the mage (180 = look at your own face).
		const float Orbit = PMRagdoll::CVarThirdPersonYaw.GetValueOnGameThread();
		FRotator ViewRot = Character->GetControlRotation();
		ViewRot.Yaw += Orbit;
		CameraLocation = Eye - ViewRot.Vector() * ThirdPerson + FVector(0.f, 0.f, 20.f);
		Camera->bUsePawnControlRotation = FMath::IsNearlyZero(Orbit);
		if (!Camera->bUsePawnControlRotation)
		{
			Camera->SetWorldRotation((Eye - CameraLocation).Rotation());
		}
	}
	else
	{
		Camera->bUsePawnControlRotation = true;
	}
	Camera->SetWorldLocation(CameraLocation);
}

// --- Console commands for testing ----------------------------------------------------------------

namespace PMRagdollConsole
{
	APlayerController* LocalPC(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	UPMActiveRagdollComponent* LocalBody(UWorld* World)
	{
		APlayerController* PC = LocalPC(World);
		return (PC && PC->GetPawn()) ? PC->GetPawn()->FindComponentByClass<UPMActiveRagdollComponent>() : nullptr;
	}

	static FAutoConsoleCommandWithWorld Interact(TEXT("pm.Interact"), TEXT("pm.Interact - same as pressing E (pick up / drag / drop)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPMActiveRagdollComponent* Body = LocalBody(World))
			{
				Body->Interact();
			}
		}));

	static FAutoConsoleCommandWithWorld Throw(TEXT("pm.Throw"), TEXT("pm.Throw - throw what you're carrying."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPMActiveRagdollComponent* Body = LocalBody(World))
			{
				Body->Throw();
			}
		}));

	static FAutoConsoleCommandWithWorld PunchCmd(TEXT("pm.Punch"), TEXT("pm.Punch - punch, same as LMB with empty hands."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPMActiveRagdollComponent* Body = LocalBody(World))
			{
				Body->Punch();
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
