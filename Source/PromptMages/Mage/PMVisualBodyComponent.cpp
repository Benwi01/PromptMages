// Prompt-Mages: shows a different (low-poly) character model that copies the hidden physics mannequin's pose.

#include "PMVisualBodyComponent.h"
#include "PromptMages.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "ReferenceSkeleton.h"

namespace PMVisual
{
	/** Component-space reference pose of every bone. */
	void ComponentRefPose(const FReferenceSkeleton& Ref, TArray<FTransform>& Out)
	{
		const TArray<FTransform>& Local = Ref.GetRefBonePose();
		Out.SetNum(Local.Num());
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			Out[i] = Parent == INDEX_NONE ? Local[i] : Local[i] * Out[Parent];
		}
	}
}

UPMVisualBodyComponent::UPMVisualBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After animation, physics and the ragdoll have finished posing the mannequin this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/MannequinDemake/ue_mannequin_demake.ue_mannequin_demake")));

	// Low-poly "mannequin demake" (Blender DEF- bones) -> UE5 mannequin.
	auto Map = [this](const TCHAR* Dst, const TCHAR* Src) { BoneMap.Add(Dst, Src); };
	Map(TEXT("DEF-spine"), TEXT("pelvis"));
	Map(TEXT("DEF-spine.001"), TEXT("spine_01"));
	Map(TEXT("DEF-spine.002"), TEXT("spine_03"));
	Map(TEXT("DEF-spine.003"), TEXT("spine_04"));
	Map(TEXT("DEF-spine.004"), TEXT("spine_05"));
	Map(TEXT("DEF-neck"), TEXT("neck_01"));
	Map(TEXT("DEF-head"), TEXT("head"));
	for (const TCHAR* Side : { TEXT("L"), TEXT("R") })
	{
		const FString D = FString::Printf(TEXT(".%s"), Side);
		const FString S = FString(TEXT("_")) + FString(Side).ToLower();
		auto MapSide = [&](const TCHAR* DstBase, const TCHAR* SrcBase)
		{
			BoneMap.Add(FName(FString(DstBase) + D), FName(FString(SrcBase) + S));
		};
		MapSide(TEXT("DEF-shoulder"), TEXT("clavicle"));
		MapSide(TEXT("DEF-upper_arm"), TEXT("upperarm"));
		MapSide(TEXT("DEF-forearm"), TEXT("lowerarm"));
		MapSide(TEXT("DEF-hand"), TEXT("hand"));
		MapSide(TEXT("DEF-thigh"), TEXT("thigh"));
		MapSide(TEXT("DEF-shin"), TEXT("calf"));
		MapSide(TEXT("DEF-foot"), TEXT("foot"));
		MapSide(TEXT("DEF-toe"), TEXT("ball"));
		for (const TCHAR* Finger : { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("thumb") })
		{
			for (int32 i = 1; i <= 3; ++i)
			{
				const FString DstBase = FString::Printf(TEXT("DEF-%s%s.%02d"), FCString::Strcmp(Finger, TEXT("thumb")) == 0 ? TEXT("") : TEXT("f_"), Finger, i);
				const FString SrcBase = FString::Printf(TEXT("%s_%02d"), Finger, i);
				BoneMap.Add(FName(DstBase + D), FName(SrcBase + S));
			}
		}
	}
}

void UPMVisualBodyComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMesh* Mesh = BodyMesh.LoadSynchronous();
	if (!Character || !Mesh || !Character->GetMesh())
	{
		UE_LOG(LogPromptMages, Warning, TEXT("Visual body: no character or body mesh, keeping the mannequin."));
		return;
	}
	Source = Character->GetMesh();

	Visual = NewObject<UPoseableMeshComponent>(GetOwner(), TEXT("PMVisualBody"));
	Visual->SetSkinnedAssetAndUpdate(Mesh);
	Visual->SetupAttachment(Source);
	Visual->SetRelativeRotation(ModelRotation);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCastShadow(true);
	Visual->RegisterComponent();

	// The mannequin stays fully alive (animation + physics) but invisible.
	Source->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Source->SetHiddenInGame(true);

	if (Character->IsLocallyControlled())
	{
		Visual->HideBoneByName(HeadBone, PBO_None);
	}

	BuildLinks();
	CopyPose();
}

