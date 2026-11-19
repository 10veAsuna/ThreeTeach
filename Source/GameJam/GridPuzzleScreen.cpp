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

namespace
{
	const FLinearColor Ink(.83f,.92f,.92f);
	const FLinearColor Muted(.51f,.68f,.71f);
	const FLinearColor Blue(.34f,.81f,1.f);
	const FLinearColor Orange(1.f,.59f,.32f);
	const FLinearColor Panel(.014f,.035f,.054f,.95f);
	const FLinearColor Dim(.006f,.015f,.026f,.72f);
	const FSlateColorBrush Fill(FLinearColor::White);

	FSlateFontInfo Font(int32 Size, bool bLatin)
	{
		if (bLatin) { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }
		return FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"), Size);
	}

	TSharedRef<STextBlock> Text(FText Label, int32 Size, FLinearColor Color = Ink, bool bLatin = false)
	{
		return SNew(STextBlock).Text(MoveTemp(Label)).Font(Font(Size,bLatin)).ColorAndOpacity(Color);
	}

	TSharedRef<SBorder> Card(const TSharedRef<SWidget>& Content, FLinearColor Color = Panel, FMargin Padding = FMargin(22))
	{
		return SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Color).Padding(Padding)[Content];
	}

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

	TSharedRef<SWidget> Button(FText Label, TWeakObjectPtr<AGridManager> Manager,
		void (AGridManager::*Action)(), bool bPrimary = false)
	{
		return ActionButton(MoveTemp(Label), FOnClicked::CreateLambda([Manager, Action]()
			{
				if (AGridManager* Puzzle = Manager.Get()) { (Puzzle->*Action)(); }
				return FReply::Handled();
			}), bPrimary);
	}

	TSharedRef<SWidget> Menu(AGridManager& Manager)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
			[Text(LOCTEXT("Theme", "GAME JAM   /   同行"), 16, Blue)]
			+ SVerticalBox::Slot().AutoHeight()
			[Text(LOCTEXT("Title", "借我一步"), 84)]
			+ SVerticalBox::Slot().AutoHeight().Padding(6,0,0,34)
			[Text(LOCTEXT("Subtitle", "O N E   S T E P   F O R   Y O U"), 17, Muted, true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(5,0,0,32)
			[Text(LOCTEXT("Intro", "你无法走在自己留下的路上。\n所以，我把每一步都留给你。"), 20, Muted)]
			+ SVerticalBox::Slot().AutoHeight().Padding(5,0,0,22)
			[Text(FText::Format(LOCTEXT("EchoProgress", "回响  {0} / {1}"),
				FText::AsNumber(Manager.GetEarnedEchoCount()), FText::AsNumber(Manager.GetEchoTargetCount())),
				16, Orange)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[
				SNew(SBox).WidthOverride(260).HAlign(HAlign_Fill)
				[
					Button(Manager.HasProgress() ? LOCTEXT("ContinueGame", "继续游戏") : LOCTEXT("Play", "开始游戏"),
						&Manager, &AGridManager::Start, true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Button(LOCTEXT("Select", "选择关卡"), &Manager, &AGridManager::ShowSelection)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Button(LOCTEXT("Controls", "操作说明"), &Manager, &AGridManager::ShowControls)]
			+ SVerticalBox::Slot().AutoHeight()
			[Button(LOCTEXT("Exit", "退出游戏"), &Manager, &AGridManager::Exit)];
	}

	TSharedRef<SWidget> LevelSelection(AGridManager& Manager)
	{
		const TWeakObjectPtr<AGridManager> Weak(&Manager);
		const auto MakeLevelButton = [&Manager, Weak](int32 Index)
		{
			const FText Label = Index >= Manager.GetCampaignLevelCount() ? Manager.GetLevelTitle(Index) :
				FText::Format(LOCTEXT("LevelButton", "{0}  {1}"),
				FText::AsNumber(Index + 1), Manager.GetLevelTitle(Index));
			return ActionButton(Label, FOnClicked::CreateLambda([Weak, Index]()
			{
				if (AGridManager* Puzzle = Weak.Get()) { Puzzle->SelectLevel(Index); }
				return FReply::Handled();
			}), Index == Manager.GetResumeIndex(), Manager.CanSelectLevel(Index));
		};

		TSharedRef<SUniformGridPanel> Chapters = SNew(SUniformGridPanel).SlotPadding(FMargin(0,0,8,8))
			.MinDesiredSlotWidth(223.f);
		for (int32 Index = 0; Index < Manager.GetMainLevelCount(); ++Index)
		{
			Chapters->AddSlot(Index % 4, Index / 4)[MakeLevelButton(Index)];
		}
		TSharedRef<SHorizontalBox> Challenges = SNew(SHorizontalBox);
		for (int32 Index = Manager.GetMainLevelCount(); Index < Manager.GetCampaignLevelCount(); ++Index)
		{
			Challenges->AddSlot().AutoWidth().Padding(0,0,8,0)[MakeLevelButton(Index)];
		}

		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
			[Text(LOCTEXT("Select", "选择关卡"), 38)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
			[Text(FText::Format(LOCTEXT("EchoSelection", "已找到回响：{0} / {1}"),
				FText::AsNumber(Manager.GetEarnedEchoCount()), FText::AsNumber(Manager.GetEchoTargetCount())),
				16, Orange)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[Text(LOCTEXT("MainChapters", "主线"), 18, Blue)]
			+ SVerticalBox::Slot().AutoHeight()[Chapters];
		if (Manager.GetCampaignLevelCount() > Manager.GetMainLevelCount())
		{
			Body->AddSlot().AutoHeight().Padding(0,18,0,8)
			[Text(Manager.AreChallengesUnlocked() ? LOCTEXT("Challenges", "挑战") :
				LOCTEXT("LockedChallenges", "挑战 · 完成主线后解锁"), 18, Orange)];
			Body->AddSlot().AutoHeight()[Challenges];
		}
		TSharedRef<SHorizontalBox> Prototypes = SNew(SHorizontalBox);
		for (int32 Index = Manager.GetCampaignLevelCount(); Index < Manager.GetLevelCount(); ++Index)
		{
			Prototypes->AddSlot().AutoWidth().Padding(0,0,8,0)[MakeLevelButton(Index)];
		}
		Body->AddSlot().AutoHeight().Padding(0,18,0,8)[Text(LOCTEXT("Prototypes", "玩法试玩"), 18, Blue)];
		Body->AddSlot().AutoHeight()[Prototypes];
		Body->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0,20,0,0)
		[Button(LOCTEXT("MainMenu", "返回主菜单"), &Manager, &AGridManager::ShowMenu)];
		return Card(Body, Panel, FMargin(30,26));
	}

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
						if (Manager->IsFinished(Role)) { return LOCTEXT("Arrived", "已抵达"); }
						return Manager->GetActiveRole() == Role ? LOCTEXT("Active", "当前控制") : LOCTEXT("Waiting", "等待");
					})
				]
			]
		];
	}

	void AddHUD(SOverlay& Layout, AGridManager& Manager)
	{
		const TWeakObjectPtr<AGridManager> Weak(&Manager);
		const FText Chapter = Manager.IsPrototype() ? LOCTEXT("Prototypes", "玩法试玩") :
			Manager.IsChallenge() ? LOCTEXT("Challenges", "挑战") :
			FText::Format(LOCTEXT("Chapter", "{0} / {1}"),
				FText::AsNumber(Manager.GetLevelNumber()), FText::AsNumber(Manager.GetMainLevelCount()));
		Layout.AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(32,27)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(Chapter, 14, Blue, true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Text(Manager.GetLevelTitle(), 30)],
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
				.ShadowOffset(FVector2D(2,2)).ShadowColorAndOpacity(FLinearColor::Black)
				.Text_Lambda([Weak]() { return Weak.IsValid() ? Weak->GetFeedback() : FText::GetEmpty(); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[Card(Text(Manager.GetLevelHint(), 18), Panel, FMargin(17,8))]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,5,0,0)
			[Text(Manager.HasLevelAction() ? LOCTEXT("PrototypeKeyBar", "WASD / 方向键  移动      Tab  切换      E  机关      Z  撤销      R  重置      Esc  返回") :
				LOCTEXT("KeyBar", "WASD / 方向键  移动      Tab  切换      Z  撤销      R  重置      Esc  暂停"), 15, Muted)]
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

	TSharedRef<SWidget> Modal(AGridManager& Manager)
	{
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		auto Heading = [&Body](FText Label)
		{
			Body->AddSlot().AutoHeight().Padding(0,0,0,20)[Text(MoveTemp(Label), 38)];
		};
		auto Row = [&Body](FText Label, FText Detail)
		{
			Body->AddSlot().AutoHeight().Padding(0,0,0,12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SBox).WidthOverride(175)[Text(MoveTemp(Label), 18, Blue)]]
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
			Heading(LOCTEXT("NewMechanic", "本关新机制"));
			Body->AddSlot().AutoHeight().Padding(0,0,0,10)
			[
				SNew(STextBlock).Font(Font(30,false)).ColorAndOpacity(Blue)
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
			[Button(LOCTEXT("EnterLevel", "明白了，开始本关"), &Manager, &AGridManager::DismissMechanicIntro, true)];
			break;
		case EGridScreen::Controls:
			Heading(LOCTEXT("ControlTitle", "操作说明"));
			Row(LOCTEXT("Movement", "WASD / 方向键"), LOCTEXT("GridMovement", "每次移动一格"));
			Row(LOCTEXT("SwitchKey", "Tab"), LOCTEXT("SwitchPlayer", "切换蓝色 A 和橙色 B"));
			Row(LOCTEXT("InteractKey", "E"), LOCTEXT("InteractHelp", "在标记处录印、预览或确认机关"));
			Row(LOCTEXT("UndoKey", "Z / Ctrl+Z"), LOCTEXT("UndoHelp", "撤销上一次成功移动，可连续撤销"));
			Row(LOCTEXT("ResetKey", "R"), LOCTEXT("ResetHelp", "重置本关"));
			Row(LOCTEXT("PauseKey", "Esc"), LOCTEXT("PauseHelp", "暂停或返回"));
			Body->AddSlot().AutoHeight().Padding(0,7,0,14)
			[
				SNew(SBox).WidthOverride(760)
				[
					SNew(STextBlock).Font(Font(18,false)).ColorAndOpacity(Muted).AutoWrapText(true)
					.Text(LOCTEXT("Rule",
						"站台供两人落脚。离开站台后，只能踩同伴留下的脚印；空格、自己的脚印和已坍塌格都不能进入。\n"
						"踩上同伴的印会把它改成自己的颜色，这正是把下一步留给同伴的方式。\n"
						"OUT · 发件印板：角色踩上 OUT 时会读取来源脚印的当前颜色，并把它寄给对应连线末端的 IN。\n"
						"IN · 收件印板：保留寄出瞬间的颜色，角色必须走到 IN 上才能借用；空 IN 不能进入。\n"
						"WAIT · 正在送达：目标被占用或送印尚未到期时，印板会闪烁等待。\n"
						"1 · 一次性印板：离开后立即坍塌，不能再回来。"))
				]
			];
			Action(LOCTEXT("Back", "返回"), &AGridManager::TogglePause, true);
			break;
		case EGridScreen::Paused:
			Heading(LOCTEXT("Paused", "暂停"));
			Action(LOCTEXT("Resume", "继续游戏"), &AGridManager::TogglePause, true);
			if (Manager.HasUndoMove()) { Action(LOCTEXT("Undo", "撤销上一步"), &AGridManager::UndoMove); }
			Action(LOCTEXT("Reset", "重置本关"), &AGridManager::ResetLevel);
			Action(LOCTEXT("Controls", "操作说明"), &AGridManager::ShowControls);
			Action(LOCTEXT("Select", "选择关卡"), &AGridManager::ShowSelection);
			Action(LOCTEXT("MainMenu", "返回主菜单"), &AGridManager::ShowMenu);
			break;
		case EGridScreen::Complete:
			Heading(LOCTEXT("Clear", "两人已抵达"));
			Body->AddSlot().AutoHeight().Padding(0,0,0,17)
			[
				Text(FText::Format(LOCTEXT("ClearDetails", "{0}   /   共 {1} 步"),
					Manager.GetLevelTitle(), FText::AsNumber(Manager.GetMoveCount())), 18, Muted)
			];
			if (Manager.HasCurrentLevelEcho())
			{
				Body->AddSlot().AutoHeight().Padding(0,0,0,17)
				[
					Text(FText::Format(LOCTEXT("EchoFound", "回响已留下 · {0}"),
						Manager.GetLevelEchoName(Manager.GetLevelNumber() - 1)), 17, Orange)
				];
			}
			if (Manager.IsFinalLevel())
			{
				Action(LOCTEXT("ViewEnding", "查看结局"), &AGridManager::Advance, true);
			}
			else
			{
				Action((Manager.IsChallenge() || Manager.IsPrototype()) ? LOCTEXT("ReturnToSelection", "返回选关") : LOCTEXT("Continue", "下一关"),
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
	else if (Screen == EGridScreen::Selection)
	{
		Frame->AddSlot()[SNew(SBorder).BorderImage(&Fill).BorderBackgroundColor(Dim)];
		Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[LevelSelection(Manager)];
	}
	else if (Screen == EGridScreen::Ending)
	{
		if (Manager.GetEndingReady())
		{
			Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,32)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[Text(LOCTEXT("EndingLine1", "你走过的路，成为了我的路。"), 30)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,8,0,14)
					[Text(LOCTEXT("EndingLine2", "而我们，终于走到了这里。"), 20, Muted)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
						[Button(LOCTEXT("SelectChallenges", "挑战关卡"), &Manager, &AGridManager::ShowSelection, true)]
						+ SHorizontalBox::Slot().AutoWidth()
						[Button(LOCTEXT("MainMenu", "返回主菜单"), &Manager, &AGridManager::ShowMenu)]
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
