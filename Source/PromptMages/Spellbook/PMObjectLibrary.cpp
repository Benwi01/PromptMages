// Prompt-Mages: the authored vocabulary - nouns, modifiers, size and quantity words.

#include "PMObjectLibrary.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

const FName FPMObjectLibrary::ColorParameterName(TEXT("Color"));

namespace PMLib
{
	FPMShapePart Part(EPMShape Shape, const FVector& Size, const FLinearColor& Color, const FVector& Offset = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FPMShapePart P;
		P.Shape = Shape;
		P.Size = Size;
		P.Color = Color;
		P.Offset = Offset;
		P.Rotation = Rotation;
		return P;
	}

	FPMTraits Traits(float WeightKg, float Buoyancy, float Flammability, float Elasticity, float Friction = 0.7f, bool bMetal = false)
	{
		FPMTraits T;
		T.WeightKg = WeightKg;
		T.Buoyancy = Buoyancy;
		T.Flammability = Flammability;
		T.Elasticity = Elasticity;
		T.Friction = Friction;
		T.bMetal = bMetal;
		return T;
	}

	const FLinearColor Wood(0.35f, 0.2f, 0.08f);
	const FLinearColor LightWood(0.62f, 0.42f, 0.22f);
	const FLinearColor Iron(0.08f, 0.08f, 0.09f);
	const FLinearColor Steel(0.25f, 0.25f, 0.27f);
}

const FPMObjectLibrary& FPMObjectLibrary::Get()
{
	static const FPMObjectLibrary Instance;
	return Instance;
}

FPMObjectLibrary::FPMObjectLibrary()
{
	BuildNouns();
	BuildModifiers();
	BuildWords();
}