void UPMVisualBodyComponent::BuildLinks()
{
	const FReferenceSkeleton& DstRef = Visual->GetSkinnedAsset()->GetRefSkeleton();
	const FReferenceSkeleton& SrcRef = Source->GetSkinnedAsset()->GetRefSkeleton();

	TArray<FTransform> SrcRefComponent;
	PMVisual::ComponentRefPose(SrcRef, SrcRefComponent);
	PMVisual::ComponentRefPose(DstRef, DstRefComponent);
	DstRefLocal = DstRef.GetRefBonePose();
	TargetComponent = DstRefComponent;

	// Importers often turn "DEF-upper_arm.L" into "DEF-upper_arm_L", so match names with dots and underscores treated alike.
	auto Normalize = [](const FName& Name) { return Name.ToString().Replace(TEXT("."), TEXT("_")).ToLower(); };
	TMap<FString, FName> NormalizedMap;
	for (const TPair<FName, FName>& Pair : BoneMap)
	{
		NormalizedMap.Add(Normalize(Pair.Key), Pair.Value);
	}

	Links.SetNum(DstRef.GetNum());
	int32 Mapped = 0;
	for (int32 i = 0; i < DstRef.GetNum(); ++i)
	{
		FBoneLink& Link = Links[i];
		Link.Dst = i;
		Link.DstRefRotation = DstRefComponent[i].GetRotation();
		if (const FName* SrcName = NormalizedMap.Find(Normalize(DstRef.GetBoneName(i))))
		{
			Link.Src = SrcRef.FindBoneIndex(*SrcName);
			if (Link.Src != INDEX_NONE)
			{
				Link.SrcRefRotation = SrcRefComponent[Link.Src].GetRotation();
				++Mapped;
			}
		}
	}

	SrcPelvis = SrcRef.FindBoneIndex(TEXT("pelvis"));
	DstHip = INDEX_NONE;
	for (int32 i = 0; i < DstRef.GetNum(); ++i)
	{
		if (Normalize(DstRef.GetBoneName(i)) == Normalize(HipBone))
		{
			DstHip = i;
		}
	}
	if (SrcPelvis != INDEX_NONE)
	{
		SrcPelvisRefLocation = SrcRefComponent[SrcPelvis].GetLocation();
	}
	UE_LOG(LogPromptMages, Log, TEXT("Visual body: %d of %d bones follow the mannequin."), Mapped, DstRef.GetNum());
}

void UPMVisualBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Show our own head again in third person (pm.ThirdPerson).
	if (Visual && Source)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		static IConsoleVariable* ThirdPerson = IConsoleManager::Get().FindConsoleVariable(TEXT("pm.ThirdPerson"));
		const bool bHideHead = Pawn && Pawn->IsLocallyControlled() && !(ThirdPerson && ThirdPerson->GetFloat() > 0.f);
		const int32 Head = Visual->GetBoneIndex(HeadBone);
		if (Head != INDEX_NONE && Visual->IsBoneHidden(Head) != bHideHead)
		{
			if (bHideHead)
			{
				Visual->HideBoneByName(HeadBone, PBO_None);
			}
			else
			{
				Visual->UnHideBoneByName(HeadBone);
			}
		}
	}
	CopyPose();
}

void UPMVisualBodyComponent::CopyPose()
{
	if (!Visual || !Source || Links.Num() == 0)
	{
		return;
	}
	const TArray<FTransform>& SrcPose = Source->GetComponentSpaceTransforms();
	if (SrcPose.Num() == 0)
	{
		return;
	}
	const FReferenceSkeleton& DstRef = Visual->GetSkinnedAsset()->GetRefSkeleton();

	// Rotation that takes the mannequin's component space to the visual model's component space.
	const FQuat SrcToDst = Visual->GetComponentQuat().Inverse() * Source->GetComponentQuat();

	for (const FBoneLink& Link : Links)
	{
		const int32 i = Link.Dst;
		const int32 Parent = DstRef.GetParentIndex(i);
		const FTransform ParentComponent = Parent == INDEX_NONE ? FTransform::Identity : TargetComponent[Parent];

		if (Link.Src == INDEX_NONE || !SrcPose.IsValidIndex(Link.Src))
		{
			TargetComponent[i] = DstRefLocal[i] * ParentComponent;
			continue;
		}

		// How far the mannequin's bone has turned from its reference pose, applied to our bone's reference pose.
		const FQuat Delta = SrcToDst * (SrcPose[Link.Src].GetRotation() * Link.SrcRefRotation.Inverse()) * SrcToDst.Inverse();
		const FQuat Rotation = (Delta * Link.DstRefRotation).GetNormalized();

		FVector Location;
		if (i == DstHip && SrcPose.IsValidIndex(SrcPelvis))
		{
			// The hips follow the mannequin's pelvis (walking bob, crouching, bending).
			Location = DstRefComponent[i].GetLocation() + SrcToDst.RotateVector(SrcPose[SrcPelvis].GetLocation() - SrcPelvisRefLocation);
		}
		else
		{
			Location = ParentComponent.TransformPosition(DstRefLocal[i].GetLocation());
		}
		TargetComponent[i] = FTransform(Rotation, Location, DstRefComponent[i].GetScale3D());
	}

	// Back to parent-relative transforms for the poseable mesh.
	TArray<FTransform>& Local = Visual->BoneSpaceTransforms;
	if (Local.Num() != TargetComponent.Num())
	{
		return;
	}
	for (int32 i = 0; i < TargetComponent.Num(); ++i)
	{
		const int32 Parent = DstRef.GetParentIndex(i);
		Local[i] = Parent == INDEX_NONE ? TargetComponent[i] : TargetComponent[i].GetRelativeTransform(TargetComponent[Parent]);
	}
	Visual->RefreshBoneTransforms();
}
