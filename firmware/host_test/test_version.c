#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db_version.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_order(void)
{
    const char *up[] = {
        "not a version", "v0.9.9", "v1.0.0-rc2",        "v1.0.0-rc3", "v1.0.0-rc3-1-g9925b3f-dirty",
        "v1.0.0-rc10",   "v1.0.0", "v1.0.0-4-gabcdef0", "v1.0.1",     "v1.1.0",
        "v2.0.0",
    };
    for (size_t i = 0; i < sizeof(up) / sizeof(up[0]); i++) {
        for (size_t j = 0; j < sizeof(up) / sizeof(up[0]); j++) {
            int c = db_version_cmp(up[i], up[j]);
            int want = i < j ? -1 : i > j;
            TEST_ASSERT_EQUAL_INT_MESSAGE(want, c < 0 ? -1 : c > 0, up[i]);
        }
    }
}

static void test_dirty_and_prerelease(void)
{
    TEST_ASSERT_EQUAL_INT(0, db_version_cmp("v1.0.0-dirty", "v1.0.0")); /* a local build of the release */
    TEST_ASSERT_TRUE(db_version_pre("v1.0.0-rc3"));
    TEST_ASSERT_TRUE(db_version_pre("v1.0.0-rc3-1-g9925b3f-dirty"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-4-gabcdef0"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-dirty"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-4-gdeadbee"));
}

#define REPO "https://github.com/FusselTV/DeckConnector"

/* An entry as GitHub's releases.atom has it: the notes are escaped HTML. */
static char *entry(char *at, const char *tag, const char *notes)
{
    return at + sprintf(at,
                        "<entry><id>tag:github.com,2008:Repository/1/%s</id><link rel=\"alternate\" "
                        "type=\"text/html\" href=\"" REPO "/releases/tag/%s\"/><title>%s</title><content "
                        "type=\"html\">%s</content></entry>",
                        tag, tag, tag, notes);
}

static void test_the_newest_release_in_a_feed(void)
{
    char feed[2048], tag[40], base[96];
    char *at =
        feed + sprintf(feed, "<feed><link type=\"text/html\" rel=\"alternate\" href=\"" REPO "/releases\"/>");
    at = entry(at, "v1.3.0-rc1", "");
    at = entry(at, "v1.2.0", "&lt;p&gt;fixes&lt;/p&gt;");
    entry(at, "v1.1.0", "");
    TEST_ASSERT_TRUE(db_version_newest(feed, false, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.2.0", tag);
    TEST_ASSERT_EQUAL_STRING(REPO, base);
    TEST_ASSERT_TRUE(db_version_newest(feed, true, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.3.0-rc1", tag);
    *strstr(feed, "/releases/tag/v1.1.0") = '\0'; /* a feed cut off at FEED_MAX */
    TEST_ASSERT_TRUE(db_version_newest(feed, false, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.2.0", tag);
    entry(feed, "v1.1.5", ""); /* a backport above a newer release */
    entry(feed + strlen(feed), "v1.2.0", "");
    TEST_ASSERT_TRUE(db_version_newest(feed, false, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.2.0", tag);
    TEST_ASSERT_FALSE(db_version_newest(NULL, true, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_FALSE(db_version_newest("<feed></feed>", true, tag, sizeof(tag), base, sizeof(base)));
}

/* A newer tag named in the notes, plain or as a link, is no release. */
/* A tag or repository too long for the caller's room, or no version, is skipped whole, never cut. */
static void test_links_that_do_not_fit_are_skipped(void)
{
    char feed[2048], tag[40], base[96];
    char *at = feed + sprintf(feed, "<link href=\"https://github.com/%0100d/releases/tag/v9.0.0\"/>", 0);
    at = entry(at, "v8.0.0-averyveryveryveryveryverylongname1", "");
    at = entry(at, "nightly", "");
    entry(at, "v1.2.0", "");
    TEST_ASSERT_TRUE(db_version_newest(feed, true, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.2.0", tag);
    TEST_ASSERT_EQUAL_STRING(REPO, base);
}

static void test_a_tag_in_the_notes_is_not_offered(void)
{
    char feed[2048], tag[40], base[96];
    entry(feed, "v1.2.0",
          "see https://example.com/x/releases/tag/v9.0.0 and &lt;a href=&quot;" REPO
          "/releases/tag/v8.0.0&quot;&gt;v8&lt;/a&gt; \"" REPO "/releases/tag/v7.0.0\"");
    TEST_ASSERT_TRUE(db_version_newest(feed, false, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_EQUAL_STRING("v1.2.0", tag);
    TEST_ASSERT_EQUAL_STRING(REPO, base);
}

/* 128 KB of ever newer tag paths and no quote (the parser before walked back to the start for each), then
 * 128 KB of links with the only href at the end. */
static void test_a_huge_feed_without_quotes_is_quick(void)
{
    size_t n = 128 * 1024;
    char *feed = malloc(n + 32);
    size_t at = 0;
    for (unsigned i = 0; at < n; i++) {
        at += (size_t)sprintf(feed + at, "/releases/tag/v1.0.%u<", i);
    }
    char tag[40], base[96];
    clock_t t = clock();
    TEST_ASSERT_FALSE(db_version_newest(feed, true, tag, sizeof(tag), base, sizeof(base)));
    for (at = 0; at < n; at += 8) {
        memcpy(feed + at, "<link a>", 8);
    }
    strcpy(feed + at, "href=\"/releases/tag/v1.0.0\"");
    TEST_ASSERT_FALSE(db_version_newest(feed, true, tag, sizeof(tag), base, sizeof(base)));
    TEST_ASSERT_LESS_THAN(CLOCKS_PER_SEC / 10, clock() - t);
    free(feed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_order);
    RUN_TEST(test_dirty_and_prerelease);
    RUN_TEST(test_the_newest_release_in_a_feed);
    RUN_TEST(test_a_tag_in_the_notes_is_not_offered);
    RUN_TEST(test_links_that_do_not_fit_are_skipped);
    RUN_TEST(test_a_huge_feed_without_quotes_is_quick);
    return UNITY_END();
}