void FPMObjectLibrary::BuildNouns()
{
	using namespace PMLib;

	auto Add = [this](FName Id, const FString& Display, TArray<FString> Words, float Ink, const FPMTraits& T, TArray<FPMShapePart> Parts)
	{
		FPMNounDef& N = Nouns.AddDefaulted_GetRef();
		N.Id = Id;
		N.DisplayName = Display;
		N.Words = MoveTemp(Words);
		N.InkCost = Ink;
		N.Traits = T;
		N.Parts = MoveTemp(Parts);
	};

	Add("Barrel", TEXT("Barrel"), { TEXT("barrel"), TEXT("keg"), TEXT("cask") }, 3.f, Traits(30.f, 0.8f, 2.f, 0.25f), {
		Part(EPMShape::Cylinder, FVector(60, 60, 90), Wood),
		Part(EPMShape::Cylinder, FVector(63, 63, 6), Iron, FVector(0, 0, 32)),
		Part(EPMShape::Cylinder, FVector(63, 63, 6), Iron, FVector(0, 0, -32)),
	});

	// 500 kg on purpose: GDD 3.3 prices the anvil as the go-to heavy hitter.
	Add("Anvil", TEXT("Anvil"), { TEXT("anvil") }, 5.f, Traits(500.f, -1.f, 0.f, 0.05f, 0.6f, true), {
		Part(EPMShape::Cube, FVector(70, 30, 18), Steel),
		Part(EPMShape::Cube, FVector(36, 26, 30), Steel, FVector(0, 0, -24)),
		Part(EPMShape::Cube, FVector(56, 36, 10), Steel, FVector(0, 0, -42)),
		Part(EPMShape::Cone, FVector(26, 26, 36), Steel, FVector(52, 0, 0), FRotator(-90, 0, 0)),
	});

	Add("Crate", TEXT("Crate"), { TEXT("crate"), TEXT("box") }, 2.f, Traits(25.f, 0.6f, 2.f, 0.2f), {
		Part(EPMShape::Cube, FVector(80, 80, 80), LightWood),
		Part(EPMShape::Cube, FVector(82, 82, 8), Wood, FVector(0, 0, 30)),
		Part(EPMShape::Cube, FVector(82, 82, 8), Wood, FVector(0, 0, -30)),
	});

	// The prize turnip from Level 1 is 600 kg, so a summoned one is too (GDD 4.1 disaster step 1).
	Add("Turnip", TEXT("Turnip"), { TEXT("turnip") }, 4.f, Traits(600.f, 0.3f, 1.f, 0.3f), {
		Part(EPMShape::Sphere, FVector(150, 150, 140), FLinearColor(0.9f, 0.86f, 0.82f)),
		Part(EPMShape::Sphere, FVector(146, 146, 80), FLinearColor(0.45f, 0.08f, 0.35f), FVector(0, 0, 28)),
		Part(EPMShape::Cone, FVector(40, 40, 70), FLinearColor(0.15f, 0.5f, 0.1f), FVector(0, 0, 95)),
	});

	Add("Plank", TEXT("Plank"), { TEXT("plank"), TEXT("board"), TEXT("ramp") }, 2.f, Traits(12.f, 0.7f, 2.f, 0.2f), {
		Part(EPMShape::Cube, FVector(300, 40, 6), LightWood),
	});

	Add("Ball", TEXT("Ball"), { TEXT("ball") }, 1.f, Traits(1.f, 0.9f, 1.f, 0.75f), {
		Part(EPMShape::Sphere, FVector(40, 40, 40), FLinearColor(0.8f, 0.08f, 0.05f)),
	});

	Add("Bucket", TEXT("Bucket"), { TEXT("bucket"), TEXT("pail") }, 1.f, Traits(2.f, 0.5f, 1.f, 0.2f), {
		Part(EPMShape::Cylinder, FVector(40, 40, 40), FLinearColor(0.45f, 0.32f, 0.18f)),
		Part(EPMShape::Cylinder, FVector(42, 42, 4), Iron, FVector(0, 0, 15)),
	});

	Add("Chair", TEXT("Chair"), { TEXT("chair"), TEXT("stool"), TEXT("seat") }, 2.f, Traits(7.f, 0.7f, 2.f, 0.2f), {
		Part(EPMShape::Cube, FVector(50, 50, 6), LightWood),
		Part(EPMShape::Cube, FVector(6, 50, 55), LightWood, FVector(-22, 0, 30)),
		Part(EPMShape::Cylinder, FVector(5, 5, 45), Wood, FVector(20, 20, -25)),
		Part(EPMShape::Cylinder, FVector(5, 5, 45), Wood, FVector(20, -20, -25)),
		Part(EPMShape::Cylinder, FVector(5, 5, 45), Wood, FVector(-20, 20, -25)),
		Part(EPMShape::Cylinder, FVector(5, 5, 45), Wood, FVector(-20, -20, -25)),
	});

	Add("Cow", TEXT("Cow"), { TEXT("cow"), TEXT("ox"), TEXT("cattle") }, 6.f, Traits(650.f, 0.9f, 1.f, 0.3f), {
		Part(EPMShape::Cube, FVector(160, 70, 80), FLinearColor(0.92f, 0.92f, 0.9f)),
		Part(EPMShape::Cube, FVector(50, 72, 40), FLinearColor(0.03f, 0.03f, 0.03f), FVector(25, 0, 12)),
		Part(EPMShape::Cube, FVector(45, 40, 45), FLinearColor(0.92f, 0.92f, 0.9f), FVector(95, 0, 30)),
		Part(EPMShape::Cube, FVector(12, 30, 18), FLinearColor(0.9f, 0.55f, 0.6f), FVector(120, 0, 22)),
		Part(EPMShape::Cylinder, FVector(18, 18, 70), FLinearColor(0.2f, 0.15f, 0.1f), FVector(55, 22, -70)),
		Part(EPMShape::Cylinder, FVector(18, 18, 70), FLinearColor(0.2f, 0.15f, 0.1f), FVector(55, -22, -70)),
		Part(EPMShape::Cylinder, FVector(18, 18, 70), FLinearColor(0.2f, 0.15f, 0.1f), FVector(-55, 22, -70)),
		Part(EPMShape::Cylinder, FVector(18, 18, 70), FLinearColor(0.2f, 0.15f, 0.1f), FVector(-55, -22, -70)),
	});

	Add("Haybale", TEXT("Hay Bale"), { TEXT("haybale"), TEXT("hay"), TEXT("bale") }, 2.f, Traits(25.f, 0.8f, 3.f, 0.15f), {
		Part(EPMShape::Cube, FVector(110, 60, 50), FLinearColor(0.85f, 0.68f, 0.22f)),
		Part(EPMShape::Cube, FVector(4, 62, 52), FLinearColor(0.45f, 0.3f, 0.1f), FVector(30, 0, 0)),
		Part(EPMShape::Cube, FVector(4, 62, 52), FLinearColor(0.45f, 0.3f, 0.1f), FVector(-30, 0, 0)),
	});

	MysteryCrate.Id = "MysteryCrate";
	MysteryCrate.DisplayName = TEXT("Mystery Crate");
	MysteryCrate.InkCost = 4.f;
	MysteryCrate.Traits = Traits(15.f, 0.5f, 1.f, 0.3f);
	MysteryCrate.Parts = {
		Part(EPMShape::Cube, FVector(70, 70, 70), FLinearColor(0.7f, 0.08f, 0.75f)),
		Part(EPMShape::Cube, FVector(72, 20, 72), FLinearColor(1.f, 0.85f, 0.1f)),
		Part(EPMShape::Cube, FVector(20, 72, 72), FLinearColor(1.f, 0.85f, 0.1f)),
	};
}

