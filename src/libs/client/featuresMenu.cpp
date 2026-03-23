#include <tgfclient.h>
#include "featuresMenu.h"
#include "AiFeatures.h"

static void *featuresHandle    = NULL;
static int   analysisButtonId  = -1;
static int commentaryButtonId  = -1;
static int      coachButtonId  = -1;
static int   engineerButtonId  = -1;

static void ToggleAnalysis(void *unused)
{
    analysis = !analysis;
    GfuiButtonSetText(
        featuresHandle,
        analysisButtonId,
        analysis ? "Granite Analysis: ON" : "Granite Analysis: OFF"
    );
}

static void DisableAll(void)
{
    commentary = false;
    coach = false;
    engineer = false;

    GfuiButtonSetText(featuresHandle, commentaryButtonId, "Live Commentary: OFF");
    GfuiButtonSetText(featuresHandle, coachButtonId, "Granite Live Coach: OFF");
    GfuiButtonSetText(featuresHandle, engineerButtonId, "Race Engineer: OFF");
}

static void ToggleCommentary(void *unused)
{
    bool wasOn = commentary;
    DisableAll();
    if (!wasOn)
    {
        commentary = true;
        GfuiButtonSetText(featuresHandle, commentaryButtonId, "Live Commentary: ON");
    }
}

static void ToggleCoach(void *unused)
{
    bool wasOn = coach;
    DisableAll();
    if (!wasOn)
    {
        coach = true;
        GfuiButtonSetText(featuresHandle, coachButtonId, "Granite Live Coach: ON");
    }
}

static void ToggleEngineer(void *unused)
{
    bool wasOn = engineer;
    DisableAll();
    if (!wasOn)
    {
        engineer = true;
        GfuiButtonSetText(featuresHandle, engineerButtonId, "Race Engineer: ON");
    }
}

void* FeaturesMenuInit(void *prevMenu)
{
    if (featuresHandle) return featuresHandle;

    featuresHandle = GfuiMenuScreenCreate("FEATURES");
    GfuiScreenAddBgImg(featuresHandle, "data/img/splash-options.png");

    analysisButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        analysis ? "Granite Analysis: ON" : "Granite Analysis: OFF",
        "Enable or disable Granite Analysis",
        NULL,
        ToggleAnalysis
    );

    commentaryButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        commentary ? "Live Commentary: ON" : "Live Commentary: OFF",
        "Enable or disable Live Commentary",
        NULL,
        ToggleCommentary
    );

    coachButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        coach ? "Granite Live Coach: ON" : "Granite Live Coach: OFF",
        "Enable or disable Granite Live Coach",
        NULL,
        ToggleCoach
    );

    engineerButtonId = GfuiMenuButtonCreate(
        featuresHandle,
        engineer ? "Race Engineer: ON" : "Race Engineer: OFF",
        "Enable or disable Race Engineer",
        NULL,
        ToggleEngineer
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