#pragma once

#include <base/types.h>
#include <DBPoco/Net/IPAddress.h>


namespace DBPoco
{
    namespace Net
    {
        class SocketAddress;
    }
}

namespace DB
{
    /** Lets you check if the address is similar to `localhost`.
     * The purpose of this check is usually to make an assumption,
     *  that when we go to this address via the Internet, we'll get to ourselves.
     * Please note that this check is not accurate:
     * - the address is simply compared to the addresses of the network interfaces;
     * - only the first address is taken for each network interface;
     * - the routing rules that affect which network interface we go to the specified address are not checked.
     */
    bool isLocalAddress(const DBPoco::Net::SocketAddress & address, UInt16 clickhouse_port);
    bool isLocalAddress(const DBPoco::Net::SocketAddress & address);
    bool isLocalAddress(const DBPoco::Net::IPAddress & address);

    /// Returns host name difference with name prefix, used for load balancing
    size_t getHostNamePrefixDistance(const std::string & local_hostname, const std::string & host);
    /// Returns host name difference with Levenshtein Distance.
    size_t getHostNameLevenshteinDistance(const std::string & local_hostname, const std::string & host);
}