void FPMObjectLibrary::BuildModifiers()
{
	auto Add = [this](FName Id, const FString& Display, TArray<FString> Words, float Ink, TFunction<void(FPMTraits&)> Apply)
	{
		FPMModifierDef& M = Modifiers.AddDefaulted_GetRef();
		M.Id = Id;
		M.DisplayName = Display;
		M.Words = MoveTemp(Words);
		M.InkCost = Ink;
		M.Apply = MoveTemp(Apply);
	};

	// Values from the GDD 3.3 modifier table.
	Add("Bouncy", TEXT("Bouncy"), { TEXT("bouncy"), TEXT("bouncing"), TEXT("springy"), TEXT("rubber") }, 2.f,
		[](FPMTraits& T) { T.Elasticity = 1.05f; });
	Add("Greasy", TEXT("Greasy"), { TEXT("greasy"), TEXT("oily"), TEXT("slippery"), TEXT("slick") }, 2.f,
		[](FPMTraits& T) { T.Friction = 0.02f; T.Flammability += 1.f; T.bGreasy = true; });
	Add("Floating", TEXT("Floating"), { TEXT("floating"), TEXT("floaty"), TEXT("buoyant"), TEXT("levitating"), TEXT("flying") }, 3.f,
		[](FPMTraits& T) { T.Buoyancy += 2.f; });
	Add("Heavy", TEXT("Heavy"), { TEXT("heavy"), TEXT("heavier"), TEXT("weighty"), TEXT("dense") }, 1.f,
		[](FPMTraits& T) { T.WeightKg *= 5.f; });
	Add("Light", TEXT("Light"), { TEXT("light"), TEXT("lightweight"), TEXT("feathery"), TEXT("hollow") }, 1.f,
		[](FPMTraits& T) { T.WeightKg *= 0.2f; });
}

