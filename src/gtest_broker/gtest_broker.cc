// Copyright 2020 Fancapital Inc.  All rights reserved.
#include "gtest_broker.h"
// 价格实时撮合
string code = "IC2506.CFFEX";
int64_t market = co::kMarketCFFEX;
int64_t bs_flag = co::kBsFlagBuy;
int64_t oc_flag = co::kOcFlagOpen;
double high_order_price = 5660;  // 成交价格
double low_order_price = 5600;  // 不成交价格
string order_no;
int volume = 1;


TEST(BrokerFunction, Init) {
    InitBroker();
}

TEST(BrokerFunction, Order) {
    co::fbs::TradeOrderMessageT msg;
    msg.items.clear();
    msg.id = x::UUID();
    msg.trade_type = co::kTradeTypeSpot;
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    {
        std::unique_ptr<co::fbs::TradeOrderT> _order = std::make_unique<co::fbs::TradeOrderT>();
        _order->id = x::UUID();
        _order->timestamp = x::RawDateTime();
        _order->fund_id = fund_id;
        _order->market = market;
        _order->code = code;
        _order->bs_flag = bs_flag;
        _order->oc_flag = oc_flag;
        _order->price = low_order_price;
        _order->volume = volume;
        _order->price_type = kQOrderTypeLimit;
        msg.items.emplace_back(std::move(_order));
    }
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::TradeOrderMessage::Pack(fbb, &msg));
    std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
    broker->SendTradeOrder(raw);
    std::vector<RawData> all_recode = GetQueneData();
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBTradeOrderRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::TradeOrderMessage>((void *) it.message_.data());
            LOG_INFO << "order rep = " << ToString(*rep);
            co::fbs::TradeOrderMessageT msg;
            rep->UnPackTo(&msg);
            for (auto &itor: msg.items) {
                order_no = itor->order_no;
                // order_no 和 error 不可能都是空值
                if (itor->order_no.length() == 0 && itor->error.length() == 0) {
                    EXPECT_TRUE(false);
                } else {
                    EXPECT_TRUE(true);
                }
            }
        }
    }
}

TEST(BrokerFunction, Withdraw) {
    co::fbs::TradeWithdrawMessageT msg;
    msg.id = x::UUID();
    msg.trade_type = co::kTradeTypeSpot;
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    msg.order_no = order_no;
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::TradeWithdrawMessage::Pack(fbb, &msg));
    std::string raw((const char *) fbb.GetBufferPointer(), fbb.GetSize());
    broker->SendTradeWithdraw(raw);
    std::vector<RawData> all_recode = GetQueneData();
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBTradeWithdrawRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::TradeWithdrawMessage>((void*)it.message_.data());
            LOG_INFO << "rep = " << ToString(*rep);
            co::fbs::TradeWithdrawMessageT rep_msg;
            rep->UnPackTo(&rep_msg);
            EXPECT_STREQ(rep_msg.order_no.c_str(), order_no.c_str());
        } else if (it.function_id_ == kFuncFBTradeKnock) {
            auto knock = flatbuffers::GetMutableRoot<co::fbs::TradeKnock>((void*)it.message_.data());
            LOG_INFO << "knock = " << ToString(*knock);
            co::fbs::TradeKnockT _knock;
            knock->UnPackTo(&_knock);
            EXPECT_GT(_knock.timestamp, start_time);
            EXPECT_STREQ(_knock.code.c_str(), code.c_str());
            EXPECT_STREQ(_knock.order_no.c_str(), order_no.c_str());
            EXPECT_EQ(_knock.market, market);
            EXPECT_EQ(_knock.bs_flag, bs_flag);
            EXPECT_EQ(_knock.oc_flag, oc_flag);
            EXPECT_EQ(_knock.match_type, co::kMatchTypeWithdrawOK);
            EXPECT_DOUBLE_EQ(_knock.match_price, 0);
            EXPECT_EQ(_knock.match_volume, volume);
            EXPECT_DOUBLE_EQ(_knock.match_amount, 0);
            string match_no = "_" + order_no;
            EXPECT_STREQ(_knock.match_no.c_str(), match_no.c_str());
        }
    }
    // 再撤一次，肯定出错
    broker->SendTradeWithdraw(raw);
    all_recode.clear();
    all_recode = GetQueneData();
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBTradeWithdrawRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::TradeWithdrawMessage>((void*)it.message_.data());
            LOG_INFO << "rep = " << ToString(*rep);
            co::fbs::TradeWithdrawMessageT rep_msg;
            rep->UnPackTo(&rep_msg);
            EXPECT_STREQ(rep_msg.order_no.c_str(), order_no.c_str());
            EXPECT_FALSE(rep_msg.error.empty());
        } else if (it.function_id_ == kFuncFBTradeKnock) {
            auto knock = flatbuffers::GetMutableRoot<co::fbs::TradeKnock>((void*)it.message_.data());
            LOG_INFO << "knock = " << ToString(*knock);
        }
    }
}

