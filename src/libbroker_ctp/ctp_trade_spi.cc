#include <boost/algorithm/string.hpp>
#include "ctp_trade_spi.h"
#include "ctp_broker.h"

namespace co {
    CTPTradeSpi::CTPTradeSpi(CTPBroker* broker) : CThostFtdcTraderSpi(), broker_(broker) {
        start_index_ = x::RawTime();
        query_instruments_finish_.store(false);
        broker_id_ = Config::Instance()->ctp_broker_id();
        investor_id_ = Config::Instance()->ctp_investor_id();
    }

    void CTPTradeSpi::ReqAuthenticate() {
        LOG_INFO << "authenticate ...";
        string app_id = Config::Instance()->ctp_app_id();
        string product_info = Config::Instance()->ctp_product_info();
        string auth_code = Config::Instance()->ctp_auth_code();

        CThostFtdcReqAuthenticateField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.UserID, investor_id_.c_str());
        strcpy(req.AppID, app_id.c_str());
        strcpy(req.UserProductInfo, product_info.c_str());
        strcpy(req.AuthCode, auth_code.c_str());
        int rc = 0;
        while ((rc = api_->ReqAuthenticate(&req, GetRequestID())) != 0) {
            LOG_WARN << "ReqAuthenticate failed: " << CtpApiError(rc) << ", try ...";
            x::Sleep(CTP_FLOW_CONTROL_MS);
        }
    }

    void CTPTradeSpi::ReqUserLogin() {
        LOG_INFO << "login ...";
        string pwd = Config::Instance()->ctp_password();
        CThostFtdcReqUserLoginField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.UserID, investor_id_.c_str());
        strcpy(req.Password, pwd.c_str());
        int rc = 0;
        while ((rc = api_->ReqUserLogin(&req, GetRequestID())) != 0) {
            LOG_WARN << "ReqUserLogin failed: " << CtpApiError(rc) << ", try ...";
            x::Sleep(CTP_FLOW_CONTROL_MS);
        }
    }

    void CTPTradeSpi::ReqSettlementInfoConfirm() {
        LOG_INFO << "confirm settlement info ...";
        CThostFtdcSettlementInfoConfirmField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.InvestorID, investor_id_.c_str());
        int rc = api_->ReqSettlementInfoConfirm(&req, GetRequestID());
        if (rc != 0) {
            LOG_ERROR << "ReqSettlementInfoConfirm failed: " << CtpApiError(rc);
        }
    }

    void CTPTradeSpi::ReqQryInstrument() {
        LOG_INFO << "query all future contracts ...";
        CThostFtdcQryInstrumentField req;
        memset(&req, 0, sizeof(req));
        int rc = 0;
        while ((rc = api_->ReqQryInstrument(&req, GetRequestID())) != 0) {
            if (is_flow_control(rc)) {
                LOG_WARN << "ReqQryInstrument failed: " << CtpApiError(rc)
                    << ", retry in " << CTP_FLOW_CONTROL_MS << "ms ...";
                x::Sleep(CTP_FLOW_CONTROL_MS);
                continue;
            } else {
                LOG_ERROR << "ReqQryInstrument failed: " << CtpApiError(rc);
                break;
            }
        }
    }

