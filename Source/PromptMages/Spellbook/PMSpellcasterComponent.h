// Prompt-Mages: lets a player open the Spellbook, cast prompts and grab objects.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PMPromptParser.h"
#include "PMSpellcasterComponent.generated.h"

class APlayerController;
class UInputComponent;
class UPhysicsHandleComponent;
class UPMHudWidget;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * Add to a player pawn. Handles:
 *  - Tab: open the Spellbook (GDD 2.3); the spawn point is wherever the crosshair was.
 *  - Casting with the per-player Quill Cooldown and the shared Ink pool.
 *  - Left mouse: a simple one-hand grab with a grip stamina pool and a 40 kg solo-lift limit.
 *    Mouse wheel changes reach. This is the stand-in for the active-ragdoll arms in Slice 2.
 */
UCLASS(ClassGroup = (PromptMages), meta = (BlueprintSpawnableComponent))
class PROMPTMAGES_API UPMSpellcasterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPMSpellcasterComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Spellbook ---
	void OpenSpellbook();
	void CloseSpellbook();
	bool IsSpellbookOpen() const { return bSpellbookOpen; }

	/** Parses Text against the level's banned words. */
	FPMParsedPrompt PreviewPrompt(const FString& Text) const;
	/** Current price of a parsed prompt, including surge pricing. */
	float GetCurrentCost(const FPMParsedPrompt& Prompt) const;

	/** Casts Text at the locked target point. Returns true if anything spawned. Closes the book on success. */
	bool CastPrompt(const FString& Text);

	/** Up/Down arrow history. Direction +1 = older. */
	FString RecallPrompt(int32 Direction);

	float GetCooldownRemaining() const;
	FString GetCasterName() const;

	// --- Grab ---
	bool IsGrabbing() const;
	float GetGripStamina() const { return GripStamina; }

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Casting")
	float CastCooldown = 3.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Casting")
	float MaxTargetDistance = 5000.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Grab")
	float GrabReach = 350.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Grab")
	float MaxSoloLiftKg = 40.f;

	/** Seconds of grip at max load (GDD 2.1). Lighter loads last longer. */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Grab")
	float GripSecondsAtMaxLoad = 8.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	APlayerController* GetLocalPC() const;
	bool GetView(FVector& OutLocation, FRotator& OutRotation) const;
	void TryInitLocalPlayer();

	void OnTabPressed();
	void OnGrabPressed();
	void OnGrabReleased();
	void OnReachIn();
	void OnReachOut();

	FVector FindTargetPoint() const;
	void ShowTargetMarker(bool bShow);
	void Release(bool bSlipped);
	void Toast(const FString& Message) const;

	UPROPERTY()
	TObjectPtr<UInputComponent> CasterInput;

	UPROPERTY()
	TObjectPtr<UPMHudWidget> Hud;

	UPROPERTY()
	TObjectPtr<UPhysicsHandleComponent> Handle;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> TargetMarker;

	TWeakObjectPtr<APlayerController> BoundPC;

	bool bSpellbookOpen = false;
	FVector TargetPoint = FVector::ZeroVector;
	double LastCastTime = -1000.0;

	TArray<FString> History;
	int32 HistoryIndex = -1;

	float HoldDistance = 200.f;
	float HeldMassKg = 0.f;
	/** 0..1 */
	float GripStamina = 1.f;

	static constexpr int32 MaxHistory = 5;
};
