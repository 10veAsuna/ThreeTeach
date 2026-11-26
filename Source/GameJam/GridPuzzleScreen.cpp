#include "GridPuzzleScreen.h"

#include "GridPuzzleGame.h"
#include "Brushes/SlateColorBrush.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GridPuzzleScreen"

// 菜单、HUD 与结算界面使用 Slate 在 C++ 中构建，交互按钮调用管理器的公共接口。
namespace
{
	const FLinearColor Ink(.83f,.92f,.92f);
	const FLinearColor Muted(.51f,.68f,.71f);
	const FLinearColor Blue(.34f,.81f,1.f);
	const FLinearColor Orange(1.f,.59f,.32f);
	const FLinearColor Panel(.014f,.035f,.054f,.95f);
	const FLinearColor Dim(.006f,.015f,.026f,.72f);
	const FSlateColorBrush Fill(FLinearColor::White);

	// Noto Sans / Source Han Serif 都是 OFL 字体，风格接近幻想 RPG 界面。
	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		const TCHAR* File = bBold ? TEXT("OneStep/Fonts/NotoSansSC-Bold.otf") :
			TEXT("OneStep/Fonts/MiSans-Regular.otf");
		return FSlateFontInfo(FPaths::ProjectContentDir() / File, Size);
	}

	FSlateFontInfo DisplayFont(int32 Size)
	{
		return FSlateFontInfo(FPaths::ProjectContentDir() /
			TEXT("OneStep/Fonts/NotoSerifSC-VF.ttf"), Size);
	}

	TSharedRef<STextBlock> Text(FText Label, int32 Size, FLinearColor Color = Ink, bool bLatin = false)
	{
		return SNew(STextBlock).Text(MoveTemp(Label)).Font(Font(Size,bLatin)).ColorAndOpacity(Color);
	}

	TSharedRef<STextBlock> DisplayText(FText Label, int32 Size, FLinearColor Color = Ink)
	{
		return SNew(STextBlock).Text(MoveTemp(Label)).Font(DisplayFont(Size)).ColorAndOpacity(Color);
	}

	TSharedRef<SBorder> Card(const TSharedRef<SWidget>& Content, FLinearColor Color = Panel, FMargin Padding = FMargin(22))
	{
		return SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Color).Padding(Padding)[Content];
	}

	// 统一按钮的颜色、内边距与交互样式，避免各页面重复设置。
	TSharedRef<SWidget> ActionButton(FText Label, FOnClicked Action, bool bPrimary = false, bool bEnabled = true)
	{
		static const FButtonStyle Primary = FButtonStyle()
			.SetNormal(FSlateColorBrush(FLinearColor(.087f,.26f,.33f)))
			.SetHovered(FSlateColorBrush(FLinearColor(.12f,.37f,.45f)))
			.SetPressed(FSlateColorBrush(FLinearColor(.068f,.18f,.25f)));
		static const FButtonStyle Secondary = FButtonStyle()
			.SetNormal(FSlateColorBrush(FLinearColor(.029f,.074f,.105f)))
			.SetHovered(FSlateColorBrush(FLinearColor(.078f,.16f,.20f)))
			.SetPressed(FSlateColorBrush(FLinearColor(.032f,.10f,.14f)));
		return SNew(SButton)
			.ButtonStyle(bPrimary ? &Primary : &Secondary)
			.ContentPadding(FMargin(18,10))
			.IsFocusable(false)
			.IsEnabled(bEnabled)
			.HAlign(HAlign_Left)
			.OnClicked(MoveTemp(Action))
			[Text(MoveTemp(Label), 20, bPrimary ? Ink : Muted)];
	}

	// 回调通过弱引用访问管理器，Actor 已销毁时跳过调用。
	TSharedRef<SWidget> Button(FText Label, TWeakObjectPtr<AGridManager> Manager,
		void (AGridManager::*Action)(), bool bPrimary = false)
	{
		return ActionButton(MoveTemp(Label), FOnClicked::CreateLambda([Manager, Action]()
			{
				if (AGridManager* Puzzle = Manager.Get()) { (Puzzle->*Action)(); }
				return FReply::Handled();
			}), bPrimary);
	}

	// 主菜单按钮根据存档选择显示开始或继续，并展示回响收集进度。
	TSharedRef<SWidget> Menu(AGridManager& Manager)
	{
		FText StartLabel = LOCTEXT("Play", "开始游戏");
		if (Manager.IsAllClearAchievementUnlocked()) { StartLabel = LOCTEXT("ReplayChapters", "重玩章节"); }
		else if (Manager.HasProgress())
		{
			StartLabel = FText::Format(LOCTEXT("ResumeChapter", "继续第 {0} 章"), FText::AsNumber(Manager.GetResumeIndex() + 1));
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
			[Text(LOCTEXT("Theme", "同行"), 16, Blue)]
			+ SVerticalBox::Slot().AutoHeight()
			[DisplayText(LOCTEXT("Title", "彼此的路"), 84)]
			+ SVerticalBox::Slot().AutoHeight().Padding(6,0,0,34)
			[Text(LOCTEXT("Subtitle", "PATHS WE SHARE"), 17, Muted, true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(5,0,0,32)
			[Text(LOCTEXT("Intro", "你先走。等我跟上，再给你留路。"), 20, Muted)]
			+ SVerticalBox::Slot().AutoHeight().Padding(5,0,0,22)
			[Text(FText::Format(LOCTEXT("AchievementProgress", "成就  {0} / {1}"),
				FText::AsNumber(Manager.GetUnlockedAchievementCount()), FText::AsNumber(Manager.GetAchievementTargetCount())),
				16, Orange)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[
				SNew(SBox).WidthOverride(260).HAlign(HAlign_Fill)
				[
					Button(StartLabel,
						&Manager, &AGridManager::Start, true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Button(LOCTEXT("Select", "选关"), &Manager, &AGridManager::ShowSelection)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Button(LOCTEXT("Achievements", "成就"), &Manager, &AGridManager::ShowAchievements)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Button(LOCTEXT("Controls", "玩法说明"), &Manager, &AGridManager::ShowControls)]
			+ SVerticalBox::Slot().AutoHeight()
			[Button(LOCTEXT("Exit", "退出游戏"), &Manager, &AGridManager::Exit)];
	}

	// 使用同一份 Levels 数组排列主线、挑战和试玩，解锁条件交给管理器判断。
	TSharedRef<SWidget> LevelSelection(AGridManager& Manager)
	{
		const TWeakObjectPtr<AGridManager> Weak(&Manager);
		const auto MakeLevelButton = [&Manager, Weak](int32 Index)
		{
			const FText BaseLabel = Index >= Manager.GetCampaignLevelCount() ? Manager.GetLevelTitle(Index) :
				FText::Format(LOCTEXT("LevelButton", "{0}  {1}"),
				FText::AsNumber(Index + 1), Manager.GetLevelTitle(Index));
			FText Label = BaseLabel;
			if (Index < Manager.GetCampaignLevelCount())
			{
				const FText Status = Manager.IsLevelCompleted(Index) ? LOCTEXT("CompletedChapter", "已完成") :
					Manager.CanSelectLevel(Index) ? LOCTEXT("PlayableChapter", "可游玩") : LOCTEXT("LockedChapter", "未解锁");
				Label = FText::Format(LOCTEXT("ChapterStatus", "{0} · {1}"), BaseLabel, Status);
			}
			return ActionButton(Label, FOnClicked::CreateLambda([Weak, Index]()
			{
				if (AGridManager* Puzzle = Weak.Get()) { Puzzle->SelectLevel(Index); }
				return FReply::Handled();
			}), Index == Manager.GetResumeIndex(), Manager.CanSelectLevel(Index));
		};
		if (Manager.GetScreen() == EGridScreen::Practice)
		{
			TSharedRef<SUniformGridPanel> Lessons = SNew(SUniformGridPanel).SlotPadding(FMargin(0,0,8,8))
				.MinDesiredSlotWidth(180.f);
			for (int32 Index = Manager.GetCampaignLevelCount(); Index < Manager.GetLevelCount(); ++Index)
			{
				const int32 Slot = Index - Manager.GetCampaignLevelCount();
				Lessons->AddSlot(Slot % 3, Slot / 3)[MakeLevelButton(Index)];
			}
			return Card(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)[DisplayText(LOCTEXT("Practice", "机关练习"), 38)]
				+ SVerticalBox::Slot().AutoHeight()[Lessons]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,16,0,0)
				[Button(LOCTEXT("BackToChapters", "返回选关"), &Manager, &AGridManager::ShowSelection)], Panel, FMargin(30,26));
		}

		TSharedRef<SUniformGridPanel> Chapters = SNew(SUniformGridPanel).SlotPadding(FMargin(0,0,8,8))
			.MinDesiredSlotWidth(223.f);
		for (int32 Index = 0; Index < Manager.GetMainLevelCount(); ++Index)
		{
			Chapters->AddSlot(Index % 3, Index / 3)[MakeLevelButton(Index)];
		}
		TSharedRef<SHorizontalBox> Challenges = SNew(SHorizontalBox);
		for (int32 Index = Manager.GetMainLevelCount(); Index < Manager.GetCampaignLevelCount(); ++Index)
		{
			Challenges->AddSlot().AutoWidth().Padding(0,0,8,0)[MakeLevelButton(Index)];
		}

		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
			[DisplayText(LOCTEXT("Select", "选关"), 38)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
			[Text(FText::Format(LOCTEXT("AchievementSelection", "成就  {0} / {1}"),
				FText::AsNumber(Manager.GetUnlockedAchievementCount()), FText::AsNumber(Manager.GetAchievementTargetCount())),
				16, Orange)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Text(LOCTEXT("MainChapters", "章节"), 18, Blue)]
			+ SVerticalBox::Slot().AutoHeight()[Chapters];
		if (Manager.GetCampaignLevelCount() > Manager.GetMainLevelCount())
		{
			Body->AddSlot().AutoHeight().Padding(0,18,0,8)
			[Text(Manager.AreChallengesUnlocked() ? LOCTEXT("Challenges", "挑战") :
				LOCTEXT("LockedChallenges", "通关后解锁挑战"), 18, Orange)];
			Body->AddSlot().AutoHeight()[Challenges];
		}
		Body->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0,14,0,0)
		[Button(LOCTEXT("Practice", "机关练习"), &Manager, &AGridManager::ShowPractice)];
		Body->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0,20,0,0)
		[Button(LOCTEXT("MainMenu", "返回菜单"), &Manager, &AGridManager::ShowMenu)];
		return Card(Body, Panel, FMargin(30,26));
	}

	TSharedRef<SWidget> Achievements(AGridManager& Manager)
	{
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel)
			.SlotPadding(FMargin(8))
			.MinDesiredSlotWidth(420.f);
		for (int32 Index = 0; Index < Manager.GetAchievementTargetCount(); ++Index)
		{
			const bool bUnlocked = Manager.IsAchievementUnlocked(Index);
			const bool bAllClear = Index == Manager.GetMainLevelCount();
			Grid->AddSlot(Index % 2, Index / 2)
			[
				SNew(SBorder)
				.BorderImage(&Fill)
				.BorderBackgroundColor(bUnlocked ?
					FLinearColor(.075f,.18f,.21f,.98f) : FLinearColor(.020f,.050f,.070f,.96f))
				.Padding(FMargin(18,14))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,14,0)
					[Text(bUnlocked ? LOCTEXT("UnlockedMark", "◆") : LOCTEXT("LockedMark", "◇"),
						24, bUnlocked ? Orange : Muted)]
					+ SHorizontalBox::Slot().VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[Text(bUnlocked ? Manager.GetAchievementName(Index) :
							(bAllClear ? Manager.GetAchievementName(Index) : LOCTEXT("HiddenAchievement", "未发现回响")),
							20, bUnlocked ? Ink : Muted, bUnlocked)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
						[Text(Manager.GetAchievementDescription(Index), 15, Muted)]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).HAlign(HAlign_Right)
					[Text(bUnlocked ? LOCTEXT("Unlocked", "已解锁") : LOCTEXT("Locked", "未解锁"),
						15, bUnlocked ? Orange : Muted)]
				]
			];
		}

		return Card(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[DisplayText(LOCTEXT("AchievementTitle", "成就与回响"), 38)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,18)
			[Text(FText::Format(LOCTEXT("AchievementSummary", "已解锁 {0} / {1}"),
				FText::AsNumber(Manager.GetUnlockedAchievementCount()),
				FText::AsNumber(Manager.GetAchievementTargetCount())), 18, Orange)]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,20,0,0)
			[Button(LOCTEXT("BackToMenu", "返回菜单"), &Manager, &AGridManager::ShowMenu, true)],
			Panel, FMargin(30,26));
	}

	// 绑定动态属性读取当前角色状态，切换人物时可直接刷新文本和背景。
	TSharedRef<SWidget> RoleStatus(TWeakObjectPtr<AGridManager> Manager, EPlayerRole Role)
	{
		const bool bBlue = Role == EPlayerRole::PlayerA;
		return SNew(SBox).WidthOverride(174)
		[
			SNew(SBorder).BorderImage(&Fill).Padding(FMargin(14,9))
			.BorderBackgroundColor_Lambda([Manager, Role]()
			{
				return FSlateColor(Manager.IsValid() && Manager->GetActiveRole() == Role ?
					FLinearColor(.077f,.19f,.23f,.98f) : Panel);
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,11,0)
				[Text(bBlue ? LOCTEXT("A", "A") : LOCTEXT("B", "B"), 20, bBlue ? Blue : Orange, true)]
				+ SHorizontalBox::Slot().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Font(16,false)).ColorAndOpacity(Muted).Text_Lambda([Manager,Role]()
					{
						if (!Manager.IsValid()) { return FText::GetEmpty(); }
						if (Manager->IsFinished(Role)) { return LOCTEXT("Arrived", "已到达"); }
						return Manager->GetActiveRole() == Role ? LOCTEXT("Active", "操作中") : LOCTEXT("Waiting", "等待");
					})
				]
			]
		];
	}

	// 游玩 HUD 显示关卡、角色、录制序列、规则反馈和步数。
	void AddHUD(SOverlay& Layout, AGridManager& Manager)
	{
		const TWeakObjectPtr<AGridManager> Weak(&Manager);
		const FText Chapter = Manager.IsPrototype() ? LOCTEXT("Prototypes", "试试机关") :
			Manager.IsChallenge() ? LOCTEXT("Challenges", "挑战") :
			FText::Format(LOCTEXT("Chapter", "第 {0} 章 / 共 {1} 章"),
				FText::AsNumber(Manager.GetLevelNumber()), FText::AsNumber(Manager.GetMainLevelCount()));
		Layout.AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(32,27)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(Chapter, 14, Blue, true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Text(Manager.GetLevelTitle(), 28)],
					Panel, FMargin(16,8))
			]
			+ SHorizontalBox::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[RoleStatus(Weak, EPlayerRole::PlayerA)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[RoleStatus(Weak, EPlayerRole::PlayerB)]
			]
		];
		Layout.AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,23)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Font(18,false)).ColorAndOpacity(Blue)
				.Text_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetRecordingStatus() : FText::GetEmpty(); })
				.Visibility_Lambda([Weak]()
				{
					return Weak.IsValid() && !Weak->GetRecordingStatus().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,10)
			[
				SNew(STextBlock).Font(Font(18,false)).ColorAndOpacity(Orange)
				.WrapTextAt(900.f).Justification(ETextJustify::Center)
				.ShadowOffset(FVector2D(2,2)).ShadowColorAndOpacity(FLinearColor::Black)
				.Text_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetFeedback() : FText::GetEmpty(); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[Card(SNew(STextBlock).Font(Font(18, false)).ColorAndOpacity(Ink)
				.WrapTextAt(900.f).Justification(ETextJustify::Center)
				.Text_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetLevelHint() : FText::GetEmpty(); }),
				Panel, FMargin(17,8))]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,5,0,0)
			[SNew(STextBlock).Font(Font(15,false)).ColorAndOpacity(Muted).Text_Lambda([Weak]()
			{
				if (!Weak.IsValid()) { return FText::GetEmpty(); }
				const FText CoreKeys = Weak->HasLevelAction() ? LOCTEXT("ActionKeys", "方向键 移动　Tab 换人　E 机关　Z 撤销　H 提示　Esc 暂停") :
					LOCTEXT("CoreKeys", "方向键 移动　Tab 换人　Z 撤销　H 提示　Esc 暂停");
				const FText LinkKeys = Weak->GetLinkHelp();
				return LinkKeys.IsEmpty() ? CoreKeys : FText::Format(LOCTEXT("PlayKeys", "{0}　{1}"), CoreKeys, LinkKeys);
			})]
		];
		Layout.AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,34,28)
		[
			SNew(STextBlock).Font(Font(15,false)).ColorAndOpacity(Muted)
			.Text_Lambda([Weak]()
			{
				return Weak.IsValid() ? FText::Format(LOCTEXT("Moves", "{0} 步"),
					FText::AsNumber(Weak->GetMoveCount())) : FText::GetEmpty();
			})
		];
	}

	// 暂停、操作说明、机关引导与通关页共享居中弹层的布局方式。
	TSharedRef<SWidget> Modal(AGridManager& Manager)
	{
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		auto Heading = [&Body](FText Label)
		{
			Body->AddSlot().AutoHeight().Padding(0,0,0,20)[DisplayText(MoveTemp(Label), 38)];
		};
		auto Row = [&Body](FText Label, FText Detail)
		{
			Body->AddSlot().AutoHeight().Padding(0,0,0,12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SBox).WidthOverride(175)[Text(MoveTemp(Label), 17, Blue)]]
				+ SHorizontalBox::Slot()[Text(MoveTemp(Detail), 18, Ink)]
			];
		};
		auto Action = [&Body, &Manager](FText Label, void (AGridManager::*Callback)(), bool bPrimary = false)
		{
			Body->AddSlot().AutoHeight().Padding(0,7,0,0)[Button(MoveTemp(Label), &Manager, Callback, bPrimary)];
		};

		switch (Manager.GetScreen())
		{
		case EGridScreen::MechanicIntro:
			Heading(LOCTEXT("NewMechanic", "机关说明"));
			Body->AddSlot().AutoHeight().Padding(0,0,0,10)
			[
				SNew(STextBlock).Font(DisplayFont(30)).ColorAndOpacity(Blue)
				.Text(Manager.GetMechanicIntroTitle())
			];
			Body->AddSlot().AutoHeight().Padding(0,0,0,16)
			[
				SNew(SBox).WidthOverride(720)
				[
					SNew(STextBlock).Font(Font(19,false)).ColorAndOpacity(Ink)
					.Text(Manager.GetMechanicIntroBody()).AutoWrapText(true)
				]
			];
			Body->AddSlot().AutoHeight().Padding(16,12)
			[
				SNew(SBorder).BorderImage(&Fill)
				.BorderBackgroundColor(FLinearColor(.025f,.07f,.09f,.95f))
				[
					SNew(STextBlock).Font(Font(17,false)).ColorAndOpacity(Muted)
					.Text(Manager.GetMechanicIntroExample()).AutoWrapText(true)
				]
			];
			Body->AddSlot().AutoHeight().Padding(0,16,0,0)
			[Button(LOCTEXT("EnterLevel", "进入关卡"), &Manager, &AGridManager::DismissMechanicIntro, true)];
			break;
		case EGridScreen::Controls:
			Heading(LOCTEXT("ControlTitle", "玩法说明"));
			Body->AddSlot().AutoHeight().Padding(0,0,0,14)
			[
				SNew(SBox).WidthOverride(760)
				[
					SNew(STextBlock).Font(Font(18,false)).ColorAndOpacity(Ink).AutoWrapText(true)
					.Text(LOCTEXT("Rule",
						"A 踩橙色方头脚印，B 踩蓝色圆头脚印。踩过的那格会留下自己的脚印。等你让开，对方就能跟上。\n"
						"脚印格要有对方的脚印才能走。让两人各自走到出口，就能过关。"))
				]
			];
			Row(LOCTEXT("Movement", "WASD / 方向键"), LOCTEXT("GridMovement", "向指定方向走"));
			Row(LOCTEXT("SwitchKey", "Tab"), LOCTEXT("SwitchPlayer", "切换 A 和 B"));
			Row(LOCTEXT("PreviewKey", "F1"), LOCTEXT("PreviewHelp", "查看可移动的位置"));
			Row(LOCTEXT("HintKey", "H"), LOCTEXT("HintHelp", "看看下一步该让谁走，往哪儿走"));
			Row(LOCTEXT("LinksKey", "L"), LOCTEXT("LinksHelp", "传印连线在相关、全部和隐藏之间切换"));
			Row(LOCTEXT("InteractKey", "E"), LOCTEXT("InteractHelp", "使用机关。出现预览时，再按一次确认"));
			Row(LOCTEXT("UndoKey", "Z / Ctrl+Z"), LOCTEXT("UndoHelp", "退回上一步，可以连按"));
			Row(LOCTEXT("ResetKey", "R"), LOCTEXT("ResetHelp", "从这关的起点重来"));
			Row(LOCTEXT("MusicKey", "M"), LOCTEXT("MusicHelp", "打开或关闭音乐"));
			Row(LOCTEXT("PauseKey", "Esc"), LOCTEXT("PauseHelp", "暂停，或返回上一页"));
			Action(LOCTEXT("Back", "返回"), &AGridManager::TogglePause, true);
			break;
		case EGridScreen::Paused:
			Heading(LOCTEXT("Paused", "暂停游戏"));
			Action(LOCTEXT("Resume", "继续游戏"), &AGridManager::TogglePause, true);
			Action(LOCTEXT("NextStep", "下一步提示"), &AGridManager::RequestNextStep);
			if (Manager.HasUndoMove()) { Action(LOCTEXT("Undo", "撤销上一步"), &AGridManager::UndoMove); }
			Action(LOCTEXT("Reset", "重新开始"), &AGridManager::RequestReset);
			Body->AddSlot().AutoHeight().Padding(0,7,0,0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().Padding(0,0,7,0)
				[Button(Manager.IsMusicEnabled() ? LOCTEXT("MusicOn", "音乐 · 开") : LOCTEXT("MusicOff", "音乐 · 关"), &Manager, &AGridManager::ToggleMusic)]
				+ SHorizontalBox::Slot()
				[Button(Manager.IsSoundEnabled() ? LOCTEXT("EffectsOn", "音效 · 开") : LOCTEXT("EffectsOff", "音效 · 关"), &Manager, &AGridManager::ToggleEffects)]
			];
			Action(LOCTEXT("Controls", "玩法说明"), &AGridManager::ShowControls);
			Action(LOCTEXT("Select", "选关"), &AGridManager::RequestSelection);
			Action(LOCTEXT("MainMenu", "返回菜单"), &AGridManager::RequestMenu);
			break;
		case EGridScreen::Confirm:
			Heading(LOCTEXT("ConfirmHeading", "确认操作"));
			Body->AddSlot().AutoHeight()[Text(Manager.GetBoardChangePrompt(), 20, Ink)];
			Body->AddSlot().AutoHeight().Padding(0,8,0,14)[Text(LOCTEXT("ConfirmDetail", "本关走法会清空。已解锁章节和回响会保留。"), 16, Muted)];
			Action(LOCTEXT("CancelChange", "取消"), &AGridManager::CancelBoardChange, true);
			Action(LOCTEXT("ConfirmChange", "确认"), &AGridManager::ConfirmBoardChange);
			break;
		case EGridScreen::Complete:
			Heading(LOCTEXT("Clear", "两人都到了"));
			Body->AddSlot().AutoHeight().Padding(0,0,0,17)
			[
				Text(FText::Format(LOCTEXT("ClearDetails", "{0}  ·  {1} 步"),
					Manager.GetLevelTitle(), FText::AsNumber(Manager.GetMoveCount())), 18, Muted)
			];
			if (Manager.HasCurrentLevelEcho())
			{
				Body->AddSlot().AutoHeight().Padding(0,0,0,17)
				[
					Text(FText::Format(LOCTEXT("EchoFound", "找到回响：{0}"),
						Manager.GetLevelEchoName(Manager.GetLevelNumber() - 1)), 17, Orange)
				];
			}
			if (Manager.IsAllClearAchievementUnlocked())
			{
				Body->AddSlot().AutoHeight().Padding(0,0,0,17)
				[Text(LOCTEXT("AllClearAchievement", "成就解锁：并肩至终"), 17, Orange)];
			}
			if (Manager.IsFinalLevel())
			{
				Action(LOCTEXT("ViewEnding", "观看结尾"), &AGridManager::Advance, true);
			}
			else
			{
				Action(Manager.IsPrototype() ? LOCTEXT("ReturnToPractice", "返回机关练习") : Manager.IsChallenge() ?
					LOCTEXT("ReturnToSelection", "返回选关") : LOCTEXT("Continue", "下一关"),
					&AGridManager::Advance, true);
			}
			if (Manager.HasUndoMove()) { Action(LOCTEXT("Undo", "撤销上一步"), &AGridManager::UndoMove); }
			break;
		default:
			break;
		}
		return Card(SNew(SBox).MinDesiredWidth(
			Manager.GetScreen() == EGridScreen::MechanicIntro ? 800.f : 421.f)[Body],
			Panel, FMargin(30,26));
	}
}

