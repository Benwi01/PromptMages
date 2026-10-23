// Prompt-Mages: the active-ragdoll body and two independent grabbing arms (GDD 2.1).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PMActiveRagdollComponent.generated.h"

class APlayerController;
class UCharacterMovementComponent;
class UInputComponent;
class UPhysicsConstraintComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EPMArm : uint8
{
	Left,
	Right
};

/** Runtime state for one arm. */
USTRUCT()
struct FPMArmState
{
	GENERATED_BODY()

	FName HandBone;
	FName UpperArmBone;
	FName ClavicleBone;
	FKey Button;
	/** -1 for the left arm, +1 for the right. */
	float Side = 1.f;

	bool bButtonDown = false;

	/** Pulls the hand toward the crosshair while the button is held. */
	UPROPERTY()
	TObjectPtr<UPhysicsHandleComponent> Reach;

	/** Welds the hand to whatever it touched. */
	UPROPERTY()
	TObjectPtr<UPhysicsConstraintComponent> Grip;

	TWeakObjectPtr<UPrimitiveComponent> GripTarget;
	/** Grip point in the gripped component's local space. */
	FVector GripLocalPoint = FVector::ZeroVector;
	bool bGripStatic = false;

	/** 0..1 grip stamina (GDD: about 8 s at max load). */
	float Stamina = 1.f;
	/** Current load as a fraction of what one arm can hold. */
	float Load = 0.f;
	double RegripBlockedUntil = 0.0;

	bool IsGripping() const { return Grip != nullptr; }
};

/**
 * Turns the character into a "slightly drunk puppet that you mostly control" (GDD 2.1).
 *
 * Hybrid active ragdoll: the capsule + CharacterMovement still decides where the player goes,
 * while every body below the pelvis is simulated and pulled toward the animation pose by
 * physical-animation motors. Motors weaken in the air and while sprinting.
 *
 * Arms: hold LMB / RMB to reach the left / right hand toward the crosshair (mouse wheel = reach).
 * A reaching hand grips whatever it touches. Grip drains per-arm stamina by load and slips when empty.
 * Gripping static geometry holds you back like a rope (hang and swing); two hands on an edge
 * while looking up pulls you onto it. Forces from held objects pull the body (two-way).
 * The camera rides the physical head with heavy smoothing and 30% of its bob.
 */
UCLASS(ClassGroup = (PromptMages), meta = (BlueprintSpawnableComponent))
class PROMPTMAGES_API UPMActiveRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPMActiveRagdollComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsGripping(EPMArm Arm) const { return GetArm(Arm).IsGripping(); }
	bool IsReaching(EPMArm Arm) const;
	float GetGripStamina(EPMArm Arm) const { return GetArm(Arm).Stamina; }
	float GetLoad(EPMArm Arm) const { return GetArm(Arm).Load; }
	bool IsClimbing() const { return bClimbing; }

	/** Testing hook: press or release a hand as if its mouse button changed (pm.Hand console command). */
	void SetHandHeld(EPMArm Arm, bool bHeld) { GetArm(Arm).bButtonDown = bHeld; }
	bool IsSprinting() const { return bSprinting; }

	// --- Body ---

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float BodyMassKg = 70.f;

	/** Simulate the legs too (floppier, but feet can catch on things). Off = legs follow the animation exactly. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	bool bSimulateLegs = true;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FPhysicalAnimationData LegDrive;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FPhysicalAnimationData SpineDrive;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FPhysicalAnimationData ArmDrive;

	/** Arm motors while reaching or gripping: weak, so the reach spring and held loads win. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FPhysicalAnimationData ReachingArmDrive;

	/** Motor strength multiplier while airborne (GDD: motors drop during jumps). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float AirborneMotorScale = 0.35f;

	/** Arm motor multiplier while sprinting (GDD: faster but arm motors weaken). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float SprintArmScale = 0.5f;

	// --- Movement ---

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float WalkSpeed = 450.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float SprintSpeed = 700.f;

	/** Reaches walk speed in about 0.3 s (GDD 2.1). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float Acceleration = 1500.f;

	/** Low braking so stopping takes a step or two. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float BrakingDeceleration = 650.f;

	// --- Arms ---

	/** Shoulder-to-hand reach in cm. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float ArmLength = 70.f;

	/** Distance from the camera the hands aim for; the mouse wheel changes it. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float ReachDistance = 90.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	FVector2D ReachDistanceRange = FVector2D(35.f, 140.f);

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float ReachStiffness = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float ReachDamping = 250.f;

	/** How close (cm) the hand must be to something to grip it. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float GripRadius = 16.f;

	/** What one arm can hold at full stamina drain rate (two arms = 40 kg, the GDD's solo lift). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float MaxLoadPerArmKg = 20.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float GripSecondsAtMaxLoad = 8.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float GripRegenPerSecond = 0.35f;

	/** Fraction of a held object's pull transferred to the body (two-way forces). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Arms")
	float ForceTransfer = 1.f;

	// --- Climbing ---

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Climb")
	float ClimbSpeed = 260.f;

	/** Look at least this far up (degrees) with both hands on an edge to pull up. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Climb")
	float ClimbLookUpPitch = 15.f;

	// --- Camera ---

	/** How much of the physical head's motion reaches the camera (GDD: about 30%). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraHeadFollow = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraSmoothSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraForwardOffset = 10.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FPMArmState& GetArm(EPMArm Arm) { return Arms[static_cast<int32>(Arm)]; }
	const FPMArmState& GetArm(EPMArm Arm) const { return Arms[static_cast<int32>(Arm)]; }

	void SetupBody();
	void SetupLocalView();
	void TryBindInput();
	APlayerController* GetLocalPC() const;
	bool GetView(FVector& OutLocation, FRotator& OutRotation) const;
	bool IsSpellbookOpen() const;

	void OnLeftPressed()   { Arms[0].bButtonDown = true; }
	void OnLeftReleased()  { Arms[0].bButtonDown = false; }
	void OnRightPressed()  { Arms[1].bButtonDown = true; }
	void OnRightReleased() { Arms[1].bButtonDown = false; }
	void OnReachOut();
	void OnReachIn();
	void OnSprintPressed();
	void OnSprintReleased();

	void UpdateArm(FPMArmState& Arm, float DeltaTime, const FVector& ViewLocation, const FRotator& ViewRotation);
	void StartReach(FPMArmState& Arm);
	void StopReach(FPMArmState& Arm);
	void TryGrip(FPMArmState& Arm);
	void ReleaseGrip(FPMArmState& Arm, bool bSlipped);
	void ApplyArmDrive(const FPMArmState& Arm);
	FVector GetShoulderLocation(const FPMArmState& Arm) const;
	FVector GetGripAnchor(const FPMArmState& Arm) const;

	void UpdateMotors(float DeltaTime);
	void UpdateForcesAndLeash(float DeltaTime);
	void UpdateClimb(float DeltaTime);
	void UpdateCamera(float DeltaTime);
	void Toast(const FString& Message) const;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> Movement;

	UPROPERTY()
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	UPROPERTY()
	TObjectPtr<UInputComponent> ArmInput;

	UPROPERTY()
	FPMArmState Arms[2];

	TWeakObjectPtr<APlayerController> BoundPC;

	bool bBodyReady = false;
	bool bSprinting = false;
	bool bClimbing = false;
	bool bArmsLocked = false;
	float MotorScale = 1.f;
	bool bCameraInitialised = false;
	FVector SmoothedHead = FVector::ZeroVector;
};
