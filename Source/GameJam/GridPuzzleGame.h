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
class UGridMusicComponent;

// 菜单、引导、游玩、暂停、结算与结局共用一个界面状态，由 SetScreen 统一切换。
enum class EGridScreen : uint8 { Menu, Selection, Achievements, Controls, MechanicIntro, Playing, Paused, Transition, Complete, Ending };

// 蓝图可绑定这些事件，在 C++ 完成规则结算后追加音效、特效或其他表现。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGridTileEvent, FTileData, Tile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGridCharacterEvent, EPlayerRole, Role, FIntPoint, Position);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGridLevelEvent);

// 跨次启动保存的进度，包含主线解锁、已收集回响和已阅读的机关引导。
UCLASS()
class GAMEJAM_API UGridPuzzleProgress : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 CompletedMainLevels = 0;
	UPROPERTY() TArray<int32> CompletedLevels;
	UPROPERTY() TArray<int32> EarnedEchoes;
	UPROPERTY() TArray<int32> SeenMechanicIntros;
};

// 场景与规则的协调者。创建地块和角色，接收输入，驱动界面与动画。
// 继承蓝图 BP_Board 负责指定关卡资源、地块类、角色类和材质。
UCLASS(Blueprintable)
class GAMEJAM_API AGridManager : public AActor
{
	GENERATED_BODY()

public:
	AGridManager();
	// GameMode 生成管理器后调用，准备存档、灯光、摄像机并进入主菜单。
	void Initialize(APlayerController& Controller);
	void Start();
	void ShowControls();
	void ShowMenu();
	void ShowSelection();
	void ShowAchievements();
	void DismissMechanicIntro();
	void SelectLevel(int32 Index);
	void Advance();
	void TogglePause();
	void ToggleMovePreview();
	void ToggleMusic();
	void Exit();

	// 默认关卡定义在 GridPuzzleChapters.cpp 中，Python 可据此生成可编辑的 Data Asset。
	UFUNCTION(BlueprintCallable, Category="Puzzle") static TArray<UGridLevelDataAsset*> CreateDefaultLevels();
	static TArray<UGridLevelDataAsset*> CreatePrototypeLevels();
	UFUNCTION(BlueprintCallable, Category="Puzzle") static TArray<UGridLevelDataAsset*> CreateFusionLevels();
	UFUNCTION(BlueprintCallable, Category="Puzzle") static TArray<UGridLevelDataAsset*> CreateCampaignLevels();

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

	// 主线、挑战关按数组顺序配置，玩法试玩在 Initialize 时追加。
	UPROPERTY(EditDefaultsOnly, Category="Level") TArray<TObjectPtr<UGridLevelDataAsset>> Levels;
	UPROPERTY(EditDefaultsOnly, Category="Look") TSubclassOf<AGridTile> TileClass;
	UPROPERTY(EditDefaultsOnly, Category="Look") TSubclassOf<AGridPuzzleCharacter> CharacterClass;
	UPROPERTY(EditDefaultsOnly, Category="Look") TObjectPtr<UMaterialInterface> PaletteMaterial;
	UPROPERTY(EditDefaultsOnly, Category="Sound") bool bPlaySound = true;
	UPROPERTY(EditDefaultsOnly, Category="Sound") bool bPlayMusic = true;
	UPROPERTY(EditDefaultsOnly, Category="Sound", meta=(ClampMin="0", ClampMax="1")) float MusicVolume = .65f;
	bool IsMusicEnabled() const { return bPlayMusic && !bMusicMuted && MusicVolume > 0.f; }

