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

	/** Costume pieces attached to the body (the mage outfit). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Costume")
	TArray<FPMCostumePart> Costume;

	UPoseableMeshComponent* GetVisual() const { return Visual; }

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

	void BuildLinks();
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
	int32 SrcPelvis = INDEX_NONE;
	int32 DstHip = INDEX_NONE;
	FVector SrcPelvisRefLocation = FVector::ZeroVector;
};
