//
// RWLock.h
//
// Library: Foundation
// Package: Threading
// Module:  RWLock
//
// Definition of the RWLock class.
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef DB_Foundation_RWLock_INCLUDED
#define DB_Foundation_RWLock_INCLUDED


#include "DBPoco/Exception.h"
#include "DBPoco/Foundation.h"


#if   DB_POCO_OS == DB_POCO_OS_ANDROID
#    include "DBPoco/RWLock_Android.h"
#else
#    include "DBPoco/RWLock_POSIX.h"
#endif


namespace DBPoco
{


class ScopedRWLock;
class ScopedReadRWLock;
class ScopedWriteRWLock;


class Foundation_API RWLock : private RWLockImpl
/// A reader writer lock allows multiple concurrent
/// readers or one exclusive writer.
{
public:
    typedef ScopedRWLock ScopedLock;
    typedef ScopedReadRWLock ScopedReadLock;
    typedef ScopedWriteRWLock ScopedWriteLock;

    RWLock();
    /// Creates the Reader/Writer lock.

    ~RWLock();
    /// Destroys the Reader/Writer lock.

    void readLock();
    /// Acquires a read lock. If another thread currently holds a write lock,
    /// waits until the write lock is released.

    bool tryReadLock();
    /// Tries to acquire a read lock. Immediately returns true if successful, or
    /// false if another thread currently holds a write lock.

    void writeLock();
    /// Acquires a write lock. If one or more other threads currently hold
    /// locks, waits until all locks are released. The results are undefined
    /// if the same thread already holds a read or write lock

    bool tryWriteLock();
    /// Tries to acquire a write lock. Immediately returns true if successful,
    /// or false if one or more other threads currently hold
    /// locks. The result is undefined if the same thread already
    /// holds a read or write lock.

    void unlock();
    /// Releases the read or write lock.

private:
    RWLock(const RWLock &);
    RWLock & operator=(const RWLock &);
};


class Foundation_API ScopedRWLock
/// A variant of ScopedLock for reader/writer locks.
{
public:
    ScopedRWLock(RWLock & rwl, bool write = false);
    ~ScopedRWLock();

private:
    RWLock & _rwl;

    ScopedRWLock();
    ScopedRWLock(const ScopedRWLock &);
    ScopedRWLock & operator=(const ScopedRWLock &);
};


class Foundation_API ScopedReadRWLock : public ScopedRWLock
/// A variant of ScopedLock for reader locks.
{
public:
    ScopedReadRWLock(RWLock & rwl);
    ~ScopedReadRWLock();
};


class Foundation_API ScopedWriteRWLock : public ScopedRWLock
/// A variant of ScopedLock for writer locks.
{
public:
    ScopedWriteRWLock(RWLock & rwl);
    ~ScopedWriteRWLock();
};


//
// inlines
//
inline void RWLock::readLock()
{
    readLockImpl();
}


inline bool RWLock::tryReadLock()
{
    return tryReadLockImpl();
}


inline void RWLock::writeLock()
{
    writeLockImpl();
}


inline bool RWLock::tryWriteLock()
{
    return tryWriteLockImpl();
}


inline void RWLock::unlock()
{
    unlockImpl();
}


inline ScopedRWLock::ScopedRWLock(RWLock & rwl, bool write) : _rwl(rwl)
{
    if (write)
        _rwl.writeLock();
    else
        _rwl.readLock();
}


inline ScopedRWLock::~ScopedRWLock()
{
    try
    {
        _rwl.unlock();
    }
    catch (...)
    {
        DB_poco_unexpected();
    }
}


inline ScopedReadRWLock::ScopedReadRWLock(RWLock & rwl) : ScopedRWLock(rwl, false)
{
}


inline ScopedReadRWLock::~ScopedReadRWLock()
{
}


inline ScopedWriteRWLock::ScopedWriteRWLock(RWLock & rwl) : ScopedRWLock(rwl, true)
{
}


inline ScopedWriteRWLock::~ScopedWriteRWLock()
{
}


} // namespace DBPoco


#endif // DB_Foundation_RWLock_INCLUDED
