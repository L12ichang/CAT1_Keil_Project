#include "zk_sunriset.h"

#if ZK_ENABLE_SUNRISE_PLAN

#include "zk_property.h"
#include "sys_aip1302.h"
#include <math.h>

/*
 * NOAA 日出日落算法实现
 *
 * 参考：NOAA Solar Calculator / ESRL Global Monitoring Laboratory
 * 步骤概要：
 *   1. 计算年积日（Day of Year）
 *   2. 估算太阳平黄经
 *   3. 计算太阳平近点角 M
 *   4. 计算太阳真黄经 L
 *   5. 计算太阳赤经 RA
 *   6. 计算太阳赤纬 decl
 *   7. 计算时角 H
 *   8. 计算 UTC 日出/日落分钟数
 *   9. 转换到本地时间
 */

#define SR_PI       3.14159265f
#define SR_RAD      (SR_PI / 180.0f)
#define SR_DEG      (180.0f / SR_PI)
#define SR_ZENITH   90.833f    /* 日出日落天顶角（含大气折射） */

/* 缓存：同一天内重复查询直接返回 */
static int sr_last_year;
static int sr_last_mon;
static int sr_last_day;
static int sr_cached_result;   /* 0=成功缓存, 其他=错误码 */
static int sr_cached_sr;       /* 日出分钟 */
static int sr_cached_ss;       /* 日落分钟 */

/* 年积日：1月1日=1 */
static int sr_day_of_year(int year, int mon, int day)
{
    int n1 = (275 * mon) / 9;
    int n2 = (mon + 9) / 12;
    int n3 = 1 + ((year - 4 * (year / 4) + 2) / 3);
    return n1 - (n2 * n3) + day - 30;
}

/* 将角度归一化到 [0, 360) */
static float sr_normalize(float angle)
{
    float a = fmodf(angle, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a;
}

/* 核心计算：根据日期、经纬度、时区计算日出日落分钟数 */
static int sr_calc(int year, int mon, int day,
                   float lat, float lng, int tz_min,
                   int *sr_out, int *ss_out)
{
    int n;
    float lng_hour;
    float t_rise, t_set;
    float M_rise, M_set;
    float L_rise, L_set;
    float RA_rise, RA_set;
    float sin_dec, cos_dec;
    float lat_rad, ha;
    float cos_ha;
    float utc_rise, utc_set;
    int rise, set;

    n = sr_day_of_year(year, mon, day);
    lng_hour = lng / 15.0f;

    /* ---- 估算太阳平黄经（日出/日落分别计算轨道位置） ---- */
    t_rise = (float)n + (6.0f - lng_hour) / 24.0f;
    t_set  = (float)n + (18.0f - lng_hour) / 24.0f;

    /* ---- 太阳平近点角 M ---- */
    M_rise = 0.9856f * t_rise - 3.289f;
    M_set  = 0.9856f * t_set  - 3.289f;

    /* ---- 太阳真黄经 L = M + 1.916*sin(M) + 0.020*sin(2M) + 282.634 ---- */
    L_rise = M_rise + 1.916f * sinf(M_rise * SR_RAD)
           + 0.020f * sinf(2.0f * M_rise * SR_RAD) + 282.634f;
    L_set  = M_set  + 1.916f * sinf(M_set  * SR_RAD)
           + 0.020f * sinf(2.0f * M_set  * SR_RAD) + 282.634f;

    L_rise = sr_normalize(L_rise);
    L_set  = sr_normalize(L_set);

    /* ---- 太阳赤经 RA（需做象限对齐） ---- */
    RA_rise = SR_DEG * atanf(0.91764f * tanf(L_rise * SR_RAD));
    RA_set  = SR_DEG * atanf(0.91764f * tanf(L_set  * SR_RAD));

    RA_rise += (floorf(L_rise / 90.0f) - floorf(RA_rise / 90.0f)) * 90.0f;
    RA_set  += (floorf(L_set  / 90.0f) - floorf(RA_set  / 90.0f)) * 90.0f;

    RA_rise /= 15.0f;  /* 转成小时 */
    RA_set  /= 15.0f;

    /* ---- 太阳赤纬 decl ---- */
    sin_dec = 0.39782f * sinf(L_rise * SR_RAD);
    cos_dec = cosf(asinf(sin_dec));

    /* ---- 时角 H（日出日落共用赤纬近似） ---- */
    lat_rad = lat * SR_RAD;
    cos_ha = (cosf(SR_ZENITH * SR_RAD) - sin_dec * sinf(lat_rad))
           / (cos_dec * cosf(lat_rad));

    /* 极昼/极夜检测 */
    if (cos_ha > 1.0f)  return -2;  /* 极夜 */
    if (cos_ha < -1.0f) return -2;  /* 极昼 */

    ha = SR_DEG * acosf(cos_ha);

    /* ---- UTC 日出/日落分钟 ---- */
    utc_rise = 720.0f - 4.0f * (lng + ha);
    utc_set  = 720.0f - 4.0f * (lng - ha);

    /* ---- 转本地时间 ---- */
    rise = (int)utc_rise + tz_min;
    set  = (int)utc_set  + tz_min;

    if (rise < 0)    rise += 1440;
    if (rise >= 1440) rise -= 1440;
    if (set < 0)     set  += 1440;
    if (set >= 1440) set  -= 1440;

    *sr_out = rise;
    *ss_out = set;
    return 0;
}

int zk_sunriset_get(int *sr_minute, int *ss_minute)
{
    const zk_device_config_t *cfg;
    int year, mon, day;
    float lat, lng;
    int tz_min;
    int ret;
    int sr, ss;

    if (sr_minute == NULL || ss_minute == NULL)
    {
        return -1;
    }

    /* RTC 就绪检查 */
    if (apprtc_RtcTime.ready != BOOL_TRUE ||
        apprtc_RtcTime.year < 2020)
    {
        return -3;
    }

    year = apprtc_RtcTime.year;
    mon  = apprtc_RtcTime.mon;
    day  = apprtc_RtcTime.day;

    /* 命中缓存 */
    if (year == sr_last_year && mon == sr_last_mon && day == sr_last_day)
    {
        if (sr_cached_result == 0)
        {
            *sr_minute = sr_cached_sr;
            *ss_minute = sr_cached_ss;
            return 0;
        }
        return sr_cached_result;
    }

    /* 读取设备经纬度和时区 */
    cfg = zk_device_config_get();
    /* lat/lng 存储为度*1000000（微度），转为度 */
    lat = (float)cfg->lat / 1000000.0f;
    lng = (float)cfg->lng / 1000000.0f;
    tz_min = cfg->zone * 60;  /* zone存小时，转为分钟 */

    /* 执行计算 */
    ret = sr_calc(year, mon, day, lat, lng, tz_min, &sr, &ss);

    /* 更新缓存 */
    sr_last_year = year;
    sr_last_mon  = mon;
    sr_last_day  = day;
    sr_cached_result = ret;
    if (ret == 0)
    {
        sr_cached_sr = sr;
        sr_cached_ss = ss;
        *sr_minute = sr;
        *ss_minute = ss;
    }

    return ret;
}

#endif
