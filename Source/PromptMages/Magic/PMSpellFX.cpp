// Prompt-Mages: short-lived greybox visual effects (bursts, beams, gust cones) built from basic shapes.

#include "PMSpellFX.h"
#include "PMObjectLibrary.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace PMVFX
{
	static void SetParams(UNiagaraComponent* FX, const FLinearColor& Color, float Scale)
	{
		FX->SetVariableLinearColor(TEXT("Color"), Color);
		FX->SetVariableFloat(TEXT("Scale"), Scale);
	}

	UNiagaraComponent* Spawn(UWorld* World, const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Location,
		const FRotator& Rotation, const FLinearColor& Color, float Scale)
	{
		UNiagaraSystem* Asset = System.IsNull() ? nullptr : System.LoadSynchronous();
		if (!World || !Asset)
		{
			return nullptr;
		}
		UNiagaraComponent* FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Asset, Location, Rotation, FVector::OneVector, true, false);
		if (FX)
		{
			SetParams(FX, Color, Scale);
			FX->Activate(true);
		}
		return FX;
	}

	UNiagaraComponent* SpawnAttached(USceneComponent* Parent, const TSoftObjectPtr<UNiagaraSystem>& System, const FLinearColor& Color, float Scale)
	{
		UNiagaraSystem* Asset = System.IsNull() ? nullptr : System.LoadSynchronous();
		if (!Parent || !Asset)
		{
			return nullptr;
		}
		UNiagaraComponent* FX = UNiagaraFunctionLibrary::SpawnSystemAttached(Asset, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, false, false);
		if (FX)
		{
			SetParams(FX, Color, Scale);
			FX->Activate(true);
		}
		return FX;
	}
}

APMSpellFX::APMSpellFX()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetUsingAbsoluteScale(true);
	Light->SetCastShadows(false);
}

void APMSpellFX::Setup(EPMFXShape InShape, const FLinearColor& Color, const FVector& InStartScale, const FVector& InEndScale, float InDuration, float LightRadius)
{
	const EPMShape Shape = InShape == EPMFXShape::Burst ? EPMShape::Sphere : (InShape == EPMFXShape::Beam ? EPMShape::Cylinder : EPMShape::Cone);
	Mesh->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(Shape));
	if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FPMObjectLibrary::GetShapeMaterial(), this))
	{
		// The basic material has no glow and clips bright colours to white, so use the plain colour; the light does the glow.
		MID->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, Color);
		Mesh->SetMaterial(0, MID);
	}
	StartScale = InStartScale;
	EndScale = InEndScale;
	Duration = FMath::Max(InDuration, 0.05f);
	Mesh->SetWorldScale3D(StartScale);

	Light->SetLightColor(Color);
	StartIntensity = 8000.f;
	Light->SetIntensity(StartIntensity);
	Light->SetAttenuationRadius(LightRadius);
	SetLifeSpan(Duration + 0.1f);
}

APMSpellFX* APMSpellFX::SpawnBurst(UWorld* World, const FVector& Location, float Radius, const FLinearColor& Color, float Duration)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APMSpellFX* FX = World->SpawnActor<APMSpellFX>(APMSpellFX::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (FX)
	{
		const float D = Radius * 2.f / 100.f;
		FX->Setup(EPMFXShape::Burst, Color, FVector(D * 0.2f), FVector(D), Duration, Radius * 3.f);
	}
	return FX;
}

APMSpellFX* APMSpellFX::SpawnBeam(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Duration)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector Delta = To - From;
	const float Length = FMath::Max(Delta.Size(), 1.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APMSpellFX* FX = World->SpawnActor<APMSpellFX>(APMSpellFX::StaticClass(), (From + To) * 0.5f,
		FRotationMatrix::MakeFromZ(Delta / Length).Rotator(), Params);
	if (FX)
	{
		FX->Setup(EPMFXShape::Beam, Color, FVector(0.05f, 0.05f, Length / 100.f), FVector(0.01f, 0.01f, Length / 100.f), Duration, 300.f);
	}
	return FX;
}

APMSpellFX* APMSpellFX::SpawnCone(UWorld* World, const FVector& Origin, const FVector& Direction, float Length, float Radius, const FLinearColor& Color, float Duration)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector Dir = Direction.GetSafeNormal();
	// The basic cone points up (+Z) with its centre at the origin; flip it so the wide end faces away from the wand.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APMSpellFX* FX = World->SpawnActor<APMSpellFX>(APMSpellFX::StaticClass(), Origin + Dir * Length * 0.5f,
		FRotationMatrix::MakeFromZ(-Dir).Rotator(), Params);
	if (FX)
	{
		const float D = Radius * 2.f / 100.f;
		FX->Setup(EPMFXShape::Cone, Color, FVector(D * 0.3f, D * 0.3f, Length / 100.f), FVector(D, D, Length / 100.f), Duration, Length);
	}
	return FX;
}

void APMSpellFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
	Mesh->SetWorldScale3D(FMath::Lerp(StartScale, EndScale, FMath::Sqrt(T)));
	Light->SetIntensity(StartIntensity * (1.f - T));
	if (T >= 1.f)
	{
		Destroy();
	}
}