//    void CTPTradeSpi::ReqQryInvestorPosition() {
//        MemGetTradePositionMessage msg {};
//        string id = x::UUID();
//        strncpy(msg.id, id.c_str(), id.length());
//        strcpy(msg.fund_id, investor_id_.c_str());
//        msg.timestamp = x::RawDateTime();
//        OnQueryTradePosition(&msg);
//    }

    void CTPTradeSpi::OnQueryTradeAsset(MemGetTradeAssetMessage* msg) {
        LOG_INFO << __FUNCTION__;
        rsp_query_msg_.clear();
        CThostFtdcQryTradingAccountField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.InvestorID, investor_id_.c_str());
        strcpy(req.CurrencyID, "CNY");  // 只查询人民币资金
        int request_id = GetRequestID();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradeAssetMessage));
            query_msg_.emplace(std::make_pair(request_id, raw));
        }
        int ret = api_->ReqQryTradingAccount(&req, request_id);
        if (ret != 0) {
            sprintf(msg->error, "query asset error: %d", ret);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradeAssetMessage));
            broker_->SendRtnMessage(raw, kMemTypeQueryTradeAssetRep);
            std::unique_lock<std::mutex> lock(mutex_);
            query_msg_.erase(request_id);
        }
    }

    void CTPTradeSpi::OnQueryTradePosition(MemGetTradePositionMessage* msg) {
        all_pos_.clear();
        CThostFtdcQryInvestorPositionField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.InvestorID, investor_id_.c_str());
        int request_id = GetRequestID();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradePositionMessage));
            query_msg_.emplace(std::make_pair(request_id, raw));
        }
        int ret = api_->ReqQryInvestorPosition(&req, request_id);
        if (ret != 0) {
            LOG_ERROR << "query position error: " << ret;
            sprintf(msg->error, "position asset error: %d", ret);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradePositionMessage));
            broker_->SendRtnMessage(raw, kMemTypeQueryTradePositionReq);
            std::unique_lock<std::mutex> lock(mutex_);
            query_msg_.erase(request_id);
        }
    }

    void CTPTradeSpi::OnQueryTradeKnock(MemGetTradeKnockMessage* msg) {
        all_knock_.clear();

        CThostFtdcQryTradeField req;
        memset(&req, 0, sizeof(req));
        strcpy(req.BrokerID, broker_id_.c_str());
        strcpy(req.InvestorID, investor_id_.c_str());
        strcpy(req.TradeTimeStart, msg->cursor);

        int request_id = GetRequestID();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradeKnockMessage));
            query_msg_.emplace(std::make_pair(request_id, raw));
        }
        int ret = api_->ReqQryTrade(&req, request_id);
        if (ret != 0) {
            LOG_ERROR << "query knock error: " << ret;
            sprintf(msg->error, "knock asset error: %d", ret);
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemGetTradePositionMessage));
            broker_->SendRtnMessage(raw, kMemTypeQueryTradeKnockReq);
            std::unique_lock<std::mutex> lock(mutex_);
            query_msg_.erase(request_id);
        }
    }

    void CTPTradeSpi::OnTradeOrder(MemTradeOrderMessage* msg) {
        // Thost收到报单指令, 如果没有通过参数校验, 拒绝接受报单指令。用户就会收到OnRspOrderInsert消息, 其中包含了错误编码和错误消息。
        // 如果Thost接受了报单指令, 用户不会收到OnRspOrderInsert, 而会收到OnRtnOrder, 用来更新委托状态。
        // 交易所收到报单后, 通过校验。用户会收到OnRtnOrder、OnRtnTrade。
        // 如果交易所认为报单错误, 用户就会收到OnErrRtnOrderInsert。
        // -------------------------------------------------
        // 1.处理自动开平仓逻辑,
        // 2.执行两个风控策略检查：1.股指期货禁止自动平今仓;2.股指期货当日最大开仓数限制。如果风控检查失败, 则会抛出异常

        string error_msg;
        int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg->items_size;
        auto items = msg->items;
        if (msg->items_size == 1) {
            int request_id = GetRequestID();
            auto order = items + 0;
            char ctp_code[16] = "";
            for (size_t i = 0; i < strlen(order->code); i++) {
                if (order->code[i] == '.') {
                    break;
                } else {
                    ctp_code[i] = order->code[i];
                }
            }
            LOG_INFO << "ReqOrderInsert, order_code: " << order->code
                     << ", ctp_code: " << ctp_code
                     << ", bs_flag: " << msg->bs_flag
                     << ", oc_flag: " << order->oc_flag
                     << ", volume: " << order->volume
                     << ", request_id: " << request_id;

            CThostFtdcInputOrderField req;
            memset(&req, 0, sizeof(req));
            strcpy(req.BrokerID, broker_id_.c_str());
            strcpy(req.InvestorID, investor_id_.c_str());
            strcpy(req.InstrumentID, ctp_code);

            sprintf(req.OrderRef, "%d", request_id);
            req.Direction = bs_flag2ctp(msg->bs_flag);
            req.CombOffsetFlag[0] = oc_flag2ctp(order->oc_flag);
            req.CombHedgeFlag[0] = THOST_FTDC_CIDT_Speculation;
            req.VolumeTotalOriginal = order->volume;
            req.VolumeCondition = THOST_FTDC_VC_AV;  /// 成交量类型：任何数量
            req.MinVolume = 1;  // 最小成交量
            req.IsAutoSuspend = 0;  /// 自动挂起标志: 否
            req.UserForceClose = 0;  /// 用户强评标志: 否
            req.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;  /// 强平原因: 非强平
            req.IsSwapOrder = 0;  // 互换单标志
            req.OrderPriceType = order_price_type2ctp(order->price_type);  // 报单价格条件
            req.LimitPrice = order->price;  /// 价格
            req.TimeCondition = THOST_FTDC_TC_GFD; ///有效期类型
            req.ContingentCondition = THOST_FTDC_CC_Immediately; // 触发条件：立即

            {
                string raw = string(reinterpret_cast<char*>(msg), length);
                req_msg_.emplace(std::make_pair(request_id, raw));
            }
            int ret = api_->ReqOrderInsert(&req, request_id);
            if (ret != 0) {
                error_msg = "order failed, ret: " + std::to_string(ret) + ", " + CtpApiError(ret);
                std::unique_lock<std::mutex> lock(mutex_);
                req_msg_.erase(request_id);
            }
        } else {
            error_msg = "order item is not valid.";
        }

        if (error_msg.length() > 0) {
            strcpy(msg->error, error_msg.c_str());
            string raw = string(reinterpret_cast<char*>(msg), length);
            broker_->SendRtnMessage(raw, kMemTypeTradeOrderRep);
        }
    }

    void CTPTradeSpi::OnTradeWithdraw(MemTradeWithdrawMessage* msg) {
        // 撤单响应和回报
        // Thost收到撤单指令, 如果没有通过参数校验, 拒绝接受撤单指令. 用户就会收到OnRspOrderAction 消息, 其中包含了错误编码和错误消息.
        // 如果 Thost 接受了撤单指令, 用户不会收到OnRspOrderAction, 而会收到OnRtnOrder, 用来更新委托状态.
        // 交易所收到撤单后, 通过校验, 执行了撤单操作. 用户会收到OnRtnOrder
        // 如果交易所认为报单错误, 用户就会收到OnErrRtnOrderAction
        string error_msg;
        string order_no = msg->order_no;
        if (!order_no.empty()) {
            CThostFtdcInputOrderActionField req;
            memset(&req, 0, sizeof(req));
            strcpy(req.BrokerID, broker_id_.c_str());
            strcpy(req.InvestorID, investor_id_.c_str());
            req.ActionFlag = THOST_FTDC_AF_Delete;  // 操作标志: 删除

            vector<string> vec_info;
            boost::split(vec_info, order_no, boost::is_any_of("_"), boost::token_compress_on);
            if (vec_info.size() == 4) {
                int front_id = atoi(vec_info[0].c_str());
                int session_id = atoi(vec_info[1].c_str());
                req.FrontID = front_id;
                req.SessionID = session_id;
                strncpy(req.OrderRef, vec_info[2].c_str(), vec_info[2].length());
                strncpy(req.InstrumentID, vec_info[3].c_str(), vec_info[3].length());
                int request_id = GetRequestID();
                // 撤单错误使用withdraw_msg_, 正确时使用req_msg_
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    string raw = string(reinterpret_cast<char *>(msg), sizeof(MemTradeWithdrawMessage));
                    req_msg_.emplace(std::make_pair(request_id, raw));
                    withdraw_msg_.emplace(std::make_pair(order_no, raw));
                }
                int ret = api_->ReqOrderAction(&req, request_id);
                if (ret != 0) {
                    error_msg = "withdraw faildd, ret: " + std::to_string(ret) + ", " + CtpApiError(ret);
                    std::unique_lock<std::mutex> lock(mutex_);
                    req_msg_.erase(request_id);
                    withdraw_msg_.erase(order_no);
                }
            } else {
                error_msg = "not valid order_no: " + order_no;
            }
        } else {
            error_msg = "order_no is empty";
        }

        if (error_msg.length() > 0) {
            strcpy(msg->error, error_msg.c_str());
            string raw = string(reinterpret_cast<char*>(msg), sizeof(MemTradeWithdrawMessage));
            broker_->SendRtnMessage(raw, kMemTypeTradeWithdrawRep);
        }
    }

    // ------------------------------------------------------------------------
    /// 当客户端与交易后台建立起通信连接时(还未登录前), 该方法被调用
    void CTPTradeSpi::OnFrontConnected() {
        LOG_INFO << "connect to CTP trade server ok";
        Start();
    }

    /// 当客户端与交易后台通信连接断开时, 该方法被调用. 当发生这个情况后,  API会自动重新连接, 客户端可不做处理.
    void CTPTradeSpi::OnFrontDisconnected(int nReason) {
        stringstream ss;
        ss << "ret=" << nReason << ", msg=";
        switch (nReason) {
            case 0x1001:  //  0x1001 网络读失败
                ss << "read error";
                break;
            case 0x1002:  // 0x1002 网络写失败
                ss << "write error";
                break;
            case 0x2001:  // 0x2001 接收心跳超时
                ss << "recv heartbeat timeout";
                break;
            case 0x2002:  // 0x2002 发送心跳失败
                ss << "send heartbeat timeout";
                break;
            case 0x2003:  // 0x2003 收到错误报文
                ss << "recv broken data";
                break;
            default:
                ss << "unknown error";
                break;
        }
        LOG_INFO << "connection is broken: " << ss.str();
        x::Sleep(2000);
    }

    void CTPTradeSpi::OnRspUserLogout(CThostFtdcUserLogoutField* pUserLogout, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
            LOG_INFO << "logout ok";
        } else {
            LOG_ERROR << "logout failed: ret=" << pRspInfo->ErrorID << ", msg=" << CtpToUTF8(pRspInfo->ErrorMsg);
        }
    }

    /// 客户端认证响应//
    void CTPTradeSpi::OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
            LOG_INFO << "authenticate ok";
            ReqUserLogin();
        } else {
            LOG_ERROR << "authenticate failed: " << CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
        }
    }

    /// 登录请求响应//
    void CTPTradeSpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
            date_ = atoi(api_->GetTradingDay());
            front_id_ = pRspUserLogin->FrontID;
            session_id_ = pRspUserLogin->SessionID;
            LOG_INFO << "login ok: trading_day = " << date_ << ", front_id = " << front_id_ << ", session_id = " << session_id_ << ", max_order_ref = " << pRspUserLogin->MaxOrderRef;
            if (IsMonday(date_)) {
                pre_trading_day_ = x::PreDay(date_, 3);
                pre_trading_day_next_ = x::PreDay(date_, 2);
            } else {
                pre_trading_day_ = x::PreDay(date_);
                pre_trading_day_next_ = date_;
            }
            LOG_INFO << "pre_trading_day: " << pre_trading_day_ << ", pre_trading_day_next: " << pre_trading_day_next_;
            if (date_ < 19700101 || date_ > 29991231) {
                LOG_ERROR << "illegal trading day: " << date_;
            } else {
                ReqSettlementInfoConfirm();
            }
        } else {
            LOG_ERROR << "login failed: " << CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
        }
    }

    void CTPTradeSpi::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
            string date = pSettlementInfoConfirm ? pSettlementInfoConfirm->ConfirmDate : "";
            LOG_INFO << "confirm settlement info ok: confirm_date = " << date;
        } else {
            LOG_WARN << "confirm settlement info failed: " << CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
        }
        ReqQryInstrument();
    }

    void CTPTradeSpi::OnRspQryInstrument(CThostFtdcInstrumentField* p, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
            if (p) {
                string ctp_code = p->InstrumentID;
                int64_t market = ctp_market2std(p->ExchangeID);
                if (market == co::kMarketCZCE) {
                    InsertCzceCode(ctp_code);
                }
                if (market) {
                    string suffix = MarketToSuffix(market).data();
                    string code = ctp_code + suffix;
                    int multiple = p->VolumeMultiple > 0 ? p->VolumeMultiple : 1;
                    all_instruments_.insert(make_pair(code, make_pair(x::GBKToUTF8(x::Trim(p->InstrumentName)), multiple)));
                }
            }
            if (bIsLast) {
                LOG_INFO << "query all future contracts ok: contracts = " << all_instruments_.size();
                query_instruments_finish_.store(true);
            }
        } else {
            LOG_ERROR << "query all future contracts failed: " << CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
        }
    }

    void CTPTradeSpi::OnRspQryTradingAccount(CThostFtdcTradingAccountField* pTradingAccount, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        try {
            if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
                if (pTradingAccount) {
                    memcpy(&accout_field_, pTradingAccount, sizeof(CThostFtdcTradingAccountField));
                }
            }

            if (bIsLast) {
                string req_message;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = query_msg_.find(nRequestID);
                    if (it != query_msg_.end()) {
                        req_message = it->second;
                        query_msg_.erase(it);
                    } else {
                        LOG_ERROR << "OnRspQryTradingAccount, not find nRequestID: " << nRequestID;
                    }
                }

                if (req_message.empty()) {
                    LOG_ERROR << "OnRspQryTradingAccount, req_message is empty. ";
                    return;
                }
                int total_num = 1;
                string error;
                if (pRspInfo && pRspInfo->ErrorID != 0) {
                    total_num = 0;
                    error = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
                }
                int length = sizeof(MemGetTradeAssetMessage) + sizeof(MemTradeAsset) * total_num;
                char buffer[length] = "";
                MemGetTradeAssetMessage* rep = (MemGetTradeAssetMessage*)buffer;
                memcpy(rep, req_message.data(), sizeof(MemGetTradeAssetMessage));
                rep->items_size = total_num;
                if (total_num == 1) {
                    MemTradeAsset* item = rep->items + 0;
                    strcpy(item->fund_id, investor_id_.c_str());
                    item->balance = 0;
                    item->usable = accout_field_.Available;
                    item->margin = accout_field_.CurrMargin;  // 保证金 = 占用保证金 + 冻结保证金//
                    item->equity = ctp_equity(&accout_field_);
                } else {
                    strcpy(rep->error, error.c_str());
                }
                string raw = string(reinterpret_cast<char*>(buffer), length);
                broker_->SendRtnMessage(raw, kMemTypeQueryTradeAssetRep);
            }
        } catch (std::exception& e) {
            LOG_ERROR << "OnRspQryTradingAccount: " << e.what();
        }
    }

