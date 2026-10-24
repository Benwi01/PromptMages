// Prompt-Mages: the active-ragdoll body, its arms and E-to-pick-up (GDD 2.1).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PMActiveRagdollComponent.generated.h"

class APlayerController;
class UCharacterMovementComponent;
class UInputComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class USoundBase;

/** Runtime state for one arm. */
USTRUCT()
struct FPMArmState
{
	GENERATED_BODY()

	FName HandBone;
	FName ClavicleBone;
	/** -1 for the left arm, +1 for the right. */
	float Side = 1.f;

	/** Pulls the hand to where it should be (hands-up pose, the held object, the wand). */
	UPROPERTY()
	TObjectPtr<UPhysicsHandleComponent> Reach;
};

/**
 * Turns the character into a "slightly drunk puppet that you mostly control" (GDD 2.1).
 *
 * Hybrid active ragdoll: the capsule + CharacterMovement decides where the player goes, while every body
 * below the pelvis is simulated and pulled toward the animation pose by physical-animation motors.
 *
 * Arms are placed for you: they hang relaxed until you punch or handle something, then the fists come up in a
 * guard for GuardHoldSeconds before relaxing again. Holding something, they go to it. The wand hand holds the wand.
 *
 * LMB punches (alternating fists): it knocks objects away and shoves other characters.
 * E picks up what the crosshair is on: light things (up to CarryMaxKg) are carried in front of you,
 * heavier ones (up to DragMaxKg) are dragged and slow you down. E again drops, LMB throws.
 */
