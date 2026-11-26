#pragma once

class UObject;
class UAudioComponent;

// 音效类型下标与 GridPuzzleSound.cpp 中的 Cues 参数表保持对应。
enum class EGridSound { Step, Print, Transfer, Reject, Switch, Collapse, Goal, Complete, Select, Bridge, Ending };

inline constexpr float GridCompletionDuration = 2.5f;
inline constexpr float GridEndingDuration = 4.7f;

// 按事件现场合成短音效，Context 用于找到播放声音的世界。
UAudioComponent* PlayGridSound(UObject* Context, EGridSound Sound, float Volume = 1.f);
