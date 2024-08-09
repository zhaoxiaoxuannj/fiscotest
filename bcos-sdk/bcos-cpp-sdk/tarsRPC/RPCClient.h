#pragma once

#include "Handle.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/RPC.h"

namespace bcos::sdk
{

struct Config
{
    std::string connectionString;
    long sendQueueSize = 0;
    int timeoutMs = 60000;
};

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

    static void onMessage(tars::ReqMessagePtr message);

public:
    RPCClient(Config const& config);
    bcostars::RPCPrx& rpcProxy();

    static std::string toConnectionString(const std::vector<std::string>& hostAndPorts);
    std::string generateNonce();
};

class SendTransaction : public bcos::sdk::Handle<bcos::protocol::TransactionReceipt::Ptr>
{
public:
    SendTransaction(RPCClient& rpcClient);
    SendTransaction& send(const bcos::protocol::Transaction& transaction);
};

class Call : public bcos::sdk::Handle<protocol::TransactionReceipt::Ptr>
{
public:
    Call(RPCClient& rpcClient);
    Call& send(const protocol::Transaction& transaction);
};

class BlockNumber : public bcos::sdk::Handle<long>
{
public:
    BlockNumber(RPCClient& rpcClient);
    BlockNumber& send();
};

}  // namespace bcos::sdk