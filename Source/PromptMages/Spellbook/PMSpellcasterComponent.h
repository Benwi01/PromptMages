// Prompt-Mages: lets a player open the Spellbook and cast prompts.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PMPromptParser.h"
#include "PMSpellcasterComponent.generated.h"

class APlayerController;
class UInputComponent;
class UPMHudWidget;
class UStaticMeshComponent;

/**
 * Add to a player pawn. Handles:
 *  - Tab: open the Spellbook (GDD 2.3); the spawn point is wherever the crosshair was.
 *  - Casting with the per-player Quill Cooldown and the shared Ink pool.
 * Grabbing lives in UPMActiveRagdollComponent (the arms).
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

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Casting")
	float CastCooldown = 3.f;

	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Casting")
	float MaxTargetDistance = 5000.f;

	/** Shown in the Spellbook's physics preview: heavier objects need a friend (GDD 2.1). */
	UPROPERTY(EditAnywhere, Category = "Prompt Mages|Casting")
	float MaxSoloLiftKg = 40.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	APlayerController* GetLocalPC() const;
	bool GetView(FVector& OutLocation, FRotator& OutRotation) const;
	void TryInitLocalPlayer();

	void OnTabPressed();

	FVector FindTargetPoint() const;
	void ShowTargetMarker(bool bShow);
	void Toast(const FString& Message) const;

	UPROPERTY()
	TObjectPtr<UInputComponent> CasterInput;

	UPROPERTY()
	TObjectPtr<UPMHudWidget> Hud;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> TargetMarker;

	TWeakObjectPtr<APlayerController> BoundPC;

	bool bSpellbookOpen = false;
	FVector TargetPoint = FVector::ZeroVector;
	double LastCastTime = -1000.0;

	TArray<FString> History;
	int32 HistoryIndex = -1;

	static constexpr int32 MaxHistory = 5;
};
