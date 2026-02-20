#include <tgfclient.h>
#include "featuresMenu.h"
#include "AiFeatures.h"

static void *featuresHandle = NULL;
static int   analysisButtonId   = -1;
static int   commentaryButtonId = -1;
static int   coachButtonId      = -1;

static void ToggleAnalysis(void *unused)
{
    analysis = !analysis;
    GfuiButtonSetText(
        featuresHandle,
        analysisButtonId,
        analysis ? "Granite Analysis: ON" : "Granite Analysis: OFF"
    );
}

static void ToggleCommentary(void *unused)
{
    commentary = !commentary;
    GfuiButtonSetText(
        featuresHandle,
        commentaryButtonId,
        commentary ? "Live Commentary: ON" : "Live Commentary: OFF"
    );
}

static void ToggleCoach(void *unused)
{
    coach = !coach;
    GfuiButtonSetText(
        featuresHandle,
        coachButtonId,
        coach ? "Granite Live Coach: ON" : "Granite Live Coach: OFF"
    );
}

void* FeaturesMenuInit(void *prevMenu)
{
    if (featuresHandle) return featuresHandle;

    featuresHandle = GfuiMenuScreenCreate("FEATURES");
    GfuiScreenAddBgImg(featuresHandle, "data/img/splash-options.png");

    analysisButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        analysis ? "Granite AI Coach: ON" : "Granite AI Coach: OFF",
        "Enable or disable Granite AI Coach",
        NULL,
        ToggleAnalysis
    );

    commentaryButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        commentary ? "Feature 2 (Commentary): ON" : "Feature 2 (Commentary): OFF",
        "Enable or disable Commentary",
        NULL,
        ToggleCommentary
    );

    coachButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        coach ? "Feature 3 (Coach): ON" : "Feature 3 (Coach): OFF",
        "Enable or disable Coach",
        NULL,
        ToggleCoach
    );

    GfuiMenuBackQuitButtonCreate(
        featuresHandle,
        "Back",
        "Return to Options",
        prevMenu,
        GfuiScreenActivate
    );

    return featuresHandle;
}