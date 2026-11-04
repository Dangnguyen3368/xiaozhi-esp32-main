// Amount parsing and Vietnamese number reading for the bank announcer (no ESP-IDF deps, so it
// can be tested on a PC).
#include "bank_notify.h"

#include <cctype>
#include <cstring>

namespace bank_notify {
namespace {

std::string Lower(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool StartsWith(const std::string& text, size_t pos, const char* prefix) {
    return text.compare(pos, std::strlen(prefix), prefix) == 0;
}

size_t SkipSpaces(const std::string& text, size_t pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == ':' || text[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

// Length of a currency word at `pos` of lowercased text, 0 when none.
size_t CurrencyAt(const std::string& lower, size_t pos) {
    static const char* const kWords[] = {"vnđ", "vnĐ", "vnd", "đồng", "dong", "đ", "Đ", "₫"};
    for (const char* word : kWords) {
        if (StartsWith(lower, pos, word)) {
            return std::strlen(word);
        }
    }
    return 0;
}

// Parses "1,500,000" / "1.500.000" / "500000" at `pos`. Sets `end` and `grouped` (it had
// thousands separators). A decimal part like ".00" ends the number. Returns false on no digits.
bool ParseNumber(const std::string& text, size_t pos, uint64_t& value, size_t& end, bool& grouped) {
    value = 0;
    grouped = false;
    size_t i = pos;
    int digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + (text[i] - '0');
        ++i;
        if (++digits > 15) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    while (i < text.size() && (text[i] == ',' || text[i] == '.')) {
        size_t j = i + 1;
        int group = 0;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
            ++j;
            ++group;
        }
        if (group != 3) {
            break;
        }
        for (size_t k = i + 1; k < j; ++k) {
            value = value * 10 + (text[k] - '0');
        }
        if ((digits += 3) > 15) {
            return false;
        }
        grouped = true;
        i = j;
    }
    end = i;
    return true;
}

// Amount right after a sign at `pos` ("+500,000VND", "+ VND 500,000"), 0 when there is none.
uint64_t AmountAfterSign(const std::string& lower, size_t pos) {
    size_t i = SkipSpaces(lower, pos);
    bool currency_before = false;
    if (size_t n = CurrencyAt(lower, i)) {
        currency_before = true;
        i = SkipSpaces(lower, i + n);
    }
    uint64_t value = 0;
    size_t end = 0;
    bool grouped = false;
    if (!ParseNumber(lower, i, value, end, grouped)) {
        return 0;
    }
    const bool currency_after = CurrencyAt(lower, SkipSpaces(lower, end)) > 0;
    // A phone number (+84...) has neither separators nor a currency next to it.
    return grouped || currency_before || currency_after ? value : 0;
}

void AddDigit(std::vector<const char*>& words, int digit) {
    static const char* const kDigits[] = {"khong", "mot", "hai", "ba", "bon",
                                          "nam", "sau", "bay", "tam", "chin"};
    words.push_back(kDigits[digit]);
}

// Reads 0..999. `full` reads leading zeros ("không trăm linh năm") for groups after the first.
void AddGroup(std::vector<const char*>& words, int n, bool full) {
    const int hundreds = n / 100;
    const int tens = n / 10 % 10;
    const int units = n % 10;
    if (full || hundreds > 0) {
        AddDigit(words, hundreds);
        words.push_back("tram");
    }
    const bool had_hundreds = full || hundreds > 0;
    if (tens == 0) {
        if (units > 0) {
            if (had_hundreds) {
                words.push_back("linh");
            }
            AddDigit(words, units);
        }
    } else if (tens == 1) {
        words.push_back("muoi");
        if (units == 5) {
            words.push_back("lam");
        } else if (units > 0) {
            AddDigit(words, units);
        }
    } else {
        AddDigit(words, tens);
        words.push_back("muoi2");
        if (units == 1) {
            words.push_back("mot2");
        } else if (units == 5) {
            words.push_back("lam");
        } else if (units > 0) {
            AddDigit(words, units);
        }
    }
}

}  // namespace

uint64_t ParseIncomingAmount(const std::string& text) {
    const std::string lower = Lower(text);
    bool debit = false;
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] == '+') {
            if (uint64_t amount = AmountAfterSign(lower, i + 1)) {
                return amount;
            }
        } else if (lower[i] == '-' && AmountAfterSign(lower, i + 1) > 0) {
            debit = true;
        }
    }
    if (debit) {
        return 0;
    }
    // No sign: "Bạn vừa nhận được 50.000đ từ ..." (e-wallets).
    static const char* const kIncoming[] = {"nhận", "nhan duoc", "ghi có", "cộng", "received"};
    bool incoming = false;
    for (const char* word : kIncoming) {
        incoming = incoming || lower.find(word) != std::string::npos;
    }
    if (!incoming) {
        return 0;
    }
    for (size_t i = 0; i < lower.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lower[i])) ||
            (i > 0 && std::isalnum(static_cast<unsigned char>(lower[i - 1])))) {
            continue;
        }
        uint64_t value = 0;
        size_t end = 0;
        bool grouped = false;
        if (ParseNumber(lower, i, value, end, grouped) && value > 0 &&
            CurrencyAt(lower, SkipSpaces(lower, end)) > 0) {
            return value;
        }
        while (i + 1 < lower.size() && (std::isdigit(static_cast<unsigned char>(lower[i + 1])) ||
                                        lower[i + 1] == ',' || lower[i + 1] == '.')) {
            ++i;
        }
    }
    return 0;
}

std::vector<const char*> AmountWords(uint64_t amount) {
    std::vector<const char*> words;
    if (amount == 0 || amount > 999999999999ULL) {
        return words;
    }
    static const char* const kUnits[] = {nullptr, "nghin", "trieu", "ty"};
    const int groups[] = {static_cast<int>(amount / 1000000000ULL % 1000),
                          static_cast<int>(amount / 1000000 % 1000),
                          static_cast<int>(amount / 1000 % 1000), static_cast<int>(amount % 1000)};
    bool started = false;
    for (int g = 0; g < 4; ++g) {
        if (groups[g] == 0) {
            continue;
        }
        AddGroup(words, groups[g], started);
        if (kUnits[3 - g] != nullptr) {
            words.push_back(kUnits[3 - g]);
        }
        started = true;
    }
    return words;
}

}  // namespace bank_notify