TEST(BrokerFunction, Knock) {
    co::fbs::TradeOrderMessageT msg;
    msg.items.clear();
    msg.id = x::UUID();
    msg.trade_type = co::kTradeTypeSpot;
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    {
        std::unique_ptr<co::fbs::TradeOrderT> _order = std::make_unique<co::fbs::TradeOrderT>();
        _order->id = x::UUID();
        _order->timestamp = x::RawDateTime();
        _order->fund_id = fund_id;
        _order->market = market;
        _order->code = code;
        _order->bs_flag = kBsFlagBuy;
        _order->oc_flag = co::kOcFlagOpen;
        _order->price = high_order_price;
        _order->volume = volume;
        _order->price_type = kQOrderTypeLimit;
        msg.items.emplace_back(std::move(_order));
    }
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::TradeOrderMessage::Pack(fbb, &msg));
    std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
    broker->SendTradeOrder(raw);
    std::vector<RawData> all_recode = GetQueneData();
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBTradeOrderRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::TradeOrderMessage>((void*)it.message_.data());
            LOG_INFO << "order rep = " << ToString(*rep);
            co::fbs::TradeOrderMessageT msg;
            rep->UnPackTo(&msg);
            for (auto& itor : msg.items) {
                order_no = itor->order_no;
            }
        } else if (it.function_id_ == kFuncFBTradeKnock) {
            auto knock = flatbuffers::GetMutableRoot<co::fbs::TradeKnock>((void*)it.message_.data());
            LOG_INFO << "knock = " << ToString(*knock);
            co::fbs::TradeKnockT _knock;
            knock->UnPackTo(&_knock);
            EXPECT_GT(_knock.timestamp, start_time);
            EXPECT_STREQ(_knock.code.c_str(), code.c_str());
            EXPECT_EQ(_knock.market, market);
            EXPECT_EQ(_knock.bs_flag, bs_flag);
            EXPECT_EQ(_knock.oc_flag, oc_flag);
            EXPECT_EQ(_knock.match_type, co::kMatchTypeOK);
            EXPECT_GT(_knock.match_price, 0);
            EXPECT_EQ(_knock.match_volume, volume);
            EXPECT_GT(_knock.match_no.length(), 0);
            EXPECT_GT(_knock.match_amount, 0);
            EXPECT_GT(_knock.bs_flag, 0);
            EXPECT_STREQ(_knock.order_no.c_str(), order_no.c_str());
        }
    }
}

TEST(BrokerFunction, QueryAssset) {
    co::fbs::GetTradeAssetMessageT msg;
    msg.items.clear();
    msg.id = x::UUID();
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::GetTradeAssetMessage::Pack(fbb, &msg));
    std::string raw((const char *) fbb.GetBufferPointer(), fbb.GetSize());
    broker->QueryTradeAsset(raw);
    std::vector<RawData> all_recode = GetQueneData();
    for (auto &it: all_recode) {
        if (it.function_id_ == kFuncFBGetTradeAssetRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::GetTradeAssetMessage>((void *) it.message_.data());
            LOG_INFO << "asset rep = " << ToString(*rep);
            co::fbs::GetTradeAssetMessageT msg;
            rep->UnPackTo(&msg);
            for (auto &arr: msg.items) {
                auto _item = flatbuffers::GetRoot<co::fbs::TradeAsset>(arr->data.data());
                co::fbs::TradeAssetT item;
                _item->UnPackTo(&item);
                EXPECT_GT(item.usable, 0);
            }
        }
    }
}


