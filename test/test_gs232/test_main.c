#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "gs232.h"

void setUp(void) {}
void tearDown(void) {}

static const gs232_position_t POS_OK = { .az_ok = true, .az = 123, .el_installed = true, .el_ok = true, .el = 45 };
static char reply[64];
static gs232_action_t act;

static size_t run(const char *line, gs232_variant_t v, const gs232_position_t *pos)
{
    return gs232_handle(line, v, pos, &act, reply, sizeof reply);
}

static void test_position_replies_gs232a(void)
{
    TEST_ASSERT_EQUAL(7, run("C", GS232_VARIANT_A, &POS_OK));
    TEST_ASSERT_EQUAL_STRING("+0123\r\n", reply);
    run("C2", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("+0123+0045\r\n", reply);
    run("B", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("+0045\r\n", reply);
    TEST_ASSERT_EQUAL(GS232_ACT_NONE, act.type);
}

static void test_position_replies_gs232b(void)
{
    run("C", GS232_VARIANT_B, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("AZ=123\r\n", reply);
    run("C2", GS232_VARIANT_B, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("AZ=123 EL=045\r\n", reply);
    run("B", GS232_VARIANT_B, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("EL=045\r\n", reply);
}

// Replies must parse with the exact sscanf formats Hamlib uses.
static void test_hamlib_parses_replies(void)
{
    int az = -1, el = -1;
    run("C2", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(2, sscanf(reply, "+0%d+0%d", &az, &el));
    TEST_ASSERT_EQUAL(123, az);
    TEST_ASSERT_EQUAL(45, el);
    az = el = -1;
    run("C2", GS232_VARIANT_B, &POS_OK);
    TEST_ASSERT_EQUAL(2, sscanf(reply, "AZ=%d EL=%d", &az, &el));
    TEST_ASSERT_EQUAL(123, az);
    TEST_ASSERT_EQUAL(45, el);
}

static void test_no_reply_when_data_invalid(void)
{
    gs232_position_t az_dead = POS_OK;
    az_dead.az_ok = false;
    TEST_ASSERT_EQUAL(0, run("C", GS232_VARIANT_A, &az_dead));
    TEST_ASSERT_EQUAL(0, run("C2", GS232_VARIANT_A, &az_dead));
    gs232_position_t el_dead = POS_OK;
    el_dead.el_ok = false;
    TEST_ASSERT_EQUAL(0, run("C2", GS232_VARIANT_B, &el_dead));
    TEST_ASSERT_EQUAL(0, run("B", GS232_VARIANT_B, &el_dead));
    TEST_ASSERT_NOT_EQUAL(0, run("C", GS232_VARIANT_B, &el_dead));
}

static void test_elevation_not_installed_reads_zero(void)
{
    gs232_position_t az_only = { .az_ok = true, .az = 7, .el_installed = false };
    run("C2", GS232_VARIANT_A, &az_only);
    TEST_ASSERT_EQUAL_STRING("+0007+0000\r\n", reply);
}

static void test_negative_elevation_reported_as_zero(void)
{
    gs232_position_t low = POS_OK;
    low.el = -5;
    run("C2", GS232_VARIANT_A, &low);
    TEST_ASSERT_EQUAL_STRING("+0123+0000\r\n", reply);
}

static void test_goto_commands(void)
{
    TEST_ASSERT_EQUAL(0, run("M270", GS232_VARIANT_A, &POS_OK));
    TEST_ASSERT_EQUAL(GS232_ACT_AZ_GOTO, act.type);
    TEST_ASSERT_EQUAL(270, act.az);

    TEST_ASSERT_EQUAL(0, run("W090 030", GS232_VARIANT_B, &POS_OK));
    TEST_ASSERT_EQUAL(GS232_ACT_AZEL_GOTO, act.type);
    TEST_ASSERT_EQUAL(90, act.az);
    TEST_ASSERT_EQUAL(30, act.el);

    run("m400", GS232_VARIANT_A, &POS_OK);   // lowercase, overlap range folds to 0-359
    TEST_ASSERT_EQUAL(GS232_ACT_AZ_GOTO, act.type);
    TEST_ASSERT_EQUAL(40, act.az);
}

static void test_w_without_elevation_unit_moves_azimuth_only(void)
{
    gs232_position_t az_only = { .az_ok = true, .az = 7, .el_installed = false };
    run("W180 000", GS232_VARIANT_A, &az_only);
    TEST_ASSERT_EQUAL(GS232_ACT_AZ_GOTO, act.type);
    TEST_ASSERT_EQUAL(180, act.az);
}

static void test_stop_and_manual_commands(void)
{
    run("S", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_ACT_STOP_ALL, act.type);
    run("A", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_ACT_STOP_AZ, act.type);
    run("E", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_ACT_STOP_EL, act.type);
    run("R", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_ACT_AZ_GOTO, act.type);
    TEST_ASSERT_EQUAL(GS232_AZ_RIGHT_LIMIT, act.az);
    run("L", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_AZ_LEFT_LIMIT, act.az);
    run("U", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_ACT_EL_GOTO, act.type);
    TEST_ASSERT_EQUAL(GS232_EL_UP_LIMIT, act.el);
    run("D", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL(GS232_EL_DOWN_LIMIT, act.el);
}

static void test_invalid_and_empty_lines(void)
{
    TEST_ASSERT_EQUAL(0, run("", GS232_VARIANT_B, &POS_OK));
    TEST_ASSERT_EQUAL(GS232_ACT_NONE, act.type);
    run("Q", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("?>\r\n", reply);
    run("M", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("?>\r\n", reply);
    run("M12x", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("?>\r\n", reply);
    TEST_ASSERT_EQUAL(GS232_ACT_NONE, act.type);
    run("W090", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("?>\r\n", reply);
    run("W090 200", GS232_VARIANT_A, &POS_OK);
    TEST_ASSERT_EQUAL_STRING("?>\r\n", reply);
    TEST_ASSERT_EQUAL(0, run("X2", GS232_VARIANT_A, &POS_OK));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_position_replies_gs232a);
    RUN_TEST(test_position_replies_gs232b);
    RUN_TEST(test_hamlib_parses_replies);
    RUN_TEST(test_no_reply_when_data_invalid);
    RUN_TEST(test_elevation_not_installed_reads_zero);
    RUN_TEST(test_negative_elevation_reported_as_zero);
    RUN_TEST(test_goto_commands);
    RUN_TEST(test_w_without_elevation_unit_moves_azimuth_only);
    RUN_TEST(test_stop_and_manual_commands);
    RUN_TEST(test_invalid_and_empty_lines);
    return UNITY_END();
}