void FPMObjectLibrary::BuildWords()
{
	auto AddSize = [this](const FString& Display, TArray<FString> Words, float Scale, float Ink)
	{
		FPMSizeDef& S = Sizes.AddDefaulted_GetRef();
		S.DisplayName = Display;
		S.Words = MoveTemp(Words);
		S.Scale = Scale;
		S.InkMultiplier = Ink;
	};
	AddSize(TEXT("Tiny"), { TEXT("tiny"), TEXT("mini"), TEXT("itsy"), TEXT("teeny") }, 0.25f, 0.5f);
	AddSize(TEXT("Small"), { TEXT("small"), TEXT("little") }, 0.5f, 0.75f);
	AddSize(TEXT("Large"), { TEXT("large"), TEXT("big") }, 1.5f, 1.5f);
	AddSize(TEXT("Huge"), { TEXT("huge"), TEXT("giant"), TEXT("enormous") }, 3.f, 2.f);
	AddSize(TEXT("Colossal"), { TEXT("colossal"), TEXT("gigantic"), TEXT("massive") }, 8.f, 5.f);

	const TCHAR* Numbers[] = { TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"), TEXT("five"), TEXT("six"),
		TEXT("seven"), TEXT("eight"), TEXT("nine"), TEXT("ten"), TEXT("eleven"), TEXT("twelve") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Numbers); ++i)
	{
		Quantities.Add(Numbers[i], i + 1);
	}
	Quantities.Add(TEXT("dozen"), 12);
	Quantities.Add(TEXT("pair"), 2);
	Quantities.Add(TEXT("couple"), 2);
	Quantities.Add(TEXT("few"), 3);
	Quantities.Add(TEXT("several"), 4);

	for (const TCHAR* W : { TEXT("a"), TEXT("an"), TEXT("the"), TEXT("please"), TEXT("some"), TEXT("ol"), TEXT("of"),
		TEXT("i"), TEXT("want"), TEXT("need"), TEXT("give"), TEXT("me"), TEXT("us"), TEXT("make"), TEXT("summon"), TEXT("spawn"),
		TEXT("cast"), TEXT("conjure"), TEXT("with"), TEXT("and"), TEXT("very"), TEXT("really"), TEXT("super"), TEXT("just"),
		TEXT("that"), TEXT("is"), TEXT("thing"), TEXT("kinda"), TEXT("quite") })
	{
		Filler.Add(W);
	}

	for (const FPMNounDef& N : Nouns) { Vocabulary.Append(N.Words); }
	for (const FPMModifierDef& M : Modifiers) { Vocabulary.Append(M.Words); }
	for (const FPMSizeDef& S : Sizes) { Vocabulary.Append(S.Words); }
	Vocabulary.Sort([](const FString& A, const FString& B) { return A.Len() != B.Len() ? A.Len() < B.Len() : A < B; });
}

const FPMNounDef* FPMObjectLibrary::FindNoun(const FString& Word) const
{
	return Nouns.FindByPredicate([&Word](const FPMNounDef& N) { return N.Words.Contains(Word); });
}

const FPMModifierDef* FPMObjectLibrary::FindModifier(const FString& Word) const
{
	return Modifiers.FindByPredicate([&Word](const FPMModifierDef& M) { return M.Words.Contains(Word); });
}

const FPMSizeDef* FPMObjectLibrary::FindSize(const FString& Word) const
{
	return Sizes.FindByPredicate([&Word](const FPMSizeDef& S) { return S.Words.Contains(Word); });
}

int32 FPMObjectLibrary::FindQuantity(const FString& Word) const
{
	if (const int32* Q = Quantities.Find(Word))
	{
		return *Q;
	}
	if (Word.IsNumeric())
	{
		return FMath::Max(1, FCString::Atoi(*Word));
	}
	return 0;
}

bool FPMObjectLibrary::IsFiller(const FString& Word) const
{
	return Filler.Contains(Word);
}

const FPMNounDef& FPMObjectLibrary::GetRandomNoun() const
{
	return Nouns[FMath::RandRange(0, Nouns.Num() - 1)];
}

UStaticMesh* FPMObjectLibrary::GetShapeMesh(EPMShape Shape)
{
	const TCHAR* Path = TEXT("/Engine/BasicShapes/Cube.Cube");
	switch (Shape)
	{
	case EPMShape::Sphere:   Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); break;
	case EPMShape::Cylinder: Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); break;
	case EPMShape::Cone:     Path = TEXT("/Engine/BasicShapes/Cone.Cone"); break;
	default: break;
	}
	return LoadObject<UStaticMesh>(nullptr, Path);
}

UMaterialInterface* FPMObjectLibrary::GetShapeMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}
