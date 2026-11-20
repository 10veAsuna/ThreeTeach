#pragma once

class UObject;

// 音效类型下标与 GridPuzzleSound.cpp 中的 Cues 参数表保持对应。
enum class EGridSound { Step, Print, Transfer, Reject, Switch, Collapse, Goal, Complete, Select, Bridge };

// 按事件现场合成短音效，Context 用于找到播放声音的世界。
void PlayGridSound(UObject* Context, EGridSound Sound);
