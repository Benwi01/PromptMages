// Prompt-Mages: the apprentice wand - Fireball, Gust, Tether and Frost bolt, paid for with personal mana.

#include "PMWandComponent.h"
#include "PMObjectLibrary.h"
#include "PMSpawnedObject.h"
#include "PMSpellFX.h"
#include "PMSpellProjectile.h"
#include "PMSpellSubsystem.h"
#include "PMSpellcasterComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PMVisualBodyComponent.h"
#include "PMActiveRagdollComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace PMWand
{
	const FName Hand(TEXT("hand_r"));
}

UPMWandComponent::UPMWandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the visible body is posed, so the wand sits exactly in this frame's fist.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPMWandComponent::BeginPlay()
{
	Super::BeginPlay();
	Mana = MaxMana;

	// The wand model (SourceArt/MannequinDemake/make_wand.py), modelled along +Z with the grip at the origin.
	WandRoot = NewObject<USceneComponent>(GetOwner(), TEXT("PMWandRoot"));
	WandRoot->SetUsingAbsoluteLocation(true);
	WandRoot->SetUsingAbsoluteRotation(true);
	WandRoot->SetUsingAbsoluteScale(true);
	WandRoot->SetupAttachment(GetOwner()->GetRootComponent());
	WandRoot->RegisterComponent();
	auto AddPart = [this](const TCHAR* Asset) -> UStaticMeshComponent*
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/PromptMages/Wand/%s.%s"), Asset, Asset));
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("Wand: missing %s (run make_wand.py)."), Asset);
			return nullptr;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner());
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetupAttachment(WandRoot);
		Part->RegisterComponent();
		WandParts.Add(Part);
		return Part;
	};
	AddPart(TEXT("SM_Wand_Grip"));
	AddPart(TEXT("SM_Wand_Shaft"));
	AddPart(TEXT("SM_Wand_Rings"));
	// The crystal glows in the selected spell's colour.
	if (UStaticMeshComponent* Crystal = AddPart(TEXT("SM_Wand_Crystal")))
	{
		TipMaterial = Crystal->CreateDynamicMaterialInstance(0);
		if (TipMaterial)
		{
			TipMaterial->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, FPMSpellDef::Get(Selected).Color);
		}
	}
	WandRoot->SetVisibility(false, true);

	// Tick after the visible body so the wand follows this frame's hand.
	if (UPMVisualBodyComponent* VisualBody = GetOwner()->FindComponentByClass<UPMVisualBodyComponent>())
	{
		AddTickPrerequisiteComponent(VisualBody);
	}

	TryBindInput();
}

void UPMWandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = BoundPC.Get())
	{
		if (WandInput)
		{
			PC->PopInputComponent(WandInput);
		}
	}
	Super::EndPlay(EndPlayReason);
}

APlayerController* UPMWandComponent::GetLocalPC() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? PC : nullptr;
}

void UPMWandComponent::TryBindInput()
{
	APlayerController* PC = GetLocalPC();
	if (!PC || BoundPC.Get() == PC)
	{
		return;
	}
	BoundPC = PC;

	WandInput = NewObject<UInputComponent>(GetOwner(), TEXT("PMWandInput"));
	WandInput->RegisterComponent();
	WandInput->BindKey(EKeys::Q, IE_Pressed, this, &UPMWandComponent::OnToggleWand);
	// RMB is shared with the right hand; don't consume it so the arms still see it when the wand is away.
	WandInput->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &UPMWandComponent::OnCastPressed).bConsumeInput = false;
	WandInput->BindKey(EKeys::One, IE_Pressed, this, &UPMWandComponent::OnSelect1);
	WandInput->BindKey(EKeys::Two, IE_Pressed, this, &UPMWandComponent::OnSelect2);
	WandInput->BindKey(EKeys::Three, IE_Pressed, this, &UPMWandComponent::OnSelect3);
	WandInput->BindKey(EKeys::Four, IE_Pressed, this, &UPMWandComponent::OnSelect4);
	PC->PushInputComponent(WandInput);
}

void UPMWandComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!BoundPC.IsValid())
	{
		TryBindInput();
	}
	Mana = FMath::Min(MaxMana, Mana + ManaRegenPerSecond * DeltaTime);
	CastKick = FMath::FInterpTo(CastKick, 0.f, DeltaTime, 12.f);
	UpdateCrystal(DeltaTime);
	UpdateWandVisual();
}

