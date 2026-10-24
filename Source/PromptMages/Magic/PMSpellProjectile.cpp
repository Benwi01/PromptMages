// Prompt-Mages: wand projectiles (Fireball, Frost bolt).

#include "PMSpellProjectile.h"
#include "PMWandComponent.h"
#include "PMObjectLibrary.h"
#include "PMSpawnedObject.h"
#include "PMSpellFX.h"
#include "PMSpellSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

namespace PMSpellHelpers
{
	/** Everything near Center that can be pushed: simulating components (one per actor) and characters. */
	void Gather(UWorld* World, const FVector& Center, float Radius, AActor* Ignore,
		TArray<UPrimitiveComponent*>& OutBodies, TArray<ACharacter*>& OutCharacters)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PMSpellGather), false, Ignore);
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);

		TSet<AActor*> Seen;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Actor = Overlap.GetActor();
			if (!Actor || Seen.Contains(Actor))
			{
				continue;
			}
			if (ACharacter* Character = Cast<ACharacter>(Actor))
			{
				Seen.Add(Actor);
				OutCharacters.Add(Character);
				continue;
			}
			UPrimitiveComponent* Comp = Overlap.GetComponent();
			if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Actor))
			{
				Comp = Spawned->GetBody(); // Push the welded root.
			}
			if (Comp && Comp->IsSimulatingPhysics())
			{
				Seen.Add(Actor);
				OutBodies.Add(Comp);
			}
		}
	}
}

APMSpellProjectile::APMSpellProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(12.f);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Collision;

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	Core->SetupAttachment(Collision);
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->SetCastShadow(false);
	Core->SetRelativeScale3D(FVector(0.28f));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Collision);
	Light->SetIntensity(5000.f);
	Light->SetAttenuationRadius(400.f);
	Light->SetCastShadows(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->bAutoActivate = false;

	InitialLifeSpan = 4.f;
}

void APMSpellProjectile::Launch(EPMSpell InSpell, AActor* InCaster, const FVector& Direction)
{
	Spell = InSpell;
	Caster = InCaster;
	const FPMSpellDef& Def = FPMSpellDef::Get(Spell);

	Core->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(EPMShape::Sphere));
	if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FPMObjectLibrary::GetShapeMaterial(), this))
	{
		MID->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, Def.Color);
		Core->SetMaterial(0, MID);
	}
	Light->SetLightColor(Def.Color);

	if (InCaster)
	{
		Collision->IgnoreActorWhenMoving(InCaster, true);
	}

	// Fireballs lob in a gentle arc; frost bolts fly straight and fast.
	const bool bFire = Spell == EPMSpell::Fireball;
	Movement->ProjectileGravityScale = bFire ? 0.35f : 0.f;
	const UPMWandComponent* Wand = InCaster ? InCaster->FindComponentByClass<UPMWandComponent>() : nullptr;
	Movement->InitialSpeed = bFire ? (Wand ? Wand->FireballSpeed : 2200.f) : (Wand ? Wand->FrostSpeed : 3200.f);
	Movement->MaxSpeed = Movement->InitialSpeed;
	Movement->Velocity = Direction.GetSafeNormal() * Movement->InitialSpeed;
	Movement->OnProjectileStop.AddDynamic(this, &APMSpellProjectile::OnStop);
	Movement->Activate();
}

void APMSpellProjectile::OnStop(const FHitResult& Hit)
{
	Detonate(Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation());
}

void APMSpellProjectile::LifeSpanExpired()
{
	Detonate(GetActorLocation());
	Super::LifeSpanExpired();
}

void APMSpellProjectile::Detonate(const FVector& Location)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;

	if (Spell == EPMSpell::Fireball)
	{
		Explode(GetWorld(), Location, Caster.Get());
	}
	else
	{
		Shatter(GetWorld(), Location, Caster.Get());
	}
	Destroy();
}

