#pragma once

#include "clickhouse_config.h"

#if USE_NLP

#include <base/types.h>
#include <DBPoco/Util/Application.h>

#include <mutex>
#include <unordered_map>


namespace DB
{

class ILemmatizer
{
public:
    using TokenPtr = std::shared_ptr<char []>;

    virtual TokenPtr lemmatize(const char * token) = 0;

    virtual ~ILemmatizer() = default;
};


class Lemmatizers
{
public:
    using LemmPtr = std::shared_ptr<ILemmatizer>;

private:
    std::mutex mutex;
    std::unordered_map<String, LemmPtr> lemmatizers;
    std::unordered_map<String, String> paths;

public:
    explicit Lemmatizers(const DBPoco::Util::AbstractConfiguration & config);

    LemmPtr getLemmatizer(const String & name);
};

}

#endif
