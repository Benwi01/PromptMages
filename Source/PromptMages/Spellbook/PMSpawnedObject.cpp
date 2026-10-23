// Prompt-Mages: a physics object summoned from a prompt.

#include "PMSpawnedObject.h"
#include "PMObjectLibrary.h"
#include "PMSpellSubsystem.h"
#include "PromptMages.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"

namespace PMObj
{
	void Paint(UStaticMeshComponent* Comp, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FPMObjectLibrary::GetShapeMaterial(), Comp))
		{
			MID->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, Color);
			Comp->SetMaterial(0, MID);
		}
	}

	/** Approximate volume of a part, used to split the object's weight across its welded parts. */
	float PartVolume(const FPMShapePart& P)
	{
		const float Box = P.Size.X * P.Size.Y * P.Size.Z;
		switch (P.Shape)
		{
		case EPMShape::Sphere:   return Box * PI / 6.f;
		case EPMShape::Cylinder: return Box * PI / 4.f;
		case EPMShape::Cone:     return Box * PI / 12.f;
		default:                 return Box;
		}
	}
}

APMSpawnedObject::APMSpawnedObject()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetNotifyRigidBodyCollision(true);
	Body->SetGenerateOverlapEvents(false);
	RootComponent = Body;

	Shadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shadow"));
	Shadow->SetupAttachment(Body);
	Shadow->SetUsingAbsoluteLocation(true);
	Shadow->SetUsingAbsoluteRotation(true);
	Shadow->SetUsingAbsoluteScale(true);
	Shadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shadow->SetCastShadow(false);
	Shadow->SetVisibility(false);
}

void APMSpawnedObject::Setup(const FPMNounDef& Def, const FPMTraits& InTraits, float SizeScale, const FString& InDisplayName,
	const FString& InCasterName, bool bInMystery, const FString& InMisheardWord)
{
	Traits = InTraits;
	DisplayName = InDisplayName;
	CasterName = InCasterName;
	bMystery = bInMystery;
	MisheardWord = InMisheardWord;
	SpawnTime = GetWorld()->GetTimeSeconds();
	StartZ = GetActorLocation().Z;
	ShadowRadius = ComputeRadius(Def, SizeScale);

#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("PM_%s"), *DisplayName.Replace(TEXT(" "), TEXT("_"))));
#endif

	// One physics material per object, built from the traits.
	PhysMaterial = NewObject<UPhysicalMaterial>(this);
	PhysMaterial->Friction = Traits.Friction;
	PhysMaterial->StaticFriction = Traits.Friction;
	PhysMaterial->bOverrideFrictionCombineMode = true;
	PhysMaterial->FrictionCombineMode = Traits.bGreasy ? EFrictionCombineMode::Min : EFrictionCombineMode::Average;
	PhysMaterial->Restitution = FMath::Min(Traits.Elasticity, 1.f); // Energy gain above 1.0 is added in OnBodyHit.
	PhysMaterial->bOverrideRestitutionCombineMode = true;
	PhysMaterial->RestitutionCombineMode = Traits.Elasticity > 0.6f ? EFrictionCombineMode::Max : EFrictionCombineMode::Average;

	check(Def.Parts.Num() > 0);
	const FPMShapePart& RootPart = Def.Parts[0];
	const FVector RootScale = RootPart.Size / 100.f * SizeScale;

	Body->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(RootPart.Shape));
	Body->SetWorldScale3D(RootScale);
	Body->SetPhysMaterialOverride(PhysMaterial);
	PMObj::Paint(Body, RootPart.Color);

	Shadow->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(EPMShape::Cylinder));
	PMObj::Paint(Shadow, FLinearColor(0.01f, 0.01f, 0.012f));

	// Welded parts each contribute their own mass, so give every part a volume-weighted share of the total weight.
	float TotalVolume = 0.f;
	for (const FPMShapePart& P : Def.Parts)
	{
		TotalVolume += PMObj::PartVolume(P);
	}
	auto PartMass = [&](const FPMShapePart& P)
	{
		return FMath::Max(0.01f, Traits.WeightKg * PMObj::PartVolume(P) / FMath::Max(TotalVolume, 1.f));
	};

	Body->SetMassOverrideInKg(NAME_None, PartMass(RootPart), true);
	Body->SetSimulatePhysics(true);

	// Extra parts use absolute scale so the root's non-uniform scale doesn't distort them; they weld to the root body.
	for (int32 i = 1; i < Def.Parts.Num(); ++i)
	{
		const FPMShapePart& P = Def.Parts[i];
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(P.Shape));
		Comp->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Comp->SetUsingAbsoluteScale(true);
		Comp->SetRelativeScale3D(P.Size / 100.f * SizeScale);
		Comp->SetRelativeLocation((P.Offset * SizeScale) / RootScale);
		Comp->SetRelativeRotation(P.Rotation);
		Comp->SetPhysMaterialOverride(PhysMaterial);
		Comp->SetNotifyRigidBodyCollision(true);
		Comp->BodyInstance.SetMassOverride(PartMass(P), true);
		PMObj::Paint(Comp, P.Color);
		Comp->RegisterComponent();
		Comp->AttachToComponent(Body, FAttachmentTransformRules(EAttachmentRule::KeepRelative, true));
		AddInstanceComponent(Comp);
		Comp->OnComponentHit.AddDynamic(this, &APMSpawnedObject::OnBodyHit);
		ExtraParts.Add(Comp);
	}

	UE_LOG(LogPromptMages, Verbose, TEXT("%s: target %.1f kg, physics mass %.1f kg"), *DisplayName, Traits.WeightKg, Body->GetMass());

	const bool bFloats = Traits.FloatsInAir();
	Body->SetLinearDamping(bFloats ? 0.8f : (Traits.Elasticity > 1.f ? 0.f : 0.01f));
	Body->SetAngularDamping(bFloats ? 1.5f : 0.1f);

	Body->OnComponentHit.AddDynamic(this, &APMSpawnedObject::OnBodyHit);
	Shadow->SetVisibility(true);
	UpdateShadow();
}

