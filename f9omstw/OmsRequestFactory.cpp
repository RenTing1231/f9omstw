﻿// \file f9omstw/OmsRequestFactory.cpp
// \author fonwinz@gmail.com
#include "f9omstw/OmsRequestFactory.hpp"
#include "f9omstw/OmsRequestRunner.hpp"

namespace f9omstw {

OmsRequestFactory::~OmsRequestFactory() {
}
OmsRequestSP OmsRequestFactory::MakeRequest() {
   OmsRequestSP retval = this->MakeRequestImpl();
   retval->Initialize(*this);
   return retval;
}

} // namespaces
