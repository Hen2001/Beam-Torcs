#include <tgfclient.h>
#include "featuresMenu.h"
#include "AiFeatures.h"

static void *featuresHandle = NULL;



static void ToggleResponseBot(void *unused)
{
    analysis = !analysis;
}

void* FeaturesMenuInit(void *prevMenu)
{
    if (featuresHandle) return featuresHandle;

    featuresHandle = GfuiMenuScreenCreate("FEATURES");

    GfuiScreenAddBgImg(featuresHandle, "data/img/splash-options.png");

    const char* label = analysis ?
        "Granite AI Coach: ON" :
        "Granite AI Coach: OFF";

    GfuiMenuButtonCreate(featuresHandle,
                         label,
                         "Enable or disable Granite AI Coach",
                         NULL,
                         ToggleResponseBot);

    GfuiMenuButtonCreate(featuresHandle,
                         "Future Feature 2",
                         "Reserved",
                         NULL,
                         NULL);

    GfuiMenuButtonCreate(featuresHandle,
                         "Future Feature 3",
                         "Reserved",
                         NULL,
                         NULL);

    GfuiMenuBackQuitButtonCreate(featuresHandle,
                                 "Back",
                                 "Return to Options",
                                 prevMenu,
                                 GfuiScreenActivate);

    return featuresHandle;
}