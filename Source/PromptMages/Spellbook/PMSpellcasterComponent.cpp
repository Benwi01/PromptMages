// Prompt-Mages: lets a player open the Spellbook and cast prompts.

#include "PMSpellcasterComponent.h"
#include "PMHudWidget.h"
#include "PMObjectLibrary.h"
#include "PMSpawnedObject.h"
#include "PMSpellSubsystem.h"
#include "PromptMages.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"

UPMSpellcasterComponent::UPMSpellcasterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPMSpellcasterComponent::BeginPlay()
{
	Super::BeginPlay();
	TryInitLocalPlayer();
}

void UPMSpellcasterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = BoundPC.Get())
	{
		if (CasterInput)
		{
			PC->PopInputComponent(CasterInput);
		}
		if (bSpellbookOpen)
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
	if (Hud)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
	}
	if (TargetMarker)
	{
		TargetMarker->DestroyComponent();
		TargetMarker = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

APlayerController* UPMSpellcasterComponent::GetLocalPC() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? PC : nullptr;
}

void UPMSpellcasterComponent::OnHelpPressed()
{
	if (Hud && !IsSpellbookOpen())
	{
		Hud->ShowHelp(HelpShowAgainSeconds);
	}
}

void UPMSpellcasterComponent::TryInitLocalPlayer()
{
	APlayerController* PC = GetLocalPC();
	if (!PC || BoundPC.Get() == PC)
	{
		return;
	}
	BoundPC = PC;

	// Our own input component, pushed on top of the template's Enhanced Input bindings.
	CasterInput = NewObject<UInputComponent>(GetOwner(), TEXT("PMSpellcasterInput"));
	CasterInput->RegisterComponent();
	CasterInput->BindKey(EKeys::Tab, IE_Pressed, this, &UPMSpellcasterComponent::OnTabPressed);
	CasterInput->BindKey(EKeys::H, IE_Pressed, this, &UPMSpellcasterComponent::OnHelpPressed).bConsumeInput = false;
	PC->PushInputComponent(CasterInput);

	if (!TargetMarker)
	{
		TargetMarker = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("PMTargetMarker"));
		TargetMarker->SetStaticMesh(FPMObjectLibrary::GetShapeMesh(EPMShape::Cylinder));
		TargetMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetMarker->SetCastShadow(false);
		TargetMarker->SetUsingAbsoluteLocation(true);
		TargetMarker->SetUsingAbsoluteRotation(true);
		TargetMarker->SetUsingAbsoluteScale(true);
		TargetMarker->SetWorldScale3D(FVector(1.2f, 1.2f, 0.02f));
		TargetMarker->SetHiddenInGame(true);
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FPMObjectLibrary::GetShapeMaterial(), TargetMarker))
		{
			MID->SetVectorParameterValue(FPMObjectLibrary::ColorParameterName, FLinearColor(0.1f, 0.9f, 1.f));
			TargetMarker->SetMaterial(0, MID);
		}
		TargetMarker->RegisterComponent();
	}

	if (!Hud)
	{
		Hud = CreateWidget<UPMHudWidget>(PC, UPMHudWidget::StaticClass());
		if (Hud)
		{
			Hud->InitHud(this);
			Hud->AddToViewport(10);
		}
	}
}

void UPMSpellcasterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The pawn may be possessed after BeginPlay; keep trying until we have a local controller.
	if (!BoundPC.IsValid())
	{
		TryInitLocalPlayer();
		return;
	}
}

bool UPMSpellcasterComponent::GetView(FVector& OutLocation, FRotator& OutRotation) const
{
	if (APlayerController* PC = BoundPC.Get())
	{
		PC->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}
	return false;
}

FString UPMSpellcasterComponent::GetCasterName() const
{
	// Single player: the default player name is the PC's hostname, which reads badly in the feed.
	if (GetNetMode() == NM_Standalone)
	{
		return TEXT("You");
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
	const FString Name = PS ? PS->GetPlayerName() : FString();
	return Name.IsEmpty() ? TEXT("Mage") : Name;
}

void UPMSpellcasterComponent::Toast(const FString& Message) const
{
	if (UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>())
	{
		Spells->Toast(Message);
	}
}

// --- Spellbook -----------------------------------------------------------------------------------

void UPMSpellcasterComponent::OnTabPressed()
{
	if (bSpellbookOpen)
	{
		CloseSpellbook();
	}
	else
	{
		OpenSpellbook();
	}
}

FVector UPMSpellcasterComponent::FindTargetPoint() const
{
	FVector ViewLoc;
	FRotator ViewRot;
	if (!GetView(ViewLoc, ViewRot))
	{
		return GetOwner()->GetActorLocation();
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PMTarget), false, GetOwner());
	FHitResult Hit;
	const FVector End = ViewLoc + ViewRot.Vector() * MaxTargetDistance;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, End, ECC_Visibility, Params))
	{
		return Hit.ImpactPoint;
	}

	// Aiming at the sky: drop the point to the ground below the far end.
	FHitResult Down;
	if (GetWorld()->LineTraceSingleByChannel(Down, End, End - FVector(0.f, 0.f, 20000.f), ECC_Visibility, Params))
	{
		return Down.ImpactPoint;
	}
	return End;
}

