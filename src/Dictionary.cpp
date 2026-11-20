#include "TV1TSF.h"
#include "resource.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr size_t kMaxCandidates = 200;
constexpr size_t kHeaderSize = 64;
constexpr size_t kIndexEntrySize = 24;
constexpr uint32_t kFormatVersion = 1;
constexpr char kMagic[] = {'B', 'G', 'D', 'I', 'C', 'T', '\0', '\0'};

struct EntryRef
{
    std::string_view code;
    std::string_view text;
    int64_t weight;
};

uint32_t ReadU32(const uint8_t* bytes) noexcept
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

int64_t ReadI64(const uint8_t* bytes) noexcept
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return static_cast<int64_t>(value);
}

bool ContainsRegion(size_t totalSize, uint32_t offset, uint32_t length) noexcept
{
    return offset <= totalSize && length <= totalSize - offset;
}

bool ContainsSubregion(uint32_t regionSize, uint32_t offset, uint32_t length) noexcept
{
    return offset <= regionSize && length <= regionSize - offset;
}

struct DictionaryView
{
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    uint32_t entryCount = 0;
    uint32_t indexOffset = 0;
    uint32_t codeOffset = 0;
    uint32_t codeSize = 0;
    uint32_t textOffset = 0;
    uint32_t textSize = 0;
    bool valid = false;

    DictionaryView() = default;

    DictionaryView(const void* data, size_t dataSize) noexcept
    {
        if (data == nullptr || dataSize < kHeaderSize)
            return;

        const auto* source = static_cast<const uint8_t*>(data);
        if (std::memcmp(source, kMagic, sizeof(kMagic)) != 0 ||
            ReadU32(source + 8) != kFormatVersion ||
            ReadU32(source + 12) != kHeaderSize ||
            ReadU32(source + 20) != kIndexEntrySize)
        {
            return;
        }

        const uint32_t count = ReadU32(source + 16);
        const uint32_t index = ReadU32(source + 24);
        const uint32_t indexSize = ReadU32(source + 28);
        const uint32_t codes = ReadU32(source + 32);
        const uint32_t codesSize = ReadU32(source + 36);
        const uint32_t texts = ReadU32(source + 40);
        const uint32_t textsSize = ReadU32(source + 44);
        if (static_cast<uint64_t>(count) * kIndexEntrySize != indexSize ||
            !ContainsRegion(dataSize, index, indexSize) ||
            !ContainsRegion(dataSize, codes, codesSize) ||
            !ContainsRegion(dataSize, texts, textsSize))
        {
            return;
        }

        bytes = source;
        size = dataSize;
        entryCount = count;
        indexOffset = index;
        codeOffset = codes;
        codeSize = codesSize;
        textOffset = texts;
        textSize = textsSize;
        valid = true;
    }

    std::optional<EntryRef> GetEntry(size_t position) const noexcept
    {
        if (!valid || position >= entryCount)
            return std::nullopt;

        const size_t record = static_cast<size_t>(indexOffset) + position * kIndexEntrySize;
        const uint32_t code = ReadU32(bytes + record);
        const uint32_t codeLength = ReadU32(bytes + record + 4);
        const uint32_t text = ReadU32(bytes + record + 8);
        const uint32_t textLength = ReadU32(bytes + record + 12);
        if (!ContainsSubregion(codeSize, code, codeLength) ||
            !ContainsSubregion(textSize, text, textLength))
        {
            return std::nullopt;
        }

        const auto* codeData = reinterpret_cast<const char*>(bytes + codeOffset + code);
        const auto* textData = reinterpret_cast<const char*>(bytes + textOffset + text);
        return EntryRef{{codeData, codeLength}, {textData, textLength}, ReadI64(bytes + record + 16)};
    }
};

DictionaryView LoadDictionary(HMODULE module, int resourceId) noexcept
{
    if (module == nullptr)
        return {};

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr)
        return {};
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    if (loaded == nullptr || size == 0)
        return {};
    const void* bytes = LockResource(loaded);
    return DictionaryView(bytes, static_cast<size_t>(size));
}

const DictionaryView& GetEnglishDictionary(HMODULE module) noexcept
{
    static const DictionaryView english = LoadDictionary(module, IDR_ENGLISH_DICTIONARY);
    return english;
}

const DictionaryView& GetDictionary(InputScheme scheme, HMODULE module) noexcept
{
    if (scheme == InputScheme::Pinyin)
    {
        static const DictionaryView pinyin = LoadDictionary(module, IDR_PINYIN_DICTIONARY);
        return pinyin;
    }
    static const DictionaryView wubi86 = LoadDictionary(module, IDR_WUBI86_DICTIONARY);
    return wubi86;
}

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX))
        return {};

    const int length = MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr,
                                           0);
    if (length <= 0)
        return {};

    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            result.data(),
                            length) != length)
    {
        return {};
    }
    return result;
}

