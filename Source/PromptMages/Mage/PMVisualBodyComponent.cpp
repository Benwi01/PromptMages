// Prompt-Mages: shows a different (low-poly) character model that copies the hidden physics mannequin's pose.

#include "PMVisualBodyComponent.h"
#include "PromptMages.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "ReferenceSkeleton.h"

namespace PMVisual
{
	TAutoConsoleVariable<int32> CVarShowPhysicsBody(TEXT("pm.ShowPhysicsBody"), 0,
		TEXT("1 = also show the hidden physics mannequin that drives the visual body (debug)."));

	TAutoConsoleVariable<float> CVarMaxHeadBend(TEXT("pm.MaxHeadBend"), 25.f,
		TEXT("Most the visible neck and head may each bend away from their rest pose, in degrees (0 = no limit)."));

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

	// The mage outfit (built by SourceArt/MannequinDemake/make_mage_costume.py).
	auto Wear = [this](const TCHAR* Asset, const TCHAR* Bone, bool bHead)
	{
		FPMCostumePart& Part = Costume.AddDefaulted_GetRef();
		Part.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/PromptMages/Mage/%s.%s"), Asset, Asset)));
		Part.Bone = Bone;
		Part.bHideInFirstPerson = bHead;
	};
	Wear(TEXT("SM_Mage_Hat"), TEXT("DEF-head"), true);
	Wear(TEXT("SM_Mage_HatBand"), TEXT("DEF-head"), true);
	Wear(TEXT("SM_Mage_Beard"), TEXT("DEF-head"), true);
	Wear(TEXT("SM_Mage_RobeSkirt"), TEXT("DEF-spine"), false);
	Wear(TEXT("SM_Mage_Belt"), TEXT("DEF-spine"), false);
	Wear(TEXT("SM_Mage_CuffL"), TEXT("DEF-forearm_L"), true); // Right in front of the camera in first person.
	Wear(TEXT("SM_Mage_CuffR"), TEXT("DEF-forearm_R"), true);

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
	BuildFingerCurls();
	AttachCostume();
	CopyPose();
}

