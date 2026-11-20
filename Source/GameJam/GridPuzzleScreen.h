#pragma once

#include "CoreMinimal.h"

class AGridManager;
class SWidget;

// 根据管理器当前界面状态生成 Slate 根控件，由管理器添加到游戏视口。
TSharedRef<SWidget> MakeGridPuzzleScreen(AGridManager& Manager);