// Position：表示当前持仓数量
// TodayPosition：表示今新开仓
// YdPosition：表示昨日收盘时持仓数量（≠ 当前的昨仓数量，静态，日间不随着开平仓而变化）
// 当前的昨仓数量 = ∑Position -∑TodayPosition

/* 有2手昨仓, 开了10手今仓，再平掉一手今天后的日志
InstrumentID: rb2510, 多空方向: 3, 当前的昨仓数量: 0, YdPosition: 0, Position: 1, TodayPosition: 1, OpenVolume: 1, CloseVolume: 0, LongFrozen: 0, ShortFrozen: 0
InstrumentID: rb2510, 多空方向: 2, 当前的昨仓数量: 0, YdPosition: 0, Position: 9, TodayPosition: 9, OpenVolume: 10, CloseVolume: 1, LongFrozen: 1, ShortFrozen: 0
InstrumentID: rb2510, 多空方向: 2, 当前的昨仓数量: 0, YdPosition: 2, Position: 0, TodayPosition: 0, OpenVolume: 0, CloseVolume: 2, LongFrozen: 0, ShortFrozen: 0
*/
    void CTPTradeSpi::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        try {
            if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
                if (pInvestorPosition) {
                    LOG_INFO << "InstrumentID: " << pInvestorPosition->InstrumentID
                        << ", 多空方向: " << pInvestorPosition->PosiDirection
                        << ", 当前的昨仓数量: " << pInvestorPosition->Position - pInvestorPosition->TodayPosition
                        << ", YdPosition: " << pInvestorPosition->YdPosition
                        << ", Position: " << pInvestorPosition->Position
                        << ", TodayPosition: " << pInvestorPosition->TodayPosition
                        << ", OpenVolume: " << pInvestorPosition->OpenVolume
                        << ", CloseVolume: " << pInvestorPosition->CloseVolume
                        << ", LongFrozen: " << pInvestorPosition->LongFrozen
                        << ", ShortFrozen: " << pInvestorPosition->ShortFrozen;
                    string ctp_code = pInvestorPosition->InstrumentID;
                    int64_t market = ctp_market2std(pInvestorPosition->ExchangeID);
                    if (market == co::kMarketCZCE) {
                        InsertCzceCode(ctp_code);
                    }
                    string suffix = MarketToSuffix(market).data();
                    string code = ctp_code + suffix;
                    int64_t bs_flag = ctp_ls_flag2std(pInvestorPosition->PosiDirection);
                    auto it = all_pos_.find(code);
                    if (it == all_pos_.end()) {
                        std::unique_ptr<MemTradePosition> item = std::make_unique<MemTradePosition>();
                        item->timestamp = x::RawDateTime();
                        strcpy(item->fund_id, investor_id_.c_str());
                        strcpy(item->code, code.c_str());
                        item->market = market;
                        if (auto itr = all_instruments_.find(code); itr != all_instruments_.end()) {
                            strcpy(item->name, itr->second.first.c_str());
                        }
                        all_pos_.insert(std::make_pair(code, std::move(item)));
                        it = all_pos_.find(code);
                    }
                    // YdPositionYdPositionYdPositionYdPosition YdPosition 表示昨日收盘时持仓数量(静态数值, 日间不随着开平而变化)//
                    // 当前的昨持仓 = 当前持仓数量 - 今开仓数量//
                    if (bs_flag == kBsFlagBuy) {
                        it->second->long_volume += pInvestorPosition->Position;
                        if (pInvestorPosition->YdPosition > 0) {
                            it->second->long_pre_volume = (pInvestorPosition->Position - pInvestorPosition->TodayPosition); // 昨持仓, 此刻还有多少
                        }
                    } else if (bs_flag == kBsFlagSell) {
                        it->second->short_volume += pInvestorPosition->Position;
                        it->second->short_pre_volume = (pInvestorPosition->Position - pInvestorPosition->TodayPosition);
                    }
                }
            } else {
                rsp_query_msg_ = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
            }

            if (bIsLast) {
                for (auto it = all_pos_.begin(); it != all_pos_.end(); ++it) {
                    LOG_INFO << it->second->code
                        << ", long_volume: " << it->second->long_volume
                        << ", long_pre_volume: " << it->second->long_pre_volume
                        << ", short_volume: " << it->second->short_volume
                        << ", short_pre_volume: " << it->second->short_pre_volume;
                }

                string req_message;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = query_msg_.find(nRequestID);
                    if (it != query_msg_.end()) {
                        req_message = it->second;
                        query_msg_.erase(it);
                    } else {
                        LOG_ERROR << "OnRspQryInvestorPosition, not find nRequestID: " << nRequestID;
                    }
                }

                if (req_message.empty()) {
                    LOG_ERROR << "OnRspQryInvestorPosition, req_message is empty. ";
                    return;
                }
                int total_num = all_pos_.size();
                int length = sizeof(MemGetTradePositionMessage) + sizeof(MemTradePosition) * total_num;
                char buffer[length] = "";
                MemGetTradePositionMessage* rep = (MemGetTradePositionMessage*)buffer;
                memcpy(rep, req_message.data(), sizeof(MemGetTradePositionMessage));
                strcpy(rep->error, rsp_query_msg_.c_str());
                rep->items_size = all_pos_.size();
                int i = 0;
                for (auto it = all_pos_.begin(); it != all_pos_.end(); ++it) {
                    MemTradePosition *pos = rep->items + i;
                    memcpy(pos, it->second.get(), sizeof(MemTradePosition));
                    i++;
                }
                broker_->SendRtnMessage(string(buffer, length), kMemTypeQueryTradePositionRep);
            }
        } catch (std::exception& e) {
            LOG_ERROR << "OnRspQryInvestorPosition: " << e.what();
        }
    }

    /// 请求查询报单响应//
    void CTPTradeSpi::OnRspQryOrder(CThostFtdcOrderField* pOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    }

    /// 请求查询成交响应//
    void CTPTradeSpi::OnRspQryTrade(CThostFtdcTradeField* pTrade, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        try {
            if (pRspInfo == NULL || pRspInfo->ErrorID == 0) {
                if (pTrade) {
                    string order_sys_id = x::Trim(pTrade->OrderSysID);
                    string order_no;
                    map<string, string>::iterator itr_order_no = order_nos_.find(order_sys_id);
                    if (itr_order_no != order_nos_.end()) {
                        order_no = itr_order_no->second;
                    }
                    if (!order_no.empty()) {
                        string ctp_code = pTrade->InstrumentID;
                        int64_t market = ctp_market2std(pTrade->ExchangeID);
                        if (market == co::kMarketCZCE) {
                            InsertCzceCode(ctp_code);
                        }
                        string suffix = MarketToSuffix(market).data();
                        string code = ctp_code + suffix;
                        string match_no = x::Trim(pTrade->TradingDay) + "_" + x::Trim(pTrade->TradeID);
                        double match_amount = pTrade->Price * pTrade->Volume;
                        string name;
                        auto it = all_instruments_.find(code);
                        if (it != all_instruments_.end()) {
                            name = it->second.first;
                            match_amount = match_amount * it->second.second;
                        }

                        std::unique_ptr<MemTradeKnock> item = std::make_unique<MemTradeKnock>();
                        strcpy(item->fund_id, investor_id_.c_str());
                        strcpy(item->code, code.c_str());
                        strcpy(item->name, name.c_str());
                        strcpy(item->order_no, order_no.c_str());
                        strcpy(item->match_no, match_no.c_str());

                        if (strcmp(pTrade->TradeTime, "06:00:00") > 0 && strcmp(pTrade->TradeTime, "18:00:00") <= 0) {
                            item->timestamp = CtpTimestamp(atoll(pTrade->TradingDay), pTrade->TradeTime);
                        } else if (strcmp(pTrade->TradeTime, "18:00:00") > 0 && strcmp(pTrade->TradeTime, "23:59:59") <= 0) {
                            item->timestamp = CtpTimestamp(pre_trading_day_, pTrade->TradeTime);
                        } else {
                            item->timestamp = CtpTimestamp(pre_trading_day_next_, pTrade->TradeTime);
                        }
                        item->market = market;
                        item->bs_flag = ctp_bs_flag2std(pTrade->Direction);
                        item->oc_flag = ctp_oc_flag2std(pTrade->OffsetFlag);
                        item->match_type = kMatchTypeOK;
                        item->match_volume = pTrade->Volume;
                        item->match_price = pTrade->Price;
                        item->match_amount = match_amount;
                        all_knock_.emplace_back(std::move(item));
                    } else {
                        LOG_WARN << "ignore knock because no order_no found of order_sys_id: " << order_sys_id;
                    }
                    query_cursor_ = pTrade->TradeTime;
                }
            } else {
                rsp_query_msg_ = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
            }

            if (bIsLast) {
                string req_message;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = query_msg_.find(nRequestID);
                    if (it != query_msg_.end()) {
                        req_message = it->second;
                        query_msg_.erase(it);
                    } else {
                        LOG_ERROR << "OnRspQryTrade, not find nRequestID: " << nRequestID;
                    }
                }
                if (req_message.empty()) {
                    LOG_ERROR << "OnRspQryTrade, req_message is empty. ";
                    return;
                }

                int total_num = all_pos_.size();
                int length = sizeof(MemGetTradeKnockMessage) + sizeof(MemTradeKnock) * total_num;
                char buffer[length] = "";
                MemGetTradeKnockMessage* rep = (MemGetTradeKnockMessage*)buffer;
                memcpy(rep, req_message.data(), sizeof(MemGetTradeKnockMessage));
                strcpy(rep->error, rsp_query_msg_.c_str());
                strcpy(rep->next_cursor, query_cursor_.c_str());
                int i = 0;
                for (auto it = all_pos_.begin(); it != all_pos_.end(); ++it) {
                    MemTradeKnock *pos = rep->items + i++;
                    memcpy(pos, it->second.get(), sizeof(MemTradeKnock));
                }
                broker_->SendRtnMessage(string(buffer, length), kMemTypeQueryTradeKnockRep);
            }
        } catch (std::exception& e) {
            LOG_ERROR << "OnRspQryTrade: " << e.what();
        }
    }

    /// 报单录入请求响应(CTP打回的废单会通过该函数返回)
    void CTPTradeSpi::OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        if (pInputOrder) {
            LOG_INFO << __FUNCTION__ << ", InstrumentID: " << pInputOrder->InstrumentID
                << ", OrderRef: " << pInputOrder->OrderRef
                << ", ExchangeID: " << pInputOrder->ExchangeID
                << ", Direction: " << pInputOrder->Direction
                << ", CombOffsetFlag: " << pInputOrder->CombOffsetFlag
                << ", VolumeTotalOriginal: " << pInputOrder->VolumeTotalOriginal
                << ", nRequestID: " << nRequestID;
        }

        try {
            string req_message;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = req_msg_.find(nRequestID);
                if (it != req_msg_.end()) {
                    req_message = it->second;
                    req_msg_.erase(it);
                } else {
                    LOG_ERROR << "OnRspOrderInsert, not find nRequestID: " << nRequestID;
                }
            }

            if (req_message.empty()) {
                LOG_ERROR << "OnRspOrderInsert, req_message is empty. ";
                return;
            }
            MemTradeOrderMessage* req = (MemTradeOrderMessage*)req_message.data();
            int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * req->items_size;
            char buffer[length] = "";
            memcpy(buffer, req, length);
            MemTradeOrderMessage* rep = (MemTradeOrderMessage*)buffer;
            string error = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
            strcpy(rep->error, error.c_str());
            broker_->SendRtnMessage(string(buffer, length), kMemTypeTradeOrderRep);
        } catch (std::exception& e) {
            LOG_ERROR << "OnOrderTicketError: " << e.what();
        }
    }

    // 交易所打回的废单会通过该函数返回//
    void CTPTradeSpi::OnErrRtnOrderInsert(CThostFtdcInputOrderField* pInputOrder, CThostFtdcRspInfoField* pRspInfo) {
        if (pInputOrder) {
            LOG_INFO << "OnErrRtnOrderInsert, InstrumentID: " << pInputOrder->InstrumentID
                << ", OrderRef: " << pInputOrder->OrderRef
                << ", Direction: " << pInputOrder->Direction
                << ", LimitPrice: " << pInputOrder->LimitPrice
                << ", VolumeTotalOriginal: " << pInputOrder->VolumeTotalOriginal
                << ", ExchangeID: " << pInputOrder->ExchangeID
                << ", CombOffsetFlag: " << pInputOrder->CombOffsetFlag
                << ", nRequestID: " << pInputOrder->RequestID;
        }

        try {
            string req_message;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = req_msg_.find(pInputOrder->RequestID);
                if (it != req_msg_.end()) {
                    req_message = it->second;
                    req_msg_.erase(it);
                } else {
                    LOG_ERROR << "OnErrRtnOrderInsert, not find nRequestID: " << pInputOrder->RequestID;
                    return;
                }
            }

            if (req_message.empty()) {
                LOG_ERROR << "OnErrRtnOrderInsert, req_message is empty. ";
                return;
            }
            MemTradeOrderMessage* req = (MemTradeOrderMessage*)req_message.data();
            int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * req->items_size;
            char buffer[length] = "";
            memcpy(buffer, req, length);
            MemTradeOrderMessage* rep = (MemTradeOrderMessage*)buffer;
            if (pRspInfo && pRspInfo->ErrorID != 0) {
                string error = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
                strcpy(rep->error, error.c_str());
            }
            broker_->SendRtnMessage(string(buffer, length), kMemTypeTradeOrderRep);
        } catch (std::exception& e) {
            LOG_ERROR << "OnOrderTicketError: " << e.what();
        }
    }

    /// 报单操作请求响应//
    void CTPTradeSpi::OnRspOrderAction(CThostFtdcInputOrderActionField* pInputOrderAction, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        try {
            LOG_INFO << __FUNCTION__ << ", nRequestID: " << nRequestID << ", ErrorId: " << pRspInfo->ErrorID;
            string req_message;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = req_msg_.find(nRequestID);
                if (it != req_msg_.end()) {
                    req_message = it->second;
                    req_msg_.erase(it);
                } else {
                    LOG_ERROR << "not find nRequestID: " << nRequestID;
                }
            }

            if (req_message.empty()) {
                LOG_ERROR << "req_message is empty. ";
                return;
            }
            MemTradeWithdrawMessage* req = (MemTradeWithdrawMessage*)req_message.data();
            int length = sizeof(MemTradeWithdrawMessage);
            char buffer[length] = "";
            memcpy(buffer, req, length);
            MemTradeWithdrawMessage* rep = (MemTradeWithdrawMessage*)buffer;
            if (pRspInfo && pRspInfo->ErrorID != 0) {
                string error = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
                strcpy(rep->error, error.c_str());
            }
            broker_->SendRtnMessage(string(buffer, length), kMemTypeTradeWithdrawRep);
        } catch (std::exception& e) {
            LOG_ERROR << "OnRspOrderAction: " << e.what();
        }
    }

    void CTPTradeSpi::OnErrRtnOrderAction(CThostFtdcOrderActionField* pOrderAction, CThostFtdcRspInfoField* pRspInfo) {
        try {
            LOG_INFO << __FUNCTION__ << ", nRequestID: " << pOrderAction->RequestID << ", ErrorId: " << pRspInfo->ErrorID;

            string req_message;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = req_msg_.find(pOrderAction->RequestID);
                if (it != req_msg_.end()) {
                    req_message = it->second;
                    req_msg_.erase(it);
                } else {
                    LOG_ERROR << "not find nRequestID: " << pOrderAction->RequestID;
                }
            }

            if (req_message.empty()) {
                LOG_ERROR << "req_message is empty. ";
                return;
            }
            MemTradeWithdrawMessage* req = (MemTradeWithdrawMessage*)req_message.data();
            int length = sizeof(MemTradeWithdrawMessage);
            char buffer[length] = "";
            memcpy(buffer, req, length);
            MemTradeWithdrawMessage* rep = (MemTradeWithdrawMessage*)buffer;
            if (pRspInfo && pRspInfo->ErrorID != 0) {
                string error = CtpError(pRspInfo->ErrorID, pRspInfo->ErrorMsg);
                strcpy(rep->error, error.c_str());
            }
            broker_->SendRtnMessage(string(buffer, length), kMemTypeTradeWithdrawRep);
        } catch (std::exception& e) {
            LOG_ERROR << "OnErrRtnOrderAction: " << e.what();
        }
    }

