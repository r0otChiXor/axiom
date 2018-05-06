// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The Castle developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <math.h>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Masternode manager */
CMasternodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CMasternode>& t1,
        const pair<int64_t, CMasternode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CMasternodeDB
//

CMasternodeDB::CMasternodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "MasternodeCache";
}

bool CMasternodeDB::Write(const CMasternodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssMasternodes(SER_DISK, CLIENT_VERSION);
    ssMasternodes << strMagicMessage;                   // masternode cache file specific magic message
    ssMasternodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssMasternodes << mnodemanToSave;
    uint256 hash = Hash(ssMasternodes.begin(), ssMasternodes.end());
    ssMasternodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssMasternodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("masternode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CMasternodeDB::ReadResult CMasternodeDB::Read(CMasternodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssMasternodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssMasternodes.begin(), ssMasternodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..

        ssMasternodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssMasternodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CMasternodeMan object
        ssMasternodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("masternode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("masternode","Masternode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("masternode","Masternode manager - result:\n");
        LogPrint("masternode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpMasternodes()
{
    int64_t nStart = GetTimeMillis();

    CMasternodeDB mndb;
    CMasternodeMan tempMnodeman;

    LogPrint("masternode","Verifying mncache.dat format...\n");
    CMasternodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasternodeDB::FileError)
        LogPrint("masternode","Missing masternode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CMasternodeDB::Ok) {
        LogPrint("masternode","Error reading mncache.dat: ");
        if (readResult == CMasternodeDB::IncorrectFormat)
            LogPrint("masternode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("masternode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("masternode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("masternode","Masternode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CMasternodeMan::CMasternodeMan()
{
    nDsqCount = 0;
}

bool CMasternodeMan::Add(CMasternode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled() && !mn.IsPotential())
        return false;

    if (Find(mn.vin)) {
	return false;
    }

    if (mn.IsEnabled()) {
        LogPrintf("CMasternodeMan: Adding new Masternode %s - %i now\n", mn.vin.prevout.hash.ToString(), vMasternodes.size() + 1);
        vMasternodes.push_back(mn);
    } else {
	uint256 hash = mn.vin.prevout.hash;
        LogPrintf("CMasternodeMan: Adding new Potential Masternode %s - %i now\n", hash.ToString(), vPotentialMasternodes.size() + 1);
        vPotentialMasternodes.push_back(mn);
    }
    activeMasternode.ManageStatus();

    return true;
}

void CMasternodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMasternodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("masternode", "CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    LogPrintf("Checking vMasternodes : %d\n", vMasternodes.size());
    for (auto mn : vMasternodes) {
        mn.Check();
    }

    LogPrintf("Checking vPotentialMasternodes : %d\n", vPotentialMasternodes.size());
    // Clear all visited bits
    for (auto& mn : vPotentialMasternodes) {
	mn.SetVisited(false);
    }

    // Loop until all visited bits are set
    volatile bool done = false;
    while (!done) {
	// iterate through and check nodues until the first one that gets
	// moved to the active list.  When one gets moved, stop iterating
	// so we don't mess up the iteration
        for (auto& mn : vPotentialMasternodes) {
	    if (!mn.IsVisited()) {
		mn.SetVisited(true);
		if (!mn.Check()) {
		    break;
		}
	    }
	}

	// Check if we've visted all nodes in the list
	done = true;
	for (auto& mn : vPotentialMasternodes) {
	    if (!mn.IsVisited()) {
                done = false;
		break;
	    }
	}
    }
}

void CMasternodeMan::RemoveInactive(std::vector<CMasternode>& vec,
	                            bool forceExpiredRemoval)
{
    //remove inactive and outdated
    vector<CMasternode>::iterator it = vec.begin();
    while (it != vec.end()) {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE ||
            (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
            LogPrintf("CMasternodeMan: Removing inactive Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), vec.size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
            while (it3 != mapSeenMasternodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    masternodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMasternodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masternode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
            while (it2 != mWeAskedForMasternodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForMasternodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vec.erase(it);
        } else {
            ++it;
        }
    }
}

void CMasternodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    int64_t now = GetTime();
    int64_t pingExpiry = now - (MASTERNODE_REMOVAL_SECONDS * 2);

    //remove inactive and outdated
    LogPrintf("Pruning vMasternodes\n");
    RemoveInactive(vMasternodes, forceExpiredRemoval);
    LogPrintf("Pruning vPotentialMasternodes\n");
    RemoveInactive(vPotentialMasternodes, forceExpiredRemoval);

    // check who's asked for the Masternode list
    for (auto it = mAskedUsForMasternodeList.begin();
         it != mAskedUsForMasternodeList.end(); ) {
        if (it->second < now) {
            it = mAskedUsForMasternodeList.erase(it);
        } else {
            ++it;
        }
    }

    // check who we asked for the Masternode list
    for (auto it = mWeAskedForMasternodeList.begin();
         it != mWeAskedForMasternodeList.end(); ) {
        if (it->second < now) {
            it = mWeAskedForMasternodeList.erase(it);
        } else {
            ++it;
        }
    }

    // check which Masternodes we've asked for
    for (auto it = mWeAskedForMasternodeListEntry.begin();
         it != mWeAskedForMasternodeListEntry.end(); ) {
        if (it->second < now) {
            it = mWeAskedForMasternodeListEntry.erase(it);
        } else {
            ++it;
        }
    }

    // remove expired mapSeenMasternodeBroadcast
    for (auto it = mapSeenMasternodeBroadcast.begin();
         it != mapSeenMasternodeBroadcast.end(); ) {
        if (it->second.lastPing.sigTime < pingExpiry) {
	    uint256 hash = it->second.GetHash();
            it = mapSeenMasternodeBroadcast.erase(it);
            masternodeSync.mapSeenSyncMNB.erase(hash);
        } else {
            ++it;
        }
    }

    // remove expired mapSeenMasternodePing
    for (auto it = mapSeenMasternodePing.begin();
         it != mapSeenMasternodePing.end(); ) {
        if (it->second.sigTime < pingExpiry) {
            it = mapSeenMasternodePing.erase(it);
        } else {
            ++it;
        }
    }
}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    vMasternodes.clear();
    vPotentialMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nDsqCount = 0;
}

int CMasternodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled masternodes

        nStable_size++;
    }

    return nStable_size;
}

int CMasternodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMasternodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);
            if (it != mWeAskedForMasternodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("masternode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;
}

void CMasternodeMan::DsegpUpdate(CNode* pnode)
{
    LOCK(cs);

    LogPrintf("DsegpUpdate\n");
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForPotentialMasternodeList.find(pnode->addr);
            if (it != mWeAskedForPotentialMasternodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("masternode", "dsegp - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dsegp", CTxIn());
    int64_t askAgain = GetTime() + MASTERNODES_DSEGP_SECONDS;
    mWeAskedForPotentialMasternodeList[pnode->addr] = askAgain;
}

CMasternode* CMasternodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    BOOST_FOREACH (CMasternode& mn, vPotentialMasternodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CMasternode* CMasternodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    BOOST_FOREACH (CMasternode& mn, vPotentialMasternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CMasternode* CMasternodeMan::FindActive(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CMasternode* CMasternodeMan::Find(const CPubKey& pubKeyMasternode)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    BOOST_FOREACH (CMasternode& mn, vPotentialMasternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
CMasternode* CMasternodeMan::GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CMasternode* pBestMasternode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecMasternodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masternodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are masternodes
        if (mn.GetMasternodeInputAge() < nMnCount) continue;

        vecMasternodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecMasternodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextMasternodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecMasternodeLastPaid.rbegin(), vecMasternodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecMasternodeLastPaid) {
        CMasternode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestMasternode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestMasternode;
}

CMasternode* CMasternodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CMasternode* CMasternodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CMasternode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Masternode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CMasternodeMan::GetMasternodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMasternodeScores;
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("masternode","Skipping Masternode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                if (fDebug) LogPrint("masternode","Skipping just activated Masternode. Age: %ld\n", nMasternode_Age);
                continue;                                                   // Skip masternodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasternodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecMasternodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CMasternode> > CMasternodeMan::GetMasternodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CMasternode> > vecMasternodeScores;
    std::vector<pair<int, CMasternode> > vecMasternodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecMasternodeRanks;

    // scan for winner
    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecMasternodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasternodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMasternode) & s, vecMasternodeScores) {
        rank++;
        vecMasternodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecMasternodeRanks;
}

CMasternode* CMasternodeMan::GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMasternodeScores;

    // scan for winner
    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMasternodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecMasternodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CMasternodeMan::ProcessMasternodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->fObfuScationMaster) {
            if (obfuScationPool.pSubmittedToMasternode != NULL && pnode->addr == obfuScationPool.pSubmittedToMasternode->addr) continue;
            LogPrint("masternode","Closing Masternode connection peer=%i \n", pnode->GetId());
            pnode->fObfuScationMaster = false;
            pnode->Release();
        }
    }
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Masternode Broadcast
        CMasternodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
            masternodeSync.AddedMasternodeList(mnb.GetHash());
            return;
        }
        mapSeenMasternodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS, "mnb fail");

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Masternode
        //  - this is expensive, so it's only done once per Masternode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrint("masternode","mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33, "mnb mismatched pubkey");
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            masternodeSync.AddedMasternodeList(mnb.GetHash());
        } else {
            LogPrint("masternode","mnb - Rejected Masternode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS, "mnb rejected");
        }
    }

    else if (strCommand == "mnpb") { // Potential Masternode Broadcast
	LogPrintf("ProcessMessage - mnpb\n");
        CMasternodeBroadcast mnb;
	std::vector<CNetAddr> seenNodes;
        vRecv >> mnb;
	vRecv >> seenNodes;

        if (mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
            masternodeSync.AddedMasternodePotentialList(mnb.GetHash(), seenNodes);
            return;
        }
        mapSeenMasternodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS, "mnpb fail");

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Masternode
        //  - this is expensive, so it's only done once per Masternode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrint("masternode","mnpb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33, "mnpb mismatched pubkey");
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            masternodeSync.AddedMasternodePotentialList(mnb.GetHash(), seenNodes);
        } else {
            LogPrint("masternode","mnpb - Rejected Masternode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS, "mnpb rejected");
        }
    }

    else if (strCommand == "mnp") { //Masternode Ping
        CMasternodePing mnp;
        vRecv >> mnp;

        LogPrint("masternode", "mnp - Masternode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenMasternodePing.count(mnp.GetHash())) return; //seen
        mapSeenMasternodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS, "mnp fail");
        } else {
            // if nothing significant failed, search existing Masternode list
            CMasternode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Masternode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasternodeList.find(pfrom->addr);
                if (i != mAskedUsForMasternodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34, "dseg already asked");
                        LogPrint("masternode","dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CMasternode& mn, vMasternodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("masternode", "dseg - Sending Masternode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenMasternodeBroadcast.count(hash)) mapSeenMasternodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("masternode", "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", MASTERNODE_SYNC_LIST, nInvCount);
            LogPrint("masternode", "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    } else if (strCommand == "dsegp") { //Get Potential Masternode list or specific entry

	LogPrintf("ProcessMessage - dsegp\n");
        CTxIn vin;
        vRecv >> vin;

        if (vin != CTxIn()) {
	    LogPrintf("dsegp - vin: %s, hash %s\n", vin.ToString(), vin.prevout.hash.ToString());
            pfrom->PushInventory(CInv(MSG_MASTERNODE_POTENTIAL_ANNOUNCE, vin.prevout.hash));
	    return;
	}

        int nInvCount = 0;

        //local network
        bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

        if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
            auto i = mAskedUsForPotentialMasternodeList.find(pfrom->addr);
            if (i != mAskedUsForPotentialMasternodeList.end()) {
                int64_t t = i->second;
                if (GetTime() < t) {
                    Misbehaving(pfrom->GetId(), 34, "dsegp already asked");
                    LogPrint("masternode","dsegp - peer already asked me for the list\n");
                    return;
                }
            }
            int64_t askAgain = GetTime() + MASTERNODES_DSEGP_SECONDS;
            mAskedUsForPotentialMasternodeList[pfrom->addr] = askAgain;
        }

        for (auto& it : mMasternodesSeen) {
	    LogPrintf("dsegp - Sending Potential Masternode entry - hash %s \n", it.first.ToString());
            pfrom->PushInventory(CInv(MSG_MASTERNODE_POTENTIAL_ANNOUNCE, it.first));
            nInvCount++;
        }

        pfrom->PushMessage("ssc", MASTERNODE_SYNC_POTENTIAL, nInvCount);
        LogPrint("masternode", "dsegp - Sent %d Potential Masternode entries to peer %i\n", nInvCount, pfrom->GetId());
    }

    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    // Light version for OLD MASSTERNODES - fake pings, no self-activation
    else if (strCommand == "dsee") { //ObfuScation Election Entry

        if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("masternode","dsee - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1, "dsee sig too far in future");
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion) + donationAddress.ToString() + boost::lexical_cast<std::string>(donationPercentage);

        if (protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
            LogPrint("masternode","dsee - ignoring outdated Masternode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, masternodePayments.GetMinMasternodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1, "dsee old mn version");
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if (pubkeyScript.size() != 25) {
            LogPrint("masternode","dsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100, "dsee pubkey wrong size");
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

        if (pubkeyScript2.size() != 25) {
            LogPrint("masternode","dsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100, "dsee pubkey2 wrong size");
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrint("masternode","dsee - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100, "dsee not empty scriptsig");
            return;
        }

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrint("masternode","dsee - Got bad Masternode address signature\n");
            Misbehaving(pfrom->GetId(), 100, "dsee bad mn addr sig");
            return;
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (addr.GetPort() != 35433) return;
        } else if (addr.GetPort() == 35433)
            return;

        //search existing Masternode list, this is where we update existing Masternodes with new dsee broadcasts
        CMasternode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > MASTERNODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < MASTERNODE_MIN_MNB_SECONDS) return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint("masternode", "dsee - Got updated entry for %s\n", vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyMasternode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CMasternodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        TRY_LOCK(cs_vNodes, lockNodes);
                        if (!lockNodes) return;
                        BOOST_FOREACH (CNode* pnode, vNodes)
                            if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                                pnode->PushMessage("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenDsee;
        if (mapSeenDsee.count(vin.prevout) && mapSeenDsee[vin.prevout] == pubkey) {
            LogPrint("masternode", "dsee - already seen this vin %s\n", vin.prevout.ToString());
            return;
        }
        mapSeenDsee.insert(make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Masternode
        //  - this is expensive, so it's only done once per Masternode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrint("masternode","dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100, "dsee mismatched pubkey");
            return;
        }


        LogPrint("masternode", "dsee - Got NEW OLD Masternode entry %s\n", vin.prevout.hash.ToString());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()

        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(39999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableInputs(mempool, state, CTransaction(tx), false, NULL);
        }

        if (fAcceptable) {
            if (GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS) {
                LogPrint("masternode","dsee - Input must have least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20, "dsee insufficient confirms");
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 1000 Castle tx got MASTERNODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            CTransaction tx2;
            GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 10000 CSTL tx -> 1 confirmation
                CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1]; // block where tx got MASTERNODE_MIN_CONFIRMATIONS
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint("masternode","mnb - Bad sigTime %d for Masternode %s (%i conf block is at %d)\n",
                        sigTime, vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2 * 60 * 60);

            // add Masternode
            CMasternode mn = CMasternode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyMasternode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CMasternodePing(vin);
            mn.Check(true);
            // add v11 masternodes, v12 should be added by mnb only
            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint("masternode", "dsee - Accepted OLD Masternode entry %i %i\n", count, current);
                Add(mn);
            }
            if (mn.IsEnabled()) {
                TRY_LOCK(cs_vNodes, lockNodes);
                if (!lockNodes) return;
                BOOST_FOREACH (CNode* pnode, vNodes)
                    if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                        pnode->PushMessage("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
            }
        } else {
            LogPrint("masternode","dsee - Rejected Masternode entry %s\n", vin.prevout.hash.ToString());

            int nDoS = 0;
            if (state.IsInvalid(nDoS)) {
                LogPrint("masternode","dsee - %s from %i %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->GetId(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS, "dsee rejected mn entry");
            }
        }
    }

    else if (strCommand == "dseep") { //ObfuScation Election Entry Ping

        if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrint("masternode","dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("masternode","dseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1, "dseep sig in future");
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrint("masternode","dseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1, "dseep sig in past");
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
        if (i != mWeAskedForMasternodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // see if we have this Masternode
        CMasternode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
            // LogPrint("masternode","dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > MASTERNODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if (!obfuScationSigner.VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
                    LogPrint("masternode","dseep - Got bad Masternode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100, "bad mn addr sig");
                    return;
                }

                // fake ping for v11 masternodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION) pmn->lastPing = CMasternodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    TRY_LOCK(cs_vNodes, lockNodes);
                    if (!lockNodes) return;
                    LogPrint("masternode", "dseep - relaying %s \n", vin.prevout.hash.ToString());
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto())
                            pnode->PushMessage("dseep", vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("masternode", "dseep - Couldn't find Masternode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin);
    }

    /*
     * END OF "REMOVE"
     */
}

CMasternode CMasternodeMan::Remove(CTxIn vin)
{
    return RemoveFromVector(vMasternodes, vin);
}

CMasternode CMasternodeMan::RemovePotential(CTxIn vin)
{
    return RemoveFromVector(vPotentialMasternodes, vin);
}

CMasternode CMasternodeMan::RemoveFromVector(std::vector<CMasternode>& vec,
                                              CTxIn vin)
{
    LOCK(cs);

    vector<CMasternode>::iterator it = vec.begin();
    while (it != vec.end()) {
        if ((*it).vin == vin) {
            LogPrintf("CMasternodeMan: Removing Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), vec.size() - 1);
	    CMasternode mn(*it);
            it = vec.erase(it);
            return mn;
        }
        ++it;
    }
    return CMasternode();
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast mnb)
{
    LOCK(cs);
    mapSeenMasternodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    LogPrint("masternode","CMasternodeMan::UpdateMasternodeList -- masternode=%s\n", mnb.vin.prevout.ToStringShort());

    CMasternode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CMasternode mn(mnb);
        if (Add(mn)) {
            masternodeSync.AddedMasternodeList(mnb.GetHash());
        }
    } else if (pmn->UpdateFromNewBroadcast(mnb)) {
        masternodeSync.AddedMasternodeList(mnb.GetHash());
    }
}

void CMasternodeMan::UpdateMasternodePotentialList(CMasternodeBroadcast mnb)
{
    LOCK(cs);
    mapSeenMasternodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    LogPrintf("CMasternodeMan::UpdateMasternodePotentialList -- masternode=%s\n", mnb.vin.prevout.ToStringShort());

    std::vector<CNetAddr> myaddr;
    myaddr.push_back(activeMasternode.service);

    CMasternode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CMasternode mn(mnb);
        if (Add(mn)) {
            masternodeSync.AddedMasternodePotentialList(mnb.GetHash(), myaddr);
        }
    } else if (pmn->UpdateFromNewBroadcast(mnb)) {
        masternodeSync.AddedMasternodePotentialList(mnb.GetHash(), myaddr);
    }
}

std::string CMasternodeMan::ToString() const
{
    std::ostringstream info;

    info << "Masternodes: " << (int)vMasternodes.size() <<
	    " Potential Masternodes: " << (int)vPotentialMasternodes.size() <<
	    ", peers who asked us for Masternode list: " <<
	    (int)mAskedUsForMasternodeList.size() <<
	    ", peers we asked for Masternode list: " <<
	    (int)mWeAskedForMasternodeList.size() <<
	    ", entries in Masternode list we asked for: "
	    << (int)mWeAskedForMasternodeListEntry.size() <<
	    ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

bool CMasternodeMan::CheckConsensus(CMasternode &mn)
{
    auto search = SeenByNodes(mn.vin.prevout.GetHash());
    int seenBy = search.size();
    int needed = ConsensusRequired();

    if (needed < 0) {
	LogPrintf("At cap.  No consensus needed\n");
        return false;
    }

    LogPrintf("CheckConsensus - seenBy: %d, needed: %d\n", seenBy, needed);
    return (seenBy >= needed);
}

int CMasternodeMan::ConsensusRequired()
{
    int numMn = stable_size();
    int maxMn = MaxMasternodeCount();

    if (numMn == 0)
	return 0;

    if (numMn >= maxMn)
	return -1;

    double N = (double)numMn;
    double Nmax = (double)maxMn;
    double needed = (2.0 * N) / 3.0 * exp((N * N)/(Nmax * Nmax) - 1.0) + 1.0;

    return int(needed);
}

int CMasternodeMan::MaxMasternodeCount()
{
    // 2 min blocks, get number of intervals
    int64_t nHeight = chainActive.Tip()->nHeight;
    int nIntervals = nHeight / Params().SubsidyHalvingInterval();
    int max = Params().MasternodeMaxBase() +
	      nIntervals * Params().MasternodeMaxIncrement();

    LogPrintf("MaxMasternodeCount: %d\n", max);
    return max;
}

std::vector<CNetAddr>& CMasternodeMan::SeenByNodes(const uint256& hash)
{
    auto search = mMasternodesSeen.find(hash);
    std::vector<CNetAddr> nodelist;

    if (search == mMasternodesSeen.end()) {
	mMasternodesSeen[hash] = nodelist;
	search = mMasternodesSeen.find(hash);
    }

    return search->second;
}


void CMasternodeMan::RemoveSeenByNodes(const uint256& hash,
                                       std::vector<CNetAddr>& seenNodes)
{
    LogPrintf("RemoveSeenByNodes\n");
    std::vector<CNetAddr> temp;
    std::vector<CNetAddr> search = SeenByNodes(hash);

    std::sort(search.begin(), search.end());
    std::sort(seenNodes.begin(), seenNodes.end());
    std::set_difference(search.begin(), search.end(), seenNodes.begin(),
		        seenNodes.end(), std::back_inserter(temp));
    mMasternodesSeen[hash] = temp;
}

void CMasternodeMan::UpdateSeenByNodes(const uint256& hash,
                                       std::vector<CNetAddr>& seenNodes)
{
    LogPrintf("UpdateSeenNodes\n");
    std::vector<CNetAddr> temp;
    std::vector<CNetAddr> search = SeenByNodes(hash);

    std::sort(search.begin(), search.end());
    std::sort(seenNodes.begin(), seenNodes.end());
    std::set_union(search.begin(), search.end(), seenNodes.begin(),
		   seenNodes.end(), std::back_inserter(temp));
    mMasternodesSeen[hash] = temp;
}

void CMasternodeMan::RemoveAllSeenByNodes(std::vector<CNetAddr>& seenNodes)
{
    LogPrintf("RemoveAllSeenByNodes\n");
    for (auto it = mMasternodesSeen.begin(); it != mMasternodesSeen.end(); ++it) {
	RemoveSeenByNodes(it->first, seenNodes);
    }
}

void CMasternodeMan::UpdateAllSeenByNodes(std::vector<CNetAddr>& seenNodes)
{
    LogPrintf("UpdateAllSeenNodes\n");
    for (auto it = mMasternodesSeen.begin(); it != mMasternodesSeen.end(); ++it) {
	UpdateSeenByNodes(it->first, seenNodes);
    }
}
