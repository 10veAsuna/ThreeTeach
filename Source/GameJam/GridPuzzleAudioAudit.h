#pragma once

class UObject;
class UGridMusicComponent;

// 仅在显式传入 -OneStepAudioAudit 时录制引擎输出，不改玩家进度。
void BeginGridAudioAudit(UObject* Context, UGridMusicComponent* Music, bool bSoundEnabled, bool bMusicEnabled);
