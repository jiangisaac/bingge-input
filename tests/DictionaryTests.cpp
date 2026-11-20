#include "TV1TSF.h"

#include <psapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
constexpr size_t kHeaderSize = 64;

struct TestEntry
{
    std::string code;
    std::string text;
    int64_t weight;
};

bool Contains(const std::vector<std::wstring>& candidates, const wchar_t* expected)
{
    return std::find(candidates.begin(), candidates.end(), expected) != candidates.end();
}

size_t PositionOf(const std::vector<std::wstring>& candidates, const wchar_t* expected)
{
    const auto found = std::find(candidates.begin(), candidates.end(), expected);
    return static_cast<size_t>(std::distance(candidates.begin(), found));
}

bool IsUnique(const std::vector<std::wstring>& candidates)
{
    const std::unordered_set<std::wstring> unique(candidates.begin(), candidates.end());
    return unique.size() == candidates.size();
}

bool ExpectCandidates(InputScheme scheme,
                      const wchar_t* code,
                      HMODULE module,
                      const std::vector<std::wstring>& expected)
{
    const auto candidates = LookupCandidates(scheme, code, module);
    for (const auto& text : expected)
    {
        if (!Contains(candidates, text.c_str()))
        {
            std::wcerr << L"Missing candidate " << text << L" for " << code << L'\n';
            return false;
        }
    }
    return true;
}

