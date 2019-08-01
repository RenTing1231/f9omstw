﻿// \file f9omstws/OmsTwsTradingLineFixSend.cpp
// \author fonwinz@gmail.com
#include "f9omstws/OmsTwsTradingLineFix.hpp"
#include "f9omstws/OmsTwsTradingLineMgr.hpp"
#include "f9omstws/OmsTwsOrder.hpp"
#include "fon9/fix/FixApDef.hpp"

namespace f9omstw {

static inline char* RevPutFixed(char* pout, const size_t szFixed, fon9::StrView val) {
   memset(pout -= szFixed, ' ', szFixed);
   const size_t szVal = val.size();
   memcpy(pout, val.begin(), szVal <= szFixed ? szVal : szFixed);
   return pout;
}
static inline char* RevPutStr(char* pout, const void* pstr, size_t sz) {
   return reinterpret_cast<char*>(memcpy(pout -= sz, pstr, sz));
}
static inline char* RevPutStr(char* pout, fon9::StrView val) {
   return RevPutStr(pout, val.begin(), val.size());
}

TwsTradingLineFix::SendResult TwsTradingLineFix::SendRequest(f9fmkt::TradingRequest& req) {
   assert(dynamic_cast<OmsRequestTrade*>(&req) != nullptr);
   assert(dynamic_cast<TwsTradingLineMgr*>(&this->FixManager_) != nullptr);

   const auto now = fon9::UtcNow();
   const auto fcInterval = this->FlowCounter_.Check(now);
   if (fon9_UNLIKELY(fcInterval.GetOrigValue() > 0))
      return f9fmkt::ToFlowControlResult(fcInterval);

   fon9::DyObj<OmsRequestRunnerInCore> tmpRunner;
   const OmsRequestTrade*  curReq = static_cast<OmsRequestTrade*>(&req);
   OmsRequestRunnerInCore* runner = static_cast<TwsTradingLineMgr*>(&this->FixManager_)
      ->MakeRunner(tmpRunner, *curReq, 256u);

   enum TwseApCode : char {
      TwseApCode_Regular = '0',
      TwseApCode_FixedPrice = '7',
      TwseApCode_OddLot = '2',
   }  twseApCode;
   fon9_WARN_DISABLE_SWITCH;
   switch (req.SessionId()) {
   case f9fmkt_TradingSessionId_Normal:      twseApCode = TwseApCode_Regular;    break;
   case f9fmkt_TradingSessionId_OddLot:      twseApCode = TwseApCode_OddLot;     break;
   case f9fmkt_TradingSessionId_FixedPrice:  twseApCode = TwseApCode_FixedPrice; break;
   default:
      runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_SessionId, nullptr);
      return SendResult::RejectRequest;
   }
   fon9_WARN_POP;

   OmsOrder*               order  = runner->OrderRaw_.Order_;
   const OmsTwsRequestIni* iniReq = static_cast<const OmsTwsRequestIni*>(order->Initiator());
   f9fix::FixBuilder       fixb;
   //                   NewSingle   Replace  Cancel   Status
   // OrigClOrdID                      v        v
   // ClOrdID              v           v        v        v
   // OrderID              v           v        v        v
   // Symbol               v           v        v        v
   // Side                 v           v        v        v
   // Account              v           v        v
   // OrderQty             v           v
   // OrdType              v           v
   // TimeInForce          v            
   // TwseOrdType          v           v
   // Price                v           v
   // TwseRejStaleOrd      v           v        v
   // TwseExCode           v           v        v        v
   // TwseIvacnoFlag       v           v        v        v
   // TransactTime         v           v        v

   // "|TransactTime=yyyymmdd-hh:mm:ss.mmm"
   fixb.PutUtcTime(now);
   char* pout = RevPutStr(fixb.GetBuffer().AllocPrefix(256), f9fix_SPLTAGEQ(TransactTime));

