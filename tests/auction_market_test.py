"""Compile and exercise the actual market C++ against a stateful core contract."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
headers = ["Common.h", "AuctionHouseBotConfig.h", "AuctionHouseBotCommon.h", "AuctionHouseMgr.h",
           "Config.h", "DatabaseEnv.h", "QueryResult.h", "Item.h", "ItemTemplate.h", "Log.h", "ObjectMgr.h", "Random.h",
           "DBCStores.h", "SpellInfo.h", "SpellMgr.h", "AuctionHouseSearcher.h", "CharacterCache.h", "Chat.h", "Creature.h", "Mail.h", "Player.h",
           "ScriptMgr.h", "Util.h", "WorldSession.h"]
with tempfile.TemporaryDirectory() as folder:
    tmp = Path(folder)
    for header in headers:
        (tmp / header).write_text('#pragma once\n#include "auction_market_fixture.h"\n')
    binary = tmp / "market-test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I" + str(tmp),
                    "-I" + str(ROOT / "tests"), str(ROOT / "tests/auction_market_fixture.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
