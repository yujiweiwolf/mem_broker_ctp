#include <regex>
#include <boost/filesystem.hpp>
#include "../libbroker_ctp/ctp_broker.h"
#include "../libbroker_ctp/ctp_trade_spi.h"
using namespace co;
using namespace std;

string fund_id;
string mem_dir;
string mem_req_file;
string mem_rep_file;

void order_shfe(shared_ptr<MemBaseBroker> broker) {
    int total_order_num = 1;
    string id = x::UUID();
    int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * total_order_num;
    char buffer[length] = "";
    MemTradeOrderMessage* msg = (MemTradeOrderMessage*) buffer;
    strncpy(msg->id, id.c_str(), id.length());
    strcpy(msg->fund_id, fund_id.c_str());
    msg->bs_flag = kBsFlagBuy;
    msg->items_size = total_order_num;
    MemTradeOrder* item = (MemTradeOrder*)((char*)buffer + sizeof(MemTradeOrderMessage));
    for (int i = 0; i < total_order_num; i++) {
        MemTradeOrder* order = item + i;
        order->volume = 100;
        order->market = kMarketCFFEX;
        order->price = 10.01 + 0.01 * i;
        order->price_type = kQOrderTypeLimit;
        strcpy(order->code, "IC2506.CFFEX");
        LOG_INFO << "send order, code: " << order->code << ", volume: " << order->volume << ", price: " << order->price;
    }
    msg->timestamp = x::RawDateTime();
    broker->SendTradeOrder(msg);
}

void withdraw(shared_ptr<MemBaseBroker> broker) {
    string id = x::UUID();
    MemTradeWithdrawMessage msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.id, id.c_str(), id.length());
    strcpy(msg.fund_id, fund_id.c_str());
    cout << "please input order_no" << endl;
    cin >> msg.order_no;
    LOG_INFO << "send withdraw, fund_id: " << msg.fund_id << ", order_no: " << msg.order_no;
    msg.timestamp = x::RawDateTime();
    broker->SendTradeWithdraw(&msg);
}

// BUY OPEN IC2507.CFFEX 1 6809.9
// BUY CLOSE IC2507.CFFEX 1 6809.9
// BUY AUTO IC2507.CFFEX 1 6809.9
// BUY SELL IC2507.CFFEX 1 6809.9
void order(shared_ptr<MemBaseBroker> broker) {
    int total_order_num = 1;
    string id = x::UUID();
    int length = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * total_order_num;
    char buffer[length] = "";
    MemTradeOrderMessage* msg = (MemTradeOrderMessage*) buffer;
    strncpy(msg->id, id.c_str(), id.length());
    strcpy(msg->fund_id, fund_id.c_str());
    int64_t bs_flag = 0;
    msg->items_size = total_order_num;
    MemTradeOrder* item = (MemTradeOrder*)((char*)buffer + sizeof(MemTradeOrderMessage));
    for (int i = 0; i < total_order_num; i++) {
        MemTradeOrder* order = item + i;
        getchar();
        cout << "please input : BUY OPEN IC2507.CFFEX 1 6809.9\n";
        std::string input;
        string code;
        getline(std::cin, input);
        std::cout << "your input is: # " << input << " #" << std::endl;
        std::smatch result;
        if (regex_match(input, result, std::regex("^(BUY|SELL) (OPEN|CLOSE|AUTO) ([0-9A-Za-z]{1,10})\.([A-Z]{1,10}) ([0-9]{1,10}) ([.0-9]{1,10})$")))
        {
            string command = result[1].str();
            string oc = result[2].str();
            string instrument = result[3].str();
            string market = result[4].str();
            string volume = result[5].str();
            string price = result[6].str();
            if (command == "BUY") {
                bs_flag = kBsFlagBuy;
            } else if (command == "SELL") {
                bs_flag = kBsFlagSell;
            }

            if (oc == "OPEN") {
                order->oc_flag = co::kOcFlagOpen;
            } else if (oc == "CLOSE") {
                order->oc_flag = co::kOcFlagClose;
            } else {
                order->oc_flag = co::kOcFlagAuto;
            }

            if (market == "CFFEX") {
                order->market = kMarketCFFEX;
                code = instrument + "." + market;
            } else if (market == "DCE") {
                order->market = kMarketDCE;
                code = instrument + "." + market;
            } else if (market == "CZCE") {
                order->market = kMarketDCE;
                code = instrument + "." + market;
            } else if (market == "SHFE") {
                order->market = kMarketSHFE;
                code = instrument + "." + market;
            } else if (market == "INE") {
                order->market = kMarketINE;
                code = instrument + "." + market;
            }
            order->volume = atoll(volume.c_str());
            order->price = atof(price.c_str());
            strcpy(order->code, code.c_str());
        } else {
            LOG_ERROR << "parse failed!";
            return;
        }
        LOG_INFO << "send order, code: " << order->code << ", volume: " << order->volume << ", price: " << order->price;
    }
    msg->bs_flag = bs_flag;
    msg->timestamp = x::RawDateTime();
    broker->SendTradeOrder(msg);
}

void query_asset(shared_ptr<MemBaseBroker> broker) {
    string id = x::UUID();
    MemGetTradeAssetMessage msg {};
    strncpy(msg.id, id.c_str(), id.length());
    strcpy(msg.fund_id, fund_id.c_str());
    msg.timestamp = x::RawDateTime();
    broker->SendQueryTradeAsset(&msg);
}

