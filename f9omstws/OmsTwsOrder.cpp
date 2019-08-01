﻿// \file f9omstws/OmsTwsOrder.cpp
// \author fonwinz@gmail.com
#include "f9omstws/OmsTwsOrder.hpp"
#include "fon9/seed/FieldMaker.hpp"

namespace f9omstw {

void OmsTwsOrderRaw::MakeFields(fon9::seed::Fields& flds) {
   base::MakeFields<OmsTwsOrderRaw>(flds);
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, OType));

   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LastExgTime));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, BeforeQty));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, AfterQty));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LeavesQty));

   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LastFilledTime));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, CumQty));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, CumAmt));

   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LastPriTime));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LastPriType));
   flds.Add(fon9_MakeField2(OmsTwsOrderRaw, LastPri));
}

void OmsTwsOrderRaw::ContinuePrevUpdate() {
   base::ContinuePrevUpdate();
   OmsTwsOrderRawDat::ContinuePrevUpdate(*static_cast<const OmsTwsOrderRaw*>(this->Order_->LastNotPending()));
}
void OmsTwsOrderRaw::OnOrderReject() {
   assert(f9fmkt_OrderSt_IsRejected(this->OrderSt_));
   this->AfterQty_ = 0;
   this->LeavesQty_ = 0;
}

} // namespaces
