#include "TV1TSF.h"
#include "resource.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <unordered_set>

namespace
{
struct Entry
{
    std::wstring code;
    std::wstring text;
};

using Table = std::vector<Entry>;

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
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

Table LoadTable(HMODULE module, int resourceId)
{
    Table table;
    if (module == nullptr)
        return table;

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr)
        return table;
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const char*>(LockResource(loaded));
    if (size == 0 || bytes == nullptr)
        return table;

    const std::string_view data(bytes, size);
    size_t offset = 0;
    while (offset < data.size())
    {
        const size_t newline = data.find('\n', offset);
        const size_t end = newline == std::string_view::npos ? data.size() : newline;
        std::string_view line = data.substr(offset, end - offset);
        offset = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        const size_t firstTab = line.find('\t');
        const size_t secondTab = firstTab == std::string_view::npos
            ? std::string_view::npos
            : line.find('\t', firstTab + 1);
        if (firstTab == std::string_view::npos || secondTab == std::string_view::npos)
            continue;

        const std::string_view code = line.substr(0, firstTab);
        std::wstring text = Utf8ToWide(line.substr(firstTab + 1, secondTab - firstTab - 1));
        if (code.empty() || text.empty())
            continue;
        table.push_back({std::wstring(code.begin(), code.end()), std::move(text)});
    }
    return table;
}

const Table& GetTable(InputScheme scheme, HMODULE module)
{
    if (scheme == InputScheme::Pinyin)
    {
        static const Table pinyin = LoadTable(module, IDR_PINYIN_DICTIONARY);
        return pinyin;
    }
    static const Table wubi86 = LoadTable(module, IDR_WUBI86_DICTIONARY);
    return wubi86;
}

std::vector<std::wstring> LookupTable(const Table& table, const std::wstring& code)
{
    const auto begin = std::lower_bound(table.begin(), table.end(), code,
        [](const Entry& entry, const std::wstring& value) { return entry.code < value; });
    const auto end = std::upper_bound(begin, table.end(), code,
        [](const std::wstring& value, const Entry& entry) { return value < entry.code; });

    std::vector<std::wstring> candidates;
    candidates.reserve(static_cast<size_t>(std::distance(begin, end)));
    for (auto it = begin; it != end; ++it)
        candidates.push_back(it->text);
    return candidates;
}
}

std::vector<std::wstring> LookupCandidates(InputScheme scheme,
                                           const std::wstring& code,
                                           HMODULE resourceModule)
{
    if (scheme == InputScheme::Pinyin)
        return LookupTable(GetTable(scheme, resourceModule), code);

    std::vector<std::wstring> candidates;
    std::unordered_set<std::wstring> seen;
    const auto append = [&candidates, &seen](std::vector<std::wstring> source) {
        for (auto& text : source)
        {
            if (seen.insert(text).second)
                candidates.push_back(std::move(text));
        }
    };

    if (code.size() <= 4)
        append(LookupTable(GetTable(InputScheme::Wubi86, resourceModule), code));
    append(LookupTable(GetTable(InputScheme::Pinyin, resourceModule), code));
    return candidates;
}