size_t LowerBound(const DictionaryView& dictionary, std::string_view code, bool& ok) noexcept
{
    size_t first = 0;
    size_t count = dictionary.entryCount;
    while (count > 0)
    {
        const size_t step = count / 2;
        const size_t position = first + step;
        const auto entry = dictionary.GetEntry(position);
        if (!entry)
        {
            ok = false;
            return first;
        }
        if (entry->code < code)
        {
            first = position + 1;
            count -= step + 1;
        }
        else
        {
            count = step;
        }
    }
    return first;
}

size_t UpperBound(const DictionaryView& dictionary, std::string_view code, bool& ok) noexcept
{
    size_t first = 0;
    size_t count = dictionary.entryCount;
    while (count > 0)
    {
        const size_t step = count / 2;
        const size_t position = first + step;
        const auto entry = dictionary.GetEntry(position);
        if (!entry)
        {
            ok = false;
            return first;
        }
        if (!(code < entry->code))
        {
            first = position + 1;
            count -= step + 1;
        }
        else
        {
            count = step;
        }
    }
    return first;
}

struct CandidateCollector
{
    std::vector<EntryRef> entries;
    std::unordered_set<std::string_view> seen;

    bool Full() const noexcept { return entries.size() >= kMaxCandidates; }

    void Add(const EntryRef& entry)
    {
        if (!Full() && seen.insert(entry.text).second)
            entries.push_back(entry);
    }
};

void AppendExact(const DictionaryView& dictionary,
                 std::string_view code,
                 CandidateCollector& collector)
{
    if (!dictionary.valid || collector.Full())
        return;

    bool ok = true;
    const size_t begin = LowerBound(dictionary, code, ok);
    const size_t end = UpperBound(dictionary, code, ok);
    if (!ok)
        return;
    for (size_t position = begin; position < end && !collector.Full(); ++position)
    {
        const auto entry = dictionary.GetEntry(position);
        if (!entry)
            return;
        collector.Add(*entry);
    }
}

struct PrefixOrder
{
    bool operator()(const EntryRef& left, const EntryRef& right) const noexcept
    {
        if (left.weight != right.weight)
            return left.weight > right.weight;
        if (left.code != right.code)
            return left.code < right.code;
        return left.text < right.text;
    }
};

class PrefixSelection
{
public:
    PrefixSelection(size_t limit, const std::unordered_set<std::string_view>& excluded)
        : limit_(limit), excluded_(excluded)
    {
    }

    void Offer(const EntryRef& entry)
    {
        if (limit_ == 0)
            return;
        if (ordered_.size() == limit_ && !order_(entry, *std::prev(ordered_.end())))
            return;
        if (excluded_.find(entry.text) != excluded_.end())
            return;

        const auto existing = positions_.find(entry.text);
        if (existing != positions_.end())
        {
            if (order_(*existing->second, entry))
                return;
            ordered_.erase(existing->second);
            existing->second = ordered_.insert(entry).first;
            return;
        }

        if (ordered_.size() == limit_)
        {
            const auto worst = std::prev(ordered_.end());
            positions_.erase(worst->text);
            ordered_.erase(worst);
        }
        positions_.emplace(entry.text, ordered_.insert(entry).first);
    }

    void AppendTo(CandidateCollector& collector) const
    {
        for (const auto& entry : ordered_)
            collector.Add(entry);
    }

private:
    using Ordered = std::set<EntryRef, PrefixOrder>;
    using Iterator = Ordered::iterator;

    size_t limit_;
    const std::unordered_set<std::string_view>& excluded_;
    PrefixOrder order_;
    Ordered ordered_;
    std::unordered_map<std::string_view, Iterator> positions_;
};

void AppendPrefix(const DictionaryView& dictionary,
                  std::string_view code,
                  CandidateCollector& collector)
{
    if (!dictionary.valid || collector.Full())
        return;

    bool ok = true;
    const size_t begin = LowerBound(dictionary, code, ok);
    if (!ok)
        return;

    PrefixSelection selection(kMaxCandidates - collector.entries.size(), collector.seen);
    for (size_t position = begin; position < dictionary.entryCount; ++position)
    {
        const auto entry = dictionary.GetEntry(position);
        if (!entry)
            return;
        if (entry->code.size() < code.size() || entry->code.substr(0, code.size()) != code)
            break;
        if (entry->code != code)
            selection.Offer(*entry);
    }
    selection.AppendTo(collector);
}

