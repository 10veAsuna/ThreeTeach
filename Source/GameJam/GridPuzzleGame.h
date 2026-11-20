#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "GridPuzzleRules.h"
#include "GridPuzzleGame.generated.h"

class ACameraActor;
class AGridTile;
class AGridPuzzleCharacter;
class UMaterialInterface;
class SWidget;

enum class EGridScreen : uint8 { Menu, Selection, Controls, MechanicIntro, Playing, Paused, Transition, Complete, Ending };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGridTileEvent, FTileData, Tile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGridCharacterEvent, EPlayerRole, Role, FIntPoint, Position);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGridLevelEvent);

UCLASS()
class GAMEJAM_API UGridPuzzleProgress : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 CompletedMainLevels = 0;
	UPROPERTY() TArray<int32> EarnedEchoes;
	UPROPERTY() TArray<int32> SeenMechanicIntros;
};

UCLASS(Blueprintable)
class GAMEJAM_API AGridManager : public AActor
{
	GENERATED_BODY()

public:
	AGridManager();
	void Initialize(APlayerController& Controller);
	void Start();
	void ShowControls();
	void ShowMenu();
	void ShowSelection();
	void DismissMechanicIntro();
	void SelectLevel(int32 Index);
	void Advance();
	void TogglePause();
	void Exit();

	UFUNCTION(BlueprintCallable, Category="Puzzle") static TArray<UGridLevelDataAsset*> CreateDefaultLevels();
	static TArray<UGridLevelDataAsset*> CreatePrototypeLevels();

	UFUNCTION(BlueprintCallable, Category="Puzzle") void Move(FIntPoint Direction);
	UFUNCTION(BlueprintCallable, Category="Puzzle") void SwitchCharacter();
	UFUNCTION(BlueprintCallable, Category="Puzzle") void UndoMove();
	UFUNCTION(BlueprintCallable, Category="Puzzle") void UseAction();
	UFUNCTION(BlueprintCallable, Category="Puzzle") void ResetLevel();

	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridTileEvent OnFootprintChanged;
	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridCharacterEvent OnCharacterMoved;
	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridTileEvent OnSwitchActivated;
	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridTileEvent OnTileCollapsed;
	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridTileEvent OnBridgeChanged;
	UPROPERTY(BlueprintAssignable, Category="Puzzle") FGridLevelEvent OnLevelCompleted;

	UPROPERTY(EditDefaultsOnly, Category="Level") TArray<TObjectPtr<UGridLevelDataAsset>> Levels;
	UPROPERTY(EditDefaultsOnly, Category="Look") TSubclassOf<AGridTile> TileClass;
	UPROPERTY(EditDefaultsOnly, Category="Look") TSubclassOf<AGridPuzzleCharacter> CharacterClass;
	UPROPERTY(EditDefaultsOnly, Category="Look") TObjectPtr<UMaterialInterface> PaletteMaterial;
	UPROPERTY(EditDefaultsOnly, Category="Sound") bool bPlaySound = true;

