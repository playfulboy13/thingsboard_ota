#include "edge_controller.h"

float pressHistory[PRESS_HISTORY_SIZE];
uint32_t timeHistory[PRESS_HISTORY_SIZE];

int pressIndex=0;
int pressFilled=0;

/* ============================================================
   HELPER
   ============================================================ */

int isNodeOnline(int idx)
{
    if (idx < 0 || idx >= 6) return 0;
    return node_data[idx].online ? 1 : 0;
}

float safePositive(float v)
{
    return (v > 0) ? v : 0;
}

uint32_t calcDeltaTime(uint32_t now, uint32_t old)
{
    /* xử lý rollover 24h */
    return (now >= old) ? (now - old) : (now + 86400 - old);
}

/* ============================================================
   TÍNH TOÁN
   ============================================================ */
void updatePressureHistory(float p, uint32_t nowSec)
{
    pressHistory[pressIndex] = p;
    timeHistory[pressIndex]  = nowSec;

    pressIndex = (pressIndex + 1) % PRESS_HISTORY_SIZE;

    if (pressFilled < PRESS_HISTORY_SIZE)
        pressFilled++;
}

float calcPressureTrendTimed(uint32_t nowSec, uint32_t windowSec)
{
    /* ✅ FIX: không cần full buffer */
    if (pressFilled < 5)
        return 0;

    float sum = 0;
    int cnt = 0;

    int newest = (pressIndex - 1 + PRESS_HISTORY_SIZE) % PRESS_HISTORY_SIZE;
    float p_new = pressHistory[newest];

    for (int i = 0; i < pressFilled; i++)
    {
        int idx = (pressIndex - 1 - i + PRESS_HISTORY_SIZE) % PRESS_HISTORY_SIZE;

        uint32_t dt = calcDeltaTime(nowSec, timeHistory[idx]);

        /* lấy nhiều điểm quanh window để smooth */
        if (dt >= windowSec && dt < windowSec + 10)
        {
            float p_old = pressHistory[idx];
            sum += (p_new - p_old);
            cnt++;
        }
    }

    if (cnt > 0)
        return sum / cnt;

    return 0;
}

float calcAvgPressureOnline(void)
{
    float sum = 0.0f;
    int cnt = 0;

    /* node 1,2,6 */
    int g1[] = {0,1,5};

    for (int i=0;i<3;i++)
    {
        int n = g1[i];
        if (isNodeOnline(n))
        {
            float p = node_data[n].sensor[5];   // sensor6
            if (p > 0)
            {
                sum += p;
                cnt++;
            }
        }
    }

    /* node 3,5 */
    int g2[] = {2,4};

    for (int i=0;i<2;i++)
    {
        int n = g2[i];
        if (isNodeOnline(n))
        {
            float p = node_data[n].sensor[5];   // sensor6
            if (p > 0)
            {
                sum += p;
                cnt++;
            }
        }
    }

    /* gateway fake_sensor5 */
    if (lora_info.fake_sensor[4] > 0)
    {
        sum += lora_info.fake_sensor[4];
        cnt++;
    }

    if (cnt == 0) return 0;
    return sum / cnt;
}

/* 1 hPa ~ 8.3m */
float calcHeightDiff(float pNode, float pRef)
{
    if (pNode <= 0 || pRef <= 0) return 0;
    return (pRef - pNode) * 8.3f;
}

float calcRainProb(float p, float t, float h)
{
    float score = 0;

    if (p > 0)
    {
        float pScore = (1012.0f - p) * 4.0f;   // giảm độ gắt
        if (pScore < 0) pScore = 0;
        if (pScore > 45) pScore = 45;
        score += pScore;
    }

    if (h > 60)
    {
        float hScore = (h - 60) * 1.2f;
        if (hScore > 35) hScore = 35;
        score += hScore;
    }

    if (t < 32)
    {
        float tScore = (32 - t) * 1.2f;
        if (tScore > 15) tScore = 15;
        score += tScore;
    }

    if (h > 80 && p < 1008)
        score += 12;

    if (h > 85 && p < 1005)
        score += 8;

    if (score > 100) score = 100;

    return score;
}

float calcET(float tempC, float lightVal, float humidity, int hour)
{
    /* ===============================
       1. LUX -> RADIATION (ổn định hơn)
       =============================== */
   float rad = lightVal * 0.000015f;   // thay vì 0.000018f

    if (rad < 0.02f) rad = 0.02f;
    if (rad > 1.1f)  rad = 1.1f;

    /* ===============================
       2. VPD (RẤT QUAN TRỌNG)
       =============================== */
    float es = 0.6108f * expf((17.27f * tempC) / (tempC + 237.3f)); // kPa
    float ea = es * (humidity / 100.0f);
    float vpd = es - ea;

    if (vpd < 0.05f) vpd = 0.05f;
    if (vpd > 3.0f)  vpd = 3.0f;

    /* ===============================
       3. HỆ SỐ GIỜ (mượt + nhạy hơn)
       =============================== */
    float hourFactor = 0.0f;

    if (hour >= 6 && hour <= 18)
    {
        float x = (hour - 6) / 12.0f;   // 0 → 1
        hourFactor = sinf(x * 3.14159f);
    }
    else
    {
        hourFactor = 0.05f; // ban đêm rất thấp
    }

    /* ===============================
       4. NHIỆT ĐỘ (tăng nhạy)
       =============================== */
    float tempFactor = 1.0f + (tempC - 25.0f) * 0.02f;

    if (tempFactor < 0.7f) tempFactor = 0.7f;
    if (tempFactor > 1.5f) tempFactor = 1.5f;

    /* ===============================
       5. ET (FAO-lite)
       =============================== */
    float et =
        (0.408f * rad * tempFactor) *
        (0.5f + vpd) *
        hourFactor;

    /* ===============================
       6. GIỚI HẠN
       =============================== */
    if (et < 0.01f) et = 0.01f;
    if (et > 1.8f)  et = 1.8f;

    return et;
}

const char* calcSoilStatus(float moisture, float ec)
{
    if (moisture < 20) return "very_dry";
    if (moisture < 35) return "dry";
    if (moisture > 80) return "waterlogged";

    if (ec > 2500) return "high_salinity";
    if (ec > 1500) return "medium_salinity";

    return "good";
}
