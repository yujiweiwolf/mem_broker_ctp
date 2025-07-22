// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include "mem_broker/mem_base_broker.h"
#include "mem_broker/mem_server.h"
#include "ctp_support.h"

namespace co {
class CTPTradeSpi;
class CTPBroker: public MemBroker {
 public:
    friend class CTPTradeSpi;
    explicit CTPBroker() = default;
    ~CTPBroker();

 protected:
    void OnInit();
    void RunCtp();

    void OnQueryTradeAsset(MemGetTradeAssetMessage* req);

    void OnQueryTradePosition(MemGetTradePositionMessage* req);

    void OnQueryTradeKnock(MemGetTradeKnockMessage* req);

    void OnTradeOrder(MemTradeOrderMessage* req);

    void OnTradeWithdraw(MemTradeWithdrawMessage* req);

 private:
    CThostFtdcTraderApi* ctp_api_ = nullptr;
    CTPTradeSpi* ctp_spi_ = nullptr;
    std::shared_ptr<std::thread> thread_; // 单独开一个线程供CTP使用，以免业务流程处理阻塞底层通信。
};
}  // namespace co