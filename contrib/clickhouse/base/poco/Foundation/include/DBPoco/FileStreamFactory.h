//
// FileStreamFactory.h
//
// Library: Foundation
// Package: URI
// Module:  FileStreamFactory
//
// Definition of the FileStreamFactory class.
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef DB_Foundation_FileStreamFactory_INCLUDED
#define DB_Foundation_FileStreamFactory_INCLUDED


#include "DBPoco/Foundation.h"
#include "DBPoco/URIStreamFactory.h"


namespace DBPoco
{


class Path;


class Foundation_API FileStreamFactory : public URIStreamFactory
/// An implementation of the URIStreamFactory interface
/// that handles file URIs.
{
public:
    FileStreamFactory();
    /// Creates the FileStreamFactory.

    ~FileStreamFactory();
    /// Destroys the FileStreamFactory.

    std::istream * open(const URI & uri);
    /// Creates and opens a file stream in binary mode for the given URI.
    /// The URI must be either a file URI or a relative URI reference
    /// containing a path to a local file.
    ///
    /// Throws an FileNotFound exception if the file cannot
    /// be opened.

    std::istream * open(const Path & path);
    /// Creates and opens a file stream in binary mode for the given path.
    ///
    /// Throws an FileNotFound exception if the file cannot
    /// be opened.
};


} // namespace DBPoco


#endif // DB_Foundation_FileStreamFactory_INCLUDED
