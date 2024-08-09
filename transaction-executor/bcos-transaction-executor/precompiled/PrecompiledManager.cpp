#include "PrecompiledManager.h"
#include "bcos-executor/src/precompiled/BFSPrecompiled.h"
#include "bcos-executor/src/precompiled/CastPrecompiled.h"
#include "bcos-executor/src/precompiled/ConsensusPrecompiled.h"
#include "bcos-executor/src/precompiled/CryptoPrecompiled.h"
#include "bcos-executor/src/precompiled/KVTablePrecompiled.h"
#include "bcos-executor/src/precompiled/ShardingPrecompiled.h"
#include "bcos-executor/src/precompiled/SystemConfigPrecompiled.h"
#include "bcos-executor/src/precompiled/TableManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/TablePrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AccountPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/AuthManagerPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/DagTransferPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/GroupSigPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/PaillierPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/RingSigPrecompiled.h"
#include "bcos-executor/src/precompiled/extension/ZkpPrecompiled.h"
#include <memory>


bcos::transaction_executor::PrecompiledManager::PrecompiledManager(crypto::Hash::Ptr hashImpl)
  : m_hashImpl(std::move(hashImpl))
{
    m_address2Precompiled.emplace_back(
        1, executor::PrecompiledContract(
               3000, 0, executor::PrecompiledRegistrar::executor("ecrecover")));
    m_address2Precompiled.emplace_back(2,
        executor::PrecompiledContract(60, 12, executor::PrecompiledRegistrar::executor("sha256")));
    m_address2Precompiled.emplace_back(
        3, executor::PrecompiledContract(
               600, 120, executor::PrecompiledRegistrar::executor("ripemd160")));
    m_address2Precompiled.emplace_back(4,
        executor::PrecompiledContract(15, 3, executor::PrecompiledRegistrar::executor("identity")));
    m_address2Precompiled.emplace_back(
        5, executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("modexp"),
               executor::PrecompiledRegistrar::executor("modexp")));
    m_address2Precompiled.emplace_back(
        6, executor::PrecompiledContract(
               150, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_add")));
    m_address2Precompiled.emplace_back(
        7, executor::PrecompiledContract(
               6000, 0, executor::PrecompiledRegistrar::executor("alt_bn128_G1_mul")));
    m_address2Precompiled.emplace_back(
        8, executor::PrecompiledContract(
               executor::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
               executor::PrecompiledRegistrar::executor("alt_bn128_pairing_product")));
    m_address2Precompiled.emplace_back(9,
        executor::PrecompiledContract(executor::PrecompiledRegistrar::pricer("blake2_compression"),
            executor::PrecompiledRegistrar::executor("blake2_compression")));

    m_address2Precompiled.emplace_back(
        0x1000, std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1003, std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1002, std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1009, std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1001, std::make_shared<precompiled::TablePrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100c, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100a, std::make_shared<precompiled::CryptoPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100e, std::make_shared<precompiled::BFSPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5003, std::make_shared<precompiled::PaillierPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5004, std::make_shared<precompiled::GroupSigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5005, std::make_shared<precompiled::RingSigPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x5100, std::make_shared<precompiled::ZkpPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x1005, std::make_shared<precompiled::AuthManagerPrecompiled>(m_hashImpl, false));
    m_address2Precompiled.emplace_back(
        0x10002, std::make_shared<precompiled::ContractAuthMgrPrecompiled>(m_hashImpl, false));
    m_address2Precompiled.emplace_back(
        0x1010, std::make_shared<precompiled::ShardingPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x100f, std::make_shared<precompiled::CastPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x10003, std::make_shared<precompiled::AccountManagerPrecompiled>(m_hashImpl));
    m_address2Precompiled.emplace_back(
        0x10004, std::make_shared<precompiled::AccountPrecompiled>(m_hashImpl));

    std::sort(m_address2Precompiled.begin(), m_address2Precompiled.end(),
        [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
}

bcos::transaction_executor::Precompiled const*
bcos::transaction_executor::PrecompiledManager::getPrecompiled(unsigned long contractAddress) const
{
    auto it = std::lower_bound(m_address2Precompiled.begin(), m_address2Precompiled.end(),
        contractAddress,
        [](const decltype(m_address2Precompiled)::value_type& lhs, unsigned long rhs) {
            return std::get<0>(lhs) < rhs;
        });
    if (it != m_address2Precompiled.end() && std::get<0>(*it) == contractAddress)
    {
        return std::addressof(std::get<1>(*it));
    }

    return nullptr;
}