TEST(BrokerFunction, QueryPosition) {
    int64_t long_volume = 0;
    int64_t bs_flag = co::kBsFlagSell;
    int64_t oc_flag = co::kOcFlagClose;
    co::fbs::GetTradePositionMessageT msg;
    msg.items.clear();
    msg.id = x::UUID();
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::GetTradePositionMessage::Pack(fbb, &msg));
    std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
    broker->QueryTradePosition(raw);
    std::vector<RawData> all_recode = GetQueneData();
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBGetTradePositionRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::GetTradePositionMessage>((void*)it.message_.data());
            // LOG_INFO << "positon rep = " << ToString(*rep);
            co::fbs::GetTradePositionMessageT msg;
            rep->UnPackTo(&msg);
            for (auto& arr : msg.items) {
                auto _item = flatbuffers::GetRoot<co::fbs::TradePosition>(arr->data.data());
                co::fbs::TradePositionT item;
                _item->UnPackTo(&item);
                if (item.code == code) {
                    LOG_INFO << code << ", long_volume: " << item.long_volume
                             << ", long_pre_volume: " << item.long_pre_volume
                             << ", short_volume: " << item.short_volume
                             << ", short_pre_volume: " << item.short_pre_volume;
                    long_volume =  item.long_volume;
                }
            }
        }
    }

    {
        co::fbs::TradeOrderMessageT msg;
        msg.items.clear();
        msg.id = x::UUID();
        msg.trade_type = co::kTradeTypeSpot;
        msg.fund_id = fund_id;
        msg.timestamp = x::RawDateTime();
        {
            std::unique_ptr<co::fbs::TradeOrderT> _order = std::make_unique<co::fbs::TradeOrderT>();
            _order->id = x::UUID();
            _order->timestamp = x::RawDateTime();
            _order->fund_id = fund_id;
            _order->market = market;
            _order->code = code;
            _order->bs_flag = bs_flag;
            _order->oc_flag = oc_flag;
            _order->price = low_order_price;
            _order->volume = volume;
            _order->price_type = kQOrderTypeLimit;
            msg.items.emplace_back(std::move(_order));
        }
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(co::fbs::TradeOrderMessage::Pack(fbb, &msg));
        std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
        broker->SendTradeOrder(raw);
    }
    x::Sleep(5000);
    {
        co::fbs::GetTradePositionMessageT msg;
        msg.items.clear();
        msg.id = x::UUID();
        msg.fund_id = fund_id;
        msg.timestamp = x::RawDateTime();
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(co::fbs::GetTradePositionMessage::Pack(fbb, &msg));
        std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
        broker->QueryTradePosition(raw);
        all_recode.clear();
        all_recode = GetQueneData();
        for (auto& it : all_recode) {
            if (it.function_id_ == kFuncFBGetTradePositionRep) {
                auto rep = flatbuffers::GetMutableRoot<co::fbs::GetTradePositionMessage>((void*)it.message_.data());
                // LOG_INFO << "positon rep = " << ToString(*rep);
                co::fbs::GetTradePositionMessageT msg;
                rep->UnPackTo(&msg);
                for (auto& arr : msg.items) {
                    auto _item = flatbuffers::GetRoot<co::fbs::TradePosition>(arr->data.data());
                    co::fbs::TradePositionT item;
                    _item->UnPackTo(&item);
                    if (item.long_can_close > 0) {
                        EXPECT_GE(item.long_volume, item.long_can_close);
                        EXPECT_GE(item.long_market_value, 0);
                    }
                    if (item.code == code) {
                        LOG_INFO << code << ", long_volume: " << item.long_volume
                                 << ", long_pre_volume: " << item.long_pre_volume
                                 << ", short_volume: " << item.short_volume
                                 << ", short_pre_volume: " << item.short_pre_volume;
                        EXPECT_EQ(item.long_volume + volume, long_volume);
                    }
                }
            } else if (it.function_id_ == kFuncFBTradeKnock) {
                auto knock = flatbuffers::GetMutableRoot<co::fbs::TradeKnock>((void*)it.message_.data());
                LOG_INFO << "knock = " << ToString(*knock);
            }
        }
    }
}

TEST(BrokerFunction, QueryKnock) {
    co::fbs::GetTradeKnockMessageT msg;
    msg.items.clear();
    msg.id = x::UUID();
    msg.fund_id = fund_id;
    msg.timestamp = x::RawDateTime();
    msg.cursor = "0";
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(co::fbs::GetTradeKnockMessage::Pack(fbb, &msg));
    std::string raw((const char*)fbb.GetBufferPointer(), fbb.GetSize());
    broker->QueryTradeKnock(raw);
    std::vector<RawData> all_recode = GetQueneData();
    string pre_cursor = msg.cursor;
    for (auto& it : all_recode) {
        if (it.function_id_ == kFuncFBGetTradeKnockRep) {
            auto rep = flatbuffers::GetMutableRoot<co::fbs::GetTradeKnockMessage>((void*)it.message_.data());
            // LOG_INFO << "rep = " << ToString(*rep);
            co::fbs::GetTradeKnockMessageT msg;
            rep->UnPackTo(&msg);
            for (auto& arr : msg.items) {
                auto _item = flatbuffers::GetRoot<co::fbs::TradeKnock>(arr->data.data());
                co::fbs::TradeKnockT item;
                _item->UnPackTo(&item);
                EXPECT_GT(item.timestamp, start_time);
                EXPECT_FALSE(item.code.empty());
                EXPECT_GT(item.market, 0);
                EXPECT_GT(item.match_type, 0);
                EXPECT_GT(item.match_volume, 0);
                EXPECT_GT(item.bs_flag, 0);
                EXPECT_GT(item.oc_flag, 0);
                EXPECT_FALSE(item.order_no.empty());
                EXPECT_FALSE(item.match_no.empty());
                if (item.match_type == co::kMatchTypeFailed) {
                    // 查询成交时无错误信息
                    // EXPECT_FALSE(item.error.empty());
                    EXPECT_EQ(item.match_price, 0);
                    EXPECT_EQ(item.match_amount, 0);
                } else if (item.match_type == co::kMatchTypeOK) {
                    EXPECT_GT(item.match_price, 0);
                    EXPECT_GT(item.match_amount, 0);
                } else if (item.match_type == co::kMatchTypeWithdrawOK) {
                    EXPECT_EQ(item.match_price, 0);
                    EXPECT_EQ(item.match_amount, 0);
                }
            }
            if (msg.items.size() > 0) {
                EXPECT_STRNE(pre_cursor.c_str(), msg.next_cursor.c_str());
            }
        }
    }
}