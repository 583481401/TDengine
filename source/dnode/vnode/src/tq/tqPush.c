/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tq.h"
#include "vnd.h"

int32_t tqPushMsg(STQ* pTq, void* msg, int32_t msgLen, tmsg_t msgType, int64_t ver) {

  if (msgType == TDMT_VND_SUBMIT) {
    tqProcessSubmitReqForSubscribe(pTq);
  }

  int32_t numOfTasks = streamMetaGetNumOfTasks(pTq->pStreamMeta);
  tqDebug("handle submit, restore:%d, size:%d", pTq->pVnode->restored, numOfTasks);

  // push data for stream processing:
  // 1. the vnode has already been restored.
  // 2. the vnode should be the leader.
  // 3. the stream is not suspended yet.
  if (!tsDisableStream && vnodeIsRoleLeader(pTq->pVnode) && pTq->pVnode->restored) {
    if (numOfTasks == 0) {
      return 0;
    }

    if (msgType == TDMT_VND_SUBMIT) {
      tqStartStreamTasks(pTq);
    }

    if (msgType == TDMT_VND_DELETE) {
      tqProcessDelReq(pTq, POINTER_SHIFT(msg, sizeof(SMsgHead)), msgLen - sizeof(SMsgHead), ver);
    }
  }

  return 0;
}

int32_t tqRegisterPushHandle(STQ* pTq, void* handle, SRpcMsg* pMsg) {
  int32_t    vgId = TD_VID(pTq->pVnode);
  STqHandle* pHandle = (STqHandle*)handle;

  if (pHandle->msg == NULL) {
    pHandle->msg = taosMemoryCalloc(1, sizeof(SRpcMsg));
    memcpy(pHandle->msg, pMsg, sizeof(SRpcMsg));
    pHandle->msg->pCont = rpcMallocCont(pMsg->contLen);
  } else {
    void* tmp = pHandle->msg->pCont;
    memcpy(pHandle->msg, pMsg, sizeof(SRpcMsg));
    pHandle->msg->pCont = tmp;
  }

  memcpy(pHandle->msg->pCont, pMsg->pCont, pMsg->contLen);
  pHandle->msg->contLen = pMsg->contLen;
  int32_t ret = taosHashPut(pTq->pPushMgr, pHandle->subKey, strlen(pHandle->subKey), &pHandle, POINTER_BYTES);
  tqDebug("vgId:%d data is over, ret:%d, consumerId:0x%" PRIx64 ", register to pHandle:%p, pCont:%p, len:%d", vgId, ret,
          pHandle->consumerId, pHandle, pHandle->msg->pCont, pHandle->msg->contLen);
  return 0;
}

int32_t tqUnregisterPushHandle(STQ* pTq, void *handle) {
  STqHandle *pHandle = (STqHandle*)handle;
  int32_t    vgId = TD_VID(pTq->pVnode);

  int32_t ret = taosHashRemove(pTq->pPushMgr, pHandle->subKey, strlen(pHandle->subKey));
  tqError("vgId:%d remove pHandle:%p,ret:%d consumer Id:0x%" PRIx64, vgId, pHandle, ret, pHandle->consumerId);

  if(pHandle->msg != NULL) {
    tqPushDataRsp(pTq, pHandle);

    rpcFreeCont(pHandle->msg->pCont);
    taosMemoryFree(pHandle->msg);
    pHandle->msg = NULL;
  }

  return 0;
}
