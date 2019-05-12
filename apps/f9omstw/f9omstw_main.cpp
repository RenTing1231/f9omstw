﻿// \file apps/f9omstw/f9omstw_main.cpp
// \author fonwinz@gmail.com
#include "fon9/framework/Framework.hpp"
#include "f9omstw/OmsPoIvListAgent.hpp"

int fon9sys_BeforeStart(fon9::Framework& fon9sys) {
   f9omstw::PolicyIvListAgent::Plant(*fon9sys.MaAuth_);
   return 0;
}

int main(int argc, char** argv) {
   return fon9::Fon9CoRun(argc, argv, &fon9sys_BeforeStart);
}