// 根据界面状态组装控件树，统一放进固定设计尺寸的缩放容器。
TSharedRef<SWidget> MakeGridPuzzleScreen(AGridManager& Manager)
{
	TSharedRef<SOverlay> Layout = SNew(SOverlay);
	TSharedRef<SOverlay> Frame = SNew(SOverlay);
	const EGridScreen Screen = Manager.GetScreen();
	if (Screen == EGridScreen::Menu)
	{
		Layout->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(66,0,0,0)
		[Menu(Manager)];
	}
	else if (Screen == EGridScreen::Selection || Screen == EGridScreen::Practice)
	{
		Frame->AddSlot()[SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Dim)];
		Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[LevelSelection(Manager)];
	}
	else if (Screen == EGridScreen::Achievements)
	{
		Frame->AddSlot()[SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Dim)];
		Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Achievements(Manager)];
	}
	else if (Screen == EGridScreen::Ending)
	{
		if (Manager.GetEndingReady())
		{
			Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,32)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[Text(LOCTEXT("EndingLine1", "A  刚才那段，我踩着你的脚印过来的。"), 26, Blue)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,8,0,14)
					[Text(LOCTEXT("EndingLine2", "B  我也是。"), 26, Orange)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
						[Button(LOCTEXT("SelectChallenges", "再选一关"), &Manager, &AGridManager::ShowSelection, true)]
						+ SHorizontalBox::Slot().AutoWidth()
						[Button(LOCTEXT("MainMenu", "返回菜单"), &Manager, &AGridManager::ShowMenu)]
					])
			];
		}
	}
	else
	{
		if (Screen == EGridScreen::Playing || Screen == EGridScreen::Transition)
		{
			AddHUD(*Layout, Manager);
		}
		else
		{
			Frame->AddSlot()[SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Dim)];
			Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Modal(Manager)];
		}
	}
	// 以 1400×788 为设计画布缩放到当前窗口，保持 HUD 与菜单的相对布局。
	Frame->AddSlot()
	[
		SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
		[
			SNew(SBox).WidthOverride(1400).HeightOverride(788)[Layout]
		]
	];
	return Frame;
}

#undef LOCTEXT_NAMESPACE
