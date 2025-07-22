//// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "broker/broker.h"
#include "../../src/libbroker_ctp/ctp_broker.h"
#include "../../src/libbroker_ctp/config.h"
#include <gtest/gtest.h>

using namespace co;
using namespace std;

struct RawData {
    RawData(string message, int64_t function_id) : message_(message), function_id_(function_id) {
    }
    string message_;
    int64_t function_id_;
};

static shared_ptr<co::Broker> broker = nullptr;
static shared_ptr<BrokerServer> server = nullptr;
static string fund_id = "";
static int64_t start_time = 0;


static void InitBroker() {
    if (!broker) {
        start_time = x::RawDate() * 1000000000LL;
        BrokerOptionsPtr opt = Config::Instance()->options();
        fund_id = Config::Instance()->ctp_investor_id();
        broker = make_shared<CTPBroker>();
        server = make_shared<BrokerServer>();
        server->Init(opt, broker);
        broker->Init(*opt, server.get());
        x::Sleep(1000);
    }
}

static std::vector<RawData> GetQueneData() {
    BrokerQueue* queue_ = server->mutable_queue();
    std::vector<RawData> all_recode;
    all_recode.clear();
    int64_t index = 0;
    while (true) {
        BrokerMsg* msg = queue_->TryPop();
        if (msg) {
            int64_t function_id = msg->function_id();
            std::string msg_raw = msg->data();
            BrokerMsg::Destory(msg);
            all_recode.emplace_back(RawData(msg_raw, function_id));
        }
        x::Sleep(10);
        index++;
        if (index == 300) {
            break;
        }
    }
    return all_recode;
}