bool ExpectEnglish(const wchar_t* code,
                   HMODULE module,
                   const std::vector<std::wstring>& expected)
{
    const auto candidates = LookupEnglishCandidates(code, module);
    for (const auto& text : expected)
    {
        if (!Contains(candidates, text.c_str()))
        {
            std::wcerr << L"Missing English candidate " << text << L" for " << code << L'\n';
            return false;
        }
    }
    return true;
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void AppendI64(std::vector<uint8_t>& bytes, int64_t value)
{
    const uint64_t bits = static_cast<uint64_t>(value);
    for (size_t i = 0; i < 8; ++i)
        bytes.push_back(static_cast<uint8_t>(bits >> (i * 8)));
}

void SetU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

std::vector<uint8_t> BuildBinary(std::vector<TestEntry> entries)
{
    std::sort(entries.begin(), entries.end(), [](const TestEntry& left, const TestEntry& right) {
        if (left.code != right.code)
            return left.code < right.code;
        if (left.weight != right.weight)
            return left.weight > right.weight;
        return std::string_view(left.text) < std::string_view(right.text);
    });

    std::vector<uint8_t> index;
    std::vector<uint8_t> codes;
    std::vector<uint8_t> texts;
    for (const auto& entry : entries)
    {
        AppendU32(index, static_cast<uint32_t>(codes.size()));
        AppendU32(index, static_cast<uint32_t>(entry.code.size()));
        AppendU32(index, static_cast<uint32_t>(texts.size()));
        AppendU32(index, static_cast<uint32_t>(entry.text.size()));
        AppendI64(index, entry.weight);
        codes.insert(codes.end(), entry.code.begin(), entry.code.end());
        texts.insert(texts.end(), entry.text.begin(), entry.text.end());
    }

    std::vector<uint8_t> result(kHeaderSize, 0);
    constexpr std::array<uint8_t, 8> magic{'B', 'G', 'D', 'I', 'C', 'T', 0, 0};
    std::copy(magic.begin(), magic.end(), result.begin());
    const uint32_t codeOffset = static_cast<uint32_t>(result.size() + index.size());
    const uint32_t textOffset = codeOffset + static_cast<uint32_t>(codes.size());
    SetU32(result, 8, 1);
    SetU32(result, 12, static_cast<uint32_t>(kHeaderSize));
    SetU32(result, 16, static_cast<uint32_t>(entries.size()));
    SetU32(result, 20, 24);
    SetU32(result, 24, static_cast<uint32_t>(result.size()));
    SetU32(result, 28, static_cast<uint32_t>(index.size()));
    SetU32(result, 32, codeOffset);
    SetU32(result, 36, static_cast<uint32_t>(codes.size()));
    SetU32(result, 40, textOffset);
    SetU32(result, 44, static_cast<uint32_t>(texts.size()));
    result.insert(result.end(), index.begin(), index.end());
    result.insert(result.end(), codes.begin(), codes.end());
    result.insert(result.end(), texts.begin(), texts.end());
    return result;
}

std::vector<std::wstring> LookupMemory(InputScheme scheme,
                                       const wchar_t* code,
                                       const std::vector<uint8_t>& pinyin,
                                       const std::vector<uint8_t>& wubi = {})
{
    return LookupCandidatesFromMemoryForTest(scheme,
                                             code,
                                             pinyin.data(),
                                             pinyin.size(),
                                             wubi.data(),
                                             wubi.size());
}

std::vector<std::wstring> LookupEnglishMemory(const wchar_t* code,
                                              const std::vector<uint8_t>& english)
{
    return LookupEnglishCandidatesFromMemoryForTest(code, english.data(), english.size());
}

bool ExpectMemory(bool condition, const wchar_t* message)
{
    if (!condition)
        std::wcerr << message << L'\n';
    return condition;
}

bool RunRealDictionaryTests(HMODULE resourceModule)
{
    bool passed = true;
    passed &= ExpectCandidates(InputScheme::Pinyin, L"wen", resourceModule, {L"文", L"问"});
    passed &= ExpectCandidates(InputScheme::Pinyin, L"jiang", resourceModule, {L"江", L"讲", L"将"});
    passed &= ExpectCandidates(InputScheme::Pinyin, L"nihao", resourceModule, {L"你好"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"a", resourceModule, {L"工"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"wq", resourceModule, {L"你"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"ggtt", resourceModule, {L"五笔"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"nihao", resourceModule, {L"你好"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"nih", resourceModule, {L"你好"});

    const auto pinyinWen = LookupCandidates(InputScheme::Pinyin, L"wen", resourceModule);
    if (pinyinWen.empty() || pinyinWen.front() != L"问")
    {
        std::wcerr << L"Pure Pinyin wen must keep 问 first\n";
        passed = false;
    }
    for (const wchar_t* code : {L"w", L"we", L"wen"})
    {
        if (LookupCandidates(InputScheme::Pinyin, code, resourceModule).empty())
        {
            std::wcerr << L"Missing incremental Pinyin candidates for " << code << L'\n';
            passed = false;
        }
    }
    for (const wchar_t* code : {L"nih", L"niha"})
    {
        const auto candidates = LookupCandidates(InputScheme::Pinyin, code, resourceModule);
        if (!Contains(candidates, L"你好"))
        {
            std::wcerr << L"Missing prefix candidate 你好 for " << code << L'\n';
            passed = false;
        }
    }

    const auto mixedWen = LookupCandidates(InputScheme::Wubi86, L"wen", resourceModule);
    if (mixedWen.empty() || mixedWen.front() != L"仍" ||
        !Contains(mixedWen, L"文") || !Contains(mixedWen, L"问"))
    {
        std::wcerr << L"Mixed Wubi/Pinyin ordering failed for wen\n";
        passed = false;
    }
    if (!IsUnique(mixedWen))
    {
        std::wcerr << L"Duplicate mixed candidates found for wen\n";
        passed = false;
    }

    const auto mixedA = LookupCandidates(InputScheme::Wubi86, L"a", resourceModule);
    if (mixedA.empty() || mixedA.front() != L"工" || !Contains(mixedA, L"啊") || !Contains(mixedA, L"阿"))
    {
        std::wcerr << L"Mixed Wubi/Pinyin ordering failed for a\n";
        passed = false;
    }

    passed &= ExpectEnglish(L"the", resourceModule, {L"the", L"they"});
    passed &= ExpectEnglish(L"work", resourceModule, {L"work", L"working"});
    passed &= ExpectEnglish(L"Wonder", resourceModule, {L"Wonder"});
    passed &= ExpectEnglish(L"WONDER", resourceModule, {L"WONDER"});
    const auto englishThe = LookupEnglishCandidates(L"the", resourceModule);
    if (englishThe.empty() || englishThe.front() != L"the")
    {
        std::wcerr << L"Exact English word must be first\n";
        passed = false;
    }
    for (const wchar_t* code : {L"a", L"th", L"the"})
    {
        if (LookupEnglishCandidates(code, resourceModule).empty())
        {
            std::wcerr << L"Missing incremental English candidates for " << code << L'\n';
            passed = false;
        }
    }

    if (!LookupCandidates(InputScheme::Pinyin, L"not-a-pinyin-code", resourceModule).empty() ||
        !LookupCandidates(InputScheme::Wubi86, L"not-a-pinyin-code", resourceModule).empty())
    {
        std::wcerr << L"Unknown code unexpectedly returned candidates\n";
        passed = false;
    }
    return passed;
}

bool RunSyntheticTests()
{
    const auto pinyin = BuildBinary({
        {"a", "low", 1},
        {"a", "high", 9},
        {"a", "same-a", 5},
        {"a", "same-b", 5},
        {"aa", "prefix-low", 50},
        {"ab", "prefix-high", 100},
        {"ac", "high", 1000},
        {"zabcd", "long-pinyin", 7},
    });
    const auto wubi = BuildBinary({
        {"a", "wubi-exact", 20},
        {"aa", "wubi-prefix", 500},
        {"zabcd", "invalid-long-wubi", 999},
    });

    bool passed = true;
    const std::vector<std::wstring> expected{
        L"high", L"same-a", L"same-b", L"low", L"prefix-high", L"prefix-low"};
    const auto exactThenPrefix = LookupMemory(InputScheme::Pinyin, L"a", pinyin);
    passed &= ExpectMemory(exactThenPrefix == expected, L"Exact/prefix weight ordering or dedup failed");
    passed &= ExpectMemory(IsUnique(exactThenPrefix), L"Synthetic candidates contain duplicates");

    const auto prefixTies = BuildBinary({
        {"aa", "z", 10},
        {"ab", "y", 10},
        {"ac", "x", 10},
    });
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", prefixTies) ==
                               std::vector<std::wstring>{L"z", L"y", L"x"},
                           L"Prefix code/text tie ordering is not deterministic");

    const auto mixed = LookupMemory(InputScheme::Wubi86, L"a", pinyin, wubi);
    passed &= ExpectMemory(mixed.size() == 8, L"Unexpected mixed synthetic candidate count");
    passed &= ExpectMemory(mixed.size() == 8 &&
                               mixed[0] == L"wubi-exact" && mixed[1] == L"high" &&
                               mixed[4] == L"low" && mixed[5] == L"wubi-prefix" &&
                               mixed[6] == L"prefix-high" && mixed[7] == L"prefix-low",
                           L"Mixed exact/prefix phase ordering failed");

    const auto longCode = LookupMemory(InputScheme::Wubi86, L"zabcd", pinyin, wubi);
    passed &= ExpectMemory(longCode == std::vector<std::wstring>{L"long-pinyin"},
                           L"Wubi table was not skipped beyond four codes");

    std::vector<TestEntry> many;
    many.reserve(251);
    for (size_t i = 0; i < 250; ++i)
    {
        many.push_back({"aa", "item" + std::to_string(i), static_cast<int64_t>(i)});
    }
    many.push_back({"ab", "item249", 10000});
    const auto manyBinary = BuildBinary(std::move(many));
    const auto limited = LookupMemory(InputScheme::Pinyin, L"a", manyBinary);
    passed &= ExpectMemory(limited.size() == 200, L"Prefix result was not limited to 200");
    passed &= ExpectMemory(!limited.empty() && limited.front() == L"item249",
                           L"Best duplicate prefix text did not retain its best rank");
    passed &= ExpectMemory(IsUnique(limited), L"Prefix limit contains duplicate text");
    passed &= ExpectMemory(limited.back() == L"item50", L"Prefix Top-200 truncation is not deterministic");

    const auto valid = BuildBinary({{"a", "ok", 1}});
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", valid) ==
                               std::vector<std::wstring>{L"ok"},
                           L"Valid in-memory dictionary failed");
    auto badMagic = valid;
    badMagic[0] = 'X';
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", badMagic).empty(),
                           L"Invalid magic was accepted");
    auto badVersion = valid;
    badVersion[8] = 2;
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", badVersion).empty(),
                           L"Unsupported version was accepted");
    auto truncated = valid;
    truncated.pop_back();
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", truncated).empty(),
                           L"Truncated dictionary was accepted");
    auto badOffset = valid;
    SetU32(badOffset, kHeaderSize, 0xFFFFFF00U);
    passed &= ExpectMemory(LookupMemory(InputScheme::Pinyin, L"a", badOffset).empty(),
                           L"Invalid entry offset was accepted");

    const auto english = BuildBinary({
        {"apple", "apple", 70},
        {"wonder", "wonder", 80},
        {"word", "word", 100},
        {"wordsmith", "wordsmith", 90},
        {"work", "work", 110},
        {"world", "world", 95},
    });
    passed &= ExpectMemory(LookupEnglishMemory(L"wo", english) ==
                               std::vector<std::wstring>{L"work", L"word", L"world", L"wordsmith", L"wonder"},
                           L"English prefix weight ordering failed");
    passed &= ExpectMemory(LookupEnglishMemory(L"word", english).front() == L"word",
                           L"English exact word was not first");
    passed &= ExpectMemory(LookupEnglishMemory(L"wonder", english).front() == L"wonder" &&
                               LookupEnglishMemory(L"Wonder", english).front() == L"Wonder" &&
                               LookupEnglishMemory(L"WONDER", english).front() == L"WONDER" &&
                               LookupEnglishMemory(L"wONDER", english).front() == L"wonder" &&
                               LookupEnglishMemory(L"A", english).front() == L"Apple",
                           L"English candidate case conversion failed");
    passed &= ExpectMemory(LookupEnglishMemory(L"word1", english).empty() &&
                               LookupEnglishMemory(L"word-", english).empty() &&
                               LookupEnglishMemory(L"单词", english).empty() &&
                               LookupEnglishMemory(L"", english).empty() &&
                               LookupEnglishMemory(std::wstring(65, L'a').c_str(), english).empty(),
                           L"Invalid English input was accepted");
    return passed;
}

struct MemoryState
{
    SIZE_T privateBytes;
    SIZE_T workingSet;
};

MemoryState GetMemoryState()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
    {
        return {};
    }
    return {counters.PrivateUsage, counters.WorkingSetSize};
}