	EGridScreen GetScreen() const { return Screen; }
	int32 GetLevelNumber() const { return LevelIndex + 1; }
	int32 GetLevelCount() const { return Levels.Num(); }
	int32 GetCampaignLevelCount() const { return CampaignLevelCount; }
	int32 GetMainLevelCount() const { return MainLevelCount; }
	int32 GetResumeIndex() const;
	bool HasProgress() const { return !CompletedLevels.IsEmpty(); }
	bool CanSelectLevel(int32 Index) const;
	bool IsChallenge() const { return LevelIndex >= MainLevelCount && !IsPrototype(); }
	bool IsPrototype() const { return LevelIndex >= CampaignLevelCount; }
	bool HasLevelAction() const { return CurrentSection() && CurrentSection()->Prototype != EGridPrototype::None; }
	bool AreChallengesUnlocked() const { return CompletedMainLevels >= MainLevelCount; }
	int32 GetMoveCount() const { return ChapterMoveCount + Rules.GetMoveCount(); }
	int32 GetSectionNumber() const { return SectionIndex + 1; }
	int32 GetSectionCount() const;
	FText GetSectionTitle() const;
	int32 GetSourceLevelIndex() const;
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
	int32 GetEarnedEchoCount() const;
	int32 GetEchoTargetCount() const;
	int32 GetAchievementTargetCount() const { return MainLevelCount + 1; }
	int32 GetUnlockedAchievementCount() const;
	bool IsAchievementUnlocked(int32 AchievementIndex) const;
	bool IsAllClearAchievementUnlocked() const;
	FText GetAchievementName(int32 AchievementIndex) const;
	FText GetAchievementDescription(int32 AchievementIndex) const;
	bool IsLevelCompleted(int32 Level) const { return CompletedLevels.Contains(Level); }
	static bool GetEchoLocationForLevel(int32 LevelIndex, FIntPoint& OutPosition);
	static bool LevelHasMechanicIntro(int32 LevelIndex);
	FText GetLevelEchoName(int32 Index) const;
	FText GetMechanicIntroTitle() const;
	FText GetMechanicIntroBody() const;
	FText GetMechanicIntroExample() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FGridChapterRuntimeTest;
#endif
	// 保存行动前完整规则与受影响格子。回放三步只压入一个条目，撤销时整段恢复。
	struct FUndoStep
	{
		FGridPuzzleRules PreviousRules;
		EPlayerRole Mover;
		TArray<FIntPoint> ChangedTiles;
		int32 Section = 0;
		int32 ChapterMoves = 0;
	};

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// 切关时重建地块和人物，编辑器静态地图只承担启动场景的作用。
	bool LoadLevel(int32 Index, int32 Section = 0, bool bContinueChapter = false);
	const UGridLevelDataAsset* CurrentSection() const;
	void SaveUndo(FGridPuzzleRules Previous, EPlayerRole Mover, TArray<FIntPoint> Changed);
	void SetScreen(EGridScreen NewScreen);
	void UpdateControl();
	void UpdateBridgeDisplays();
	void PresentMove(EPlayerRole Mover, const FGridMove& MoveResult);
	UPROPERTY() TObjectPtr<UGridMusicComponent> Music;
	bool bMusicMuted = false;
	void ApplyTileUpdates(const TArray<FIntPoint>& Positions, bool bPulse, bool& bCollapsed, bool& bBridgeChanged);
	void CheckCompletion();
	void ClearActionPreview();
	void ClearMovePreview();
	void MarkActionTile(FIntPoint Position, FText Label, FColor Color);
	void PlayReplayStep();
	void AnimateTurnedDeck(const TArray<FIntPoint>& ChangedTiles, bool bReverse);
	FText GetClosedBridgeFeedback(FIntPoint Position) const;
	void SaveLevelProgress();
	void PersistProgress();
	bool TryCollectEchoAt(FIntPoint Position);
	void BuildEnvironment();
	void PositionCamera(bool bMenu, bool bOutro = false);
	// 网格坐标统一在这里转换成居中的世界坐标，表现层共享同一套换算。
	FVector ToWorld(FIntPoint Coordinate) const;
	void BeginOutro();
	void PulseOutro();
	void FinishOutro();
	void SetFeedback(FText Message);

	// UObject 引用通过 UPROPERTY 交给 UE 追踪，运行期生成的 Actor 在这里集中管理。
	UPROPERTY() TObjectPtr<APlayerController> Player;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<AActor> Environment;
	UPROPERTY() TObjectPtr<AActor> Outro;
	UPROPERTY() TMap<FIntPoint, TObjectPtr<AGridTile>> TileActors;
	UPROPERTY() TArray<TObjectPtr<AGridPuzzleCharacter>> Characters;

	// Slate 界面由共享指针管理，Actor 退出时需从视口移除。
	TSharedPtr<SWidget> ViewportUI;
	// Rules 是当前局面，历史快照和回放快照复用同一种状态表示。
	FGridPuzzleRules Rules;
	TArray<FUndoStep> UndoHistory;
	TArray<FIntPoint> PreviewMarkers;
	TArray<FIntPoint> MovePreviewMarkers;
	TArray<FGridMove> ReplaySteps;
	TArray<FGridPuzzleRules> ReplayStates;
	FText Feedback;
	EGridScreen Screen = EGridScreen::Menu;
	EPlayerRole ActiveRole = EPlayerRole::PlayerA;
	int32 LevelIndex = 0;
	int32 SectionIndex = 0;
	int32 ChapterMoveCount = 0;
	int32 MainLevelCount = 0;
	int32 CampaignLevelCount = 0;
	int32 ReplayIndex = 0;
	int32 CompletedMainLevels = 0;
	TSet<int32> CompletedLevels;
	TSet<int32> EarnedEchoes;
	TSet<int32> SeenMechanicIntros;
	// 动画结束前暂缓下一次移动，避免视觉位置与已经结算的网格位置错开。
	float UnlockInputAt = 0.f;
	float OutroStartedAt = 0.f;
	FTimerHandle ScreenTimer;
	FTimerHandle ReplayTimer;
	FTimerHandle FeedbackTimer;
	FTimerHandle OutroTimer;
	FTimerHandle OutroPulseTimer;
	bool bEndingReady = false;
	// 防止事件回调在一次行动尚未呈现完毕时重入移动、重置或撤销。
	bool bApplyingMove = false;
	bool bControlsFromPause = false;
	bool bActionPreview = false;
	bool bMechanicIntroPending = false;
};

// 输入入口，将 Config/DefaultInput.ini 中的动作映射转交给管理器。
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
	void PreviewMoves();
	void MuteMusic();
	void Undo();
	void Reset();
	void PauseOrBack();
	void Confirm();
};

// 游戏启动入口。StartPlay 生成 BP_Board，再由管理器生成整个可玩场景。
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
