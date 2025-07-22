#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <x/x.h>
#include "mem_broker/mem_base_broker.h"
#include "risker/risk_options.h"
using namespace std;

namespace co {

    class Config {
    public:
        static Config* Instance();

        inline string ctp_trade_front() {
            return ctp_trade_front_;
        }
        inline string ctp_broker_id() {
            return ctp_broker_id_;
        }
        inline string ctp_investor_id() {
            return ctp_investor_id_;
        }
        inline string ctp_password() {
            return ctp_password_;
        }
        inline string ctp_app_id() {
            return ctp_app_id_;
        }
        inline string ctp_product_info() {
            return ctp_product_info_;
        }
        inline string ctp_auth_code() {
            return ctp_auth_code_;
        }

        inline MemBrokerOptionsPtr options() {
            return options_;
        }
        const std::vector<std::shared_ptr<RiskOptions>>& risk_opt() const {
            return risk_opts_;
        }

        inline bool disable_subscribe() {
            return disable_subscribe_;
        }

    protected:
        Config() = default;
        ~Config() = default;
        Config(const Config&) = delete;
        const Config& operator=(const Config&) = delete;

        void Init();

    private:
        static Config* instance_;

        MemBrokerOptionsPtr options_;
        std::vector<std::shared_ptr<RiskOptions>> risk_opts_;

        string ctp_trade_front_;

        string ctp_broker_id_;
        string ctp_investor_id_;
        string ctp_password_;
        string ctp_app_id_;
        string ctp_product_info_;
        string ctp_auth_code_;

        bool disable_subscribe_ = false;
    };

}