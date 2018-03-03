Castle Core integration/staging repository
=====================================

[![Build Status](https://travis-ci.org/MyCryptoCoins/Castle.svg?branch=master)](https://travis-ci.org/MyCryptoCoins/Castle) [![GitHub version](https://badge.fury.io/gh/MyCryptoCoins%2FCastle.svg)](https://badge.fury.io/gh/MyCryptoCoins%2FCastle)

Castle is an open source crypto-currency focused on fast private transactions with low transaction fees & environmental footprint.  The goal of Castle is to achieve a decentralized sustainable crypto currency with near instant full-time private transactions, fair governance and community intelligence.

Castle is based on [PIVX](http://www.pivx.org) code.

- Anonymized transactions using the [_Zerocoin Protocol_](http://www.pivx.org/zpiv).
- Fast transactions featuring guaranteed zero confirmation transactions, we call it _SwiftX_.
- Decentralized blockchain voting utilizing Masternode technology to form a DAO. The blockchain will distribute monthly treasury funds based on successful proposals submitted by the community and voted on by the DAO.

More information at [TBD Website](http://www.tbd.org) Visit our ANN thread at [BitcoinTalk](http://www.bitcointalk.org/index.php?topic=TBD)

### Coin Specs
<table>
<tr><td>Algo</td><td>Quark</td></tr>
<tr><td>Block Time</td><td>120 Seconds</td></tr>
<tr><td>Difficulty Retargeting</td><td>Every Block</td></tr>
<tr><td>Max Coin Supply from PoW</td><td>10,519,200 CSTL</td></tr>
<tr><td>Max Coin Supply from PoS</td><td>Infinite</td></tr>
<tr><td>Premine</td><td>8,000,000 CSTL*</td></tr>
</table>

*8,000,000 CSTL Premine will burned TBD

### Reward Distribution

<table>
<th colspan=4>Genesis Block</th>
<tr><th>Block Height</th><th>Reward Amount</th><th>Notes</th></tr>
<tr><td>1</td><td>8,000,000 CSTL</td><td>Initial Pre-mine, to be burnt TBD</td></tr>
</table>

### PoW Rewards Breakdown

<table>
<th>Year</th><th>Block Height</th><th>Masternodes</th><th>Miner</th><th>Budget</th>
<tr><td>1</td><td>2-262980</td><td>30% (2.4 CSTL)</td><td>8 CSTL</td><td>1% (0.08 CSTL)</td></tr>
<tr><td>2</td><td>262981-525960</td><td>30% (1.2 CSTL)</td><td>4 CSTL</td><td>1% (0.04 CSTL)</td></tr>
<tr><td>3</td><td>525961-788940</td><td>30% (0.6 CSTL)</td><td>2 CSTL</td><td>1% (0.02 CSTL)</td></tr>
<tr><td>4</td><td>788941-1051920</td><td>30% (0.3 CSTL)</td><td>1 CSTL</td><td>1% (0.01 CSTL)</td></tr>
<tr><td>5</td><td>1051921-1314900</td><td>30% (0.15 CSTL)</td><td>0.5 CSTL</td><td>1% (0.005 CSTL)</td></tr>
<tr><td>6-</td><td>1314901-</td><td>N/A</td><td>POW off</td><td>N/A</td></tr>
</table>

### PoS Rewards Breakdown

<table>
<th>Block Height</th><th>Reward</th><th>Minimum Staking Time</th><th>Masternodes</th><th>Stakers</th><th>Budget</th>
<tr><td>2-</td><td>8% APR</td><td>6 hours</td><td>20% of stake</td><td>8% APR</td><td>1% of stake</td></tr>
</table>

### Note about Masternode and Budget

The masternode payments and budget payments are minted additional to the POW and POS rewards rather than deducted from them.  This is done to make it simpler for miners and stakers to calculate their return, and to reduce the surprise factor.