int32 UPMVisualBodyComponent::FindVisualBone(const FName& Name) const
{
	const FReferenceSkeleton& DstRef = Visual->GetSkinnedAsset()->GetRefSkeleton();
	const FString Wanted = Name.ToString().Replace(TEXT("."), TEXT("_")).ToLower();
	for (int32 i = 0; i < DstRef.GetNum(); ++i)
	{
		if (DstRef.GetBoneName(i).ToString().Replace(TEXT("."), TEXT("_")).ToLower() == Wanted)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UPMVisualBodyComponent::AttachCostume()
{
	const FReferenceSkeleton& DstRef = Visual->GetSkinnedAsset()->GetRefSkeleton();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bLocal = Pawn && Pawn->IsLocallyControlled();

	for (const FPMCostumePart& Part : Costume)
	{
		UStaticMesh* Mesh = Part.Mesh.LoadSynchronous();
		const int32 Bone = FindVisualBone(Part.Bone);
		if (!Mesh || Bone == INDEX_NONE)
		{
			UE_LOG(LogPromptMages, Warning, TEXT("Costume: skipping %s on bone %s."), *Part.Mesh.ToString(), *Part.Bone.ToString());
			continue;
		}

		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(GetOwner());
		Piece->SetStaticMesh(Mesh);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(true);
		Piece->SetupAttachment(Visual, DstRef.GetBoneName(Bone));
		// Pieces are modelled in rest-pose space: undo the bone's rest transform so they sit where they were modelled.
		Piece->SetRelativeTransform(DstRefComponent[Bone].Inverse());
		if (Part.bHideInFirstPerson)
		{
			Piece->bCastHiddenShadow = true;
			Piece->SetOwnerNoSee(bLocal);
			HeadCostume.Add(Piece);
		}
		Piece->RegisterComponent();
		CostumeComponents.Add(Piece);
	}
	bHeadHidden = bLocal;
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

	// Calibrate: the two skeletons' reference poses aren't the same shape (spine curve, shoulder angle, neck tilt).
	// Turn each visual bone's reference rotation so it points the way its mannequin partner points, measured
	// toward its main child. Then the copied pose matches the mannequin's actual shape, not just its movement.
	const FQuat ModelToMannequin = FQuat(ModelRotation).Inverse();
	for (int32 i = 0; i < DstRef.GetNum(); ++i)
	{
		const FBoneLink& Link = Links[i];
		if (Link.Src == INDEX_NONE)
		{
			continue;
		}
		// Main child: prefer the spine / middle finger, otherwise the first mapped child.
		int32 Child = INDEX_NONE;
		for (int32 j = i + 1; j < DstRef.GetNum(); ++j)
		{
			if (DstRef.GetParentIndex(j) != i || Links[j].Src == INDEX_NONE)
			{
				continue;
			}
			const FString ChildName = DstRef.GetBoneName(j).ToString().ToLower();
			if (Child == INDEX_NONE || ChildName.Contains(TEXT("spine")) || ChildName.Contains(TEXT("middle")) || ChildName.Contains(TEXT("neck")) || ChildName.Contains(TEXT("head")))
			{
				Child = j;
			}
		}
		if (Child == INDEX_NONE)
		{
			continue; // Tips (head, toes, last finger joints) keep their own reference orientation.
		}
		const FVector DstDir = (DstRefComponent[Child].GetLocation() - DstRefComponent[i].GetLocation()).GetSafeNormal();
		const FVector SrcDir = ModelToMannequin.RotateVector(
			SrcRefComponent[Links[Child].Src].GetLocation() - SrcRefComponent[Link.Src].GetLocation()).GetSafeNormal();
		if (!DstDir.IsNearlyZero() && !SrcDir.IsNearlyZero())
		{
			Links[i].DstRefRotation = (FQuat::FindBetweenNormals(DstDir, SrcDir) * Link.DstRefRotation).GetNormalized();
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

void UPMVisualBodyComponent::BuildFingerCurls()
{
	BuiltFingerBend = FingerBend;
	BuiltThumbTarget = ThumbTarget;
	BuiltThumbBend = ThumbBend;
	BuiltGripPointOffset = GripPointOffset;
	const FReferenceSkeleton& DstRef = Visual->GetSkinnedAsset()->GetRefSkeleton();
	Curls.Reset();
	CurlOfBone.Init(INDEX_NONE, DstRef.GetNum());

	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const TCHAR* Side = Hand == 0 ? TEXT("L") : TEXT("R");
		auto Bone = [&](const TCHAR* Base) { return FindVisualBone(FName(FString::Printf(TEXT("%s.%s"), Base, Side))); };
		auto Pos = [&](int32 Index) { return DstRefComponent[Index].GetLocation(); };

		HandBones[Hand] = Bone(TEXT("DEF-hand"));
		const int32 Index1 = Bone(TEXT("DEF-f_index.01"));
		const int32 Pinky1 = Bone(TEXT("DEF-f_pinky.01"));
		const int32 Middle1 = Bone(TEXT("DEF-f_middle.01"));
		const int32 Middle2 = Bone(TEXT("DEF-f_middle.02"));
		const int32 Thumb1 = Bone(TEXT("DEF-thumb.01"));
		const int32 Thumb2 = Bone(TEXT("DEF-thumb.02"));
		if (HandBones[Hand] == INDEX_NONE || Index1 == INDEX_NONE || Pinky1 == INDEX_NONE || Middle1 == INDEX_NONE
			|| Middle2 == INDEX_NONE || Thumb1 == INDEX_NONE || Thumb2 == INDEX_NONE)
		{
			UE_LOG(LogPromptMages, Warning, TEXT("Visual body: can't find the %s hand's finger bones, no fists."), Side);
			continue;
		}

		// The hand's shape in the rest pose: across the knuckles, along the fingers, and out of the palm
		// (the side the thumb sits on), so we don't depend on how the bones' axes happen to point.
		const FVector Across = (Pos(Index1) - Pos(Pinky1)).GetSafeNormal();
		const FVector Along = (Pos(Middle2) - Pos(Middle1)).GetSafeNormal();
		FVector Palm = FVector::CrossProduct(Across, Along).GetSafeNormal();
		const FVector KnuckleMid = (Pos(Index1) + Pos(Pinky1)) * 0.5f;
		if (FVector::DotProduct(Pos(Thumb1) - KnuckleMid, Palm) < 0.f)
		{
			Palm = -Palm;
		}
		const FQuat HandRest = DstRefComponent[HandBones[Hand]].GetRotation();

		// Wand grip: the wand runs along the knuckle line, through the hollow of the curled fingers.
		const FVector GripBind = Pos(Middle1) * 0.75f + Pos(HandBones[Hand]) * 0.25f + Palm * (2.5f + GripPointOffset.X)
			+ Along * GripPointOffset.Y + Across * GripPointOffset.Z;
		GripAcrossLocal[Hand] = HandRest.UnrotateVector(Across);
		GripPalmLocal[Hand] = HandRest.UnrotateVector(Palm);
		GripPointLocal[Hand] = HandRest.UnrotateVector(GripBind - Pos(HandBones[Hand]));
		bHasGripFrame[Hand] = true;

		auto InHand = [&](const FQuat& Turn) { return (HandRest.Inverse() * Turn * HandRest).GetNormalized(); };
		auto Bend = [](const FVector& Axis, float Degrees) { return FQuat(Axis, FMath::DegreesToRadians(Degrees)); };

		// Fingers: turning about Along x Palm swings them toward the palm. Degrees per joint, counted from the knuckle.
		const FVector FingerAxis = FVector::CrossProduct(Along, Palm).GetSafeNormal();
		const float FingerTotal[3] = { FingerBend.X, FingerBend.X + FingerBend.Y, FingerBend.X + FingerBend.Y + FingerBend.Z };

		// Thumb: swing it across the front of the curled index and middle fingers, then wrap the tip a little.
		const FVector ThumbDir = (Pos(Thumb2) - Pos(Thumb1)).GetSafeNormal();
		const FVector FingerFront = (Pos(Index1) + Pos(Middle1)) * 0.5f + Palm * (3.f + ThumbTarget.X)
			+ Along * (ThumbTarget.Y - 1.f) + Across * ThumbTarget.Z;
		const FVector ThumbAim = (FingerFront - Pos(Thumb1)).GetSafeNormal();
		const FQuat ThumbSwing = FQuat::FindBetweenNormals(ThumbDir, ThumbAim);
		const FVector ThumbWrapAxis = FVector::CrossProduct(ThumbAim, Palm).GetSafeNormal();
		const FQuat ThumbTurns[3] = { ThumbSwing, Bend(ThumbWrapAxis, ThumbBend.X) * ThumbSwing, Bend(ThumbWrapAxis, ThumbBend.Y) * ThumbSwing };

		for (const TCHAR* Finger : { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("thumb") })
		{
			const bool bThumb = FCString::Strcmp(Finger, TEXT("thumb")) == 0;
			for (int32 Joint = 1; Joint <= 3; ++Joint)
			{
				const int32 Index = Bone(*FString::Printf(TEXT("DEF-%s%s.%02d"), bThumb ? TEXT("") : TEXT("f_"), Finger, Joint));
				if (Index == INDEX_NONE)
				{
					break;
				}
				FFingerCurl& Curl = Curls.AddDefaulted_GetRef();
				Curl.Hand = Hand;
				Curl.TurnInHand = InHand(bThumb ? ThumbTurns[Joint - 1] : Bend(FingerAxis, FingerTotal[Joint - 1]));
				CurlOfBone[Index] = Curls.Num() - 1;
			}
		}
	}
}

void UPMVisualBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// pm.ShowPhysicsBody 1: show the hidden physics mannequin too, to compare poses.
	if (Source)
	{
		static IConsoleVariable* ShowPhysics = IConsoleManager::Get().FindConsoleVariable(TEXT("pm.ShowPhysicsBody"));
		const bool bShow = ShowPhysics && ShowPhysics->GetInt() != 0;
		if (Source->bHiddenInGame == bShow)
		{
			Source->SetHiddenInGame(!bShow);
		}
	}

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
		if (bHideHead != bHeadHidden)
		{
			bHeadHidden = bHideHead;
			for (UStaticMeshComponent* Piece : HeadCostume)
			{
				Piece->SetOwnerNoSee(bHideHead);
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

	const int32 HeadIdx = DstRef.FindBoneIndex(HeadBone);
	const int32 NeckIdx = HeadIdx != INDEX_NONE ? DstRef.GetParentIndex(HeadIdx) : INDEX_NONE;
	const float MaxBend = FMath::DegreesToRadians(PMVisual::CVarMaxHeadBend.GetValueOnGameThread());

	// Fist settings changed in the Details panel (also while playing): rebuild the finger turns.
	if (FingerBend != BuiltFingerBend || ThumbTarget != BuiltThumbTarget || ThumbBend != BuiltThumbBend
		|| GripPointOffset != BuiltGripPointOffset)
	{
		BuildFingerCurls();
	}

	// Fists: ease each hand toward open/closed.
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Fist[Hand] = FMath::FInterpConstantTo(Fist[Hand], FistTarget[Hand], DeltaTime, 9.f);
	}
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		GripBlend[Hand] = FMath::FInterpConstantTo(GripBlend[Hand], bWandGrip[Hand] ? 1.f : 0.f, DeltaTime, 8.f);
		HandCorrection[Hand] = FQuat::Identity;
	}

	// Bend a finger joint toward the palm, about the hand's current knuckle axis (parents are posed first).
	auto CurlFinger = [&](int32 i)
	{
		const int32 CurlIndex = CurlOfBone.IsValidIndex(i) ? CurlOfBone[i] : INDEX_NONE;
		if (CurlIndex == INDEX_NONE)
		{
			return;
		}
		const FFingerCurl& Curl = Curls[CurlIndex];
		// Fingers turn with a wrist that was turned to hold the wand.
		if (!HandCorrection[Curl.Hand].Equals(FQuat::Identity))
		{
			TargetComponent[i].SetRotation((HandCorrection[Curl.Hand] * TargetComponent[i].GetRotation()).GetNormalized());
		}
		const float Amount = Fist[Curl.Hand];
		if (Amount <= 0.f || !TargetComponent.IsValidIndex(HandBones[Curl.Hand]))
		{
			return;
		}
		// The turn was worked out in the hand's own space: carry it over to where the hand is now.
		const FQuat HandNow = TargetComponent[HandBones[Curl.Hand]].GetRotation();
		const FQuat Turn = HandNow * FQuat::Slerp(FQuat::Identity, Curl.TurnInHand, Amount) * HandNow.Inverse();
		TargetComponent[i].SetRotation((Turn * TargetComponent[i].GetRotation()).GetNormalized());
	};

	for (const FBoneLink& Link : Links)
	{
		const int32 i = Link.Dst;
		const int32 Parent = DstRef.GetParentIndex(i);
		const FTransform ParentComponent = Parent == INDEX_NONE ? FTransform::Identity : TargetComponent[Parent];

		if (Link.Src == INDEX_NONE || !SrcPose.IsValidIndex(Link.Src))
		{
			TargetComponent[i] = DstRefLocal[i] * ParentComponent;
			CurlFinger(i);
			continue;
		}

		// How far the mannequin's bone has turned from its reference pose, applied to our bone's reference pose.
		const FQuat Delta = SrcToDst * (SrcPose[Link.Src].GetRotation() * Link.SrcRefRotation.Inverse()) * SrcToDst.Inverse();
		FQuat Rotation = (Delta * Link.DstRefRotation).GetNormalized();

		// The physical head can flop far forward (it's a heavy ball on a springy neck). Show at most
		// pm.MaxHeadBend degrees per joint so the mage doesn't stare at its own chest.
		if ((i == HeadIdx || i == NeckIdx) && Parent != INDEX_NONE && MaxBend > 0.f)
		{
			const FQuat ParentNow = ParentComponent.GetRotation();
			const FQuat LocalRest = Links[Parent].DstRefRotation.Inverse() * Link.DstRefRotation;
			const FQuat Bend = (ParentNow.Inverse() * Rotation) * LocalRest.Inverse();
			const float Angle = Bend.GetAngle();
			if (Angle > MaxBend)
			{
				Rotation = (ParentNow * FQuat::Slerp(FQuat::Identity, Bend, MaxBend / Angle) * LocalRest).GetNormalized();
			}
		}

		// Holding the wand: turn the wrist so the wand lies along the knuckle line with the palm facing in.
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			if (i == HandBones[Hand] && GripBlend[Hand] > 0.f && bHasGripFrame[Hand])
			{
				const FQuat ToComponent = Visual->GetComponentQuat().Inverse();
				const FVector WantAcross = ToComponent.RotateVector(WandGripDir[Hand]);
				const FVector WantPalm = ToComponent.RotateVector(WandGripPalm[Hand]);
				const FQuat Want = FRotationMatrix::MakeFromXY(WantAcross, WantPalm).ToQuat()
					* FRotationMatrix::MakeFromXY(GripAcrossLocal[Hand], GripPalmLocal[Hand]).ToQuat().Inverse();
				const FQuat Turned = FQuat::Slerp(Rotation, Want, GripBlend[Hand]).GetNormalized();
				HandCorrection[Hand] = (Turned * Rotation.Inverse()).GetNormalized();
				Rotation = Turned;
			}
		}

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
		CurlFinger(i);
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

	// Debug: how far each visual bone sits from the mannequin bone it copies (world cm).
	if (GFrameCounter % 120 == 0 && UE_LOG_ACTIVE(LogPromptMages, Verbose))
	{
		const FTransform DstWorld = Visual->GetComponentTransform();
		const FTransform SrcWorld = Source->GetComponentTransform();
		const FReferenceSkeleton& SrcRef = Source->GetSkinnedAsset()->GetRefSkeleton();
		FString Line;
		for (const FBoneLink& Link : Links)
		{
			if (Link.Src == INDEX_NONE || DstRef.GetBoneName(Link.Dst).ToString().Contains(TEXT("_0")))
			{
				continue; // Skip fingers to keep the line short.
			}
			const FVector D = DstWorld.TransformPosition(TargetComponent[Link.Dst].GetLocation());
			const FVector S = SrcWorld.TransformPosition(SrcPose[Link.Src].GetLocation());
			Line += FString::Printf(TEXT("%s=%.0f "), *SrcRef.GetBoneName(Link.Src).ToString(), FVector::Dist(D, S));
		}
		UE_LOG(LogPromptMages, Verbose, TEXT("Visual body offsets (cm): %s"), *Line);
	}
}

void UPMVisualBodyComponent::SetWandGrip(int32 Hand, bool bOn, const FVector& WandDirection, const FVector& PalmDirection)
{
	if (Hand != 0 && Hand != 1)
	{
		return;
	}
	bWandGrip[Hand] = bOn;
	if (bOn)
	{
		WandGripDir[Hand] = WandDirection.GetSafeNormal();
		WandGripPalm[Hand] = PalmDirection.GetSafeNormal();
	}
}

bool UPMVisualBodyComponent::GetGripPoint(int32 Hand, FVector& OutWorld) const
{
	if ((Hand != 0 && Hand != 1) || !Visual || !bHasGripFrame[Hand] || !TargetComponent.IsValidIndex(HandBones[Hand]))
	{
		return false;
	}
	// Position and rotation only: the imported bones carry a x100 scale that would blow the offset up.
	const FTransform& HandNow = TargetComponent[HandBones[Hand]];
	OutWorld = Visual->GetComponentTransform().TransformPosition(HandNow.GetLocation() + HandNow.GetRotation().RotateVector(GripPointLocal[Hand]));
	return true;
}
