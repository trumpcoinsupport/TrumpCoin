Multi patriotnode config
=======================

The multi patriotnode config allows you to control multiple patriotnodes from a single wallet. The wallet needs to have a valid collateral output of 10000 coins for each patriotnode. To use this, place a file named patriotnode.conf in the data directory of your install:
 * Windows: %APPDATA%\TrumpCoin\
 * Mac OS: ~/Library/Application Support/TrumpCoin/
 * Unix/Linux: ~/.trumpcoin/

The new patriotnode.conf format consists of a space seperated text file. Each line consisting of an alias, IP address followed by port, patriotnode private key, collateral output transaction id, collateral output index, donation address and donation percentage (the latter two are optional and should be in format "address:percentage").

Example:
```
mn1 127.0.0.2:115110 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
mn2 127.0.0.3:115110 93WaAb3htPJEV8E9aQcN23Jt97bPex7YvWfgMDTUdWJvzmrMqey aa9f1034d973377a5e733272c3d0eced1de22555ad45d6b24abadff8087948d4 0 7gnwGHt17heGpG9Crfeh4KGpYNFugPhJdh:33
mn3 127.0.0.4:115110 92Da1aYg6sbenP6uwskJgEY2XWB5LwJ7bXRqc3UPeShtHWJDjDv db478e78e3aefaa8c12d12ddd0aeace48c3b451a8b41c570d0ee375e2a02dfd9 1 7gnwGHt17heGpG9Crfeh4KGpYNFugPhJdh
```

In the example above:
* the collateral for mn1 consists of transaction 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c, output index 0 has amount 10000
* patriotnode 2 will donate 33% of its income
* patriotnode 3 will donate 100% of its income


The following new RPC commands are supported:
* list-conf: shows the parsed patriotnode.conf
* start-alias \<alias\>
* stop-alias \<alias\>
* start-many
* stop-many
* outputs: list available collateral output transaction ids and corresponding collateral output indexes

When using the multi patriotnode setup, it is advised to run the wallet with 'patriotnode=0' as it is not needed anymore.