class Stopwatch
{
public:
    Stopwatch()
    {
        QueryPerformanceFrequency(&frequency_);
        QueryPerformanceCounter(&start_);
    }

    double Milliseconds() const
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start_.QuadPart) * 1000.0 /
               static_cast<double>(frequency_.QuadPart);
    }

private:
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER start_{};
};

double Percentile(std::vector<double> sorted, double percentile)
{
    const size_t index = static_cast<size_t>(std::ceil(percentile * sorted.size())) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

std::string StressCode(size_t value)
{
    std::string code(6, 'a');
    for (size_t position = code.size(); position-- > 0;)
    {
        code[position] = static_cast<char>('a' + value % 26);
        value /= 26;
    }
    return code;
}

bool RunStressSize(size_t entryCount)
{
    std::vector<TestEntry> entries;
    entries.reserve(entryCount);
    for (size_t i = 0; i < entryCount; ++i)
    {
        entries.push_back({StressCode(i),
                           "candidate" + std::to_string(i),
                           static_cast<int64_t>((i * 7919) % 1000003)});
    }
    const auto binary = BuildBinary(std::move(entries));
    const auto before = GetMemoryState();

    double coldMilliseconds = 0.0;
    {
        Stopwatch stopwatch;
        const auto result = LookupMemory(InputScheme::Pinyin, L"a", binary);
        coldMilliseconds = stopwatch.Milliseconds();
        if (result.size() != 200)
        {
            std::wcerr << L"Stress cold query returned an unexpected count\n";
            return false;
        }
    }

    const std::array<std::wstring, 7> queries{L"a", L"aa", L"aaa", L"aaaa", L"aaaaa", L"aaaaaa", L"zzzzzz"};
    constexpr size_t kRepetitions = 5;
    std::vector<double> allTimes;
    allTimes.reserve(queries.size() * kRepetitions);
    size_t checksum = 0;
    std::wcout << L"STRESS entries=" << entryCount << L" binary_bytes=" << binary.size()
               << L" cold_ms=" << coldMilliseconds << L'\n';
    bool queryPassed = true;
    for (const auto& query : queries)
    {
        std::vector<double> times;
        times.reserve(kRepetitions);
        for (size_t repetition = 0; repetition < kRepetitions; ++repetition)
        {
            Stopwatch stopwatch;
            const auto result = LookupMemory(InputScheme::Pinyin, query.c_str(), binary);
            const double elapsed = stopwatch.Milliseconds();
            checksum += result.size();
            times.push_back(elapsed);
            allTimes.push_back(elapsed);
        }
        std::sort(times.begin(), times.end());
        const double p95 = Percentile(times, 0.95);
        std::wcout << L"QUERY code=" << query << L" samples=" << times.size()
                   << L" p50_ms=" << Percentile(times, 0.50) << L" p95_ms=" << p95 << L'\n';
        if (query != L"zzzzzz" && p95 > 50.0)
            queryPassed = false;
    }
    std::sort(allTimes.begin(), allTimes.end());
    const auto after = GetMemoryState();
    const SIZE_T privateDelta = after.privateBytes >= before.privateBytes
        ? after.privateBytes - before.privateBytes : 0;
    const SIZE_T workingSetDelta = after.workingSet >= before.workingSet
        ? after.workingSet - before.workingSet : 0;
    std::wcout << L"SUMMARY entries=" << entryCount << L" samples=" << allTimes.size()
               << L" repetitions=" << kRepetitions << L" timer=QueryPerformanceCounter"
               << L" p50_ms=" << Percentile(allTimes, 0.50)
               << L" p95_ms=" << Percentile(allTimes, 0.95)
               << L" private_delta_bytes=" << privateDelta
               << L" working_set_delta_bytes=" << workingSetDelta
               << L" checksum=" << checksum << L'\n';
    if (privateDelta > 40ULL * 1024 * 1024)
    {
        std::wcerr << L"Stress private memory delta exceeded 40 MiB\n";
        queryPassed = false;
    }
    return queryPassed;
}

bool RunStressTests()
{
    bool passed = true;
    for (const size_t count : {size_t{100000}, size_t{500000}, size_t{1000000}})
        passed &= RunStressSize(count);
    return passed;
}
}

int wmain(int argc, wchar_t* argv[])
{
    HMODULE module = GetModuleHandleW(nullptr);
    const HMODULE resourceModule = module;
    bool passed = RunRealDictionaryTests(resourceModule);
    passed &= RunSyntheticTests();
    if (!passed)
        return 1;

    if (argc == 2 && std::wcscmp(argv[1], L"--stress") == 0)
    {
        if (!RunStressTests())
            return 1;
        std::wcout << L"Dictionary stress tests passed\n";
        return 0;
    }

    std::wcout << L"Dictionary tests passed\n";
    return 0;
}
