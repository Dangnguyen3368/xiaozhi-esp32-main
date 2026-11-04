#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

#include <string>

// Vietnamese lunar calendar (UTC+7), after Ho Ngoc Duc's astronomical algorithm.
namespace lunar {

struct Date {
    int day = 1;
    int month = 1;
    int year = 2000;
    bool leap = false;  // leap (nhuận) month
};

// Solar (Gregorian) date to lunar date.
Date FromSolar(int day, int month, int year);
// Julian day number of a Gregorian date.
int JulianDay(int day, int month, int year);
// "Bính Ngọ" for the lunar year.
std::string YearName(int lunar_year);
// "Giáp Tý" for the day with Julian day number `jd`.
std::string DayName(int jd);
// "Mậu Tuất" for a lunar month of a lunar year.
std::string MonthName(int lunar_month, int lunar_year);

}  // namespace lunar

#endif  // LUNAR_CALENDAR_H
