// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// NOTE: This file is intended to be customised by the end user, and includes only local node policy logic

#include "policy/policy.h"

#include "consensus/validation.h"
#include "validation.h"
#include "coins.h"
#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

CAmount GetDustThreshold(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    // "Dust" is defined in terms of CTransaction::minRelayTxFee, which has units satoshis-per-kilobyte.
    // If you'd pay more than 1/3 in fees to spend something, then we consider it dust.
    // A typical spendable txout is 34 bytes big, and will need a CTxIn of at least 148 bytes to spend
    // i.e. total is 148 + 32 = 182 bytes. Default -minrelaytxfee is 10000 satoshis per kB
    // and that means that fee per spendable txout is 182 * 10000 / 1000 = 1820 satoshis.
    // So dust is a spendable txout less than 546 * minRelayTxFee / 1000 (in satoshis)
    // i.e. 1820 * 3 = 5460 satoshis with default -minrelaytxfee = minRelayTxFee = 10000 satoshis per kB.
    if (txout.scriptPubKey.IsUnspendable())
        return 0;

    size_t nSize = GetSerializeSize(txout, SER_DISK, 0);
    return 3 * dustRelayFeeIn.GetFee(nSize);
}

bool IsDust(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    if (txout.scriptPubKey.IsAssetScript())
        return false;
    else
        return (txout.nValue < GetDustThreshold(txout, dustRelayFeeIn));}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_MULTISIG) {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == TX_NULL_DATA &&
               (!fAcceptDatacarrier || scriptPubKey.size() > MAX_BDAP_RELAY))
        return false;
    else if (whichType == TX_RESTRICTED_ASSET_DATA && scriptPubKey.size() > MAX_OP_RETURN_RELAY)
        return false;

    return whichType != TX_NONSTANDARD;
}

bool IsStandardTx(const CTransaction& tx, std::string& reason)
{
    if ((tx.nVersion > CTransaction::MAX_STANDARD_VERSION || tx.nVersion < 1) && tx.nVersion != BDAP_TX_VERSION) {
        reason = "version";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    unsigned int sz = GetTransactionWeight(tx);
    if (sz >= MAX_STANDARD_TX_WEIGHT) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn& txin : tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    unsigned int nAssetDataOut = 0;
    txnouttype whichType;
    for (const CTxOut& txout : tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType)) {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TX_NULL_DATA) {
            // if this null data tx is not a BDAP and is larger than the max datacarrier, then return a non-standard transaction
            if (tx.nVersion != BDAP_TX_VERSION && txout.scriptPubKey.size() > nMaxDatacarrierBytes) {
                reason = "scriptpubkey";
                return false;
            }
            nDataOut++;
        }
        else if (whichType == TX_RESTRICTED_ASSET_DATA)
            nAssetDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (IsDust(txout, ::dustRelayFee)) {
            reason = "dust";
            return false;
        }
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    // only one hundred OP_DYNAMIC_ASSET txout is permitted
    if (nAssetDataOut > 100) {
        reason = "tomany-op-dyn-asset";
        return false;
    }

    return true;
}

bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxOut& prev = mapInputs.AccessCoin(tx.vin[i].prevout).out;

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;

        if (whichType == TX_SCRIPTHASH) {
            std::vector<std::vector<unsigned char> > stack;
            // convert the scriptSig into a stack, so we can inspect the redeemScript
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SIGVERSION_BASE))
                return false;
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            if (subscript.GetSigOpCount(true) > MAX_P2SH_SIGOPS) {
                return false;
            }
        }
    }

    return true;
}

CFeeRate incrementalRelayFee = CFeeRate(DEFAULT_INCREMENTAL_RELAY_FEE);
CFeeRate dustRelayFee = CFeeRate(DUST_RELAY_TX_FEE);
unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;

int64_t GetVirtualTransactionSize(int64_t nWeight, int64_t nSigOpCost)
{
    return (std::max(nWeight, nSigOpCost * nBytesPerSigOp));
}

int64_t GetVirtualTransactionSize(const CTransaction& tx, int64_t nSigOpCost)
{
    return GetVirtualTransactionSize(GetTransactionWeight(tx), nSigOpCost);
}
