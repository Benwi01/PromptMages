// Prompt-Mages: shows a different (low-poly) character model that copies the hidden physics mannequin's pose.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PMVisualBodyComponent.generated.h"

class UPoseableMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One rigid costume piece (hat, beard, robe...) that follows a bone of the visual body. */
USTRUCT(BlueprintType)
struct FPMCostumePart
{
	GENERATED_BODY()

	/** Modelled in the body's rest-pose space (cm, feet at Z=0, facing +Y), so it lands where it was modelled. */
	UPROPERTY(EditAnywhere, Category = "Costume")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Visual-body bone the piece follows. */
	UPROPERTY(EditAnywhere, Category = "Costume")
	FName Bone;

	/** Hide for the owning player in first person (things on the head), but still cast a shadow. */
	UPROPERTY(EditAnywhere, Category = "Costume")
	bool bHideInFirstPerson = false;
};

/**
 * The character's real body (CharacterMesh0, UE5 mannequin skeleton) keeps doing everything: animation,
 * active-ragdoll physics, grabbing, the camera. This component hides it and shows BodyMesh instead,
 * copying the mannequin's pose onto BodyMesh every frame bone by bone (rotation deltas from the
 * reference pose, so the two skeletons can use different names and bone axes).
 *
 * Swap the model by changing BodyMesh and BoneMap; nothing in the ragdoll needs to change.
 */
UCLASS(ClassGroup = (PromptMages), meta = (BlueprintSpawnableComponent))
class PROMPTMAGES_API UPMVisualBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPMVisualBodyComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The model players see. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	/** Visual-model bone -> mannequin bone it copies. Unmapped bones keep their reference pose. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	TMap<FName, FName> BoneMap;

	/** The visual bone whose position follows the mannequin's pelvis (everything else hangs off it). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FName HipBone = TEXT("DEF-spine");

	/** The visual head bone, hidden for the owning player so it doesn't block the camera. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FName HeadBone = TEXT("DEF-head");

	/** Extra rotation of the model relative to the mannequin, if it was authored facing another way. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Body")
	FRotator ModelRotation = FRotator::ZeroRotator;

	/** Fist: how far each finger joint bends, in degrees. X = knuckle, Y = middle joint, Z = tip joint. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Fist")
	FVector FingerBend = FVector(86.9f, 45.8f, 60.f);

	/**
	 * Fist: where the thumb points, moved from its default spot in front of the index and middle fingers (cm).
	 * X = out from the palm, Y = toward the fingertips, Z = toward the index finger (negative = toward the pinky).
	 */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Fist")
	FVector ThumbTarget = FVector(5.8f, -0.4f, -1.6f);

	/** Fist: extra bend of the thumb's middle joint (X) and tip joint (Y), in degrees. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Fist")
	FVector2D ThumbBend = FVector2D(-32.3f, -26.5f);

	/**
	 * Wand grip: where the wand passes through the fist, moved from its default spot (cm).
	 * X = out from the palm, Y = toward the fingers, Z = toward the index finger.
	 */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Wand Grip")
	FVector GripPointOffset = FVector::ZeroVector;

	/** Crouch: how much the knees point outward as well as forward when the legs bend (0 = straight ahead). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Crouch")
	float CrouchKneesOut = 0.3f;

	/** Costume pieces attached to the body (the mage outfit). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Costume")
	TArray<FPMCostumePart> Costume;

	UPoseableMeshComponent* GetVisual() const { return Visual; }

	/**
	 * Hold a wand in a hand (0 = left, 1 = right): turns the visible wrist so the wand runs across the fist along
	 * WandDirection, with the palm facing PalmDirection (world space). Eases in and out.
	 */
	void SetWandGrip(int32 Hand, bool bOn, const FVector& WandDirection, const FVector& PalmDirection);

	/** Where a wand passes through this hand's fist, in world space. False until the body is posed. */
	bool GetGripPoint(int32 Hand, FVector& OutWorld) const;

	/** Crouch: how far (cm) the hips have dropped. The visible legs bend at the knees to keep the feet on the floor. */
	void SetHipDrop(float Cm) { HipDrop = Cm; }

	/** Close (or open) a hand into a fist: 0 = left, 1 = right. Eases over about a tenth of a second. */
	void SetFist(int32 Hand, bool bClosed) { if (Hand == 0 || Hand == 1) { FistTarget[Hand] = bClosed ? 1.f : 0.f; } }

protected:
	virtual void BeginPlay() override;

private:
	struct FBoneLink
	{
		int32 Dst = INDEX_NONE;
		int32 Src = INDEX_NONE; // INDEX_NONE = keep the visual model's reference pose.
		FQuat SrcRefRotation = FQuat::Identity; // Component space.
		FQuat DstRefRotation = FQuat::Identity; // Component space.
	};

	/** One finger joint that bends when the hand makes a fist. */
	struct FFingerCurl
	{
		int32 Hand = 0;
		/** Extra turn of this joint at a full fist, in the hand's own space (counted from the knuckle). */
		FQuat TurnInHand = FQuat::Identity;
	};

	void BuildLinks();
	void BuildFingerCurls();
	void BuildLegs();
	/** Crouching: bend each visible leg so its foot stays on the floor while the hips are lowered. */
	void SolveLegs();
	void CopyPose();
	void AttachCostume();
	int32 FindVisualBone(const FName& Name) const;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CostumeComponents;

	/** The costume pieces that hide in first person (hat, beard). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> HeadCostume;

	bool bHeadHidden = false;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Source;

	UPROPERTY()
	TObjectPtr<UPoseableMeshComponent> Visual;

	/** One entry per visual bone, in bone-index order (parents before children). */
	TArray<FBoneLink> Links;
	TArray<FTransform> DstRefLocal;
	TArray<FTransform> DstRefComponent;
	TArray<FTransform> TargetComponent;
	TArray<FFingerCurl> Curls;
	/** Per visual bone: index into Curls, or INDEX_NONE. */
	TArray<int32> CurlOfBone;
	int32 HandBones[2] = { INDEX_NONE, INDEX_NONE };
	float Fist[2] = { 0.f, 0.f };
	float FistTarget[2] = { 0.f, 0.f };
	/** The fist settings the curls were last built with; changing them (even while playing) rebuilds. */
	FVector BuiltFingerBend = FVector::ZeroVector;
	FVector BuiltThumbTarget = FVector::ZeroVector;
	FVector2D BuiltThumbBend = FVector2D::ZeroVector;
	FVector BuiltGripPointOffset = FVector::ZeroVector;

	/** Each hand's knuckle line (toward the index finger), palm normal and wand grip point, in the hand bone's space. */
	bool bHasGripFrame[2] = { false, false };
	FVector GripAcrossLocal[2];
	FVector GripPalmLocal[2];
	FVector GripPointLocal[2];
	/** Wand grip requested this frame, eased in/out, and the wrist turn it caused (fingers turn along). */
	bool bWandGrip[2] = { false, false };
	FVector WandGripDir[2];
	FVector WandGripPalm[2];
	float GripBlend[2] = { 0.f, 0.f };
	FQuat HandCorrection[2] = { FQuat::Identity, FQuat::Identity };

	/** Per leg (0 = left, 1 = right): thigh, shin and foot bones, and each one's bone plus everything below it. */
	int32 LegBones[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
	TArray<int32> LegChains[2][3];
	float HipDrop = 0.f;

	int32 SrcPelvis = INDEX_NONE;
	int32 DstHip = INDEX_NONE;
	FVector SrcPelvisRefLocation = FVector::ZeroVector;
};
