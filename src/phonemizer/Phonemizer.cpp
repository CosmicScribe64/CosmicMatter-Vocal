#include "Phonemizer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vocalrack {

static std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out.push_back(c); ++i; continue; }
        int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
        uint32_t cp = c & ((1u << (6 - n)) - 1u);
        for (int j = 0; j < n && i + 1 < s.size(); ++j) cp = (cp << 6) | (static_cast<unsigned char>(s[++i]) & 0x3F);
        out.push_back(cp); ++i;
    }
    return out;
}

static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::string romajiToHiragana(const std::string& input) {
    if (input.empty() || input == "-" || input.front() == '+') return input;
    std::string source;
    source.reserve(input.size());
    for (const unsigned char c : input) {
        if (!(std::isalpha(c) || c == '\'')) return input;
        source.push_back(static_cast<char>(std::tolower(c)));
    }

    // Longest-match Hepburn/Kunrei spellings cover ordinary Japanese lyric
    // entry plus the contracted/foreign aliases present in Adachi Rei.
    static const std::unordered_map<std::string, const char*> kana = {
        {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
        {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
        {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
        {"sya", "しゃ"}, {"syu", "しゅ"}, {"syo", "しょ"},
        {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"},
        {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
        {"zya", "じゃ"}, {"zyu", "じゅ"}, {"zyo", "じょ"},
        {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
        {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
        {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
        {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
        {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
        {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
        {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
        {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
        {"tsa", "つぁ"}, {"tsi", "つぃ"}, {"tse", "つぇ"}, {"tso", "つぉ"},
        {"shi", "し"}, {"chi", "ち"}, {"tsu", "つ"},
        {"dzu", "づ"}, {"dji", "ぢ"},
        {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
        {"fyu", "ふゅ"}, {"she", "しぇ"}, {"je", "じぇ"}, {"che", "ちぇ"},
        {"ti", "てぃ"}, {"di", "でぃ"}, {"tu", "とぅ"}, {"du", "どぅ"},
        {"va", "ゔぁ"}, {"vi", "ゔぃ"}, {"vu", "ゔ"}, {"ve", "ゔぇ"}, {"vo", "ゔぉ"},
        {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
        {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
        {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
        {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
        {"ta", "た"}, {"te", "て"}, {"to", "と"},
        {"da", "だ"}, {"de", "で"}, {"do", "ど"},
        {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
        {"ha", "は"}, {"hi", "ひ"}, {"fu", "ふ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
        {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
        {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
        {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
        {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
        {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
        {"wa", "わ"}, {"wi", "うぃ"}, {"we", "うぇ"}, {"wo", "を"},
        {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    };

    std::string result;
    for (size_t index = 0; index < source.size();) {
        if (source[index] == '\'') {
            if (index > 0 && source[index - 1] == 'n') { ++index; continue; }
            return input;
        }
        if (index + 1 < source.size() && source[index] == source[index + 1] &&
            source[index] != 'a' && source[index] != 'i' && source[index] != 'u' &&
            source[index] != 'e' && source[index] != 'o' && source[index] != 'n') {
            result += "っ";
            ++index;
            continue;
        }
        bool matched = false;
        for (size_t length : {size_t(3), size_t(2), size_t(1)}) {
            if (index + length > source.size()) continue;
            const auto found = kana.find(source.substr(index, length));
            if (found == kana.end()) continue;
            result += found->second;
            index += length;
            matched = true;
            break;
        }
        if (matched) continue;
        if (source[index] == 'n') {
            result += "ん";
            ++index;
            if (index < source.size() && source[index] == '\'') ++index;
            continue;
        }
        return input;
    }
    return result;
}

std::string normalizeJapanese(const std::string& text) {
    const auto romaji = romajiToHiragana(text);
    std::string out;
    for (auto cp : decodeUtf8(romaji)) {
        if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;  // Katakana to hiragana.
        if (cp == 0x30F7) { appendUtf8(out, 0x308F); appendUtf8(out, 0x3099); continue; }
        appendUtf8(out, cp);
    }
    return out;
}

bool validJapaneseLyricInput(const std::string& text) {
    const auto normalized = normalizeJapanese(text);
    if (normalized.empty()) return false;
    if (normalized == "-" || normalized == "ー" || normalized.front() == '+') return true;
    // Successful romaji conversion contains kana rather than ASCII letters.
    // Scope this check to Japanese Auto; Direct Alias and other language
    // phonemizers may accept Latin phonetic symbols.
    return std::none_of(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return ch < 0x80 && std::isalpha(ch);
    });
}

namespace {

std::string trimAscii(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> result;
    std::stringstream input(text);
    for (std::string item; input >> item;) result.push_back(item);
    return result;
}

std::string normalizeEnglishSymbol(std::string symbol) {
    while (!symbol.empty() && std::isdigit(static_cast<unsigned char>(symbol.back()))) symbol.pop_back();
    std::string lower;
    lower.reserve(symbol.size());
    for (unsigned char ch : symbol) lower.push_back(static_cast<char>(std::tolower(ch)));
    static const std::unordered_map<std::string, std::string> arpabet = {
        {"aa", "A"}, {"ae", "{"}, {"ah", "V"}, {"ao", "O"}, {"aw", "aU"},
        {"ax", "@"}, {"ay", "aI"}, {"ch", "tS"}, {"dh", "D"}, {"dx", "4"},
        {"eh", "E"}, {"er", "3"}, {"ey", "eI"}, {"hh", "h"}, {"ih", "I"},
        {"iy", "i"}, {"jh", "dZ"}, {"ng", "N"}, {"ow", "oU"}, {"oy", "OI"},
        {"sh", "S"}, {"th", "T"}, {"uh", "U"}, {"uw", "u"}, {"y", "j"},
        {"zh", "Z"},
    };
    if (const auto found = arpabet.find(lower); found != arpabet.end()) return found->second;
    static const std::unordered_set<std::string> plainConsonants = {
        "b", "d", "f", "g", "k", "l", "m", "n", "p", "r", "s", "t", "v", "w", "z"};
    if (plainConsonants.count(lower)) return lower;
    // Preserve X-SAMPA's meaningful case and punctuation when the input was
    // already phonetic rather than ARPAbet.
    return symbol;
}

bool isXSampaVowel(const std::string& symbol) {
    static const std::unordered_set<std::string> vowels = {
        "a", "A", "@", "{", "V", "O", "aU", "aI", "E", "3", "eI", "I", "i",
        "oU", "OI", "U", "u", "Q", "Ol", "Ql", "aUn", "e@", "eN", "IN", "e", "o",
        "Ar", "Qr", "Er", "Ir", "Or", "Ur", "ir", "ur", "aIr", "aUr", "A@", "Q@",
        "E@", "I@", "O@", "U@", "i@", "u@", "aI@", "aU@", "@r", "@l", "@m", "@n",
        "@N", "1", "y", "I\\", "M", "U\\", "Y", "@\\", "@`", "3`", "A`", "Q`",
        "E`", "I`", "O`", "U`", "i`", "u`", "aI`", "aU`", "}", "2", "3\\", "6",
        "7", "8", "9", "&", "{~", "I~", "aU~", "VI", "VU", "@U", "ai", "ei", "Oi",
        "au", "ou", "Ou", "@u", "i:", "u:", "O:", "e@0", "E~", "e~", "3r", "ar",
        "or", "{l", "Al", "al", "El", "Il", "il", "ol", "ul", "Ul", "oUl", "@5",
        "u5", "O5", "A5", "E5", "I5", "i5", "mm", "nn", "ll", "NN",
    };
    return vowels.count(symbol) != 0;
}

std::vector<std::string> mappedSymbols(const std::string& symbols, bool arpabet) {
    auto result = words(symbols);
    if (arpabet) for (auto& symbol : result) symbol = normalizeEnglishSymbol(symbol);
    return result;
}

std::vector<std::string> fallbackEnglishG2p(const std::string& raw) {
    std::string text;
    for (unsigned char ch : raw) {
        if (std::isalpha(ch) || ch == '\'') text.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (text.empty()) return {};
    if (text.size() > 2 && text.compare(0, 2, "kn") == 0) text.erase(text.begin());
    if (text.size() > 2 && text.compare(0, 2, "wr") == 0) text.erase(text.begin());
    std::vector<std::string> result;
    const auto add = [&](const char* value) { result.emplace_back(value); };
    for (size_t index = 0; index < text.size();) {
        const auto has = [&](const char* value) {
            const std::string candidate(value);
            return text.compare(index, candidate.size(), candidate) == 0;
        };
        if (has("tion")) { add("S"); add("@"); add("n"); index += 4; }
        else if (has("tch")) { add("tS"); index += 3; }
        else if (has("dge")) { add("dZ"); index += 3; }
        else if (has("ch")) { add("tS"); index += 2; }
        else if (has("sh")) { add("S"); index += 2; }
        else if (has("th")) { add("T"); index += 2; }
        else if (has("ng")) { add("N"); index += 2; }
        else if (has("ph")) { add("f"); index += 2; }
        else if (has("qu")) { add("k"); add("w"); index += 2; }
        else if (has("ck")) { add("k"); index += 2; }
        else if (has("ee") || has("ea")) { add("i"); index += 2; }
        else if (has("oo")) { add("u"); index += 2; }
        else if (has("ai") || has("ay")) { add("eI"); index += 2; }
        else if (has("oa")) { add("oU"); index += 2; }
        else if (has("ow")) { add("aU"); index += 2; }
        else {
            const char ch = text[index++];
            if (ch == 'e' && index == text.size() && text.size() > 2) continue;  // common silent final e
            static const std::unordered_map<char, const char*> single = {
                {'a', "{"}, {'b', "b"}, {'c', "k"}, {'d', "d"}, {'e', "E"}, {'f', "f"},
                {'g', "g"}, {'h', "h"}, {'i', "I"}, {'j', "dZ"}, {'k', "k"}, {'l', "l"},
                {'m', "m"}, {'n', "n"}, {'o', "A"}, {'p', "p"}, {'q', "k"}, {'r', "r"},
                {'s', "s"}, {'t', "t"}, {'u', "V"}, {'v', "v"}, {'w', "w"}, {'x', "ks"},
                {'y', "i"}, {'z', "z"},
            };
            if (const auto found = single.find(ch); found != single.end()) add(found->second);
        }
    }
    return result;
}

struct EnglishDictionaryState {
    std::mutex mutex;
    std::filesystem::path path = "res/dictionaries/cmudict-0.7b.txt";
    std::vector<std::pair<std::string, std::string>> entries;
    bool attempted = false;
};

EnglishDictionaryState& englishDictionaryState() {
    static EnglishDictionaryState state;
    return state;
}

std::string dictionaryPronunciation(const std::string& key) {
    auto& state = englishDictionaryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.attempted) {
        state.attempted = true;
        std::ifstream input(state.path, std::ios::binary);
        for (std::string line; std::getline(input, line);) {
            if (line.empty() || line[0] == ';') continue;
            std::istringstream row(line);
            std::string word;
            row >> word;
            if (word.empty() || word.find('(') != std::string::npos) continue;
            std::string pronunciation;
            std::getline(row, pronunciation);
            pronunciation = trimAscii(pronunciation);
            std::transform(word.begin(), word.end(), word.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (!pronunciation.empty())
                state.entries.emplace_back(std::move(word), std::move(pronunciation));
        }
        std::sort(state.entries.begin(), state.entries.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
    }
    const auto found = std::lower_bound(
        state.entries.begin(), state.entries.end(), key,
        [](const auto& entry, const std::string& value) { return entry.first < value; });
    return found != state.entries.end() && found->first == key ? found->second : std::string();
}

}  // namespace

void setEnglishDictionaryPath(const std::filesystem::path& path) {
    auto& state = englishDictionaryState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.path == path && state.attempted) return;
    state.path = path;
    state.entries.clear();
    state.attempted = false;
}

std::vector<std::string> englishToXSampa(const std::string& lyric) {
    const auto open = lyric.rfind('[');
    const auto close = lyric.rfind(']');
    if (open != std::string::npos && close != std::string::npos && close > open)
        return mappedSymbols(lyric.substr(open + 1, close - open - 1), false);

    std::string key;
    for (unsigned char ch : trimAscii(lyric)) {
        if (std::isalpha(ch) || ch == '\'' || ch == '-') key.push_back(static_cast<char>(std::tolower(ch)));
    }
    const auto dictionary = dictionaryPronunciation(key);
    if (!dictionary.empty()) return mappedSymbols(dictionary, true);

    // Product names and singing-synth vocabulary absent from CMUdict retain
    // deterministic pronunciations instead of falling through to spelling.
    static const std::unordered_map<std::string, const char*> lexicon = {
        {"a", "AH"}, {"and", "AE N D"}, {"are", "AA R"}, {"be", "B IY"},
        {"can", "K AE N"}, {"do", "D UW"}, {"english", "IH NG G L IH SH"},
        {"for", "F AO R"}, {"hello", "HH AH L OW"}, {"hi", "HH AY"}, {"i", "AY"}, {"in", "IH N"},
        {"is", "IH Z"}, {"it", "IH T"}, {"me", "M IY"}, {"my", "M AY"},
        {"no", "N OW"}, {"of", "AH V"}, {"on", "AA N"},
        // OpenUtau's built-in Arpabet fallback treats these product names as
        // "open-ah-tau" and "you-toe". Keep the same deterministic symbols
        // so project/song vocabulary aligns across both editors.
        {"openutau", "OW P EH N AH T AW"},
        {"all", "AO L"}, {"an", "AE N"}, {"barn", "B AA R N"},
        {"cute", "K Y UW T"}, {"itch", "IH CH"}, {"its", "IH T S"},
        {"read", "R EH D"}, {"rack", "R AE K"}, {"sing", "S IH NG"},
        {"soon", "S UW N"}, {"star", "S T AA R"}, {"tea", "T IY"},
        {"test", "T EH S T"}, {"testing", "T EH S T IH NG"},
        {"the", "DH AH"}, {"this", "DH IH S"},
        {"to", "T UW"}, {"utau", "Y UW T OW"}, {"vocal", "V OW K AH L"},
        {"vocalrack", "V OW K AH L R AE K"}, {"voice", "V OY S"},
        {"vu", "V UW"}, {"we", "W IY"}, {"with", "W IH DH"},
        {"words", "W ER D Z"}, {"you", "Y UW"},
    };
    if (const auto found = lexicon.find(key); found != lexicon.end()) return mappedSymbols(found->second, true);
    return fallbackEnglishG2p(key);
}

std::string trailingXSampaVowel(const std::string& lyric) {
    const auto symbols = englishToXSampa(lyric);
    for (auto item = symbols.rbegin(); item != symbols.rend(); ++item)
        if (isXSampaVowel(*item)) return *item;
    return {};
}

std::string trailingVowel(const std::string& raw) {
    const auto text = normalizeJapanese(raw);
    const auto cps = decodeUtf8(text);
    if (cps.empty()) return {};
    const uint32_t cp = cps.back();
    static const std::unordered_map<uint32_t, const char*> special = {
        {0x3093, "n"}, {0x3063, ""}, {0x3041, "a"}, {0x3043, "i"}, {0x3045, "u"},
        {0x3047, "e"}, {0x3049, "o"}, {0x3083, "a"}, {0x3085, "u"}, {0x3087, "o"}
    };
    if (auto it = special.find(cp); it != special.end()) return it->second;
    static const std::string a = "あかがさざただなはばぱまやらわ";
    static const std::string i = "いきぎしじちぢにひびぴみりゐ";
    static const std::string u = "うくぐすずつづぬふぶぷむゆるゔ";
    static const std::string e = "えけげせぜてでねへべぺめれゑ";
    static const std::string o = "おこごそぞとどのほぼぽもよろを";
    std::string one; appendUtf8(one, cp);
    if (a.find(one) != std::string::npos) return "a";
    if (i.find(one) != std::string::npos) return "i";
    if (u.find(one) != std::string::npos) return "u";
    if (e.find(one) != std::string::npos) return "e";
    if (o.find(one) != std::string::npos) return "o";
    return {};
}

bool lyricIsExtender(const std::string& lyric) {
    const auto normalized = normalizeJapanese(lyric);
    if (normalized == "-" || normalized == "ー") return true;
    return !normalized.empty() && normalized.front() == '+';
}

std::string sustainedVowelKana(const Note* previous) {
    if (!previous) return {};
    const auto previousText = previous->aliasOverride.value_or(previous->lyric);
    const auto vowel = trailingVowel(previousText);
    const std::unordered_map<std::string, std::string> kana{
        {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"}, {"n", "ん"}};
    const auto found = kana.find(vowel);
    return found == kana.end() ? std::string{} : found->second;
}

static void attempt(PhonemeEvent& event, const Voicebank& singer, const std::string& alias) {
    if (alias.empty() || event.oto) return;
    const auto* oto = singer.findAlias(alias, event.targetMidiNote);
    event.attempts.push_back({alias, oto != nullptr});
    if (oto) { event.oto = oto; event.selectedAlias = oto->alias; }
}

std::vector<PhonemeEvent> IPhonemizer::processAll(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    return {process(note, previous, next, singer)};
}

std::vector<PhonemeEvent> IPhonemizer::processAllChain(
    const std::vector<Note>& notes, const Note* previous, const Note* next,
    const Voicebank& singer) const {
    if (notes.empty()) return {};
    Note combined = notes.front();
    combined.durationTick = notes.back().endTick() - combined.startTick;
    return processAll(combined, previous, next, singer);
}

namespace {

std::vector<std::string> japaneseVowelsForXSampa(const std::string& vowel) {
    static const std::unordered_map<std::string, std::vector<std::string>> mapped = {
        // Match OpenUtau EN-to-JA's replacement table. In particular ARPAbet
        // AE (X-SAMPA `{`) is adapted to Japanese /e/, not /a/. This is
        // audible in connected phrases such as "an up" (え to な to ぷ).
        {"a", {"a"}}, {"A", {"a"}}, {"@", {"a"}}, {"{", {"e"}}, {"V", {"a"}},
        {"O", {"o"}}, {"Q", {"o"}}, {"E", {"e"}}, {"3", {"o"}}, {"I", {"e"}},
        {"i", {"i"}}, {"U", {"o"}}, {"u", {"u"}}, {"e", {"e"}}, {"o", {"o"}},
        {"aI", {"a", "i"}}, {"eI", {"e", "i"}}, {"OI", {"o", "i"}},
        {"aU", {"a", "u"}}, {"oU", {"o", "u"}}, {"VI", {"a", "i"}},
        {"VU", {"a", "u"}}, {"@U", {"o", "u"}}, {"ai", {"a", "i"}},
        {"ei", {"e", "i"}}, {"Oi", {"o", "i"}}, {"au", {"a", "u"}},
        {"ou", {"o", "u"}}, {"Ou", {"o", "u"}}, {"@u", {"o", "u"}},
    };
    if (const auto found = mapped.find(vowel); found != mapped.end()) return found->second;
    // Rhotic, nasalized, and long variants retain the closest base vowel.
    if (!vowel.empty()) {
        if (vowel.find('i') != std::string::npos) return {"i"};
        if (vowel.find('u') != std::string::npos || vowel.find('U') != std::string::npos) return {"u"};
        if (vowel.find('e') != std::string::npos || vowel.find('E') != std::string::npos) return {"e"};
        if (vowel.find('o') != std::string::npos || vowel.find('O') != std::string::npos) return {"o"};
    }
    return {"a"};
}

std::string japaneseSoloForXSampa(const std::string& consonant) {
    static const std::unordered_map<std::string, std::string> mapped = {
        {"b", "ぶ"}, {"tS", "ちゅ"}, {"d", "ど"}, {"D", "ず"}, {"4", "る"},
        {"f", "ふ"}, {"g", "ぐ"}, {"h", "ほ"}, {"dZ", "じゅ"}, {"k", "く"},
        {"l", "う"}, {"m", "む"}, {"n", "ん"}, {"N", "ん"}, {"p", "ぷ"},
        {"r", "う"}, {"r\\", "う"}, {"s", "す"}, {"S", "しゅ"}, {"t", "と"},
        {"T", "す"}, {"v", "ふ"}, {"w", "う"}, {"W", "う"}, {"j", "い"},
        {"z", "ず"}, {"Z", "しゅ"}, {"ky", "き"}, {"gy", "ぎ"},
        {"ny", "に"}, {"hy", "ひ"}, {"by", "び"}, {"py", "ぴ"},
        {"my", "み"}, {"ry", "り"}, {"ly", "り"}, {"ts", "つ"},
    };
    if (const auto found = mapped.find(consonant); found != mapped.end()) return found->second;
    return "う";
}

std::string japaneseCvForXSampa(const std::string& consonant, const std::string& vowel) {
    static const std::unordered_map<std::string, std::string> onset = {
        {"", ""}, {"b", "b"}, {"tS", "ch"}, {"d", "d"}, {"D", "d"},
        {"4", "r"}, {"f", "f"}, {"g", "g"}, {"h", "h"}, {"dZ", "j"},
        {"k", "k"}, {"l", "r"}, {"m", "m"}, {"n", "n"}, {"N", "n"},
        {"p", "p"}, {"r", "w"}, {"r\\", "w"}, {"s", "s"}, {"S", "sh"},
        {"t", "t"}, {"T", "s"}, {"v", "v"}, {"w", "w"}, {"W", "w"},
        {"j", "y"}, {"z", "z"}, {"Z", "sh"}, {"ky", "ky"},
        {"gy", "gy"}, {"ny", "ny"}, {"hy", "hy"}, {"by", "by"},
        {"py", "py"}, {"my", "my"}, {"ry", "ry"}, {"ly", "ry"},
        {"ts", "ts"},
    };
    const auto found = onset.find(consonant);
    const std::string roman = (found == onset.end() ? std::string() : found->second) + vowel;
    if (roman == "va") return "ヴぁ";
    if (roman == "vi") return "ヴぃ";
    if (roman == "vu") return "ヴ";
    if (roman == "ve") return "ヴぇ";
    if (roman == "vo") return "ヴぉ";
    if (roman == "ye") return "いぇ";
    if (roman == "wo") return "うぉ";
    if (roman == "si") return "せぃ";
    if (roman == "ti") return "てぃ";
    if (roman == "tu") return "とぅ";
    if (roman == "di") return "でぃ";
    if (roman == "du") return "どぅ";
    auto kana = normalizeJapanese(roman);
    if (!kana.empty() && kana != roman) return kana;
    return vowel == "a" ? "あ" : vowel == "i" ? "い" : vowel == "u" ? "う"
        : vowel == "e" ? "え" : "お";
}

struct EnglishJapaneseSyllable {
    std::vector<std::string> aliases;
    std::vector<std::string> consonants;
    std::string vowel;
};

struct EnglishJapanesePlan {
    std::vector<EnglishJapaneseSyllable> syllables;
    std::vector<std::string> ending;
    std::vector<std::string> endingConsonants;
};

std::vector<std::string> collapseJapaneseClusters(std::vector<std::string> consonants) {
    static const std::unordered_map<std::string, std::string> clusters = {
        {"kj", "ky"}, {"gj", "gy"}, {"nj", "ny"}, {"hj", "hy"},
        {"bj", "by"}, {"pj", "py"}, {"mj", "my"}, {"rj", "ry"},
        {"lj", "ly"}, {"ts", "ts"},
    };
    std::vector<std::string> adjusted;
    adjusted.reserve(consonants.size());
    for (size_t index = 0; index < consonants.size();) {
        if (index + 1 < consonants.size()) {
            // SyllableBasedPhonemizer removes doubled boundary consonants.
            // The rule applies at joins such as test-to-test and my-to-you.
            if (consonants[index] == consonants[index + 1]) {
                adjusted.push_back(consonants[index]);
                index += 2;
                continue;
            }
            const auto key = consonants[index] + consonants[index + 1];
            if (const auto found = clusters.find(key); found != clusters.end()) {
                adjusted.push_back(found->second);
                index += 2;
                continue;
            }
        }
        adjusted.push_back(consonants[index]);
        ++index;
    }
    return adjusted;
}

std::vector<std::string> japaneseAliasesForSyllable(
    const std::vector<std::string>& consonants, const std::string& vowel) {
    std::vector<std::string> aliases;
    for (size_t index = 0; index + 1 < consonants.size(); ++index)
        aliases.push_back(japaneseSoloForXSampa(consonants[index]));
    aliases.push_back(japaneseCvForXSampa(
        consonants.empty() ? std::string() : consonants.back(), vowel));
    return aliases;
}

EnglishJapanesePlan englishAsJapanesePlan(const std::string& lyric) {
    const auto symbols = englishToXSampa(lyric);
    EnglishJapanesePlan plan;
    std::vector<std::string> pendingConsonants;
    for (const auto& symbol : symbols) {
        if (!isXSampaVowel(symbol)) {
            pendingConsonants.push_back(symbol);
            continue;
        }
        const auto vowels = japaneseVowelsForXSampa(symbol);
        if (vowels.empty()) continue;
        auto consonants = collapseJapaneseClusters(std::move(pendingConsonants));
        pendingConsonants.clear();
        EnglishJapaneseSyllable syllable;
        syllable.aliases = japaneseAliasesForSyllable(consonants, vowels.front());
        syllable.consonants = std::move(consonants);
        syllable.vowel = vowels.front();
        plan.syllables.push_back(std::move(syllable));

        // OpenUtau's EN-to-JA phonemizer expands diphthongs into a stretchable
        // vowel plus a y/w ending. Treating the second half as another vowel
        // put it in the middle of a note and made words such as "my" and
        // "voice" form differently from Classic.
        for (size_t index = 1; index < vowels.size(); ++index)
            pendingConsonants.push_back(vowels[index] == "i" ? "j" : "w");
    }
    pendingConsonants = collapseJapaneseClusters(std::move(pendingConsonants));
    plan.endingConsonants = pendingConsonants;
    for (const auto& consonant : plan.endingConsonants)
        plan.ending.push_back(japaneseSoloForXSampa(consonant));
    return plan;
}

std::string lastJapaneseVowelForEnglish(const std::string& lyric) {
    const auto plan = englishAsJapanesePlan(lyric);
    return plan.syllables.empty() ? std::string() : plan.syllables.back().vowel;
}

}  // namespace

PhonemeEvent JapaneseAutoPhonemizer::process(const Note& note, const Note* previous, const Note*, const Voicebank& singer) const {
    PhonemeEvent event;
    event.relativeTick = note.startTick;
    event.targetMidiNote = note.midiNote;
    event.sourceNoteId = note.id;
    const std::string authoredLyric = normalizeJapanese(note.lyric);
    const bool extender = !note.aliasOverride && lyricIsExtender(authoredLyric);
    const std::string lyric = extender ? sustainedVowelKana(previous) : authoredLyric;
    event.requestedAlias = note.aliasOverride.value_or(extender ? authoredLyric + " to " + lyric : lyric);
    if (extender && lyric.empty()) {
        event.diagnostic = "Continuation lyric " + note.lyric + " has no preceding vowel";
        return event;
    }
    if (note.aliasOverride) attempt(event, singer, *note.aliasOverride);
    if (!event.oto && previous && previous->endTick() == note.startTick) {
        const auto vowel = trailingVowel(previous->aliasOverride.value_or(previous->lyric));
        if (!vowel.empty()) attempt(event, singer, vowel + " " + lyric);
        attempt(event, singer, "* " + lyric);
    } else if (!event.oto) {
        attempt(event, singer, "- " + lyric);
    }
    attempt(event, singer, lyric);
    event.diagnostic = event.oto ? "Selected " + event.selectedAlias : "Missing alias for lyric " + note.lyric;
    return event;
}

namespace {

std::string leadingJapaneseConsonant(const std::string& lyric) {
    const auto kana = normalizeJapanese(lyric);
    static const std::vector<std::pair<std::string, std::string>> prefixes = {
        {"きゃ", "ky"}, {"きゅ", "ky"}, {"きょ", "ky"},
        {"ぎゃ", "gy"}, {"ぎゅ", "gy"}, {"ぎょ", "gy"},
        {"しゃ", "sh"}, {"しゅ", "sh"}, {"しょ", "sh"},
        {"じゃ", "j"}, {"じゅ", "j"}, {"じょ", "j"},
        {"ちゃ", "ch"}, {"ちゅ", "ch"}, {"ちょ", "ch"},
        {"にゃ", "ny"}, {"にゅ", "ny"}, {"にょ", "ny"},
        {"ひゃ", "hy"}, {"ひゅ", "hy"}, {"ひょ", "hy"},
        {"びゃ", "by"}, {"びゅ", "by"}, {"びょ", "by"},
        {"ぴゃ", "py"}, {"ぴゅ", "py"}, {"ぴょ", "py"},
        {"みゃ", "my"}, {"みゅ", "my"}, {"みょ", "my"},
        {"りゃ", "ry"}, {"りゅ", "ry"}, {"りょ", "ry"},
        {"し", "sh"}, {"ち", "ch"}, {"つ", "ts"}, {"ふ", "f"},
        {"か", "k"}, {"き", "k"}, {"く", "k"}, {"け", "k"}, {"こ", "k"},
        {"が", "g"}, {"ぎ", "g"}, {"ぐ", "g"}, {"げ", "g"}, {"ご", "g"},
        {"さ", "s"}, {"す", "s"}, {"せ", "s"}, {"そ", "s"},
        {"ざ", "z"}, {"じ", "j"}, {"ず", "z"}, {"ぜ", "z"}, {"ぞ", "z"},
        {"た", "t"}, {"て", "t"}, {"と", "t"}, {"だ", "d"}, {"で", "d"}, {"ど", "d"},
        {"な", "n"}, {"に", "n"}, {"ぬ", "n"}, {"ね", "n"}, {"の", "n"},
        {"は", "h"}, {"ひ", "h"}, {"へ", "h"}, {"ほ", "h"},
        {"ば", "b"}, {"び", "b"}, {"ぶ", "b"}, {"べ", "b"}, {"ぼ", "b"},
        {"ぱ", "p"}, {"ぴ", "p"}, {"ぷ", "p"}, {"ぺ", "p"}, {"ぽ", "p"},
        {"ま", "m"}, {"み", "m"}, {"む", "m"}, {"め", "m"}, {"も", "m"},
        {"や", "y"}, {"ゆ", "y"}, {"よ", "y"},
        {"ら", "r"}, {"り", "r"}, {"る", "r"}, {"れ", "r"}, {"ろ", "r"},
        {"わ", "w"}, {"を", "w"}, {"ん", "n"},
    };
    for (const auto& item : prefixes) if (kana.rfind(item.first, 0) == 0) return item.second;
    return {};
}

}  // namespace

PhonemeEvent JapaneseCvvcPhonemizer::process(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    return JapaneseAutoPhonemizer{}.process(note, previous, next, singer);
}

std::vector<PhonemeEvent> JapaneseCvvcPhonemizer::processAll(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    auto primary = process(note, previous, next, singer);
    std::vector<PhonemeEvent> events{std::move(primary)};
    if (note.aliasOverride || lyricIsExtender(note.lyric)) return events;
    const auto vowel = trailingVowel(note.lyric);
    if (vowel.empty()) return events;

    PhonemeEvent transition;
    transition.targetMidiNote = note.midiNote;
    transition.sourceNoteId = note.id;
    if (next && next->startTick == note.endTick() && !lyricIsExtender(next->lyric)) {
        const auto consonant = leadingJapaneseConsonant(next->lyric);
        if (consonant.empty()) return events;
        transition.relativeTick = note.startTick + note.durationTick * 3 / 4;
        transition.requestedAlias = vowel + " " + consonant;
        attempt(transition, singer, transition.requestedAlias);
        attempt(transition, singer, vowel + consonant);
    } else {
        transition.relativeTick = note.startTick + note.durationTick * 4 / 5;
        transition.requestedAlias = vowel + " R";
        attempt(transition, singer, vowel + " R");
        attempt(transition, singer, vowel + " -");
        attempt(transition, singer, vowel + "-");
    }
    // A CV-only bank legitimately has no transition alias; in that case the
    // primary CV still renders and this mode degrades without a false error.
    if (transition.oto) {
        transition.diagnostic = "Japanese CVVC selected " + transition.selectedAlias;
        events.push_back(std::move(transition));
    }
    return events;
}

namespace {

PhonemeEvent englishTransitionEvent(const Note& note, int64_t tick,
                                    const std::vector<std::string>& candidates,
                                    const Voicebank& singer, const std::string& convention) {
    PhonemeEvent event;
    event.relativeTick = tick;
    event.targetMidiNote = note.midiNote;
    event.sourceNoteId = note.id;
    event.requestedAlias = candidates.empty() ? std::string() : candidates.front();
    for (const auto& candidate : candidates) attempt(event, singer, candidate);
    event.diagnostic = event.oto ? convention + " selected " + event.selectedAlias
                                 : "Missing " + convention + " transition " + event.requestedAlias;
    return event;
}

std::string joinedSymbols(const std::vector<std::string>& symbols, size_t begin, size_t end) {
    std::string result;
    for (size_t index = begin; index < end; ++index) result += symbols[index];
    return result;
}

bool englishWordEndsInVowel(const std::string& lyric) {
    const auto symbols = englishToXSampa(lyric);
    return !symbols.empty() && isXSampaVowel(symbols.back());
}

std::vector<PhonemeEvent> xsampaEvents(const Note& note, const Note* previous,
                                       const Note* next, const Voicebank& singer) {
    if (note.aliasOverride) {
        return {englishTransitionEvent(note, note.startTick, {*note.aliasOverride}, singer,
                                       "EN X-SAMPA explicit alias")};
    }
    const bool extender = lyricIsExtender(note.lyric);
    auto symbols = extender && previous
        ? std::vector<std::string>{trailingXSampaVowel(previous->lyric)}
        : englishToXSampa(note.lyric);
    symbols.erase(std::remove(symbols.begin(), symbols.end(), std::string()), symbols.end());
    if (symbols.empty()) {
        auto missing = englishTransitionEvent(note, note.startTick, {note.lyric}, singer, "EN X-SAMPA");
        missing.diagnostic = extender ? "English continuation has no preceding vowel"
                                      : "No EN X-SAMPA pronunciation for lyric " + note.lyric;
        return {std::move(missing)};
    }
    size_t vowelIndex = symbols.size();
    for (size_t index = 0; index < symbols.size(); ++index) {
        if (isXSampaVowel(symbols[index])) { vowelIndex = index; break; }
    }
    if (vowelIndex == symbols.size()) {
        return {englishTransitionEvent(note, note.startTick,
            {joinedSymbols(symbols, 0, symbols.size())}, singer, "EN X-SAMPA")};
    }
    const std::string onset = joinedSymbols(symbols, 0, vowelIndex);
    const auto& vowel = symbols[vowelIndex];
    const bool connectedBefore = previous && previous->endTick() == note.startTick;
    const bool connectedAfter = next && next->startTick == note.endTick();
    const auto previousVowel = connectedBefore && englishWordEndsInVowel(previous->lyric)
        ? trailingXSampaVowel(previous->lyric) : std::string();
    std::vector<PhonemeEvent> events;

    if (connectedBefore && !previousVowel.empty() && !onset.empty()) {
        // Delta banks may provide a complete contextual CV (aI haI). Prefer
        // it exactly as OpenUtau does; otherwise place VC preutterance before
        // the note line and the CV on the line instead of evenly spacing both.
        auto contextual = englishTransitionEvent(note, note.startTick,
            {previousVowel + " " + onset + vowel, previousVowel + onset + vowel},
            singer, "EN X-SAMPA");
        if (contextual.oto) {
            events.push_back(std::move(contextual));
        } else {
            events.push_back(englishTransitionEvent(note, note.startTick - 105,
                {previousVowel + " " + onset, previousVowel + onset}, singer, "EN X-SAMPA"));
            events.push_back(englishTransitionEvent(note, note.startTick,
                {onset + vowel, onset + " " + vowel}, singer, "EN X-SAMPA"));
        }
    } else if (connectedBefore && !previousVowel.empty()) {
        events.push_back(englishTransitionEvent(note, note.startTick,
            {previousVowel + " " + vowel, previousVowel + vowel, vowel}, singer, "EN X-SAMPA"));
    } else if (connectedBefore) {
        events.push_back(englishTransitionEvent(note, note.startTick,
            {onset + vowel, onset + " " + vowel}, singer, "EN X-SAMPA"));
    } else {
        events.push_back(englishTransitionEvent(note, note.startTick,
            {"- " + onset + vowel, "-" + onset + vowel, onset + vowel}, singer, "EN X-SAMPA"));
    }

    std::vector<std::string> coda(
        symbols.begin() + static_cast<std::ptrdiff_t>(vowelIndex + 1), symbols.end());
    if (coda.empty()) {
        if (!connectedAfter) {
            events.push_back(englishTransitionEvent(note, note.endTick() - 60,
                {vowel + " -", vowel + "-", vowel + " R"}, singer, "EN X-SAMPA"));
        }
        return events;
    }

    const std::string codaText = joinedSymbols(coda, 0, coda.size());
    const std::string ending = connectedAfter ? std::string() : "-";
    auto cluster = englishTransitionEvent(note,
        note.endTick() - (connectedAfter ? 105 : 60),
        {vowel + " " + codaText + ending}, singer, "EN X-SAMPA");
    if (cluster.oto) {
        events.push_back(std::move(cluster));
        return events;
    }

    for (size_t index = 0; index < coda.size(); ++index) {
        std::vector<std::string> candidates;
        if (index == 0) candidates = {vowel + " " + coda[index], vowel + coda[index]};
        else candidates = {coda[index - 1] + " " + coda[index], coda[index - 1] + coda[index]};
        if (index + 1 == coda.size() && !connectedAfter) {
            candidates.insert(candidates.begin(),
                (index == 0 ? vowel : coda[index - 1]) + " " + coda[index] + "-");
            candidates.push_back(coda[index] + " -");
            candidates.push_back(coda[index] + "-");
        }
        const int64_t tick = note.endTick() - static_cast<int64_t>(
            coda.size() - index) * 60;
        events.push_back(englishTransitionEvent(note, tick, candidates, singer, "EN X-SAMPA"));
    }
    return events;
}

std::string vccvSymbol(const std::string& symbol) {
    static const std::unordered_map<std::string, std::string> mapped = {
        {"A", "a"}, {"{", "@"}, {"V", "u"}, {"O", "9"}, {"aU", "8"},
        {"aI", "I"}, {"E", "e"}, {"eI", "A"}, {"I", "i"}, {"i", "E"},
        {"oU", "O"}, {"OI", "Q"}, {"U", "6"}, {"u", "o"}, {"@", "x"},
        {"tS", "ch"}, {"D", "dh"}, {"dZ", "j"}, {"N", "ng"},
        {"S", "sh"}, {"T", "th"}, {"Z", "zh"}, {"j", "y"}, {"4", "dd"},
    };
    if (const auto found = mapped.find(symbol); found != mapped.end()) return found->second;
    return symbol;
}

bool isVccvVowel(const std::string& symbol) {
    static const std::unordered_set<std::string> vowels = {
        "a", "@", "u", "0", "8", "I", "e", "3", "A", "i", "E", "O",
        "Q", "6", "o", "1ng", "9", "&", "x", "1", "Y", "L", "W", "8n",
        "Ang", "9l"};
    return vowels.count(symbol) != 0;
}

std::vector<PhonemeEvent> vccvEvents(const Note& note, const Note* previous,
                                     const Note* next, const Voicebank& singer) {
    if (note.aliasOverride) {
        return {englishTransitionEvent(note, note.startTick, {*note.aliasOverride}, singer,
                                       "English VCCV explicit alias")};
    }
    const bool explicitHint = note.lyric.rfind('[') != std::string::npos;
    auto symbols = englishToXSampa(note.lyric);
    if (!explicitHint) for (auto& symbol : symbols) symbol = vccvSymbol(symbol);
    size_t vowelIndex = symbols.size();
    for (size_t index = 0; index < symbols.size(); ++index) {
        if (isVccvVowel(symbols[index])) { vowelIndex = index; break; }
    }
    if (vowelIndex == symbols.size()) {
        return {englishTransitionEvent(note, note.startTick, {note.lyric}, singer, "English VCCV")};
    }
    const bool connectedBefore = previous && previous->endTick() == note.startTick;
    const bool connectedAfter = next && next->startTick == note.endTick();
    const std::string onset = joinedSymbols(symbols, 0, vowelIndex);
    const std::string vowel = symbols[vowelIndex];
    std::vector<PhonemeEvent> events;
    const std::string cv = onset + vowel;
    const bool previousEndsVowel = connectedBefore && englishWordEndsInVowel(previous->lyric);
    if (previousEndsVowel && !onset.empty()) {
        const auto previousVowel = vccvSymbol(trailingXSampaVowel(previous->lyric));
        events.push_back(englishTransitionEvent(note, note.startTick - 55,
            {previousVowel + " " + onset, previousVowel + onset}, singer, "English VCCV"));
        events.push_back(englishTransitionEvent(note, note.startTick, {cv}, singer, "English VCCV"));
    } else {
        events.push_back(englishTransitionEvent(note, note.startTick,
            connectedBefore ? std::vector<std::string>{cv}
                            : std::vector<std::string>{"-" + cv, cv},
            singer, "English VCCV"));
    }

    std::vector<std::string> coda(
        symbols.begin() + static_cast<std::ptrdiff_t>(vowelIndex + 1), symbols.end());
    if (coda.empty()) {
        if (!connectedAfter) events.push_back(englishTransitionEvent(note, note.endTick() - 115,
            {vowel + "-", vowel + " -"}, singer, "English VCCV"));
    } else {
        const int64_t tail = connectedAfter ? 55 : 115;
        events.push_back(englishTransitionEvent(note,
            note.endTick() - tail - static_cast<int64_t>(coda.size() - 1) * 55,
            {vowel + coda.front() + "-", vowel + " " + coda.front()}, singer, "English VCCV"));
        for (size_t index = 1; index < coda.size(); ++index) {
            const std::string suffix = index + 1 == coda.size() && !connectedAfter ? "-" : "";
            events.push_back(englishTransitionEvent(note,
                note.endTick() - tail - static_cast<int64_t>(coda.size() - index - 1) * 55,
                {coda[index - 1] + coda[index] + suffix,
                 coda[index - 1] + " " + coda[index] + suffix}, singer, "English VCCV"));
        }
    }
    return events;
}

}  // namespace

PhonemeEvent EnglishXSampaPhonemizer::process(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    auto events = xsampaEvents(note, previous, next, singer);
    return events.empty() ? PhonemeEvent{} : events.front();
}

std::vector<PhonemeEvent> EnglishXSampaPhonemizer::processAll(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    return xsampaEvents(note, previous, next, singer);
}

PhonemeEvent EnglishVccvPhonemizer::process(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    auto events = vccvEvents(note, previous, next, singer);
    return events.empty() ? PhonemeEvent{} : events.front();
}

std::vector<PhonemeEvent> EnglishVccvPhonemizer::processAll(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    return vccvEvents(note, previous, next, singer);
}

PhonemeEvent EnglishToJapanesePhonemizer::process(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    auto events = processAll(note, previous, next, singer);
    if (!events.empty()) return events.front();
    PhonemeEvent event;
    event.relativeTick = note.startTick;
    event.targetMidiNote = note.midiNote;
    event.sourceNoteId = note.id;
    event.requestedAlias = note.lyric;
    event.diagnostic = "No English-to-Japanese pronunciation for lyric " + note.lyric;
    return event;
}

std::vector<PhonemeEvent> EnglishToJapanesePhonemizer::processAll(
    const Note& note, const Note* previous, const Note* next, const Voicebank& singer) const {
    return processAllChain(std::vector<Note>{note}, previous, next, singer);
}

std::vector<PhonemeEvent> EnglishToJapanesePhonemizer::processAllChain(
    const std::vector<Note>& notes, const Note* previous, const Note* next,
    const Voicebank& singer) const {
    if (notes.empty()) return {};
    const Note& note = notes.front();
    if (note.aliasOverride) {
        PhonemeEvent event;
        event.relativeTick = note.startTick;
        event.targetMidiNote = note.midiNote;
        event.sourceNoteId = note.id;
        event.requestedAlias = *note.aliasOverride;
        attempt(event, singer, *note.aliasOverride);
        event.diagnostic = event.oto ? "English-to-Japanese explicit alias: " + event.selectedAlias
                                     : "Missing English-to-Japanese alias: " + *note.aliasOverride;
        return {std::move(event)};
    }

    EnglishJapanesePlan plan;
    if (lyricIsExtender(note.lyric)) {
        const auto vowel = previous ? lastJapaneseVowelForEnglish(previous->lyric) : std::string();
        const std::unordered_map<std::string, std::string> kana = {
            {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"}, {"n", "ん"}};
        if (const auto found = kana.find(vowel); found != kana.end())
            plan.syllables.push_back({{found->second}, {}, vowel});
    } else {
        plan = englishAsJapanesePlan(note.lyric);
    }
    if (plan.syllables.empty()) return {};

    // SyllableBasedPhonemizer carries a connected word's ending consonants
    // into the next word's first syllable. Thus "an up" becomes え・な・ぷ,
    // rather than independently rendering あ・ん and あ・ぷ. Rebuild the
    // first syllable from the combined consonant list so clusters and the
    // CV-bearing final consonant follow the same rule as an ordinary onset.
    const bool connectedPrevious = previous && previous->endTick() == note.startTick &&
                                   !lyricIsExtender(previous->lyric);
    EnglishJapanesePlan previousPlan;
    if (connectedPrevious) {
        previousPlan = englishAsJapanesePlan(previous->lyric);
        auto consonants = previousPlan.endingConsonants;
        consonants.insert(consonants.end(), plan.syllables.front().consonants.begin(),
                          plan.syllables.front().consonants.end());
        consonants = collapseJapaneseClusters(std::move(consonants));
        plan.syllables.front().consonants = consonants;
        plan.syllables.front().aliases = japaneseAliasesForSyllable(
            consonants, plan.syllables.front().vowel);
    }

    struct AssignedSyllable {
        EnglishJapaneseSyllable syllable;
        bool aliasExtension = false;
    };
    std::vector<AssignedSyllable> assigned;
    assigned.push_back({plan.syllables.front(), false});
    size_t syllableIndex = 0;
    for (size_t noteIndex = 1;
         noteIndex < notes.size() && syllableIndex + 1 < plan.syllables.size();
         ++noteIndex) {
        const bool holdVowel = notes[noteIndex].lyric.rfind("+*", 0) == 0 ||
                               notes[noteIndex].lyric.rfind("+~", 0) == 0;
        if (holdVowel) {
            assigned.push_back({plan.syllables[syllableIndex], true});
        } else {
            ++syllableIndex;
            assigned.push_back({plan.syllables[syllableIndex], false});
        }
    }
    while (syllableIndex + 1 < plan.syllables.size()) {
        ++syllableIndex;
        assigned.push_back({plan.syllables[syllableIndex], false});
    }

    // OpenUtau phonemizes neighbouring groups from right to left. If the next
    // word borrows this word's coda, its first transition begins before the
    // note line; that pushback shortens the container OpenUtau subdivides for
    // this word. Reproduce the same musical allocation so multi-syllable main
    // phones land on the same ticks (rather than accumulating 15-tick delays).
    constexpr int64_t transitionTicks = 55;
    constexpr int64_t phraseFinalTicks = 115;
    std::vector<Note> allocationNotes = notes;
    const bool connectedNextWord = next && next->startTick == notes.back().endTick() &&
                                   !lyricIsExtender(next->lyric);
    if (connectedNextWord) {
        const auto nextPlan = englishAsJapanesePlan(next->lyric);
        if (!nextPlan.syllables.empty()) {
            auto nextConsonants = plan.endingConsonants;
            nextConsonants.insert(nextConsonants.end(),
                                  nextPlan.syllables.front().consonants.begin(),
                                  nextPlan.syllables.front().consonants.end());
            nextConsonants = collapseJapaneseClusters(std::move(nextConsonants));
            const auto nextAliases = japaneseAliasesForSyllable(
                nextConsonants, nextPlan.syllables.front().vowel);
            const int64_t pushback = static_cast<int64_t>(
                nextAliases.empty() ? 0 : nextAliases.size() - 1) * transitionTicks;
            allocationNotes.back().durationTick = std::max<int64_t>(
                15, allocationNotes.back().durationTick - pushback);
        }
    }

    // Match SyllableBasedPhonemizer.HandleNotEnoughNotes: when one authored
    // note contains several vowels, split its final note into 15-tick-aligned
    // virtual containers. Extension notes remain real containers so +* can
    // hold a syllable and a later + can advance it.
    std::vector<Note> containers;
    if (allocationNotes.size() >= assigned.size()) {
        containers = allocationNotes;
    } else {
        containers.insert(containers.end(), allocationNotes.begin(), allocationNotes.end() - 1);
        const Note& last = allocationNotes.back();
        const size_t pieces = assigned.size() - containers.size();
        const int64_t regular = std::max<int64_t>(15,
            (last.durationTick / static_cast<int64_t>(pieces) / 15) * 15);
        int64_t position = last.startTick;
        for (size_t piece = 0; piece < pieces; ++piece) {
            Note container = last;
            container.startTick = position;
            container.durationTick = piece + 1 < pieces
                ? std::min<int64_t>(regular, last.endTick() - position)
                : last.endTick() - position;
            containers.push_back(std::move(container));
            position += containers.back().durationTick;
        }
    }

    // OpenUtau's default transition at 120 BPM is 55 ticks after its 5-tick
    // quantization. Phrase endings reserve a double transition (observed as
    // 115 ticks in the pinned Classic build).
    std::vector<PhonemeEvent> events;
    size_t expectedEvents = plan.ending.size();
    for (const auto& item : assigned)
        if (!item.aliasExtension) expectedEvents += item.syllable.aliases.size();
    events.reserve(expectedEvents);

    std::string previousVowel;
    if (connectedPrevious)
        previousVowel = previousPlan.syllables.empty()
            ? std::string() : previousPlan.syllables.back().vowel;
    const auto appendEvent = [&](const std::string& unit, int64_t tick, int midi,
                                 const std::string& sourceNoteId,
                                 bool phraseInitial) {
        PhonemeEvent event;
        event.relativeTick = tick;
        event.targetMidiNote = midi;
        event.sourceNoteId = sourceNoteId;
        event.requestedAlias = unit;
        if (!previousVowel.empty()) attempt(event, singer, previousVowel + " " + unit);
        if (phraseInitial) attempt(event, singer, "- " + unit);
        attempt(event, singer, "* " + unit);
        attempt(event, singer, unit);
        // Adachi Rei and other older banks sometimes spell the v-series with
        // hiragana or decomposed dakuten; an f-series fallback is preferable
        // to a missing word when a bank has no dedicated English append.
        if (!event.oto) {
            static const std::unordered_map<std::string, std::vector<std::string>> vFallbacks = {
                {"ヴ", {"ふ", "う"}}, {"ゔ", {"ふ", "う"}},
                {"ヴぁ", {"ふぁ", "あ"}}, {"ゔぁ", {"ふぁ", "あ"}},
                {"ヴぃ", {"ふぃ", "い"}}, {"ゔぃ", {"ふぃ", "い"}},
                {"ヴぇ", {"ふぇ", "え"}}, {"ゔぇ", {"ふぇ", "え"}},
                {"ヴぉ", {"ふぉ", "うぉ", "お"}}, {"ゔぉ", {"ふぉ", "うぉ", "お"}},
            };
            if (const auto fallback = vFallbacks.find(unit); fallback != vFallbacks.end())
                for (const auto& alias : fallback->second) attempt(event, singer, alias);
        }
        event.diagnostic = event.oto
            ? "English-to-Japanese alias: " + event.selectedAlias
            : "Missing Japanese alias " + unit + " for English lyric " + note.lyric;
        events.push_back(std::move(event));
        previousVowel = trailingVowel(unit);
    };

    bool phraseInitial = !previous || previous->endTick() != note.startTick;
    for (size_t index = 0; index < assigned.size(); ++index) {
        const auto& item = assigned[index];
        const Note& container = containers[index];
        if (item.aliasExtension) {
            previousVowel = item.syllable.vowel;
            continue;
        }
        const size_t aliasCount = item.syllable.aliases.size();
        for (size_t aliasIndex = 0; aliasIndex < aliasCount; ++aliasIndex) {
            const bool mainVowel = aliasIndex + 1 == aliasCount;
            const int64_t tick = container.startTick - static_cast<int64_t>(
                aliasCount - aliasIndex - 1) * transitionTicks;
            const int targetTone = !mainVowel && index > 0
                ? containers[index - 1].midiNote : container.midiNote;
            appendEvent(item.syllable.aliases[aliasIndex], tick, targetTone,
                        container.id, phraseInitial && events.empty());
        }
        previousVowel = item.syllable.vowel;
    }

    const int64_t logicalEndTick = notes.back().endTick();
    const bool connectedNext = next && next->startTick == logicalEndTick;
    const bool transferEndingToNext = connectedNext && !lyricIsExtender(next->lyric);
    for (size_t index = 0; index < plan.ending.size() && !transferEndingToNext; ++index) {
        int64_t tick = 0;
        tick = logicalEndTick - phraseFinalTicks - static_cast<int64_t>(
            plan.ending.size() - index - 1) * transitionTicks;
        tick = std::max(note.startTick, tick);
        appendEvent(plan.ending[index], tick, notes.back().midiNote,
                    notes.back().id, phraseInitial && events.empty());
    }
    return events;
}

PhonemeEvent DirectAliasPhonemizer::process(const Note& note, const Note* previous, const Note*, const Voicebank& singer) const {
    PhonemeEvent event;
    event.relativeTick = note.startTick;
    event.targetMidiNote = note.midiNote;
    event.sourceNoteId = note.id;
    event.requestedAlias = note.aliasOverride.value_or(
        lyricIsExtender(note.lyric) ? sustainedVowelKana(previous) : note.lyric);
    attempt(event, singer, event.requestedAlias);
    event.diagnostic = event.oto ? "Direct alias " + event.selectedAlias : "Direct alias missing: " + event.requestedAlias;
    return event;
}

std::unique_ptr<IPhonemizer> makePhonemizer(const std::string& name) {
    const auto canonical = canonicalPhonemizerName(name);
    if (name == kJapaneseAutoPhonemizer) return std::make_unique<JapaneseAutoPhonemizer>();
    if (name == kJapaneseCvvcPhonemizer) return std::make_unique<JapaneseCvvcPhonemizer>();
    if (canonical == kEnglishToJapanesePhonemizer) return std::make_unique<EnglishToJapanesePhonemizer>();
    if (name == kEnglishVccvPhonemizer) return std::make_unique<EnglishVccvPhonemizer>();
    if (name == kDirectAliasPhonemizer) return std::make_unique<DirectAliasPhonemizer>();
    return std::make_unique<EnglishXSampaPhonemizer>();
}

}  // namespace vocalrack
