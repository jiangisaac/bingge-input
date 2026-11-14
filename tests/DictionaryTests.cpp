#include "TV1TSF.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>

namespace
{
bool Contains(const std::vector<std::wstring>& candidates, const wchar_t* expected)
{
    return std::find(candidates.begin(), candidates.end(), expected) != candidates.end();
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
}

int wmain()
{
    HMODULE module = GetModuleHandleW(nullptr);
    const HMODULE resourceModule = module;
    bool passed = true;
    passed &= ExpectCandidates(InputScheme::Pinyin, L"wen", resourceModule, {L"文", L"问"});
    passed &= ExpectCandidates(InputScheme::Pinyin, L"jiang", resourceModule, {L"江", L"讲", L"将"});
    passed &= ExpectCandidates(InputScheme::Pinyin, L"nihao", resourceModule, {L"你好"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"a", resourceModule, {L"工"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"wq", resourceModule, {L"你"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"ggtt", resourceModule, {L"五笔"});
    passed &= ExpectCandidates(InputScheme::Wubi86, L"nihao", resourceModule, {L"你好"});

    const auto mixedWen = LookupCandidates(InputScheme::Wubi86, L"wen", resourceModule);
    if (mixedWen.empty() || mixedWen.front() != L"仍" ||
        !Contains(mixedWen, L"文") || !Contains(mixedWen, L"问"))
    {
        std::wcerr << L"Mixed Wubi/Pinyin ordering failed for wen\n";
        passed = false;
    }
    const std::unordered_set<std::wstring> uniqueWen(mixedWen.begin(), mixedWen.end());
    if (uniqueWen.size() != mixedWen.size())
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

    if (!LookupCandidates(InputScheme::Pinyin, L"not-a-pinyin-code", resourceModule).empty() ||
        !LookupCandidates(InputScheme::Wubi86, L"not-a-pinyin-code", resourceModule).empty())
    {
        std::wcerr << L"Unknown code unexpectedly returned candidates\n";
        passed = false;
    }

    if (!passed)
        return 1;
    std::wcout << L"Dictionary tests passed\n";
    return 0;
}