void UPMWandComponent::Toast(const FString& Message) const
{
	if (UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>())
	{
		Spells->Toast(Message);
	}
}

void UPMWandComponent::SetWandOut(bool bOut)
{
	bWandOut = bOut;
	if (WandRoot)
	{
		WandRoot->SetVisibility(bOut, true);
	}
	if (!bOut)
	{
		if (UPMVisualBodyComponent* VisualBody = GetOwner()->FindComponentByClass<UPMVisualBodyComponent>())
		{
			VisualBody->SetWandGrip(1, false, FVector::ForwardVector, FVector::RightVector);
		}
	}
}

void UPMWandComponent::SelectSpell(EPMSpell Spell)
{
	Selected = Spell;
	if (TipMaterial)
	{
		TipMaterial->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, FPMSpellDef::Get(Spell).Color);
	}
	if (!bWandOut)
	{
		SetWandOut(true); // Picking a spell draws the wand.
	}
}

float UPMWandComponent::GetCooldownRemaining(EPMSpell Spell) const
{
	const int32 Index = static_cast<int32>(Spell);
	return FMath::Max(0.f, static_cast<float>(LastCastTime[Index] + GetSpellSettings(Spell).Cooldown - GetWorld()->GetTimeSeconds()));
}

const FPMSpellSettings& UPMWandComponent::GetSpellSettings(EPMSpell Spell) const
{
	switch (Spell)
	{
	case EPMSpell::Gust:   return Gust;
	case EPMSpell::Tether: return Tether;
	case EPMSpell::Frost:  return Frost;
	default:               return Fireball;
	}
}

void UPMWandComponent::UpdateCrystal(float DeltaTime)
{
	if (!TipMaterial || !bWandOut)
	{
		return;
	}
	// Bright when the selected spell can be cast, dim while it's cooling down or you're short of mana.
	const bool bReady = GetCooldownRemaining(Selected) <= 0.f && Mana >= GetSpellSettings(Selected).ManaCost;
	CrystalGlow = FMath::FInterpTo(CrystalGlow, bReady ? ReadyGlow : NotReadyGlow, DeltaTime, 10.f);
	// A cast flashes it.
	TipMaterial->SetScalarParameterValue(TEXT("Glow"), FMath::Lerp(CrystalGlow, CastFlashGlow, CastKick));
	const FLinearColor Color = FPMSpellDef::Get(Selected).Color;
	const float Shade = FMath::GetMappedRangeValueClamped(FVector2D(NotReadyGlow, ReadyGlow), FVector2D(0.35f, 1.f), CrystalGlow);
	TipMaterial->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, Color * Shade);
}

bool UPMWandComponent::GetAim(FVector& OutTip, FVector& OutDirection, FVector& OutAimPoint, FVector* OutGrip, FRotator* OutView) const
{
	const APlayerController* PC = BoundPC.Get();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!PC || !Character)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMWandAim), false, GetOwner());
	FHitResult Hit;
	const FVector Far = ViewLocation + ViewRotation.Vector() * 6000.f;
	OutAimPoint = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Far, ECC_Visibility, Params) ? FVector(Hit.ImpactPoint) : Far;

	// The wand sits in the right fist and points at whatever the crosshair is on.
	FVector Grip;
	const UPMVisualBodyComponent* VisualBody = GetOwner()->FindComponentByClass<UPMVisualBodyComponent>();
	if (!VisualBody || !VisualBody->GetGripPoint(1, Grip))
	{
		Grip = Character->GetMesh()->GetBoneLocation(PMWand::Hand);
	}
	Grip += ViewRotation.RotateVector(WandGripOffset);
	FVector WandDirection = (OutAimPoint - Grip).GetSafeNormal();
	// Tip the wand up a little so it isn't seen end-on (the spell still goes from the tip to the crosshair).
	const FVector ViewUp = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z);
	const FVector TiltAxis = FVector::CrossProduct(WandDirection, ViewUp).GetSafeNormal();
	if (!TiltAxis.IsNearlyZero())
	{
		// Whichever turn direction goes up (don't depend on the rotation sign convention).
		const FVector A = WandDirection.RotateAngleAxis(WandTiltUp, TiltAxis);
		const FVector B = WandDirection.RotateAngleAxis(-WandTiltUp, TiltAxis);
		WandDirection = FVector::DotProduct(A, ViewUp) >= FVector::DotProduct(B, ViewUp) ? A : B;
	}
	OutTip = Grip + WandDirection * WandLength;
	if (OutGrip)
	{
		*OutGrip = Grip;
	}
	if (OutView)
	{
		*OutView = ViewRotation;
	}
	OutDirection = (OutAimPoint - OutTip).GetSafeNormal();
	if (OutDirection.IsNearlyZero())
	{
		OutDirection = ViewRotation.Vector();
	}
	return true;
}

