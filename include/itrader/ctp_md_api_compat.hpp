#pragma once

#if defined(__INTELLISENSE__)

struct CThostFtdcRspInfoField {
    int ErrorID;
    char ErrorMsg[256];
};

struct CThostFtdcReqUserLoginField {
    char BrokerID[64];
    char UserID[64];
    char Password[64];
    char UserProductInfo[64];
    char InterfaceProductInfo[64];
};

struct CThostFtdcRspUserLoginField {
    char TradingDay[16];
};

struct CThostFtdcUserLogoutField {};
struct CThostFtdcMulticastInstrumentField {};
struct CThostFtdcQryMulticastInstrumentField {};
struct CThostFtdcFensUserInfoField {};
struct CThostFtdcForQuoteRspField {};

struct CThostFtdcSpecificInstrumentField {
    char InstrumentID[64];
};

struct CThostFtdcDepthMarketDataField {
    char InstrumentID[64];
    char ExchangeID[16];
    char TradingDay[16];
    char ActionDay[16];
    char UpdateTime[16];
    int UpdateMillisec;
    double LastPrice;
    double HighestPrice;
    double LowestPrice;
    int Volume;
    double Turnover;
    double OpenInterest;
    int AskVolume1;
    double AskPrice1;
    int BidVolume1;
    double BidPrice1;
};

class CThostFtdcMdSpi {
public:
    virtual ~CThostFtdcMdSpi() = default;
    virtual void OnFrontConnected() {};
    virtual void OnFrontDisconnected(int) {};
    virtual void OnHeartBeatWarning(int) {};
    virtual void OnRspUserLogin(CThostFtdcRspUserLoginField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspUserLogout(CThostFtdcUserLogoutField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspQryMulticastInstrument(CThostFtdcMulticastInstrumentField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspError(CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspSubMarketData(CThostFtdcSpecificInstrumentField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField*, CThostFtdcRspInfoField*, int, bool) {};
    virtual void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField*) {};
    virtual void OnRtnForQuoteRsp(CThostFtdcForQuoteRspField*) {};
};

class CThostFtdcMdApi {
public:
    static CThostFtdcMdApi* CreateFtdcMdApi(const char* = "", const bool = false, const bool = false, bool = true);
    static const char* GetApiVersion();
    virtual void Release() = 0;
    virtual void Init() = 0;
    virtual int Join() = 0;
    virtual const char* GetTradingDay() = 0;
    virtual void RegisterFront(char*) = 0;
    virtual void RegisterNameServer(char*) = 0;
    virtual void RegisterFensUserInfo(CThostFtdcFensUserInfoField*) = 0;
    virtual void RegisterSpi(CThostFtdcMdSpi*) = 0;
    virtual int SubscribeMarketData(char*[], int) = 0;
    virtual int UnSubscribeMarketData(char*[], int) = 0;
    virtual int SubscribeForQuoteRsp(char*[], int) = 0;
    virtual int UnSubscribeForQuoteRsp(char*[], int) = 0;
    virtual int ReqUserLogin(CThostFtdcReqUserLoginField*, int) = 0;
    virtual int ReqUserLogout(CThostFtdcUserLogoutField*, int) = 0;
    virtual int ReqQryMulticastInstrument(CThostFtdcQryMulticastInstrumentField*, int) = 0;

protected:
    ~CThostFtdcMdApi() = default;
};

#else

#include "ThostFtdcMdApi.h"

#endif