void APMSpellProjectile::Explode(UWorld* World, const FVector& Center, AActor* InCaster)
{
	if (!World)
	{
		return;
	}
	const UPMWandComponent* Wand = InCaster ? InCaster->FindComponentByClass<UPMWandComponent>() : nullptr;
	const float Radius = Wand ? Wand->FireballRadius : 350.f;
	const float Push = Wand ? Wand->FireballPush : 70000.f;
	const float Launch = Wand ? Wand->FireballLaunch : 900.f;
	APMSpellFX::SpawnBurst(World, Center, Radius * 0.6f, FPMSpellDef::Get(EPMSpell::Fireball).Color, 0.3f);

	TArray<UPrimitiveComponent*> Bodies;
	TArray<ACharacter*> Characters;
	PMSpellHelpers::Gather(World, Center, Radius, nullptr, Bodies, Characters);

	int32 Ignited = 0;
	for (UPrimitiveComponent* Body : Bodies)
	{
		const FVector ToBody = Body->GetComponentLocation() - Center;
		const float Falloff = 1.f - FMath::Clamp(ToBody.Size() / Radius, 0.f, 1.f);
		const FVector Dir = (ToBody.GetSafeNormal() + FVector(0.f, 0.f, 0.5f)).GetSafeNormal();
		// Mass matters: a crate flies, an anvil shuffles.
		const float SpeedChange = FMath::Clamp(Push / FMath::Max(Body->GetMass(), 1.f), 40.f, 1100.f) * Falloff;
		Body->AddImpulse(Dir * SpeedChange, NAME_None, /*bVelChange*/ true);

		if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Body->GetOwner()))
		{
			if (Falloff > 0.2f && Spawned->Ignite())
			{
				++Ignited;
			}
		}
	}
	for (ACharacter* Character : Characters)
	{
		const FVector ToChar = Character->GetActorLocation() - Center;
		const float Falloff = 1.f - FMath::Clamp(ToChar.Size() / Radius, 0.f, 1.f);
		Character->LaunchCharacter(ToChar.GetSafeNormal2D() * Launch * Falloff + FVector(0.f, 0.f, Launch * 0.5f * Falloff), true, true);
	}

	if (Ignited > 0)
	{
		if (UPMSpellSubsystem* Spells = World->GetSubsystem<UPMSpellSubsystem>())
		{
			Spells->Toast(Ignited == 1 ? TEXT("Something caught fire.") : FString::Printf(TEXT("%d things caught fire."), Ignited));
		}
	}
}

void APMSpellProjectile::Shatter(UWorld* World, const FVector& Center, AActor* InCaster)
{
	if (!World)
	{
		return;
	}
	const UPMWandComponent* Wand = InCaster ? InCaster->FindComponentByClass<UPMWandComponent>() : nullptr;
	const float Radius = Wand ? Wand->FrostRadius : 160.f;
	APMSpellFX::SpawnBurst(World, Center, Radius * 0.7f, FPMSpellDef::Get(EPMSpell::Frost).Color, 0.25f);

	TArray<UPrimitiveComponent*> Bodies;
	TArray<ACharacter*> Characters;
	PMSpellHelpers::Gather(World, Center, Radius, nullptr, Bodies, Characters);

	for (UPrimitiveComponent* Body : Bodies)
	{
		if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Body->GetOwner()))
		{
			Spawned->Freeze(Wand ? Wand->FrostObjectSeconds : 4.f);
		}
	}
	for (ACharacter* Character : Characters)
	{
		FreezeCharacter(Character, Wand ? Wand->FrostMageSeconds : 2.5f);
	}
}

void APMSpellProjectile::FreezeCharacter(ACharacter* Character, float Seconds)
{
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move || Move->MovementMode == MOVE_None)
	{
		return;
	}
	Move->StopMovementImmediately();
	Move->DisableMovement();

	FTimerHandle Handle;
	TWeakObjectPtr<UCharacterMovementComponent> WeakMove = Move;
	Character->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakMove]()
	{
		if (UCharacterMovementComponent* M = WeakMove.Get())
		{
			M->SetMovementMode(MOVE_Falling);
		}
	}), Seconds, false);

	if (Character->IsLocallyControlled())
	{
		if (UPMSpellSubsystem* Spells = Character->GetWorld()->GetSubsystem<UPMSpellSubsystem>())
		{
			Spells->Toast(TEXT("You're frozen solid!"));
		}
	}
}