void UPMWandComponent::UpdateWandVisual()
{
	if (!bWandOut || !WandRoot)
	{
		return;
	}
	FVector Tip, Direction, AimPoint, Grip;
	FRotator View;
	if (!GetAim(Tip, Direction, AimPoint, &Grip, &View))
	{
		return;
	}
	const FVector Along = (Tip - Grip).GetSafeNormal();
	WandRoot->SetWorldLocationAndRotation(Grip, FRotationMatrix::MakeFromZ(Along).Rotator());
	WandRoot->SetWorldScale3D(FVector(WandThickness / 2.f, WandThickness / 2.f, WandLength / 34.f));

	// Turn the visible wrist so the wand runs across the fist: palm facing in (left) and a little down.
	if (UPMVisualBodyComponent* VisualBody = GetOwner()->FindComponentByClass<UPMVisualBodyComponent>())
	{
		const FRotationMatrix Axes(View);
		FVector Palm = -Axes.GetUnitAxis(EAxis::Y) - Axes.GetUnitAxis(EAxis::Z) * 0.35f;
		Palm = (Palm - Along * FVector::DotProduct(Palm, Along)).GetSafeNormal();
		VisualBody->SetWandGrip(1, true, Along, Palm);
	}
}

void UPMWandComponent::OnCastPressed()
{
	if (bWandOut)
	{
		CastSpell();
	}
}

bool UPMWandComponent::CastSpell()
{
	if (!bWandOut)
	{
		return false;
	}
	// No wand-waving while typing in the Spellbook.
	if (const UPMSpellcasterComponent* Book = GetOwner()->FindComponentByClass<UPMSpellcasterComponent>())
	{
		if (Book->IsSpellbookOpen())
		{
			return false;
		}
	}

	const FPMSpellDef& Def = FPMSpellDef::Get(Selected);
	const FPMSpellSettings& Settings = GetSpellSettings(Selected);
	if (GetCooldownRemaining(Selected) > 0.f)
	{
		return false;
	}
	if (Mana < Settings.ManaCost)
	{
		Toast(FString::Printf(TEXT("Not enough mana for %s (%d / %d)."), *Def.Name, FMath::FloorToInt(Mana), FMath::RoundToInt(Settings.ManaCost)));
		return false;
	}

	FVector Tip, Direction, AimPoint;
	if (!GetAim(Tip, Direction, AimPoint))
	{
		return false;
	}

	Mana -= Settings.ManaCost;
	LastCastTime[static_cast<int32>(Selected)] = GetWorld()->GetTimeSeconds();

	// Cast feel: the wand flicks forward, the crystal flashes, the camera kicks.
	CastKick = 1.f;
	if (Settings.CastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Settings.CastSound, Tip);
	}
	if (UPMActiveRagdollComponent* Body = GetOwner()->FindComponentByClass<UPMActiveRagdollComponent>())
	{
		Body->AddCameraKick(CastCameraKick);
	}

	switch (Selected)
	{
	case EPMSpell::Fireball:
	case EPMSpell::Frost:
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.Instigator = Cast<APawn>(GetOwner());
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APMSpellProjectile* Projectile = GetWorld()->SpawnActor<APMSpellProjectile>(APMSpellProjectile::StaticClass(), Tip + Direction * 15.f, Direction.Rotation(), Params))
		{
			Projectile->Launch(Selected, GetOwner(), Direction);
		}
		break;
	}
	case EPMSpell::Gust:
		CastGust(Tip, Direction);
		break;
	case EPMSpell::Tether:
		CastTether(Tip, Direction, AimPoint);
		break;
	default:
		break;
	}
	return true;
}

