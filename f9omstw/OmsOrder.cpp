﻿// \file f9omstw/OmsOrder.cpp
// \author fonwinz@gmail.com
#include "f9omstw/OmsOrder.hpp"
#include "f9omstw/OmsCore.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/seed/RawRd.hpp"

namespace f9omstw {

OmsOrder::OmsOrder() {
}
OmsOrder::~OmsOrder() {
}
void OmsOrder::FreeThis() {
   delete this;
}
fon9::fmkt::SymbSP OmsOrder::FindSymb(OmsResource& res, const fon9::StrView& symbid) {
   return res.Symbs_->GetSymb(symbid);
}
OmsBrk* OmsOrder::GetBrk(OmsResource& res) const {
   if (auto* ivr = this->ScResource_.Ivr_.get())
      return f9omstw::GetBrk(ivr);
   return res.Brks_->GetBrkRec(ToStrView(this->Head_->Request_->BrkId_));
}

OmsOrderRaw* OmsOrder::InitializeByStarter(OmsOrderFactory& creator, OmsRequestBase& starter, OmsScResource* scRes) {
   assert(this->Head_ == nullptr && this->Tail_ == nullptr && this->Creator_ == nullptr
          && starter.LastUpdated() == nullptr);
   this->Creator_ = &creator;
   if (scRes)
      this->ScResource_ = std::move(*scRes);

   OmsOrderRaw* retval = creator.MakeOrderRawImpl();
   retval->InitializeByStarter(*this, starter);
   this->Head_ = this->Tail_ = retval;
   return retval;
}
OmsOrderRaw* OmsOrder::BeginUpdate(const OmsRequestBase& req) {
   OmsOrderRaw* retval = this->Creator_->MakeOrderRawImpl();
   retval->InitializeByTail(*this->Tail_, req);
   if (req.LastUpdated() == nullptr && req.RxKind() == f9fmkt_RxKind_RequestNew) {
      // req 是新單回報補單 => 必須重設 this->Head_;
      // 後續透過 this->FirstPending_ 往後處理, 最後總是會更新成正確的序列.
      this->Head_ = this->Tail_ = retval;
   }
   else
      this->Tail_ = retval;
   return retval;
}
void OmsOrder::ProcessPendingReport(OmsResource& res) {
   assert(this->FirstPending_ != nullptr);
   const OmsOrderRaw* afFirstPending;
   const OmsOrderRaw* pord = this->FirstPending_;
   this->FirstPending_ = nullptr;
   do {
      afFirstPending = nullptr;
      const OmsOrderRaw* bfTail = this->Tail_;
      while (pord) {
         if (pord->UpdateOrderSt_ == f9fmkt_OrderSt_ReportPending) {
            if (pord->Request_->LastUpdated() == pord) {
               pord->Request_->ProcessPendingReport(res);
               if (afFirstPending == nullptr && pord->Request_->LastUpdated() == pord)
                  afFirstPending = pord;
            }
         }
         pord = pord->Next();
      }
      if (bfTail == this->Tail_)
         break;
      pord = afFirstPending;
   } while (pord);
   this->FirstPending_ = afFirstPending;
}

//--------------------------------------------------------------------------//

OmsOrderRaw::~OmsOrderRaw() {
}
void OmsOrderRaw::FreeThis() {
   delete this;
}
const OmsOrderRaw* OmsOrderRaw::CastToOrder() const {
   return this;
}

void OmsOrderRaw::InitializeByStarter(OmsOrder& order, const OmsRequestBase& starter) {
   assert(order.Head() == nullptr && order.Tail() == nullptr);
   this->Market_ = starter.Market();
   this->SessionId_ = starter.SessionId();
   this->Request_ = &starter;
   this->Order_ = &order;
}
void OmsOrderRaw::InitializeByTail(const OmsOrderRaw& tail, const OmsRequestBase& req) {
   assert(tail.Next_ == nullptr && tail.Order_->Tail() == &tail);
   tail.Next_ = this;
   this->Market_ = req.Market();
   this->SessionId_ = req.SessionId();
   this->Request_ = &req;
   this->Order_ = tail.Order_;
   this->UpdSeq_ = tail.UpdSeq_ + 1;
   this->ContinuePrevUpdate(tail);
}
void OmsOrderRaw::ContinuePrevUpdate(const OmsOrderRaw& prev) {
   assert(this->Order_ == prev.Order_);
   this->UpdateOrderSt_ = this->Order_->LastOrderSt();
   this->OrdNo_ = prev.OrdNo_;
   this->ErrCode_ = OmsErrCode_NoError;
}
void OmsOrderRaw::OnOrderReject() {
}
bool OmsOrderRaw::CheckErrCodeAct(const OmsErrCodeAct&) const {
   return true;
}

void OmsOrderRaw::MakeFieldsImpl(fon9::seed::Fields& flds) {
   using namespace fon9;
   using namespace fon9::seed;
   flds.Add(FieldSP{new FieldIntHx<underlying_type_t<f9fmkt_OrderSt>>         (Named{"OrdSt"}, fon9_OffsetOfRawPointer(OmsOrderRaw, UpdateOrderSt_))});
   flds.Add(FieldSP{new FieldIntHx<underlying_type_t<f9fmkt_TradingRequestSt>>(Named{"ReqSt"}, fon9_OffsetOfRawPointer(OmsOrderRaw, RequestSt_))});
   flds.Add(fon9_MakeField2(OmsOrderRaw, ErrCode));
   flds.Add(fon9_MakeField2(OmsOrderRaw, OrdNo));
   flds.Add(fon9_MakeField2(OmsOrderRaw, Message));
   flds.Add(fon9_MakeField2(OmsOrderRaw, UpdateTime));
}

} // namespaces
