/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pad_script.h"
#include "am/si.h"

#include "am_test.h"

TEST(parse_and_hold_windows) {
    PadScript s;
    EXPECT_TRUE(pad_script_parse("200:D_LEFT:8,240:A+START:2", &s));
    EXPECT_EQ(s.count, 2U);
    EXPECT_EQ(pad_script_buttons(&s, 199), 0U);
    EXPECT_EQ(pad_script_buttons(&s, 200), SI_PAD_LEFT);
    EXPECT_EQ(pad_script_buttons(&s, 207), SI_PAD_LEFT);
    EXPECT_EQ(pad_script_buttons(&s, 208), 0U);
    EXPECT_EQ(pad_script_buttons(&s, 241), SI_PAD_A | SI_PAD_START);
    EXPECT_TRUE(pad_script_parse("", &s));
    EXPECT_EQ(s.count, 0U);
}

TEST(rejects_bad_text) {
    PadScript s;
    EXPECT_TRUE(!pad_script_parse("200:L:8", &s)); /* L/R ficam de fora */
    EXPECT_TRUE(!pad_script_parse("x:A:8", &s));
    EXPECT_TRUE(!pad_script_parse("10:A:0", &s));
    EXPECT_TRUE(!pad_script_parse("10:A:5;", &s));
}

int main(void) {
    RUN_TEST(parse_and_hold_windows);
    RUN_TEST(rejects_bad_text);
    return am_test_finish();
}