/// 报单通知, 报单成功或状态变化
// 报单流程：
// 1.客户端调用ReqOrderInsert进行报单;
// 2.CTP接收到报单请求
//   2.1 如果CTP认为报单成功, 会回调一次OnRtnOrder, 此时有FrontID、SessionId和OrderRef, 此时没有OrderSysID;
//   2.2 如果CTP认为报单失败, 会回调OnRspOrderInsert返回报单失败;
// 3.CTP将委托报给交易所
//   3.1如果交易所返回报单成功, 则会再回调一次OnRtnOrder, 此时会有OrderSysId;
//   3.2如果交易所返回报单失败（比如超出涨跌停价）, 也会再调用一次OnRtnOrder, 此时没有OrderSysId, 接着还会再调用一次OnErrRtnOrderInsert
//  RequestID的值是0
    void CTPTradeSpi::OnRtnOrder(CThostFtdcOrderField* pOrder) {
        if (pOrder) {
            LOG_INFO << "OnRtnOrder, InstrumentID: " << pOrder->InstrumentID
                << ", ExchangeID: " << pOrder->ExchangeID
                << ", FrontID: " << pOrder->FrontID
                << ", SessionID: " << pOrder->SessionID
                << ", OrderRef: " << pOrder->OrderRef
                << ", Direction: " << pOrder->Direction
                << ", CombOffsetFlag: " << pOrder->CombOffsetFlag
                << ", RequestID: " << pOrder->RequestID
                << ", OrderSysID: " << pOrder->OrderSysID
                << ", OrderStatus: " << pOrder->OrderStatus
                << ", OrderSubmitStatus: " << pOrder->OrderSubmitStatus
                << ", TradingDay: " << pOrder->TradingDay
                << ", GTDDate: " << pOrder->GTDDate
                << ", InsertDate: " << pOrder->InsertDate
                << ", InsertTime: " << pOrder->InsertTime
                << ", CancelTime: " << pOrder->CancelTime
                << ", ActiveTime: " << pOrder->ActiveTime
                << ", SuspendTime: " << pOrder->SuspendTime
                << ", UpdateTime: " << pOrder->UpdateTime
                << ", LimitPrice: " << pOrder->LimitPrice
                << ", VolumeTraded: " << pOrder->VolumeTraded
                << ", VolumeTotal: " << pOrder->VolumeTotal;
        }

        try {
            // 委托合同号: <前置编号>_<会话编号>_<报单引用>_<代码>//
            string order_sys_id = x::Trim(pOrder->OrderSysID);
            int64_t order_ref = atoi(pOrder->OrderRef);
            string ctp_code = pOrder->InstrumentID;
            stringstream ss;
            ss << pOrder->FrontID << "_" << pOrder->SessionID << "_" << order_ref << "_" << ctp_code;
            string order_no = ss.str();
            if (!order_sys_id.empty()) {
                order_nos_[order_sys_id] = order_no;
            }
            int64_t order_state = ctp_order_state2std(pOrder->OrderStatus, pOrder->OrderSubmitStatus);

            if (order_state == kOrderPartlyCanceled || order_state == kOrderFullyCanceled) {
                std::unique_lock<std::mutex> lock(mutex_);
                if (auto iter = withdraw_msg_.find(order_no); iter != withdraw_msg_.end()) {
                    broker_->SendRtnMessage(iter->second, kMemTypeTradeWithdrawRep);
                    withdraw_msg_.erase(iter);
                }
            } else {
                int RequestID = atol(x::Trim(pOrder->OrderRef).c_str());
                string req_message = "";
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = req_msg_.find(RequestID);
                    if (it != req_msg_.end()) {
                        req_message = it->second;
                        req_msg_.erase(it);
                    }
                }

                if (req_message.length()) {
                    MemTradeOrderMessage* req = (MemTradeOrderMessage*)req_message.data();
                    int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * req->items_size;
                    char buffer[length] = "";
                    memcpy(buffer, req, length);
                    MemTradeOrderMessage* rep = (MemTradeOrderMessage*)buffer;
                    auto order = rep->items + 0;
                    strcpy(order->order_no, order_no.c_str());
                    broker_->SendRtnMessage(string(buffer, length), kMemTypeTradeOrderRep);
                }
            }

            // -------------------------------------------------------------------------
            int64_t market = ctp_market2std(pOrder->ExchangeID);
            if (market == co::kMarketCZCE) {
                InsertCzceCode(ctp_code);
            }
            string suffix = MarketToSuffix(market).data();
            string code = ctp_code + suffix;

            if (order_state == kOrderPartlyCanceled || order_state == kOrderFullyCanceled || order_state == kOrderFailed) {
                char buffer[sizeof(MemTradeKnock)] = "";
                MemTradeKnock* knock = (MemTradeKnock*)buffer;
                if (order_state == kOrderFailed) {
                    knock->timestamp = x::RawDateTime();
                } else {
                    if (strcmp(pOrder->CancelTime, "06:00:00") > 0 && strcmp(pOrder->CancelTime, "18:00:00") <= 0) {
                        knock->timestamp = CtpTimestamp(date_, pOrder->CancelTime);
                    } else if (strcmp(pOrder->CancelTime, "18:00:00") > 0 && strcmp(pOrder->CancelTime, "23:59:59") <= 0) {
                        knock->timestamp = CtpTimestamp(pre_trading_day_, pOrder->CancelTime);
                    } else {
                        knock->timestamp = CtpTimestamp(pre_trading_day_next_, pOrder->CancelTime);
                    }
                }
                string match_no = "_" + order_no;
                strcpy(knock->fund_id, investor_id_.c_str());
                strcpy(knock->code, code.c_str());
                knock->market = market;
                auto it = all_instruments_.find(code);
                if (it != all_instruments_.end()) {
                    strcpy(knock->name, it->second.first.c_str());
                }
                strcpy(knock->order_no, order_no.c_str());
                strcpy(knock->match_no, match_no.c_str());
                knock->bs_flag = ctp_bs_flag2std(pOrder->Direction);
                knock->oc_flag = ctp_oc_flag2std(pOrder->CombOffsetFlag[0]);
                if (order_state == kOrderPartlyCanceled || order_state == kOrderFullyCanceled) {
                    knock->match_type = kMatchTypeWithdrawOK;
                    knock->match_volume = pOrder->VolumeTotalOriginal - pOrder->VolumeTraded;;
                } else {
                    knock->match_type = kMatchTypeFailed;
                    knock->match_volume = pOrder->VolumeTotalOriginal;
                    if (order_state == kOrderFailed) {
                        string error = x::GBKToUTF8(x::Trim(pOrder->StatusMsg));
                        strcpy(knock->error, error.c_str());
                    }
                }
                knock->match_price = 0;
                knock->match_amount = 0;
                broker_->SendRtnMessage(string(buffer, sizeof(MemTradeKnock)), kMemTypeTradeKnock);
            }
        } catch (std::exception& e) {
            LOG_ERROR << "OnRtnOrder: " << e.what();
        }
    }

    /// 成交通知(测试发现, 委托状态更新比成交数据更快)
    void CTPTradeSpi::OnRtnTrade(CThostFtdcTradeField* pTrade) {
        if (!query_instruments_finish_.load()) {
            LOG_INFO << "query instrument not finish";
            return;
        }
        if (pTrade) {
            LOG_INFO << "OnRtnTrade, InstrumentID: " << pTrade->InstrumentID
                << ", OrderRef: " << pTrade->OrderRef
                << ", Direction: " << pTrade->Direction
                << ", CombOffsetFlag: " << pTrade->OffsetFlag
                << ", TradeID: " << pTrade->TradeID
                << ", OrderSysID: " << pTrade->OrderSysID
                << ", Price: " << pTrade->Price
                << ", Volume: " << pTrade->Volume
                << ", TradeTime: " << pTrade->TradeTime
                << ", TradeDate: " << pTrade->TradeDate
                << ", TradingDay: " << pTrade->TradingDay;
        }

        try {
            // CTP推过来的成交数据中没有FrontId和SessionId, 无法生成委托合同号, 需要根据OrderSysId从映射表中查找//
            string order_sys_id = x::Trim(pTrade->OrderSysID);
            string match_no = x::Trim(pTrade->TradeID);
            map<string, string>::iterator itr_order_no = order_nos_.find(order_sys_id);
            if (itr_order_no != order_nos_.end()) {
                string order_no = itr_order_no->second;
                string ctp_code = pTrade->InstrumentID;
                int64_t market = ctp_market2std(pTrade->ExchangeID);
                if (market == co::kMarketCZCE) {
                    InsertCzceCode(ctp_code);
                }
                string suffix = MarketToSuffix(market).data();
                string code = ctp_code + suffix;
                string match_no = x::Trim(pTrade->TradingDay) + "_" + x::Trim(pTrade->TradeID);
                double match_amount = pTrade->Price * pTrade->Volume;
                string name;
                auto it = all_instruments_.find(code);
                if (it != all_instruments_.end()) {
                    name = it->second.first;
                    match_amount = match_amount * it->second.second;
                }
                char buffer[sizeof(MemTradeKnock)] = "";
                MemTradeKnock* knock = (MemTradeKnock*)buffer;
                if (strcmp(pTrade->TradeTime, "06:00:00") > 0 && strcmp(pTrade->TradeTime, "18:00:00") <= 0) {
                    knock->timestamp = CtpTimestamp(date_, pTrade->TradeTime);
                } else if (strcmp(pTrade->TradeTime, "18:00:00") > 0 && strcmp(pTrade->TradeTime, "23:59:59") <= 0) {
                    knock->timestamp = CtpTimestamp(pre_trading_day_, pTrade->TradeTime);
                } else {
                    // 00:00:01 �� 06:00:00
                    knock->timestamp = CtpTimestamp(pre_trading_day_next_, pTrade->TradeTime);
                }
                // knock->trade_type = kTradeTypeFuture;
                strcpy(knock->fund_id, investor_id_.c_str());
                strcpy(knock->code, code.c_str());
                strcpy(knock->name, name.c_str());
                strcpy(knock->order_no, order_no.c_str());
                strcpy(knock->match_no, match_no.c_str());
                knock->bs_flag = ctp_bs_flag2std(pTrade->Direction);
                knock->oc_flag = ctp_oc_flag2std(pTrade->OffsetFlag);
                knock->match_type = kMatchTypeOK;
                knock->match_volume = pTrade->Volume;
                knock->match_price = pTrade->Price;
                knock->match_amount = match_amount;
                broker_->SendRtnMessage(string(buffer, sizeof(MemTradeKnock)), kMemTypeTradeKnock);
            } else {
                LOG_WARN << "no order_no found of knock: order_sys_id = " << order_sys_id << ", match_no = " << match_no;
            }
        } catch (std::exception& e) {
            LOG_ERROR << "rec knock failed: " << e.what();
        }
    }

    /// 错误应答
    void CTPTradeSpi::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
        LOG_ERROR << "OnRspError, ret: " << pRspInfo->ErrorID << ", msg: " << CtpToUTF8(pRspInfo->ErrorMsg);
    }

    void CTPTradeSpi::Start() {
        string ctp_app_id = Config::Instance()->ctp_app_id();
        if (!ctp_app_id.empty()) {
            ReqAuthenticate();
        } else {
            ReqUserLogin();
        }
    }

    void CTPTradeSpi::Wait() {
        while (!query_instruments_finish_.load()) {
            x::Sleep(100);
        }
    }

    int CTPTradeSpi::GetRequestID() {
        return ++start_index_;
    }

}  // namespace co