	EGridScreen GetScreen() const { return Screen; }
	int32 GetLevelNumber() const { return LevelIndex + 1; }
	int32 GetLevelCount() const { return Levels.Num(); }
	int32 GetCampaignLevelCount() const { return CampaignLevelCount; }
	int32 GetMainLevelCount() const { return MainLevelCount; }
	int32 GetResumeIndex() const { return FMath::Min(CompletedMainLevels, FMath::Max(0, MainLevelCount - 1)); }
	bool HasProgress() const { return CompletedMainLevels > 0; }
	bool CanSelectLevel(int32 Index) const;
	bool IsChallenge() const { return LevelIndex >= MainLevelCount && !IsPrototype(); }
	bool IsPrototype() const { return LevelIndex >= CampaignLevelCount; }
	bool HasLevelAction() const { return Levels.IsValidIndex(LevelIndex) && Levels[LevelIndex] && Levels[LevelIndex]->Prototype != EGridPrototype::None; }
	bool AreChallengesUnlocked() const { return CompletedMainLevels >= MainLevelCount; }
	int32 GetMoveCount() const { return Rules.GetMoveCount(); }
	bool IsFinalLevel() const { return GetLevelNumber() == MainLevelCount; }
	bool IsFinished(EPlayerRole Traveler) const { return Rules.IsFinished(Traveler); }
	EPlayerRole GetActiveRole() const { return ActiveRole; }
	bool HasUndoMove() const { return !UndoHistory.IsEmpty(); }
	FText GetLevelTitle() const;
	FText GetLevelTitle(int32 Index) const;
	FText GetLevelHint() const;
	FText GetFeedback() const { return Feedback; }
	FText GetRecordingStatus() const;
	bool GetEndingReady() const { return bEndingReady; }
	bool HasCurrentLevelEcho() const { return EarnedEchoes.Contains(LevelIndex); }
	int32 GetEarnedEchoCount() const { return EarnedEchoes.Num(); }
	int32 GetEchoTargetCount() const;
	static bool GetEchoLocationForLevel(int32 LevelIndex, FIntPoint& OutPosition);
	static bool LevelHasMechanicIntro(int32 LevelIndex);
	FText GetLevelEchoName(int32 Index) const;
	FText GetMechanicIntroTitle() const;
	FText GetMechanicIntroBody() const;
	FText GetMechanicIntroExample() const;

private:
	struct FUndoStep
	{
		FGridPuzzleRules PreviousRules;
		EPlayerRole Mover;
		TArray<FIntPoint> ChangedTiles;
	};

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool LoadLevel(int32 Index);
	void SetScreen(EGridScreen NewScreen);
	void UpdateControl();
	void UpdateBridgeDisplays();
	void PresentMove(EPlayerRole Mover, const FGridMove& MoveResult);
	void ApplyTileUpdates(const TArray<FIntPoint>& Positions, bool bPulse, bool& bCollapsed, bool& bBridgeChanged);
	void CheckCompletion();
	void ClearActionPreview();
	void MarkActionTile(FIntPoint Position, FText Label, FColor Color);
	void PlayReplayStep();
	void AnimateTurnedDeck(const TArray<FIntPoint>& ChangedTiles, bool bReverse);
	FText GetClosedBridgeFeedback(FIntPoint Position) const;
	void SaveLevelProgress();
	void PersistProgress();
	bool TryCollectEchoAt(FIntPoint Position);
	void BuildEnvironment();
	void PositionCamera(bool bMenu, bool bOutro = false);
	FVector ToWorld(FIntPoint Coordinate) const;
	void BeginOutro();
	void PulseOutro();
	void FinishOutro();
	void SetFeedback(FText Message);

	UPROPERTY() TObjectPtr<APlayerController> Player;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<AActor> Environment;
	UPROPERTY() TObjectPtr<AActor> Outro;
	UPROPERTY() TMap<FIntPoint, TObjectPtr<AGridTile>> TileActors;
	UPROPERTY() TArray<TObjectPtr<AGridPuzzleCharacter>> Characters;

	TSharedPtr<SWidget> ViewportUI;
	FGridPuzzleRules Rules;
	TArray<FUndoStep> UndoHistory;
	TArray<FIntPoint> PreviewMarkers;
	TArray<FGridMove> ReplaySteps;
	TArray<FGridPuzzleRules> ReplayStates;
	FText Feedback;
	EGridScreen Screen = EGridScreen::Menu;
	EPlayerRole ActiveRole = EPlayerRole::PlayerA;
	int32 LevelIndex = 0;
	int32 MainLevelCount = 0;
	int32 CampaignLevelCount = 0;
	int32 ReplayIndex = 0;
	int32 CompletedMainLevels = 0;
	TSet<int32> EarnedEchoes;
	TSet<int32> SeenMechanicIntros;
	float UnlockInputAt = 0.f;
	float OutroStartedAt = 0.f;
	FTimerHandle ScreenTimer;
	FTimerHandle ReplayTimer;
	FTimerHandle FeedbackTimer;
	FTimerHandle OutroTimer;
	FTimerHandle OutroPulseTimer;
	bool bEndingReady = false;
	bool bApplyingMove = false;
	bool bControlsFromPause = false;
	bool bActionPreview = false;
	bool bMechanicIntroPending = false;
};

UCLASS()
class GAMEJAM_API AGridPuzzlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<AGridManager> Manager;

protected:
	virtual void SetupInputComponent() override;

private:
	void MoveUp();
	void MoveDown();
	void MoveLeft();
	void MoveRight();
	void Switch();
	void Interact();
	void Undo();
	void Reset();
	void PauseOrBack();
	void Confirm();
};

UCLASS(Blueprintable)
class GAMEJAM_API AGridPuzzleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGridPuzzleGameMode();

	UPROPERTY(EditDefaultsOnly, Category="Puzzle")
	TSubclassOf<AGridManager> ManagerClass;

protected:
	virtual void StartPlay() override;
};
