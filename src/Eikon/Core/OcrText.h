#pragma once

// Text side of OCR and the Windows engine plan for the OCR language setting. Platform/Ocr runs the engines; everything here is pure text.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon::Core::OcrText
{
    // Full-width punctuation replaced, then the first "  "
    // (two spaces) replaced by " " in a single pass (three spaces become two).
    std::wstring CleanUp(std::wstring text);

    // ocrRemoveBreaks: every run of one or more "\n" becomes one space; nothing else changes.
    std::wstring RemoveBreaks(std::wstring_view text);

    // The copied text with "\n" -> " "; longer than 120 characters -> the first 107 + "...".
    // Characters are counted in code points (surrogate pairs are never split).
    std::wstring NotificationBody(std::wstring_view text);

    // Editor OCR stops with "Area is too large" at ≥ 3001 px high or ≥ 6001 px wide.
    bool IsTooLargeForEditor(std::int64_t widthPx, std::int64_t heightPx) noexcept;

    // The OCR language setting for Windows.Media.Ocr, which runs one language per engine:
    // the engines to try, in order, until one returns text.
    //   "en-US"            -> en-US
    //   contains "zh"      -> zh-Hans, zh-Hant, en-US
    //   contains "yue"     -> zh-Hans, zh-Hant, en-US (Windows has no Cantonese; same script)
    //   anything else (X)  -> X, en-US
    // Languages Windows has no OCR engine for are replaced by the closest one that reads the same
    // script (uk -> ru, ars -> ar, nn/no -> nb) or dropped so that en-US reads them (Latin-script
    // id, ms, vi; th, hi, mr have no Windows engine at all).
    std::vector<std::wstring> EngineLanguages(std::wstring_view primaryOcrLang);

    // Picks the recognizer for `wanted` among `available` BCP-47 tags (OcrEngine.AvailableRecognizerLanguages):
    // an exact (case-insensitive) match first, then the same language with the same script for
    // Chinese ("zh-Hans" matches "zh-Hans-CN" and "zh-CN"; "zh-Hant" matches "zh-Hant-TW", "zh-TW",
    // "zh-HK"), then the same primary language for the rest ("de-DE" matches "de-CH").
    std::optional<std::size_t> MatchLanguage(std::wstring_view wanted, std::vector<std::wstring> const& available);

    // Joins the words of one recognised line. Windows OCR splits Chinese and Japanese text into one
    // "word" per character; they read unspaced, so no space is put between two CJK
    // characters (Han, kana, CJK punctuation, full-width forms). Hangul keeps its spaces.
    std::wstring JoinWords(std::vector<std::wstring> const& words);
}
