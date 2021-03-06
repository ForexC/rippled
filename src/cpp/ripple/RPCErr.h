#ifndef __RPCERR__
#define __RPCERR__

#include "../json/value.h"

enum {
	rpcSUCCESS = 0,
	rpcBAD_SYNTAX,	// Must be 1 to print usage to command line.
	rpcJSON_RPC,
	rpcFORBIDDEN,

	// Error numbers beyond this line are not stable between versions.
	// Programs should use error tokens.

	// Misc failure
	rpcLOAD_FAILED,
	rpcNO_PERMISSION,
	rpcNO_EVENTS,
	rpcNOT_STANDALONE,

	// Networking
	rpcNO_CLOSED,
	rpcNO_CURRENT,
	rpcNO_NETWORK,

	// Ledger state
	rpcACT_EXISTS,
	rpcACT_NOT_FOUND,
	rpcINSUF_FUNDS,
	rpcLGR_NOT_FOUND,
	rpcNICKNAME_MISSING,
	rpcNO_ACCOUNT,
	rpcNO_PATH,
	rpcPASSWD_CHANGED,
	rpcSRC_MISSING,
	rpcSRC_UNCLAIMED,
	rpcTXN_NOT_FOUND,
	rpcWRONG_SEED,

	// Malformed command
	rpcINVALID_PARAMS,
	rpcUNKNOWN_COMMAND,

	// Bad parameter
	rpcACT_MALFORMED,
	rpcQUALITY_MALFORMED,
	rpcBAD_BLOB,
	rpcBAD_SEED,
	rpcCOMMAND_MISSING,
	rpcDST_ACT_MALFORMED,
	rpcDST_ACT_MISSING,
	rpcDST_AMT_MALFORMED,
	rpcGETS_ACT_MALFORMED,
	rpcGETS_AMT_MALFORMED,
	rpcHOST_IP_MALFORMED,
	rpcLGR_IDXS_INVALID,
	rpcLGR_IDX_MALFORMED,
	rpcNICKNAME_MALFORMED,
	rpcNICKNAME_PERM,
	rpcPAYS_ACT_MALFORMED,
	rpcPAYS_AMT_MALFORMED,
	rpcPORT_MALFORMED,
	rpcPUBLIC_MALFORMED,
	rpcSRC_ACT_MALFORMED,
	rpcSRC_ACT_MISSING,
	rpcSRC_ACT_NOT_FOUND,
	rpcSRC_AMT_MALFORMED,
	rpcSRC_CUR_MALFORMED,
	rpcSRC_ISR_MALFORMED,

	// Internal error (should never happen)
	rpcINTERNAL,		// Generic internal error.
	rpcFAIL_GEN_DECRPYT,
	rpcNOT_IMPL,
	rpcNOT_SUPPORTED,
	rpcNO_GEN_DECRPYT,
};

Json::Value rpcError(int iError, Json::Value jvResult=Json::Value(Json::objectValue));
#endif
// vim:ts=4