void query_position(shared_ptr<MemBaseBroker> broker) {
    string id = x::UUID();
    MemGetTradePositionMessage msg {};
    strncpy(msg.id, id.c_str(), id.length());
    strcpy(msg.fund_id, fund_id.c_str());
    msg.timestamp = x::RawDateTime();
    broker->SendQueryTradePosition(&msg);
}

void query_knock(shared_ptr<MemBaseBroker> broker) {
    string id = x::UUID();
    MemGetTradeKnockMessage msg {};
    strncpy(msg.id, id.c_str(), id.length());
    strcpy(msg.fund_id, fund_id.c_str());
    string cursor;
    cout << "please input cursor" << endl;
    cin >> cursor;
    strcpy(msg.cursor, cursor.c_str());
    msg.timestamp = x::RawDateTime();
    broker->SendQueryTradeKnock(&msg);
}

void ReadRep() {
    {
        bool exit_flag = false;
        if (boost::filesystem::exists(mem_dir)) {
            boost::filesystem::path p(mem_dir);
            for (auto &file : boost::filesystem::directory_iterator(p)) {
                const string filename = file.path().filename().string();
                if (filename.find(mem_rep_file) != filename.npos) {
                    exit_flag = true;
                    break;
                }
            }
        }
        if (!exit_flag) {
            x::MMapWriter req_writer;
            req_writer.Open(mem_dir, mem_rep_file, kReqMemSize << 20, true);
        }
    }
    const void* data = nullptr;
    x::MMapReader reader;
    reader.Open(mem_dir, mem_rep_file, true);
    while (true) {
        x::Sleep(1000);
        int32_t type = reader.Next(&data);
        if (type == kMemTypeTradeOrderRep) {
            MemTradeOrderMessage* rep = (MemTradeOrderMessage*)data;
            LOG_INFO << "收到报单响应, " << ToString(rep);
        } else if (type == kMemTypeTradeWithdrawRep) {
            MemTradeWithdrawMessage* rep = (MemTradeWithdrawMessage*) data;
            LOG_INFO << "收到撤单响应, " << ToString(rep);
        } else if (type == kMemTypeTradeKnock) {
            MemTradeKnock* msg = (MemTradeKnock*) data;
            LOG_INFO << "收到成交推送, " << ToString(msg);
        } else if (type == kMemTypeTradeAsset) {
            MemTradeAsset *asset = (MemTradeAsset*) data;
            LOG_INFO << "资金变更, fund_id: " << asset->fund_id
                     << ", timestamp: " << asset->timestamp
                     << ", balance: " << asset->balance
                     << ", usable: " << asset->usable
                     << ", margin: " << asset->margin
                     << ", equity: " << asset->equity
                     << ", long_margin_usable: " << asset->long_margin_usable
                     << ", short_margin_usable: " << asset->short_margin_usable
                     << ", short_return_usable: " << asset->short_return_usable;

        } else if (type == kMemTypeTradePosition) {
            MemTradePosition* position = (MemTradePosition*) data;
            LOG_INFO << "持仓变更, code: " << position->code
                     << ", fund_id: " << position->fund_id
                     << ", timestamp: " << position->timestamp
                     << ", long_volume: " << position->long_volume
                     << ", long_market_value: " << position->long_market_value
                     << ", long_can_close: " << position->long_can_close
                     << ", short_volume: " << position->short_volume
                     << ", short_market_value: " << position->short_market_value
                     << ", short_can_open: " << position->short_can_open;
        } else if (type == kMemTypeMonitorRisk) {
            MemMonitorRiskMessage* msg = (MemMonitorRiskMessage*) data;
            LOG_ERROR << "Risk, " << msg->error << ", timestamp: " << msg->timestamp;
        } else if (type == kMemTypeHeartBeat) {
            HeartBeatMessage* msg = (HeartBeatMessage*) data;
            LOG_ERROR << "心跳, " << msg->fund_id << ", timestamp: " << msg->timestamp;
        }
    }
}

int main() {
    MemBrokerOptionsPtr options = Config::Instance()->options();
    auto risk_opt = Config::Instance()->risk_opt();

    shared_ptr<CTPBroker> broker = make_shared<CTPBroker>();
    co::MemBrokerServer server;
    server.Init(options, risk_opt, broker);
    server.Start();

    fund_id = Config::Instance()->ctp_investor_id();
    mem_dir = options->mem_dir();
    mem_req_file = options->mem_req_file();
    mem_rep_file = options->mem_rep_file();
    std::thread t1(ReadRep);

    string usage("\nTYPE  'q' to quit program\n");
    usage += "      '1' to order_shfe\n";
    usage += "      '2' to order_sz\n";
    usage += "      '3' to withdraw\n";
    usage += "      '4' to order\n";
    usage += "      '5' to query asset\n";
    usage += "      '6' to query position\n";
    usage += "      '7' to query order\n";
    usage += "      '8' to query knock\n";
    cerr << (usage);

    char c;
    while ((c = getchar()) != 'q') {
        switch (c) {
            case '1': {
                order_shfe(broker);
                break;
            }
            case '2': {
                // order_sz(broker);
                break;
            }
            case '3': {
                withdraw(broker);
                break;
            }
            case '4': {
                order(broker);
                break;
            }
            case '5': {
                query_asset(broker);
                break;
            }
            case '6': {
                query_position(broker);
                break;
            }
            case '8': {
                query_knock(broker);
                break;
            }
            default:
                break;
        }
    }
    std::cout << "Finished!" << endl;
    return 0;
}
