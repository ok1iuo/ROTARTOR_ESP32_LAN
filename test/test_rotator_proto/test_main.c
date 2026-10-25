#include <string.h>
#include <unity.h>

#include "rotator_proto.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parse_angle_accepts_unit_format(void)
{
    int deg = -1;
    TEST_ASSERT_TRUE(rotator_parse_angle("+0123", 0, 360, &deg));
    TEST_ASSERT_EQUAL_INT(123, deg);
    TEST_ASSERT_TRUE(rotator_parse_angle("\n+0000\r", 0, 360, &deg));
    TEST_ASSERT_EQUAL_INT(0, deg);
    TEST_ASSERT_TRUE(rotator_parse_angle("-0005", -20, 100, &deg));
    TEST_ASSERT_EQUAL_INT(-5, deg);
}

static void test_parse_angle_rejects_other_lines(void)
{
    int deg = 42;
    TEST_ASSERT_FALSE(rotator_parse_angle("new azimut =123.00", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("C", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("C2", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("+01x3", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("+", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("12.5", 0, 360, &deg));
    TEST_ASSERT_EQUAL_INT(42, deg);
}

static void test_parse_angle_rejects_out_of_range(void)
{
    int deg = 42;
    TEST_ASSERT_FALSE(rotator_parse_angle("+0361", 0, 360, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("-0021", -20, 100, &deg));
    TEST_ASSERT_FALSE(rotator_parse_angle("+99999999", 0, 360, &deg));
    TEST_ASSERT_EQUAL_INT(42, deg);
}

static void test_parse_version(void)
{
    char v[24];
    TEST_ASSERT_TRUE(rotator_parse_version("OK1IUO simple rotator unit ver:0.0.1-2-gabc1234 build:Oct 24 2026 21:00:00\r\n", v, sizeof v));
    TEST_ASSERT_EQUAL_STRING("0.0.1-2-gabc1234", v);
    TEST_ASSERT_TRUE(rotator_parse_version("OK1IUO simple rotator unit ver:Sep 24 2025 10:11:12\r\nCurrent Magnitude: 5", v, sizeof v));
    TEST_ASSERT_EQUAL_STRING("Sep 24 2025 10:11:12", v);
    TEST_ASSERT_FALSE(rotator_parse_version("Current Magnitude: 5", v, sizeof v));
    TEST_ASSERT_FALSE(rotator_parse_version("ver: build:x", v, sizeof v));
}

static void test_parse_version_truncates(void)
{
    char v[6];
    TEST_ASSERT_TRUE(rotator_parse_version("ver:0.0.1-15-gdeadbee", v, sizeof v));
    TEST_ASSERT_EQUAL_STRING("0.0.1", v);
}

static void test_parse_offset(void)
{
    int off = 42;
    TEST_ASSERT_TRUE(rotator_parse_offset("O OFFSET:5.00", "O OFFSET:", &off));
    TEST_ASSERT_EQUAL_INT(5, off);
    TEST_ASSERT_TRUE(rotator_parse_offset("O2 OFFSET:-3.00\r", "O2 OFFSET:", &off));
    TEST_ASSERT_EQUAL_INT(-3, off);
    TEST_ASSERT_TRUE(rotator_parse_offset("O OFFSET:0.00", "O OFFSET:", &off));
    TEST_ASSERT_EQUAL_INT(0, off);
    TEST_ASSERT_TRUE(rotator_parse_offset("O OFFSET:-179.60", "O OFFSET:", &off));
    TEST_ASSERT_EQUAL_INT(-180, off);
}

static void test_parse_offset_rejects_other_lines(void)
{
    int off = 42;
    TEST_ASSERT_FALSE(rotator_parse_offset("O2 OFFSET:5.00", "O OFFSET:", &off));   // EL line for AZ
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:5.00", "O2 OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:nan", "O OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:ovf", "O OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:", "O OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:200.00", "O OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("O OFFSET:5.00x", "O OFFSET:", &off));
    TEST_ASSERT_FALSE(rotator_parse_offset("new azimut offset =5.00", "O OFFSET:", &off));
    TEST_ASSERT_EQUAL_INT(42, off);
}

static void test_target_reached(void)
{
    TEST_ASSERT_TRUE(rotator_target_reached(100, 100));
    TEST_ASSERT_TRUE(rotator_target_reached(101, 100));
    TEST_ASSERT_TRUE(rotator_target_reached(99, 100));
    TEST_ASSERT_FALSE(rotator_target_reached(102, 100));
    TEST_ASSERT_FALSE(rotator_target_reached(98, 100));
}

static void test_line_reader_splits_unit_answer(void)
{
    // Unit answers "+0123\n\r"; a leftover "new azimut" line must come out separately.
    const char *stream = "new azimut =10.00\r\n+0123\n\r";
    rotator_line_reader_t r;
    rotator_line_reader_reset(&r);
    char line[32];
    const char *lines[4];
    char store[4][32];
    int n = 0;
    for (const char *p = stream; *p; p++) {
        if (rotator_line_reader_feed(&r, *p, line, sizeof line)) {
            strcpy(store[n], line);
            lines[n] = store[n];
            n++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("new azimut =10.00", lines[0]);
    TEST_ASSERT_EQUAL_STRING("+0123", lines[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_angle_accepts_unit_format);
    RUN_TEST(test_parse_angle_rejects_other_lines);
    RUN_TEST(test_parse_angle_rejects_out_of_range);
    RUN_TEST(test_parse_version);
    RUN_TEST(test_parse_version_truncates);
    RUN_TEST(test_parse_offset);
    RUN_TEST(test_parse_offset_rejects_other_lines);
    RUN_TEST(test_target_reached);
    RUN_TEST(test_line_reader_splits_unit_answer);
    return UNITY_END();
}
