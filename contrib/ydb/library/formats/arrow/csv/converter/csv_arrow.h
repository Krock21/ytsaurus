#pragma once

#include <contrib/ydb/public/api/protos/ydb_formats.pb.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/csv/api.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/io/api.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <set>
#include <vector>
#include <unordered_map>

namespace NKikimr::NFormats {

class TArrowCSV {
public:
    static constexpr ui32 DEFAULT_BLOCK_SIZE = 1024 * 1024;

    std::shared_ptr<arrow::RecordBatch> ReadNext(const TString& csv, TString& errString);
    std::shared_ptr<arrow::RecordBatch> ReadSingleBatch(const TString& csv, TString& errString);
    std::shared_ptr<arrow::RecordBatch> ReadSingleBatch(const TString& csv, const Ydb::Formats::CsvSettings& csvSettings, TString& errString);

    void Reset() {
        Reader = {};
    }

    void SetSkipRows(ui32 skipRows) {
        ReadOptions.skip_rows = skipRows;
    }

    void SetBlockSize(ui32 blockSize = DEFAULT_BLOCK_SIZE) {
        ReadOptions.block_size = blockSize;
    }

    void SetDelimiter(std::optional<char> delimiter) {
        if (delimiter) {
            ParseOptions.delimiter = *delimiter;
        }
    }

    void SetQuoting(bool quoting = true, char quoteChar = '"', bool doubleQuote = true) {
        ParseOptions.quoting = quoting;
        ParseOptions.quote_char = quoteChar;
        ParseOptions.double_quote = doubleQuote;
    }

    void SetEscaping(bool escaping = false, char escapeChar = '\\') {
        ParseOptions.escaping = escaping;
        ParseOptions.escape_char = escapeChar;
    }

    void SetNullValue(const TString& null = "");

protected:
    struct TColumnInfo {
        TString Name;
        std::shared_ptr<arrow::DataType> ArrowType;
        std::shared_ptr<arrow::DataType>CsvArrowType;
    };
    using TColummns = TVector<TColumnInfo>;
    TArrowCSV(const TColummns& columns, bool header, const std::set<std::string>& notNullColumns);

    static TString ErrorPrefix() {
        return "Cannot read CSV: ";
    }

private:
    arrow::csv::ReadOptions ReadOptions;
    arrow::csv::ParseOptions ParseOptions;
    arrow::csv::ConvertOptions ConvertOptions;
    std::shared_ptr<arrow::csv::StreamingReader> Reader;
    std::vector<TString> ResultColumns;
    std::unordered_map<std::string, std::shared_ptr<arrow::DataType>> OriginalColumnTypes;
    std::set<std::string> NotNullColumns;

    std::shared_ptr<arrow::RecordBatch> ConvertColumnTypes(std::shared_ptr<arrow::RecordBatch> parsedBatch) const;
};

}
