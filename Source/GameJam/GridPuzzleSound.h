#pragma once

class UObject;

enum class EGridSound { Step, Print, Transfer, Reject, Switch, Collapse, Goal, Complete, Select, Bridge };

void PlayGridSound(UObject* Context, EGridSound Sound);
