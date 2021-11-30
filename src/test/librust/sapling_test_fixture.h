// Copyright (c) 2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef TrumpCoin_SAPLING_TEST_FIXTURE_H
#define TrumpCoin_SAPLING_TEST_FIXTURE_H

#include "test/test_trumpcoin.h"

/**
 * Testing setup that configures a complete environment for Sapling testing.
 */
struct SaplingTestingSetup : public TestingSetup
{
    SaplingTestingSetup(const std::string& chainName = CBaseChainParams::MAIN);
    ~SaplingTestingSetup();
};

/**
 * Regtest setup with sapling always active
 */
struct SaplingRegTestingSetup : public SaplingTestingSetup
{
    SaplingRegTestingSetup();
};


#endif //TrumpCoin_SAPLING_TEST_FIXTURE_H
