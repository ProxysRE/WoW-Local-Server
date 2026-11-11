// Local-server arena bridge. GPL-3.0-or-later.
#ifndef LOCAL_PLAYERBOTS_1V1_H
#define LOCAL_PLAYERBOTS_1V1_H

#include "ObjectGuid.h"

bool ArenaBotBridgeEnabled();
bool RequestArenaBotOpponent(ObjectGuid player);
void CancelArenaBotOpponent(ObjectGuid player);
void AddSC_playerbots_1v1();

#endif