std::vector<std::wstring> LookupFromViews(InputScheme scheme,
                                          const std::wstring& wideCode,
                                          const DictionaryView& pinyin,
                                          const DictionaryView& wubi86)
{
    if (wideCode.empty() || wideCode.size() > 32)
        return {};

    std::string code;
    code.reserve(wideCode.size());
    for (const wchar_t letter : wideCode)
    {
        if (letter < L'a' || letter > L'z')
            return {};
        code.push_back(static_cast<char>(letter));
    }

    CandidateCollector collector;
    collector.entries.reserve(kMaxCandidates);
    collector.seen.reserve(kMaxCandidates);
    if (scheme == InputScheme::Pinyin)
    {
        AppendExact(pinyin, code, collector);
        AppendPrefix(pinyin, code, collector);
    }
    else
    {
        if (code.size() <= 4)
            AppendExact(wubi86, code, collector);
        AppendExact(pinyin, code, collector);
        if (code.size() <= 4)
            AppendPrefix(wubi86, code, collector);
        AppendPrefix(pinyin, code, collector);
    }

    std::vector<std::wstring> candidates;
    candidates.reserve(collector.entries.size());
    for (const auto& entry : collector.entries)
    {
        std::wstring text = Utf8ToWide(entry.text);
        if (!text.empty())
            candidates.push_back(std::move(text));
    }
    return candidates;
}

std::vector<std::wstring> LookupEnglishFromView(const std::wstring& wideCode,
                                                const DictionaryView& english)
{
    if (wideCode.empty() || wideCode.size() > 64)
        return {};

    std::string code;
    code.reserve(wideCode.size());
    bool allLower = true;
    bool allUpper = true;
    bool titleCase = wideCode.front() >= L'A' && wideCode.front() <= L'Z';
    for (size_t index = 0; index < wideCode.size(); ++index)
    {
        const wchar_t letter = wideCode[index];
        if (letter >= L'a' && letter <= L'z')
        {
            code.push_back(static_cast<char>(letter));
            allUpper = false;
        }
        else if (letter >= L'A' && letter <= L'Z')
        {
            code.push_back(static_cast<char>(letter - L'A' + 'a'));
            allLower = false;
            if (index > 0)
                titleCase = false;
        }
        else
        {
            return {};
        }
    }

    CandidateCollector collector;
    collector.entries.reserve(kMaxCandidates);
    collector.seen.reserve(kMaxCandidates);
    AppendExact(english, code, collector);
    AppendPrefix(english, code, collector);

    const bool upperCaseResult = allUpper && wideCode.size() > 1;
    const bool titleCaseResult = titleCase && !allLower;
    std::vector<std::wstring> candidates;
    candidates.reserve(collector.entries.size());
    for (const auto& entry : collector.entries)
    {
        std::wstring text = Utf8ToWide(entry.text);
        if (text.empty())
            continue;
        if (upperCaseResult)
        {
            for (wchar_t& letter : text)
            {
                if (letter >= L'a' && letter <= L'z')
                    letter = letter - L'a' + L'A';
            }
        }
        else if (titleCaseResult && text.front() >= L'a' && text.front() <= L'z')
        {
            text.front() = text.front() - L'a' + L'A';
        }
        candidates.push_back(std::move(text));
    }
    return candidates;
}
}

std::vector<std::wstring> LookupCandidates(InputScheme scheme,
                                           const std::wstring& code,
                                           HMODULE resourceModule)
{
    return LookupFromViews(scheme,
                           code,
                           GetDictionary(InputScheme::Pinyin, resourceModule),
                           GetDictionary(InputScheme::Wubi86, resourceModule));
}

std::vector<std::wstring> LookupEnglishCandidates(const std::wstring& code,
                                                  HMODULE resourceModule)
{
    return LookupEnglishFromView(code, GetEnglishDictionary(resourceModule));
}

#ifdef TV1TSF_DICTIONARY_TESTING
std::vector<std::wstring> LookupCandidatesFromMemoryForTest(InputScheme scheme,
                                                            const std::wstring& code,
                                                            const void* pinyinData,
                                                            size_t pinyinSize,
                                                            const void* wubiData,
                                                            size_t wubiSize)
{
    const DictionaryView pinyin(pinyinData, pinyinSize);
    const DictionaryView wubi86(wubiData, wubiSize);
    return LookupFromViews(scheme, code, pinyin, wubi86);
}

std::vector<std::wstring> LookupEnglishCandidatesFromMemoryForTest(const std::wstring& code,
                                                                   const void* englishData,
                                                                   size_t englishSize)
{
    const DictionaryView english(englishData, englishSize);
    return LookupEnglishFromView(code, english);
}
#endif