UCLASS(ClassGroup = (PromptMages), meta = (BlueprintSpawnableComponent))
class PROMPTMAGES_API UPMActiveRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPMActiveRagdollComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsHolding() const { return Held.IsValid(); }
	bool IsDragging() const { return Held.IsValid() && bDragging; }
	const FString& GetHeldName() const { return HeldName; }
	float GetHeldMassKg() const { return HeldMass; }
	/** "[E] Pick up Barrel (25 kg)" etc. for what the crosshair is on; empty when there's nothing. */
	const FString& GetInteractPrompt() const { return InteractPrompt; }

	/** True when the crosshair is on something you could pick up, drag or punch (the HUD lights up). */
	bool IsAimOnTarget() const { return bAimOnTarget; }

	/** World time of the last punch that connected (the HUD flashes the crosshair). */
	double GetLastPunchHitTime() const { return LastPunchHitTime; }
	bool IsSprinting() const { return bSprinting; }

	/** E: pick up / drag what the crosshair is on, or drop what you hold. */
	void Interact();
	/** Throw what you're carrying. Charge 0..1: 0 = gentle toss (ThrowMinPower), 1 = full throw. */
	void Throw(float Charge = 1.f);
	/** LMB with empty hands: punch with the next fist (left/right alternate; left only while the wand is out). */
	void Punch();
	/**
	 * Knock the mage off balance: the body loosens for a moment and gets shoved along Direction.
	 * Strength 0..1 (1 = the hardest hit). Called for hard impacts on the body and when punched.
	 */
	void Stagger(const FVector& Direction, float Strength);
	/** Nod the camera up by this many degrees; it springs back (visual only, aim doesn't move). */
	void AddCameraKick(float Degrees) { CameraKick = FMath::Max(CameraKick, Degrees); }
	bool IsPunching() const { return PunchTime >= 0.f && PunchTime < PunchDuration; }

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

	/** Arm motors while a hand is being placed: weaker, so the hand target wins. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FPhysicalAnimationData PlacedArmDrive;

	/** Motor strength multiplier while airborne (GDD: motors drop during jumps). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float AirborneMotorScale = 0.6f;

	/** Landing wobble: how much the body's motors dip on the hardest landing (0.5 = half strength). 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float LandingWobble = 0.5f;

	/** Landing wobble: landing speed (cm/s) that gives the full wobble. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float LandingWobbleFullSpeed = 900.f;

	/** How fast the motors recover from a landing wobble or a stagger (strength per second). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	float WobbleRecoveryPerSecond = 1.6f;

	/** Stagger: how much the motors dip on the hardest hit. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float StaggerWobble = 0.6f;

	/** Stagger: how hard the hardest hit shoves you (cm/s added to your movement). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float StaggerShove = 350.f;

	/** Stagger: how hard the hardest hit knocks the upper body back (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float StaggerBodyPush = 250.f;

	/** Stagger: hits below this momentum (kg x cm/s) are ignored; at StaggerFullMomentum you get the full stagger. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float StaggerMinMomentum = 4000.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float StaggerFullMomentum = 25000.f;

	/** How much a punch staggers the mage it hits (0..1). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Stagger")
	float PunchStagger = 0.7f;

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

	/** Grip on the ground: higher = sharper turns and quicker stops, lower = slidier. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float GroundFriction = 5.f;

	/** Upward speed of a jump (cm/s). With JumpGravityScale this sets the jump's height and hang time. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float JumpVelocity = 500.f;

	/** Gravity on the mage (1 = normal). Higher = less floaty jumps and falls. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Movement")
	float JumpGravityScale = 1.4f;

	// --- Hands ---

	/** Guard pose (fists up after punching): where each hand goes, from the eyes in cm. X = forward, Y = out to the side, Z = up. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Hands")
	FVector GuardHandOffset = FVector(55.f, 22.f, -20.f);

	/** Where the right hand holds the wand, from the eyes in cm. X = forward, Y = right, Z = up. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Hands")
	FVector WandHandOffset = FVector(52.f, 16.f, -12.f);

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Hands")
	float HandStiffness = 6000.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Hands")
	float HandDamping = 400.f;

	/** Shoulder-to-hand reach in cm. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Hands")
	float ArmLength = 70.f;

	// --- Pick up ---

	/** How far (cm from the eyes) E can reach. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float InteractRange = 250.f;

	/** Up to this mass you carry it in front of you (GDD: 40 kg solo lift). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float CarryMaxKg = 40.f;

	/** Up to this mass you can drag it; heavier needs friends. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float DragMaxKg = 250.f;

	/** Gap (cm) between the eyes and a carried object's near side; the mouse wheel changes it. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float HoldDistance = 60.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	FVector2D HoldDistanceRange = FVector2D(25.f, 140.f);

	/** Throw speed (cm/s) for a light object; heavier ones go slower. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float ThrowSpeed = 1400.f;

	/** Hold LMB this long (s) for a full-power throw. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float ThrowChargeSeconds = 0.6f;

	/** A quick tap throws at this fraction of full power. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float ThrowMinPower = 0.4f;

	/** How far (cm) a charging throw pulls the object back toward you at full charge (the wind-up). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float ThrowWindUp = 20.f;

	/** How firmly a carried object follows the camera (spring strength and damping). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float CarryStiffness = 3000.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float CarryDamping = 300.f;

	/** You lose hold of a carried object when it ends up this far (cm) from where it should be. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float CarryLoseDistance = 250.f;

	/** Same for dragged objects. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float DragLoseDistance = 220.f;

	/** Walk speed multiplier when dragging something at DragMaxKg. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Pick Up")
	float HeaviestDragSpeedScale = 0.35f;

	// --- Punch ---

	/** How far (cm from the eyes) a punch connects. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchReach = 140.f;

	/** Seconds for the fist to go out and come back. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchDuration = 0.25f;

	/** Seconds between punches. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchCooldown = 0.35f;

	/** Speed (cm/s) a punch gives a light object; heavier ones get less. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchSpeed = 900.f;

	/** Objects up to this mass get the full PunchSpeed. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchFullSpeedKg = 20.f;

	/** After punching (or using the hands), fists stay up in a guard this long, then the arms relax. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float GuardHoldSeconds = 3.f;

	/** On a hit the fist holds at the impact point this long (s) before pulling back. 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchStickSeconds = 0.06f;

	/** On a hit the camera kicks up this many degrees and springs back. 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchCameraKick = 2.f;

	/** How fast the camera kick springs back (higher = quicker). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchCameraKickRecovery = 14.f;

	/** Played at the impact point when a punch connects (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	TObjectPtr<USoundBase> PunchHitSound;

	/** Played when you throw a punch (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	TObjectPtr<USoundBase> PunchSwingSound;

	/** Played at your feet when you land; louder the harder you land (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	TObjectPtr<USoundBase> LandingSound;

	/** Landings slower than this (cm/s) make no sound, so walking off a step stays quiet. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	float LandingSoundMinSpeed = 300.f;

	/** Played when you pick something up or start dragging it (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	TObjectPtr<USoundBase> PickUpSound;

	/** Played when you let go with E, or lose hold of it (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	TObjectPtr<USoundBase> DropSound;

	/** Played when you throw; louder the harder the throw (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	TObjectPtr<USoundBase> ThrowSound;

	/** Played when something knocks you back; louder the harder the hit (empty = silent). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Sounds")
	TObjectPtr<USoundBase> StaggerSound;

	/** How hard a punch shoves another character (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Punch")
	float PunchKnockback = 550.f;

	// --- Crouch ---

	/** How far (cm) the hips drop while crouching (Ctrl). The relaxed legs fold under them. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Crouch")
	float CrouchHipDrop = 45.f;

	/** Leg motor strength while crouching. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Crouch")
	float CrouchLegScale = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Crouch")
	float CrouchSpeed = 220.f;

	// --- Camera ---

	/** How much of the physical head's motion reaches the camera. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraHeadFollow = 0.15f;

	/** Third person: camera offset from straight behind the mage, in cm. X = to the right, Y = up (over the shoulder). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	FVector2D ThirdPersonOffset = FVector2D(45.f, 20.f);

	/** Field of view (degrees, horizontal). Wider shows more of your hands and what you carry. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float FieldOfView = 100.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraSmoothSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float CameraForwardOffset = 10.f;

	/** Landing dip: how far the camera drops per cm/s of landing speed (0 = off). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float LandingDipPerSpeed = 0.012f;

	/** Landing dip: the most the camera drops on a hard landing, cm. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float LandingDipMax = 10.f;

	/** Landing dip: how fast the camera springs back up (higher = quicker). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Camera")
	float LandingDipRecovery = 8.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetupBody();
	void SetupLocalView();
	void TryBindInput();
	APlayerController* GetLocalPC() const;
	bool IsSpellbookOpen() const;
	bool IsWandOut() const;
	/** First person on this machine: the hands-up pose is shown (others see relaxed arms). */
	bool IsLocalFirstPerson() const;

	void OnInteractPressed() { Interact(); }
	void OnThrowPressed();
	void OnThrowReleased();
	/** 0..1 how charged the throw is right now (0 when not charging). */
	float GetThrowCharge() const;
	void OnHoldFarther();
	void OnHoldCloser();
	void OnSprintPressed() { bSprinting = true; }
	void OnSprintReleased() { bSprinting = false; }
	void OnCrouchPressed();
	void OnCrouchReleased();

	/** Where the mage's eyes are (not the camera, which may be pulled back in third person). */
	FVector GetEyeLocation() const;
	FRotator GetViewRotation() const;
	FVector GetShoulderLocation(const FPMArmState& Arm) const;

	/** What E would pick up right now (the welded root of a spawned object), or null. */
	UPrimitiveComponent* FindInteractTarget(FVector& OutHitPoint) const;
	void PickUp(UPrimitiveComponent* Target, const FVector& HitPoint);
	void Drop();

	void UpdateMotors(float DeltaTime);
	void UpdateCrouch(float DeltaTime);
	void ApplyBodyDrives();
	void ApplyArmDrive(const FPMArmState& Arm);
	void UpdateHeld(float DeltaTime);
	void UpdatePunch(float DeltaTime);
	/** The moment the fist lands: knock back whatever is in front. True if it hit something (OutImpact = where). */
	bool PunchHit(FVector& OutImpact);
	/** Where the crosshair points, as a direction from the mage's eyes (third person: the camera sits off to the side). */
	FVector GetAimDirection() const;
	void UpdateMoveSpeed();
	void UpdateInteractPrompt();
	void UpdateArms();
	/** Pull a hand to Target (or let it hang when bPlace is false). */
	void PlaceHand(FPMArmState& Arm, bool bPlace, const FVector& Target);
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

	/** Holds the carried or dragged object. */
	UPROPERTY()
	TObjectPtr<UPhysicsHandleComponent> HoldHandle;

	TWeakObjectPtr<APlayerController> BoundPC;
	TWeakObjectPtr<UPrimitiveComponent> Held;
	FString HeldName;
	float HeldMass = 0.f;
	bool bDragging = false;
	/** Holding LMB to charge a throw, and since when. */
	bool bChargingThrow = false;
	double ThrowChargeStart = 0.0;
	/** Carried object's rotation relative to the view's yaw, kept while carrying. */
	FQuat HeldRelativeRotation = FQuat::Identity;
	/** Rough radius (cm) of the carried object, to hold it clear of the face and place the hands. */
	float HeldRadius = 20.f;
	/** Dragged: the grab point in the object's local space, and how far ahead / how high we hold it. */
	FVector DragLocalPoint = FVector::ZeroVector;
	float DragDistance = 100.f;
	float DragHeight = 50.f;
	FString InteractPrompt;
	bool bAimOnTarget = false;
	double LastPunchHitTime = -100.0;

	/** Seconds since the current punch started (-1 = not punching), which fist, and whether it has landed. */
	float PunchTime = -1.f;
	int32 PunchArm = 0;
	bool bPunchLanded = false;
	/** A connecting punch: the fist holds at the impact for a moment. */
	float PunchStickLeft = 0.f;
	FVector PunchImpact = FVector::ZeroVector;
	/** Current camera kick in degrees (punches, casts), springing back to 0. */
	float CameraKick = 0.f;
	/** World time the arms were last used (punch, pick up, drop, throw); the guard stays up for a while after. */
	double LastArmUseTime = -1000.0;
	void NoteArmUse() { LastArmUseTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }

	bool bBodyReady = false;
	bool bSprinting = false;
	float MotorScale = 1.f;
	/** pm.BodyStiffness / pm.NeckDrive values the motors were last set with (re-applied when they change). */
	float AppliedStiffness = -1.f;
	float AppliedNeckDrive = -1.f;
	bool bCameraInitialised = false;
	FVector SmoothedHead = FVector::ZeroVector;

	/** Current hip drop (cm), eased toward CrouchHipDrop while crouched. */
	float HipDrop = 0.f;
	bool bLegsRelaxed = false;
	float DefaultMeshZ = 0.f;
	float StandingHalfHeight = 96.f;
	float StandingEyeHeight = 64.f;

	/** Landing dip state: falling last frame, how fast we were falling, and the dip (cm) now and to come. */
	bool bWasFalling = false;
	float FallSpeed = 0.f;
	float DipTarget = 0.f;
	float CameraDip = 0.f;

	/** Landing wobble / stagger: how much the motors are weakened right now (0..1), and landing detection. */
	float MotorDip = 0.f;
	bool bBodyWasFalling = false;
	float BodyFallSpeed = 0.f;
	double LastStaggerTime = -100.0;
	float LastStaggerStrength = 0.f;

	/** Something hit the body or the capsule: stagger if it came in hard. */
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
};
