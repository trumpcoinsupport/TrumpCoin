// Copyright (c) 2018 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TrumpCoin_INVALID_H
#define TrumpCoin_INVALID_H

#include <set>
#include <string>

class COutPoint;
class UniValue;

namespace invalid_out
{
    extern std::set<COutPoint> setInvalidOutPoints;

    UniValue read_json(const std::string& jsondata);

    bool ContainsOutPoint(const COutPoint& out);
    bool LoadOutpoints();
}

#endif //TrumpCoin_INVALID_H