   switch (iniReq->Side_) {
   case f9fmkt_Side_Buy:   pout = RevPutStr(pout, f9fix_SPLTAGEQ(Side) f9fix_kVAL_Side_Buy);    break;
   case f9fmkt_Side_Sell:  pout = RevPutStr(pout, f9fix_SPLTAGEQ(Side) f9fix_kVAL_Side_Sell);   break;
   case f9fmkt_Side_Unknown:
   default: // 買賣別應在風控時檢查, 但如果有疏漏, 這裡不可有預設值, 所以就拒絕吧!
      runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_Side, nullptr);
      return SendResult::RejectRequest;
   }

   // ExCode=|RejStaleOrd=|IvacNoFlag=
   if ((*--pout = iniReq->IvacNoFlag_.Chars_[0]) == '\0') // IvacNoFlag 也許要從 curReq 取得?
      *pout = ' ';                                        // 暫時不考慮 iniReq, curReq 來源不同.

   fon9::StrView       msgType;
   const f9fmkt_RxKind rxKind = curReq->RxKind();
   if (fon9_UNLIKELY(rxKind == f9fmkt_RxKind_RequestQuery)) {
      pout = RevPutStr(pout,
                       f9fix_SPLTAGEQ(TwseExCode) "0"
                       f9fix_SPLTAGEQ(TwseIvacnoFlag));
      msgType = f9fix_SPLFLDMSGTYPE(OrderStatusRequest);
   }
   else {
      pout = RevPutStr(pout,
                       f9fix_SPLTAGEQ(TwseRejStaleOrd) "N"
                       f9fix_SPLTAGEQ(TwseExCode)      "0" // '0'=Regular, '3'=Foreign stock’s order price over up/down limit flag.
                       f9fix_SPLTAGEQ(TwseIvacnoFlag));
      if (fon9_UNLIKELY(rxKind == f9fmkt_RxKind_RequestDelete)) {
__REQUEST_DELETE:
         msgType = f9fix_SPLFLDMSGTYPE(OrderCancelRequest);
      }
      else {
         const auto* lastOrd = static_cast<const OmsTwsOrderRaw*>(order->LastNotPending());
         // OmsTwsOrderRaw.OType_ 必須在風控檢查階段填入, 這裡不檢查, 如果有錯就讓交易所退單吧!
         *--pout = static_cast<char>(lastOrd->OType_);
         pout = RevPutStr(pout, f9fix_SPLTAGEQ(TwseOrdType));

         OmsTwsQty      fixQty;
         OmsTwsPri      fixPri;
         f9fmkt_PriType fixPriType;
         if (fon9_LIKELY(rxKind == f9fmkt_RxKind_RequestNew)) {
            msgType = f9fix_SPLFLDMSGTYPE(NewOrderSingle);
            // 20200323 支援 TimeInForce.
            switch (iniReq->TimeInForce_) {
            case f9fmkt_TimeInForce_ROD:
               pout = RevPutStr(pout, f9fix_SPLTAGEQ(TimeInForce) f9fix_kVAL_TimeInForce_Day);
               break;
            case f9fmkt_TimeInForce_IOC:
               pout = RevPutStr(pout, f9fix_SPLTAGEQ(TimeInForce) f9fix_kVAL_TimeInForce_IOC);
               break;
            case f9fmkt_TimeInForce_FOK:
               pout = RevPutStr(pout, f9fix_SPLTAGEQ(TimeInForce) f9fix_kVAL_TimeInForce_FOK);
               break;
            default:
            case f9fmkt_TimeInForce_QuotAutoCancel:
               runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_TimeInForce, nullptr);
               return SendResult::RejectRequest;
            }
            fixQty = iniReq->Qty_;
            fixPri = lastOrd->LastPri_;
            fixPriType = lastOrd->LastPriType_;
         }
         else if (fon9_LIKELY(rxKind == f9fmkt_RxKind_RequestChgQty)) {
            if (auto* chgQtyReq = dynamic_cast<const OmsTwsRequestChg*>(curReq)) {
               // OrderQty = 整股:欲刪數量, 零股:剩餘數量.
               // 20200323 零股也改成:欲刪數量, 那時應將 isWantToKill 設為 true;
               const bool isWantToKill = (twseApCode != TwseApCode_OddLot);
               const auto reqQty = chgQtyReq->Qty_;
               if (reqQty < 0) {
                  if (fon9_LIKELY(isWantToKill)) {
                     if ((fixQty = fon9::unsigned_cast(-reqQty)) >= lastOrd->LeavesQty_)
                        goto __REQUEST_DELETE;
                  }
                  else {
                     if (fon9::signed_cast(fixQty = lastOrd->LeavesQty_ + reqQty) <= 0)
                        goto __REQUEST_DELETE;
                  }
               }
               else if (reqQty > 0) {
                  if (fon9_LIKELY(isWantToKill)) {
                     if (fon9::signed_cast(fixQty = lastOrd->LeavesQty_ + lastOrd->CumQty_ - reqQty) <= 0) {
                        // 期望的改後數量有誤: 不可增量, 或期望的改後數量與現在(LeavesQty+CumQty)相同.
                        runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_Qty, nullptr);
                        return SendResult::RejectRequest;
                     }
                  }
                  else {
                     fixQty = fon9::unsigned_cast(reqQty);
                  }
               }
               else { // 改量時 reqQty = 0 = 刪單.
                  goto __REQUEST_DELETE;
               }
            }
            else if (auto* chgQtyIniReq = dynamic_cast<const OmsTwsRequestIni*>(curReq)) {
               // 使用 OmsTwsRequestIni 改量, 則直接把 chgQtyIniReq->Qty_ 送交易所.
               fixQty = chgQtyIniReq->Qty_;
            }
            else {
               runner->Reject(f9fmkt_TradingRequestSt_InternalRejected, OmsErrCode_UnknownRequestType, nullptr);
               return SendResult::RejectRequest;
            }
            fixPriType = f9fmkt_PriType_Limit;
            msgType = f9fix_SPLFLDMSGTYPE(OrderReplaceRequest);
         }
         else if (fon9_LIKELY(rxKind == f9fmkt_RxKind_RequestChgPri)) {
            if (auto* chgQtyReq = dynamic_cast<const OmsTwsRequestChg*>(curReq)) {
               fixPri = chgQtyReq->Pri_;
               fixPriType = chgQtyReq->PriType_;
            }
            else if (auto* chgQtyIniReq = dynamic_cast<const OmsTwsRequestIni*>(curReq)) {
               fixPri = chgQtyIniReq->Pri_;
               fixPriType = chgQtyIniReq->PriType_;
            }
            else {
               runner->Reject(f9fmkt_TradingRequestSt_InternalRejected, OmsErrCode_UnknownRequestType, nullptr);
               return SendResult::RejectRequest;
            }
            fixQty = 0;
            msgType = f9fix_SPLFLDMSGTYPE(OrderReplaceRequest);
         }
         else {
            runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_RxKind, nullptr);
            return SendResult::RejectRequest;
         }

         switch (fixPriType) {
         case f9fmkt_PriType_Unknown:
         case f9fmkt_PriType_Limit:
            // 20200323: Price: Max 5 digits + 4 decimals, 修改前為 Max 6 digits + 3 decimals
            // uint32_t pri = fon9::Decimal<uint32_t, 2>{iniReq->Pri_.GetOrigValue(), iniReq->Pri_.Scale}.GetOrigValue();
            // if (fon9_UNLIKELY(pri > 999999)) {
            //    runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_Pri, nullptr);
            //    return SendResult::RejectRequest;
            // }
            // pout = fon9::Pic9ToStrRev<2>(pout, pri % 100);
            // *--pout = '.';
            // pout = fon9::Pic9ToStrRev<4>(pout, pri / 100);
            pout = fon9::ToStrRev(pout, fixPri);
            pout = RevPutStr(pout,
                             f9fix_SPLTAGEQ(OrdType) f9fix_kVAL_OrdType_Limit
                             f9fix_SPLTAGEQ(Price));
            break;
         case f9fmkt_PriType_Market:
            pout = RevPutStr(pout,
                             f9fix_SPLTAGEQ(OrdType) f9fix_kVAL_OrdType_Market
                             f9fix_SPLTAGEQ(Price)   "0");
            break;
         default:
         case f9fmkt_PriType_MWP:
            runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_PriType, nullptr);
            return SendResult::RejectRequest;
         }

         if (fon9_LIKELY(twseApCode != TwseApCode_OddLot))
            fixQty /= order->ScResource().GetTwsSymbShUnit();
         // if (fon9_UNLIKELY(fixQty > 999)) {
         //    runner->Reject(f9fmkt_TradingRequestSt_CheckingRejected, OmsErrCode_Bad_Qty, nullptr);
         //    return SendResult::RejectRequest;
         // }
         // pout = fon9::Pic9ToStrRev<3>(pout, fixQty); // 20200323: OrderQty 為6碼, 修改前為3碼.
         pout = fon9::UIntToStrRev(pout, fixQty);
         pout = RevPutStr(pout, f9fix_SPLTAGEQ(OrderQty));
      }
      if (fon9_LIKELY(rxKind == f9fmkt_RxKind_RequestNew)) {
         if (fon9_UNLIKELY(!static_cast<TwsTradingLineMgr*>(&this->FixManager_)->AllocOrdNo(*runner)))
            return SendResult::RejectRequest;
      }
      else {
         // OrigClOrdID.
         pout = RevPutFixed(pout, f9tws::ClOrdID::size(), ToStrView(iniReq->ReqUID_));
         pout = RevPutStr(pout, f9fix_SPLTAGEQ(OrigClOrdID));
      }
      // 帳號必須填滿7碼, 前方補 '0'.
      pout = fon9::Pic9ToStrRev<7>(pout, iniReq->IvacNo_);
      pout = RevPutStr(pout, f9fix_SPLTAGEQ(Account));
   }
   pout = RevPutFixed(pout, f9tws::StkNo::size(), ToStrView(iniReq->Symbol_));
   pout = RevPutStr(pout, f9fix_SPLTAGEQ(Symbol));
   pout = RevPutStr(pout, ToStrView(runner->OrderRaw_.OrdNo_));
   pout = RevPutStr(pout, f9fix_SPLTAGEQ(OrderID));
   // ClOrdID
   pout = RevPutFixed(pout, f9tws::ClOrdID::size(), ToStrView(curReq->ReqUID_));
   pout = RevPutStr(pout, f9fix_SPLTAGEQ(ClOrdID));
   // -----------
   // fix header:
   // "SenderSubID=BrkId" 下單帳號所屬券商.
   pout = RevPutStr(pout, iniReq->BrkId_.begin(), iniReq->BrkId_.size());
   pout = RevPutStr(pout, f9fix_SPLTAGEQ(SenderSubID));
   // "TargetSubID=ApCode"
   *--pout = twseApCode;
   pout = RevPutStr(pout, f9fix_SPLTAGEQ(TargetSubID));
   // -----------
   fixb.GetBuffer().SetPrefixUsed(pout);
   this->FlowCounter_.ForceUsed(now);
   this->FixSender_->Send(msgType, std::move(fixb), &runner->ExLogForUpd_);
   runner->Update(f9fmkt_TradingRequestSt_Sending, ToStrView(this->StrSendingBy_));
   return SendResult::Sent;
}

} // namespaces
