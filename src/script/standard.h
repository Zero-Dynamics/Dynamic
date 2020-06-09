// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DYNAMIC_SCRIPT_STANDARD_H
#define DYNAMIC_SCRIPT_STANDARD_H

#include "script/interpreter.h"
#include "uint256.h"

#include <stdint.h>

#include <boost/variant.hpp>

static const bool DEFAULT_ACCEPT_DATACARRIER = true;

class CKeyID;
class CScript;
class CStealthAddress;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    CScriptID(const CScript& in);
    CScriptID(const uint160& in) : uint160(in) {}
};

static const unsigned int MAX_OP_RETURN_RELAY = 83; //! bytes (+1 for OP_RETURN, +2 for the push data opcodes)
static const unsigned int MAX_BDAP_RELAY = 2051;    //! bytes (+1 for OP_RETURN, +2 for the push data opcodes and +2048)
extern bool fAcceptDatacarrier;
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added, such as a soft-fork to enforce
 * strict DER encoding.
 * 
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

enum txnouttype {
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA,
    /** ASSET START */
    TX_NEW_ASSET,
    TX_REISSUE_ASSET,
    TX_TRANSFER_ASSET,
    TX_RESTRICTED_ASSET_DATA //!< unspendable OP_RAVEN_ASSET script that carries data
    /** ASSET END */
};

class CNoDestination
{
public:
    friend bool operator==(const CNoDestination& a, const CNoDestination& b) { return true; }
    friend bool operator<(const CNoDestination& a, const CNoDestination& b) { return true; }
};

/** 
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a CDynamicAddress
 */
typedef boost::variant<CNoDestination, CKeyID, CScriptID, CStealthAddress> CTxDestination;

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest);

const char* GetTxnOutputType(txnouttype t);

bool Solver(const CScript& scriptPubKeyIn, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetScriptForRawPubKey(const CPubKey& pubkey);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

#endif // DYNAMIC_SCRIPT_STANDARD_H
