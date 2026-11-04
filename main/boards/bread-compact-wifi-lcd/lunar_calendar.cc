#include "lunar_calendar.h"

#include <cmath>

namespace lunar {
namespace {
constexpr double kTimeZone = 7.0;
constexpr double kPi = 3.14159265358979323846;

const char* const kCan[] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
const char* const kChi[] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

int Floor(double x) { return static_cast<int>(std::floor(x)); }

// Day number of the k-th new moon after 1900-01-01 (local time).
int NewMoonDay(int k) {
    const double T = k / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double dr = kPi / 180;
    double jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    const double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    const double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    const double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2 * dr * M);
    C1 = C1 - 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(dr * 2 * Mpr);
    C1 = C1 - 0.0004 * std::sin(dr * 3 * Mpr);
    C1 = C1 + 0.0104 * std::sin(dr * 2 * F) - 0.0051 * std::sin(dr * (M + Mpr));
    C1 = C1 - 0.0074 * std::sin(dr * (M - Mpr)) + 0.0004 * std::sin(dr * (2 * F + M));
    C1 = C1 - 0.0004 * std::sin(dr * (2 * F - M)) - 0.0006 * std::sin(dr * (2 * F + Mpr));
    C1 = C1 + 0.0010 * std::sin(dr * (2 * F - Mpr)) + 0.0005 * std::sin(dr * (2 * Mpr + M));
    double deltat;
    if (T < -11) {
        deltat = 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3;
    } else {
        deltat = -0.000278 + 0.000265 * T + 0.000262 * T2;
    }
    const double jd_new = jd1 + C1 - deltat;
    return Floor(jd_new + 0.5 + kTimeZone / 24);
}

// Sun longitude sector (0..11) at the start of day `jdn`.
int SunLongitude(int jdn) {
    const double T = (jdn - 2451545.5 - kTimeZone / 24) / 36525;
    const double T2 = T * T;
    const double dr = kPi / 180;
    const double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    const double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M);
    DL = DL + (0.019993 - 0.000101 * T) * std::sin(dr * 2 * M) + 0.000290 * std::sin(dr * 3 * M);
    double L = (L0 + DL) * dr;
    L = L - kPi * 2 * Floor(L / (kPi * 2));
    return Floor(L / kPi * 6);
}

int LunarMonth11(int year) {
    const int off = JulianDay(31, 12, year) - 2415021;
    const int k = Floor(off / 29.530588853);
    int nm = NewMoonDay(k);
    if (SunLongitude(nm) >= 9) {
        nm = NewMoonDay(k - 1);
    }
    return nm;
}

int LeapMonthOffset(int a11) {
    const int k = Floor((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last = 0;
    int i = 1;
    int arc = SunLongitude(NewMoonDay(k + i));
    do {
        last = arc;
        i++;
        arc = SunLongitude(NewMoonDay(k + i));
    } while (arc != last && i < 14);
    return i - 1;
}
}  // namespace

int JulianDay(int dd, int mm, int yy) {
    const int a = (14 - mm) / 12;
    const int y = yy + 4800 - a;
    const int m = mm + 12 * a - 3;
    int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161) {
        jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    }
    return jd;
}

Date FromSolar(int dd, int mm, int yy) {
    const int day_number = JulianDay(dd, mm, yy);
    const int k = Floor((day_number - 2415021.076998695) / 29.530588853);
    int month_start = NewMoonDay(k + 1);
    if (month_start > day_number) {
        month_start = NewMoonDay(k);
    }
    int a11 = LunarMonth11(yy);
    int b11 = a11;
    Date result;
    if (a11 >= month_start) {
        result.year = yy;
        a11 = LunarMonth11(yy - 1);
    } else {
        result.year = yy + 1;
        b11 = LunarMonth11(yy + 1);
    }
    result.day = day_number - month_start + 1;
    const int diff = Floor((month_start - a11) / 29.0);
    result.month = diff + 11;
    if (b11 - a11 > 365) {
        const int leap_diff = LeapMonthOffset(a11);
        if (diff >= leap_diff) {
            result.month = diff + 10;
            if (diff == leap_diff) {
                result.leap = true;
            }
        }
    }
    if (result.month > 12) {
        result.month -= 12;
    }
    if (result.month >= 11 && diff < 4) {
        result.year -= 1;
    }
    return result;
}

std::string YearName(int y) { return std::string(kCan[(y + 6) % 10]) + " " + kChi[(y + 8) % 12]; }

std::string DayName(int jd) { return std::string(kCan[(jd + 9) % 10]) + " " + kChi[(jd + 1) % 12]; }

std::string MonthName(int m, int y) {
    return std::string(kCan[(y * 12 + m + 3) % 10]) + " " + kChi[(m + 1) % 12];
}

}  // namespace lunar