void UPMWandComponent::CastGust(const FVector& Tip, const FVector& Direction)
{
	APMSpellFX::SpawnCone(GetWorld(), Tip, Direction, GustRange * 0.6f, GustRange * 0.25f, FPMSpellDef::Get(EPMSpell::Gust).Color, 0.3f);

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMGust), false, GetOwner());
	GetWorld()->OverlapMultiByObjectType(Overlaps, Tip, FQuat::Identity, Objects, FCollisionShape::MakeSphere(GustRange), Params);

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(GustAngle));
	TSet<AActor*> Pushed;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor || Pushed.Contains(Actor))
		{
			continue;
		}
		const FVector ToTarget = Actor->GetActorLocation() - Tip;
		const float Distance = ToTarget.Size();
		if (Distance < 1.f || FVector::DotProduct(ToTarget / Distance, Direction) < CosLimit)
		{
			continue; // Outside the cone.
		}
		const float Falloff = 1.f - FMath::Clamp(Distance / GustRange, 0.f, 1.f) * 0.7f;
		const FVector Push = (Direction + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();

		if (ACharacter* Character = Cast<ACharacter>(Actor))
		{
			Pushed.Add(Actor);
			Character->LaunchCharacter(Push * 1100.f * Falloff + FVector(0.f, 0.f, 250.f), true, true);
			continue;
		}

		UPrimitiveComponent* Comp = Overlap.GetComponent();
		APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Actor);
		if (Spawned)
		{
			Comp = Spawned->GetBody();
			Spawned->Extinguish(); // The wind blows small fires out.
		}
		if (Comp && Comp->IsSimulatingPhysics())
		{
			Pushed.Add(Actor);
			const float SpeedChange = FMath::Clamp(60000.f / FMath::Max(Comp->GetMass(), 1.f), 50.f, 1300.f) * Falloff;
			Comp->AddImpulse(Push * SpeedChange, NAME_None, /*bVelChange*/ true);
		}
	}
}

void UPMWandComponent::CastTether(const FVector& Tip, const FVector& Direction, const FVector& AimPoint)
{
	ACharacter* Self = Cast<ACharacter>(GetOwner());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMTether), false, GetOwner());
	FHitResult Hit;
	const FVector End = Tip + Direction * TetherRange;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Tip, End, ECC_Visibility, Params))
	{
		APMSpellFX::SpawnBeam(GetWorld(), Tip, End, FPMSpellDef::Get(EPMSpell::Tether).Color, 0.2f);
		return; // Missed.
	}
	APMSpellFX::SpawnBeam(GetWorld(), Tip, Hit.ImpactPoint, FPMSpellDef::Get(EPMSpell::Tether).Color, 0.35f);

	UPrimitiveComponent* Target = Hit.GetComponent();
	if (APMSpawnedObject* Spawned = Cast<APMSpawnedObject>(Hit.GetActor()))
	{
		Target = Spawned->GetBody();
	}

	// Light things come to you; heavy things and walls pull you to them instead.
	const bool bMovable = Target && Target->IsSimulatingPhysics() && Target->GetMass() < 200.f;
	if (bMovable)
	{
		const FVector Pull = (Tip - Hit.ImpactPoint).GetSafeNormal();
		const float SpeedChange = FMath::Clamp(45000.f / FMath::Max(Target->GetMass(), 1.f), 200.f, 1500.f);
		Target->AddImpulse(Pull * SpeedChange + FVector(0.f, 0.f, 250.f), NAME_None, /*bVelChange*/ true);
	}
	else if (Self)
	{
		const FVector ToPoint = Hit.ImpactPoint - Self->GetActorLocation();
		Self->LaunchCharacter(ToPoint.GetSafeNormal() * FMath::Clamp(ToPoint.Size() * 1.4f, 600.f, 1600.f) + FVector(0.f, 0.f, 350.f), true, true);
	}
}

// --- Console commands for testing ----------------------------------------------------------------

namespace PMWandConsole
{
	UPMWandComponent* LocalWand(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return (PC && PC->GetPawn()) ? PC->GetPawn()->FindComponentByClass<UPMWandComponent>() : nullptr;
	}

	static FAutoConsoleCommandWithWorldAndArgs Wand(TEXT("pm.Wand"), TEXT("pm.Wand [1|0] - draw or put away the wand."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMWandComponent* W = LocalWand(World))
			{
				W->SetWandOut(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !W->IsWandOut());
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs Spell(TEXT("pm.Spell"), TEXT("pm.Spell <1-4> - select a wand spell and cast it at the crosshair."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPMWandComponent* W = LocalWand(World))
			{
				const int32 Index = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]) - 1, 0, 3) : 0;
				W->SelectSpell(static_cast<EPMSpell>(Index));
				W->CastSpell();
			}
		}));
}
