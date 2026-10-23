// Prompt-Mages: the authored vocabulary - nouns, modifiers, size and quantity words.

#pragma once

#include "CoreMinimal.h"
#include "PMTypes.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * The hand-authored word library (GDD 3.1). Every spawned object comes from here.
 * Slice 1 ships 10 nouns + the Mystery Crate, 5 modifiers and 5 size words.
 */
class PROMPTMAGES_API FPMObjectLibrary
{
public:
	static const FPMObjectLibrary& Get();

	/** Exact word lookups. Words are lowercase. */
	const FPMNounDef* FindNoun(const FString& Word) const;
	const FPMModifierDef* FindModifier(const FString& Word) const;
	const FPMSizeDef* FindSize(const FString& Word) const;
	/** Returns 0 when the word is not a quantity. */
	int32 FindQuantity(const FString& Word) const;
	bool IsFiller(const FString& Word) const;

	const FPMNounDef& GetMysteryCrate() const { return MysteryCrate; }
	const FPMNounDef& GetRandomNoun() const;
	const TArray<FPMNounDef>& GetNouns() const { return Nouns; }

	/** Every word the Spellbook can autocomplete, shortest first. */
	const TArray<FString>& GetVocabulary() const { return Vocabulary; }

	/** Engine basic shape assets used for stand-in geometry. */
	static UStaticMesh* GetShapeMesh(EPMShape Shape);
	static UMaterialInterface* GetShapeMaterial();
	static const FName ColorParameterName;

private:
	FPMObjectLibrary();
	void BuildNouns();
	void BuildModifiers();
	void BuildWords();

	TArray<FPMNounDef> Nouns;
	FPMNounDef MysteryCrate;
	TArray<FPMModifierDef> Modifiers;
	TArray<FPMSizeDef> Sizes;
	TMap<FString, int32> Quantities;
	TSet<FString> Filler;
	TArray<FString> Vocabulary;
};