void UPMSpellcasterComponent::ShowTargetMarker(bool bShow)
{
	if (!TargetMarker)
	{
		return;
	}
	if (bShow)
	{
		TargetMarker->SetWorldLocation(TargetPoint + FVector(0.f, 0.f, 2.f));
	}
	TargetMarker->SetHiddenInGame(!bShow);
}

void UPMSpellcasterComponent::OpenSpellbook()
{
	APlayerController* PC = BoundPC.Get();
	if (bSpellbookOpen || !PC || !Hud)
	{
		return;
	}
	bSpellbookOpen = true;
	HistoryIndex = -1;

	// The spawn point is wherever the crosshair was when Tab was pressed (GDD 2.3).
	TargetPoint = FindTargetPoint();
	ShowTargetMarker(true);

	Hud->ShowSpellbook(true);

	// Arms keep their grip while typing; WASD now types letters.
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Hud->GetPromptFocusWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(false);
}

void UPMSpellcasterComponent::CloseSpellbook()
{
	if (!bSpellbookOpen)
	{
		return;
	}
	bSpellbookOpen = false;
	ShowTargetMarker(false);

	if (Hud)
	{
		Hud->ShowSpellbook(false);
	}
	if (APlayerController* PC = BoundPC.Get())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

FPMParsedPrompt UPMSpellcasterComponent::PreviewPrompt(const FString& Text) const
{
	const UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>();
	static const TArray<FString> NoBans;
	return FPMPromptParser::Parse(Text, Spells ? Spells->GetBannedWords() : NoBans);
}

float UPMSpellcasterComponent::GetCurrentCost(const FPMParsedPrompt& Prompt) const
{
	const UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>();
	return Spells ? Spells->GetCurrentCost(Prompt) : Prompt.InkCost;
}

float UPMSpellcasterComponent::GetCooldownRemaining() const
{
	return FMath::Max(0.f, static_cast<float>(LastCastTime + CastCooldown - GetWorld()->GetTimeSeconds()));
}

bool UPMSpellcasterComponent::CastPrompt(const FString& Text)
{
	UPMSpellSubsystem* Spells = GetWorld()->GetSubsystem<UPMSpellSubsystem>();
	const FPMParsedPrompt Prompt = PreviewPrompt(Text);
	if (!Spells || Prompt.IsEmpty())
	{
		return false;
	}

	if (Prompt.BannedWords.Num() > 0)
	{
		Toast(FString::Printf(TEXT("Policy violation: \"%s\" is banned in this level."), *FString::Join(Prompt.BannedWords, TEXT("\", \""))));
		return false;
	}
	if (!Prompt.Noun)
	{
		Toast(TEXT("The Tome needs a thing to summon. Try a noun, like \"barrel\"."));
		return false;
	}

	const float Cooldown = GetCooldownRemaining();
	if (Cooldown > 0.f)
	{
		Toast(FString::Printf(TEXT("Your quill is still drying (%.1fs)."), Cooldown));
		return false;
	}

	const float Cost = Spells->GetCurrentCost(Prompt);
	if (!Spells->CanAfford(Cost))
	{
		Toast(FString::Printf(TEXT("Not enough Ink: %s needs %s, the team has %s."),
			*Prompt.DisplayName, *FPMPromptParser::FormatInk(Cost), *FPMPromptParser::FormatInk(Spells->GetInk())));
		return false;
	}

	const int32 Spawned = Spells->CastPrompt(Prompt, TargetPoint, GetCasterName(), GetOwner());
	if (Spawned <= 0)
	{
		return false;
	}

	LastCastTime = GetWorld()->GetTimeSeconds();
	History.Remove(Text);
	History.Insert(Text, 0);
	if (History.Num() > MaxHistory)
	{
		History.SetNum(MaxHistory);
	}

	if (Prompt.ExtraModifierWords.Num() > 0)
	{
		Toast(FString::Printf(TEXT("Max three modifiers - ignored \"%s\"."), *FString::Join(Prompt.ExtraModifierWords, TEXT("\", \""))));
	}
	if (Prompt.IgnoredWords.Num() > 0)
	{
		Toast(FString::Printf(TEXT("The Tome didn't understand \"%s\" and ignored it."), *FString::Join(Prompt.IgnoredWords, TEXT("\", \""))));
	}

	CloseSpellbook();
	return true;
}

FString UPMSpellcasterComponent::RecallPrompt(int32 Direction)
{
	if (History.Num() == 0)
	{
		return FString();
	}
	HistoryIndex = FMath::Clamp(HistoryIndex + Direction, -1, History.Num() - 1);
	return HistoryIndex >= 0 ? History[HistoryIndex] : FString();
}