float APMSpawnedObject::ComputeHalfHeight(const FPMNounDef& Def, float SizeScale)
{
	float MinZ = 0.f;
	float MaxZ = 0.f;
	for (const FPMShapePart& P : Def.Parts)
	{
		// Rotated parts: use the largest dimension so the estimate stays safe.
		const float Half = (P.Rotation.IsNearlyZero() ? P.Size.Z : P.Size.GetMax()) * 0.5f;
		MinZ = FMath::Min(MinZ, P.Offset.Z - Half);
		MaxZ = FMath::Max(MaxZ, P.Offset.Z + Half);
	}
	return FMath::Max(-MinZ, MaxZ) * SizeScale;
}

float APMSpawnedObject::ComputeRadius(const FPMNounDef& Def, float SizeScale)
{
	float Radius = 10.f;
	for (const FPMShapePart& P : Def.Parts)
	{
		const float Half = FMath::Max(P.Size.X, P.Size.Y) * 0.5f;
		Radius = FMath::Max(Radius, FVector2D(P.Offset.X, P.Offset.Y).Size() + Half);
	}
	return Radius * SizeScale;
}

void APMSpawnedObject::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDying || !Body->IsSimulatingPhysics())
	{
		return;
	}

	// Buoyancy above 1.5 lifts the object in air like a balloon (GDD 3.2).
	if (Traits.FloatsInAir())
	{
		const float Gravity = -GetWorld()->GetGravityZ();
		const float Lift = Gravity * (1.f + (Traits.Buoyancy - 1.5f) * 0.4f);
		Body->AddForce(FVector(0.f, 0.f, Lift), NAME_None, /*bAccelChange*/ true);

		if (GetActorLocation().Z > StartZ + DriftAwayHeight)
		{
			Pop(FString::Printf(TEXT("%s drifted off into the sky. Bye!"), *DisplayName));
			return;
		}
	}

	UpdateShadow();
}

