#include <math.h>
#include <stdio.h>

#include "../lobby/src/player_model.h"

int tests_run = 0;
int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("❌ FAIL: %s\n", msg); \
    } else { \
        printf("✅ PASS: %s\n", msg); \
        tests_passed++; \
    } \
} while(0)

static void test_shell_proportions(void) {
    printf("--- Testing Ronin Shell Proportions ---\n");
    float shoulder_span = 2.0f * (RONIN_SHOULDER_PAD_OFFSET + (RONIN_SHOULDER_PAD_W * 0.5f));
    ASSERT_TRUE(shoulder_span > RONIN_TORSO_W, "Shoulders are broader than torso");
    ASSERT_TRUE(RONIN_PANTS_H > RONIN_TORSO_H, "Tech cargo height exceeds cropped jacket");
    ASSERT_TRUE(RONIN_LINING_Y_BOTTOM < RONIN_LINING_Y_TOP, "Red lining has visible thickness");
}

static void test_mask_proportions(void) {
    printf("--- Testing Storm Mask Proportions ---\n");
    ASSERT_TRUE(RONIN_HEAD_W >= RONIN_FACEPLATE_W, "Faceplate fits within head width");
    ASSERT_TRUE(RONIN_VENT_H > 0.0f && RONIN_VENT_W > 0.0f, "Cyan vents have non-zero area");
    ASSERT_TRUE(RONIN_HORN_H > RONIN_HORN_W, "Broken horn is taller than wide");
}

int main(void) {
    printf("🛡️ SHANKPIT PLAYER MODEL CHECK 🛡️\n");
    test_shell_proportions();
    test_mask_proportions();
    printf("\n--------------------------------------\n");
    printf("SUMMARY: %d/%d Tests Passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
