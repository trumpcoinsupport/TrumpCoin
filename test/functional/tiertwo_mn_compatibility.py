#!/usr/bin/env python3
# Copyright (c) 2021 The TrumpCoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
"""
Test checking compatibility code between PN and DPN
"""

from decimal import Decimal

from test_framework.test_framework import TrumpCoinTier2TestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes,
)


class PatriotnodeCompatibilityTest(TrumpCoinTier2TestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 7
        self.enable_mocktime()

        self.minerPos = 0
        self.ownerOnePos = self.ownerTwoPos = 1
        self.remoteOnePos = 2
        self.remoteTwoPos = 3
        self.remoteDPN1Pos = 4
        self.remoteDPN2Pos = 5
        self.remoteDPN3Pos = 6

        self.patriotnodeOneAlias = "mnOne"
        self.patriotnodeTwoAlias = "mntwo"

        self.extra_args = [["-nuparams=v5_shield:249", "-nuparams=v6_evo:250", "-whitelist=127.0.0.1"]] * self.num_nodes
        for i in [self.remoteOnePos, self.remoteTwoPos, self.remoteDPN1Pos, self.remoteDPN2Pos, self.remoteDPN3Pos]:
            self.extra_args[i] += ["-listen", "-externalip=127.0.0.1"]
        self.extra_args[self.minerPos].append("-sporkkey=932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi")

        self.mnOnePrivkey = "9247iC59poZmqBYt9iDh9wDam6v9S1rW5XekjLGyPnDhrDkP4AK"
        self.mnTwoPrivkey = "92Hkebp3RHdDidGZ7ARgS4orxJAGyFUPDXNqtsYsiwho1HGVRbF"

        self.miner = None
        self.ownerOne = self.ownerTwo = None
        self.remoteOne = None
        self.remoteTwo = None
        self.remoteDPN1 = None
        self.remoteDPN2 = None
        self.remoteDPN3 = None

    def check_mns_status_legacy(self, node, txhash):
        status = node.getpatriotnodestatus()
        assert_equal(status["txhash"], txhash)
        assert_equal(status["message"], "Patriotnode successfully started")

    def check_mns_status(self, node, txhash):
        status = node.getpatriotnodestatus()
        assert_equal(status["proTxHash"], txhash)
        assert_equal(status["dmnstate"]["PoSePenalty"], 0)
        assert_equal(status["status"], "Ready")

    """
    Checks the block at specified height
    Returns the address of the mn paid (in the coinbase), and the json coinstake tx
    """
    def get_block_mnwinner(self, height):
        blk = self.miner.getblock(self.miner.getblockhash(height), True)
        assert_equal(blk['height'], height)
        cbase_tx = self.miner.getrawtransaction(blk['tx'][0], True)
        assert_equal(len(cbase_tx['vin']), 1)
        cbase_script = height.to_bytes(1 + height // 256, byteorder="little")
        cbase_script = len(cbase_script).to_bytes(1, byteorder="little") + cbase_script + bytearray(1)
        assert_equal(cbase_tx['vin'][0]['coinbase'], cbase_script.hex())
        assert_equal(len(cbase_tx['vout']), 1)
        assert_equal(cbase_tx['vout'][0]['value'], Decimal("3.0"))
        return cbase_tx['vout'][0]['scriptPubKey']['addresses'][0], self.miner.getrawtransaction(blk['tx'][1], True)

    def check_mn_list(self, node, txHashSet):
        # check patriotnode list from node
        mnlist = node.listpatriotnodes()
        if len(mnlist) != len(txHashSet):
            raise Exception(str(mnlist))
        foundHashes = set([mn["txhash"] for mn in mnlist if mn["txhash"] in txHashSet])
        if len(foundHashes) != len(txHashSet):
            raise Exception(str(mnlist))
        for x in mnlist:
            self.mn_addresses[x["txhash"]] = x["addr"]

    def run_test(self):
        self.mn_addresses = {}
        self.enable_mocktime()
        self.setup_3_patriotnodes_network()

        # add two more nodes to the network
        self.remoteDPN2 = self.nodes[self.remoteDPN2Pos]
        self.remoteDPN3 = self.nodes[self.remoteDPN3Pos]
        # add more direct connections to the miner
        connect_nodes(self.miner, 2)
        connect_nodes(self.remoteTwo, 0)
        connect_nodes(self.remoteDPN2, 0)
        self.sync_all()

        # check mn list from miner
        txHashSet = set([self.mnOneCollateral.hash, self.mnTwoCollateral.hash, self.proRegTx1])
        self.check_mn_list(self.miner, txHashSet)

        # check status of patriotnodes
        self.check_mns_status_legacy(self.remoteOne, self.mnOneCollateral.hash)
        self.log.info("PN1 active. Pays %s" % self.mn_addresses[self.mnOneCollateral.hash])
        self.check_mns_status_legacy(self.remoteTwo, self.mnTwoCollateral.hash)
        self.log.info("PN2 active Pays %s" % self.mn_addresses[self.mnTwoCollateral.hash])
        self.check_mns_status(self.remoteDPN1, self.proRegTx1)
        self.log.info("DPN1 active Pays %s" % self.mn_addresses[self.proRegTx1])

        # Create another DPN, this time without funding the collateral.
        # ProTx references another transaction in the owner's wallet
        self.proRegTx2, self.dmn2Privkey = self.setupDPN(
            self.ownerOne,
            self.miner,
            self.remoteDPN2Pos,
            "internal"
        )
        self.remoteDPN2.initpatriotnode(self.dmn2Privkey, "", True)

        # check list and status
        txHashSet.add(self.proRegTx2)
        self.check_mn_list(self.miner, txHashSet)
        self.check_mns_status(self.remoteDPN2, self.proRegTx2)
        self.log.info("DPN2 active Pays %s" % self.mn_addresses[self.proRegTx2])

        # Check block version and coinbase payment
        blk_count = self.miner.getblockcount()
        self.log.info("Checking block version and coinbase payment...")
        payee, cstake_tx = self.get_block_mnwinner(blk_count)
        if payee not in [self.mn_addresses[k] for k in self.mn_addresses]:
            raise Exception("payee %s not found in expected list %s" % (payee, str(self.mn_addresses)))
        assert_equal(len(cstake_tx['vin']), 1)
        assert_equal(len(cstake_tx['vout']), 2)
        assert_equal(cstake_tx['vout'][1]['value'], Decimal("497.0")) # 250 + 250 - 3
        self.log.info("Block at height %d checks out" % blk_count)

        # Now create a DPN, reusing the collateral output of a legacy PN
        self.log.info("Creating a DPN reusing the collateral of a legacy PN...")
        self.proRegTx3, self.dmn3Privkey = self.setupDPN(
            self.ownerOne,
            self.miner,
            self.remoteDPN3Pos,
            "external",
            self.mnOneCollateral,
        )
        # The remote node is shutting down the pinging service
        self.send_3_pings()

        self.remoteDPN3.initpatriotnode(self.dmn3Privkey, "", True)

        # The legacy patriotnode must no longer be in the list
        # and the DPN must have taken its place
        txHashSet.remove(self.mnOneCollateral.hash)
        txHashSet.add(self.proRegTx3)
        for node in self.nodes:
            self.check_mn_list(node, txHashSet)
        self.log.info("Patriotnode list correctly updated by all nodes.")
        self.check_mns_status(self.remoteDPN3, self.proRegTx3)
        self.log.info("DPN3 active Pays %s" % self.mn_addresses[self.proRegTx3])

        # Now try to start a legacy PN with a collateral used by a DPN
        self.log.info("Now trying to start a legacy PN with a collateral of a DPN...")
        self.controller_start_patriotnode(self.ownerOne, self.patriotnodeOneAlias)
        self.send_3_pings()

        # the patriotnode list hasn't changed
        for node in self.nodes:
            self.check_mn_list(node, txHashSet)
        self.log.info("Patriotnode list correctly unchanged in all nodes.")

        # stake 30 blocks, sync tiertwo data, and check winners
        self.log.info("Staking 30 blocks...")
        self.stake(30, [self.remoteTwo])
        self.sync_blocks()
        self.wait_until_mnsync_finished()

        # check projection
        self.log.info("Checking winners...")
        winners = set([x['winner']['address'] for x in self.miner.getpatriotnodewinners()
                       if x['winner']['address'] != "Unknown"])
        # all except mn1 must be scheduled
        mn_addresses = set([self.mn_addresses[k] for k in self.mn_addresses
                            if k != self.mnOneCollateral.hash])
        assert_equal(winners, mn_addresses)

        # check mns paid in the last 20 blocks
        self.log.info("Checking patriotnodes paid...")
        blk_count = self.miner.getblockcount()
        mn_payments = {}    # dict address --> payments count
        for i in range(blk_count - 20 + 1, blk_count + 1):
            winner, _ = self.get_block_mnwinner(i)
            if winner not in mn_payments:
                mn_payments[winner] = 0
            mn_payments[winner] += 1
        # two full 10-blocks schedule: all mns must be paid at least twice
        assert_equal(len(mn_payments), len(mn_addresses))
        assert all([x >= 2 for x in mn_payments.values()])
        self.log.info("All good.")



if __name__ == '__main__':
    PatriotnodeCompatibilityTest().main()