void APMSpawnedObject::UpdateShadow()
{
	if (!Shadow->IsVisible())
	{
		return;
	}
	if (bLanded || GetWorld()->GetTimeSeconds() - SpawnTime > 6.0)
	{
		Shadow->SetVisibility(false);
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMShadow), false, this);
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	const FVector End = Start - FVector(0.f, 0.f, 20000.f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		// The shadow grows as the object gets closer (a cheap "incoming!" read).
		const float Height = FMath::Max(0.f, Start.Z - Hit.ImpactPoint.Z);
		const float Grow = FMath::GetMappedRangeValueClamped(FVector2D(1200.f, 100.f), FVector2D(0.6f, 1.1f), Height);
		const float Diameter = ShadowRadius * 2.f * Grow / 100.f;
		Shadow->SetWorldLocationAndRotation(Hit.ImpactPoint + Hit.ImpactNormal * 1.5f, FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
		Shadow->SetWorldScale3D(FVector(Diameter, Diameter, 0.01f));
	}
	else
	{
		Shadow->SetVisibility(false);
	}
}

void APMSpawnedObject::OnBodyHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bDying)
	{
		return;
	}

	if (!bLanded)
	{
		bLanded = true;
		Shadow->SetVisibility(false);
	}

	// Elasticity above 1.0 gains energy on every bounce until it pops (GDD 3.2).
	if (Traits.Elasticity > 1.f)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		FVector Velocity = Body->GetPhysicsLinearVelocity();
		if (Now - LastBounceTime > 0.12 && Velocity.Size() > 150.f)
		{
			LastBounceTime = Now;

			// Make the contact normal point from the surface toward us.
			FVector Normal = Hit.ImpactNormal.GetSafeNormal();
			if (FVector::DotProduct(GetActorLocation() - Hit.ImpactPoint, Normal) < 0.f)
			{
				Normal = -Normal;
			}
			// If the reported velocity is still heading into the surface, reflect it ourselves.
			const float IntoSurface = FVector::DotProduct(Velocity, Normal);
			if (IntoSurface < 0.f)
			{
				Velocity -= 2.f * IntoSurface * Normal;
			}
			Velocity = (Velocity * Traits.Elasticity).GetClampedToMaxSize(6000.f);
			Body->SetPhysicsLinearVelocity(Velocity);

			UE_LOG(LogPromptMages, Verbose, TEXT("%s bounce %d -> %.0f cm/s"), *DisplayName, BounceCount + 1, Velocity.Size());
			if (++BounceCount >= MaxBounces)
			{
				Pop(FString::Printf(TEXT("%s bounced itself to pieces. *pop*"), *DisplayName));
			}
		}
	}
}

void APMSpawnedObject::Pop(const FString& Message)
{
	if (bDying)
	{
		return;
	}
	bDying = true;

	if (UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>())
	{
		Spells->Toast(Message);
	}

	// Destroy next tick: we may be inside a physics callback.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }));
}

void APMSpawnedObject::Repossess()
{
	Pop(FString::Printf(TEXT("Object cap reached: %s was Repossessed."), *DisplayName));
}

void APMSpawnedObject::RevealMystery()
{
	if (!bMystery || bDying)
	{
		return;
	}

	UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>();
	const FPMNounDef& Surprise = FPMObjectLibrary::Get().GetRandomNoun();
	const FVector Ground = GetActorLocation() - FVector(0.f, 0.f, ComputeHalfHeight(FPMObjectLibrary::Get().GetMysteryCrate(), 1.f));

	bDying = true;
	if (Spells)
	{
		Spells->Toast(FString::Printf(TEXT("The Mystery Crate (sticky note: \"%s\") contained... a %s!"), *MisheardWord, *Surprise.DisplayName));
		Spells->SpawnNoun(Surprise, Surprise.Traits, 1.f, Surprise.DisplayName, Ground, CasterName, false, FString(), 60.f);
	}
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }));
}
