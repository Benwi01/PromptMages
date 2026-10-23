// Prompt-Mages: turns typed text into one noun + up to three modifiers (GDD 3.1).

#pragma once

#include "CoreMinimal.h"
#include "PMTypes.h"

/** The result of parsing one prompt. Pointers refer to the static FPMObjectLibrary. */
struct PROMPTMAGES_API FPMParsedPrompt
{
	FString RawText;

	/** The base object. For unknown nouns this is the Mystery Crate. Null when nothing can be cast. */
	const FPMNounDef* Noun = nullptr;
	bool bMystery = false;
	/** The unknown word that became a Mystery Crate. */
	FString MisheardWord;

	/** Applied modifiers, in typed order (max 3). */
	TArray<const FPMModifierDef*> Modifiers;
	/** Modifier words past the third, ignored. */
	TArray<FString> ExtraModifierWords;
	/** Words the engine did not understand and skipped. */
	TArray<FString> IgnoredWords;
	/** Words on the level's banned list. Any banned word blocks the cast. */
	TArray<FString> BannedWords;

	const FPMSizeDef* Size = nullptr;
	float SizeScale = 1.f;
	int32 Quantity = 1;

	/** Final trait values after modifiers and size. */
	FPMTraits Traits;

	/** Ink price before surge pricing. */
	float InkCost = 0.f;

	/** Canonical form used for surge pricing ("identical prompt"). */
	FString Key;
	/** e.g. "Huge Bouncy Barrel". */
	FString DisplayName;

	bool IsEmpty() const { return RawText.TrimStartAndEnd().IsEmpty(); }
	bool CanCast() const { return Noun != nullptr && BannedWords.Num() == 0; }
};

class PROMPTMAGES_API FPMPromptParser
{
public:
	static constexpr int32 MaxModifiers = 3;
	static constexpr int32 MaxQuantity = 12;
	/** Plural nouns with no number spawn this many ("barrels" = 3 barrels). */
	static constexpr int32 DefaultPluralQuantity = 3;

	static FPMParsedPrompt Parse(const FString& Text, const TArray<FString>& BannedWords);

	/** Autocomplete for the word being typed. Returns the full suggested word, or empty. */
	static FString Suggest(const FString& Text);

	/** Replaces the word being typed with Suggestion and adds a trailing space. */
	static FString AcceptSuggestion(const FString& Text, const FString& Suggestion);

	/** Short physics preview, e.g. "30 kg | bounce 1.05 | floats". */
	static FString DescribeTraits(const FPMTraits& Traits, float MaxSoloLiftKg);

	static FString FormatInk(float Ink);
	static FString FormatWeight(float Kg);

	static TArray<FString> Tokenize(const FString& Text);
};
