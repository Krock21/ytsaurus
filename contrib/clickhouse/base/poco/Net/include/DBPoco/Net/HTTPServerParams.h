//
// HTTPServerParams.h
//
// Library: Net
// Package: HTTPServer
// Module:  HTTPServerParams
//
// Definition of the HTTPServerParams class.
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef DB_Net_HTTPServerParams_INCLUDED
#define DB_Net_HTTPServerParams_INCLUDED


#include "DBPoco/Net/Net.h"
#include "DBPoco/Net/TCPServerParams.h"


namespace DBPoco
{
namespace Net
{


    class Net_API HTTPServerParams : public TCPServerParams
    /// This class is used to specify parameters to both the
    /// HTTPServer, as well as to HTTPRequestHandler objects.
    ///
    /// Subclasses may add new parameters to the class.
    {
    public:
        typedef DBPoco::AutoPtr<HTTPServerParams> Ptr;

        HTTPServerParams();
        /// Creates the HTTPServerParams.
        ///
        /// Sets the following default values:
        ///   - timeout:              60 seconds
        ///   - keepAlive:            true
        ///   - maxKeepAliveRequests: 0
        ///   - keepAliveTimeout:     15 seconds

        void setServerName(const std::string & serverName);
        /// Sets the name and port (name:port) that the server uses to identify itself.
        ///
        /// If this is not set to valid DNS name for your host, server-generated
        /// redirections will not work.

        const std::string & getServerName() const;
        /// Returns the name and port (name:port) that the server uses to identify itself.

        void setSoftwareVersion(const std::string & softwareVersion);
        /// Sets the server software name and version that the server uses to identify
        /// itself. If this is set to a non-empty string, the server will
        /// automatically include a Server header field with the value given
        /// here in every response it sends.
        ///
        /// The format of the softwareVersion string should be name/version
        /// (e.g. MyHTTPServer/1.0).

        const std::string & getSoftwareVersion() const;
        /// Returns the server software name and version that the server uses to
        /// identify itself.

        void setTimeout(const DBPoco::Timespan & timeout);
        /// Sets the connection timeout for HTTP connections.

        const DBPoco::Timespan & getTimeout() const;
        /// Returns the connection timeout for HTTP connections.

        void setKeepAlive(bool keepAlive);
        /// Enables (keepAlive == true) or disables (keepAlive == false)
        /// persistent connections.

        bool getKeepAlive() const;
        /// Returns true iff persistent connections are enabled.

        void setKeepAliveTimeout(const DBPoco::Timespan & timeout);
        /// Sets the connection timeout for HTTP connections.

        const DBPoco::Timespan & getKeepAliveTimeout() const;
        /// Returns the connection timeout for HTTP connections.

        void setMaxKeepAliveRequests(int maxKeepAliveRequests);
        /// Specifies the maximum number of requests allowed
        /// during a persistent connection. 0 means unlimited
        /// connections.

        int getMaxKeepAliveRequests() const;
        /// Returns the maximum number of requests allowed
        /// during a persistent connection, or 0 if
        /// unlimited connections are allowed.

    protected:
        virtual ~HTTPServerParams();
        /// Destroys the HTTPServerParams.

    private:
        std::string _serverName;
        std::string _softwareVersion;
        DBPoco::Timespan _timeout;
        bool _keepAlive;
        int _maxKeepAliveRequests;
        DBPoco::Timespan _keepAliveTimeout;
    };


    //
    // inlines
    //
    inline const std::string & HTTPServerParams::getServerName() const
    {
        return _serverName;
    }


    inline const std::string & HTTPServerParams::getSoftwareVersion() const
    {
        return _softwareVersion;
    }


    inline const DBPoco::Timespan & HTTPServerParams::getTimeout() const
    {
        return _timeout;
    }


    inline bool HTTPServerParams::getKeepAlive() const
    {
        return _keepAlive;
    }


    inline int HTTPServerParams::getMaxKeepAliveRequests() const
    {
        return _maxKeepAliveRequests;
    }


    inline const DBPoco::Timespan & HTTPServerParams::getKeepAliveTimeout() const
    {
        return _keepAliveTimeout;
    }


}
} // namespace DBPoco::Net


#endif // DB_Net_HTTPServerParams_INCLUDED
