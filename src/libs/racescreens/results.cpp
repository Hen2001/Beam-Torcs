/***************************************************************************

    file                 : results.cpp
    created              : Fri Apr 14 22:36:36 CEST 2000
    copyright            : (C) 2000-2014 by Eric Espie, Bernhard Wymann
    email                : torcs@free.fr
    version              : $Id: results.cpp,v 1.6.2.10 2014/05/23 08:38:31 berniw Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/** @file
    Display results.
    @author Bernhard Wymann, Eric Espie
    @version $Id: results.cpp,v 1.6.2.10 2014/05/23 08:38:31 berniw Exp $
*/

#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include <windows.h>
#endif
#include <tgfclient.h>
#include <osspec.h>
#include <racescreens.h>
#include <robottools.h>
#include <robot.h>
#include <portability.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <string>
#include "AiFeatures.h"
#include <tgfclient.h>

std::vector<std::string> wrapText(const char* text, int maxLineLength);
std::string insertKeywordBreaks(const std::string& input);

static int	rmSaveId;
static void	*rmScrHdle = NULL;
static bool responseBot = true;
static void *featuresScr = NULL;

static void rmPracticeResults(void *prevHdle, tRmInfo *info, int start);
static void rmRaceResults(void *prevHdle, tRmInfo *info, int start);
static void rmQualifResults(void *prevHdle, tRmInfo *info, int start);
static void rmShowStandings(void *prevHdle, tRmInfo *info, int start);

#define MAX_LINES	20

typedef struct
{
	void *prevHdle;
	tRmInfo *info;
	int start;
} tRaceCall;

tRaceCall RmNextRace;
tRaceCall RmPrevRace;


static void rmSaveRes(void *vInfo)
{
	tRmInfo *info = (tRmInfo *)vInfo;
	GfParmCreateDirectory(0, info->results);
	GfParmWriteFile(0, info->results, "Results");
	GfuiVisibilitySet(rmScrHdle, rmSaveId, GFUI_INVISIBLE);
}

static void toggleResponseBot(void *unused)
{
    responseBot = !responseBot;

    printf("ResponseBot is now: %s\n", responseBot ? "ON" : "OFF");
}
static void rmFeaturesScreen(void *prevHdle)
{
    featuresScr = GfuiScreenCreate();

    GfuiScreenAddBgImg(featuresScr, "data/img/splash-result.png");
    GfuiTitleCreate(featuresScr, "Features", 8);

    // ---- Toggle Button Text ----
    const char* label = responseBot ?
        "Granite AI Coach: ON" :
        "Granite AI Coach: OFF";

    GfuiButtonCreate(featuresScr,
                     label,
                     GFUI_FONT_MEDIUM_C,
                     320,
                     320,
                     300,
                     GFUI_ALIGN_HC_VB,
                     0,
                     NULL,
                     toggleResponseBot,
                     NULL,
                     NULL,
                     NULL);

    // ---- Placeholder Buttons ----
    GfuiButtonCreate(featuresScr,
                     "Future Feature 2",
                     GFUI_FONT_MEDIUM_C,
                     320,
                     260,
                     300,
                     GFUI_ALIGN_HC_VB,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     NULL,
                     NULL);

    GfuiButtonCreate(featuresScr,
                     "Future Feature 3",
                     GFUI_FONT_MEDIUM_C,
                     320,
                     200,
                     300,
                     GFUI_ALIGN_HC_VB,
                     0,
                     NULL,
                     NULL,
                     NULL,
                     NULL,
                     NULL);

    // ---- Back Button ----
    GfuiButtonCreate(featuresScr,
                     "Back",
                     GFUI_FONT_MEDIUM_C,
                     320,
                     60,
                     200,
                     GFUI_ALIGN_HC_VB,
                     0,
                     prevHdle,
                     GfuiScreenReplace,
                     NULL,
                     NULL,
                     NULL);

    GfuiScreenActivate(featuresScr);
}


static void rmChgPracticeScreen(void *vprc)
{
	void *prevScr = rmScrHdle;
	tRaceCall *prc = (tRaceCall*)vprc;

	rmPracticeResults(prc->prevHdle, prc->info, prc->start);
	GfuiScreenRelease(prevScr);
}


static void rmPracticeResults(void *prevHdle, tRmInfo *info, int start)
{
	void *results = info->results;
	const char *race = info->_reRaceName;
	int i;
	int x1, x2, x3, x4, x5, x6;
	int offset;
	int y;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char path[BUFSIZE];
	const int TIMEFMTSIZE = 256;
	char timefmt[TIMEFMTSIZE];
	float fgcolor[4] = {1.0, 0.0, 1.0, 1.0};
	int totLaps;

	rmScrHdle = GfuiScreenCreate();
	snprintf(buf, BUFSIZE, "Practice Results");
	GfuiTitleCreate(rmScrHdle, buf, strlen(buf));
	snprintf(path, BUFSIZE, "%s/%s/%s", info->track->name, RE_SECT_RESULTS, race);
	snprintf(buf, BUFSIZE, "%s on track %s", GfParmGetStr(results, path, RM_ATTR_DRVNAME, ""), info->track->name);
	GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_LARGE_C,
			320, 420, GFUI_ALIGN_HC_VB, 0);
	GfuiScreenAddBgImg(rmScrHdle, "data/img/splash-result.png");
	
	offset = 90;
	
	x1 = offset + 30;
	x2 = offset + 50;
	x3 = offset + 130;
	x4 = offset + 240;
	x5 = offset + 310;
	x6 = offset + 400;
	
	y = 400;
	GfuiLabelCreateEx(rmScrHdle, "Lap",       fgcolor, GFUI_FONT_MEDIUM_C, x1, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Time",      fgcolor, GFUI_FONT_MEDIUM_C, x2+20, y, GFUI_ALIGN_HL_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Best",      fgcolor, GFUI_FONT_MEDIUM_C, x3+20, y, GFUI_ALIGN_HL_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Top Spd",   fgcolor, GFUI_FONT_MEDIUM_C, x4, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Min Spd",   fgcolor, GFUI_FONT_MEDIUM_C, x5, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Damages",  fgcolor, GFUI_FONT_MEDIUM_C, x6, y, GFUI_ALIGN_HC_VB, 0);
	y -= 20;
	
	snprintf(path, BUFSIZE, "%s/%s/%s", info->track->name, RE_SECT_RESULTS, race);
	totLaps = (int)GfParmGetEltNb(results, path);
	for (i = 0 + start; i < MIN(start + MAX_LINES, totLaps); i++) {
		snprintf(path, BUFSIZE, "%s/%s/%s/%d", info->track->name, RE_SECT_RESULTS, race, i + 1);

		/* Lap */
		snprintf(buf, BUFSIZE, "%d", i+1);
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C, x1, y, GFUI_ALIGN_HC_VB, 0);

		/* Time */
		GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_TIME, NULL, 0), 0);
		GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C, x2, y, GFUI_ALIGN_HL_VB, 0);

		/* Best Lap Time */
		GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_BEST_LAP_TIME, NULL, 0), 0);
		GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HL_VB, 0);

		/* Top Spd */
		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_TOP_SPEED, NULL, 0) * 3.6));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C, x4, y, GFUI_ALIGN_HC_VB, 0);

		/* Min Spd */
		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_BOT_SPEED, NULL, 0) * 3.6));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C, x5, y, GFUI_ALIGN_HC_VB, 0);

		/* Damages */
		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_DAMMAGES, NULL, 0)));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C, x6, y, GFUI_ALIGN_HC_VB, 0);

		y -= 15;
	}

	if (start > 0) {
		RmPrevRace.prevHdle = prevHdle;
		RmPrevRace.info     = info;
		RmPrevRace.start    = start - MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-up.png", "data/img/arrow-up.png",
				"data/img/arrow-up.png", "data/img/arrow-up-pushed.png",
				80, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmPrevRace, rmChgPracticeScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_UP,   "Previous Results", (void*)&RmPrevRace, rmChgPracticeScreen, NULL);
	}
	
	GfuiButtonCreate(rmScrHdle,
			"Continue",
			GFUI_FONT_LARGE,
			320,
			40,
			150,
			GFUI_ALIGN_HC_VB,
			0,
			prevHdle,
			GfuiScreenReplace,
			NULL,
			(tfuiCallback)NULL,
			(tfuiCallback)NULL);

	if (i < totLaps) {
		RmNextRace.prevHdle = prevHdle;
		RmNextRace.info     = info;
		RmNextRace.start    = start + MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-down.png", "data/img/arrow-down.png",
				"data/img/arrow-down.png", "data/img/arrow-down-pushed.png",
				540, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmNextRace, rmChgPracticeScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_DOWN, "Next Results", (void*)&RmNextRace, rmChgPracticeScreen, NULL);
	}

	GfuiAddKey(rmScrHdle, (unsigned char)27, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddKey(rmScrHdle, (unsigned char)13, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddSKey(rmScrHdle, GLUT_KEY_F12, "Take a Screen Shot", NULL, GfuiScreenShot, NULL);

	GfuiScreenActivate(rmScrHdle);
}


static void rmChgRaceScreen(void *vprc)
{
	void *prevScr = rmScrHdle;
	tRaceCall *prc = (tRaceCall*)vprc;

	rmRaceResults(prc->prevHdle, prc->info, prc->start);
	GfuiScreenRelease(prevScr);
}



std::string insertKeywordBreaks(const std::string& input)
{
    std::string text = input;

    const std::vector<std::string> keywords = {
        "Speed:",
        "Specific Coaching Focus:"
    };

    for (const auto& key : keywords) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            if (pos != 0 && text[pos - 1] != '\n') {
                text.insert(pos, "\n");
                pos += 1;
            }
            pos += key.length();
        }
    }

    return text;
}

std::vector<std::string> wrapText(const char* text, int maxLineLength)
{
    std::vector<std::string> lines;

    std::string processed = insertKeywordBreaks(text);

    size_t start = 0;
    while (start < processed.length()) {

        size_t end = processed.find('\n', start);
        std::string segment;

        if (end == std::string::npos) {
            segment = processed.substr(start);
            start = processed.length();
        } else {
            segment = processed.substr(start, end - start);
            start = end + 1;
        }

        while (segment.length() > maxLineLength) {
            int breakPos = segment.rfind(' ', maxLineLength);
            if (breakPos == std::string::npos)
                breakPos = maxLineLength;

            lines.push_back(segment.substr(0, breakPos));
            segment = segment.substr(breakPos + 1);
        }

        if (!segment.empty())
            lines.push_back(segment);
    }

    return lines;
}


static void rmGraniteAnalysis(void *prevHdle)
{
    const char* filepath = "/home/lewis/.torcs/DrivingData/granite_analysis.txt";

    void *analysisScr = GfuiScreenCreate();
    GfuiScreenAddBgImg(analysisScr, "data/img/splash-result.png");
    GfuiTitleCreate(analysisScr, "Granite AI Coach", 18);

    if (!analysis) {
        GfuiLabelCreate(analysisScr,
                        "Granite AI Coach is disabled in Features.",
                        GFUI_FONT_MEDIUM_C,
                        320, 300,
                        GFUI_ALIGN_HC_VB,
                        0);

        GfuiButtonCreate(analysisScr,
                         "Back",
                         GFUI_FONT_LARGE_C,
                         320,
                         40,
                         200,
                         GFUI_ALIGN_HC_VB,
                         0,
                         prevHdle,
                         GfuiScreenReplace,
                         NULL, NULL, NULL);

        GfuiScreenActivate(analysisScr);
        return;
    }

    
    GfuiLabelCreate(analysisScr,
                    "Generating analysis... please wait",
                    GFUI_FONT_MEDIUM_C,
                    320, 300,
                    GFUI_ALIGN_HC_VB,
                    0);

    GfuiScreenActivate(analysisScr);

   
    struct stat st;
    int attempts = 0;

    while (attempts < 60) { 
        if (stat(filepath, &st) == 0 && st.st_size > 0) {
            break;
        }
        sleep(1);
        attempts++;
    }

    
    char analysisText[4096] = {0};
    FILE* f = fopen(filepath, "r");

    if (f) {
        size_t bytesRead = fread(analysisText, 1, sizeof(analysisText) - 1, f);
        analysisText[bytesRead] = '\0';
        fclose(f);
    } else {
        strcpy(analysisText, "Analysis could not be generated.");
    }

    
    GfuiScreenRelease(analysisScr);
    analysisScr = GfuiScreenCreate();
    GfuiScreenAddBgImg(analysisScr, "data/img/splash-result.png");
    GfuiTitleCreate(analysisScr, "Granite AI Coach", 18);

    
    const int maxCharsPerLine = 100;
    int y = 430;   

    char *linePtr = strtok(analysisText, "\n");

    while (linePtr && y > 80)
    {
      
        if (strstr(linePtr, "Speed:") != NULL ||
            strstr(linePtr, "Specific Coaching Focus:") != NULL)
        {
            y -= 15;
        }

        char tempLine[1024];
        strncpy(tempLine, linePtr, sizeof(tempLine));
        tempLine[sizeof(tempLine)-1] = '\0';

        while (strlen(tempLine) > maxCharsPerLine)
        {
            int breakPos = maxCharsPerLine;

         
            for (int i = maxCharsPerLine; i > 0; i--)
            {
                if (tempLine[i] == ' ') {
                    breakPos = i;
                    break;
                }
            }

            char segment[1024];
            strncpy(segment, tempLine, breakPos);
            segment[breakPos] = '\0';

            GfuiLabelCreate(analysisScr,
                            segment,
                            GFUI_FONT_MEDIUM_C,
                            320, y,
                            GFUI_ALIGN_HC_VB,
                            0);

            y -= 20;

            memmove(tempLine,
                    tempLine + breakPos + 1,
                    strlen(tempLine) - breakPos);
        }

        if (strlen(tempLine) > 0)
        {
            GfuiLabelCreate(analysisScr,
                            tempLine,
                            GFUI_FONT_MEDIUM_C,
                            320, y,
                            GFUI_ALIGN_HC_VB,
                            0);

            y -= 22;
        }

        linePtr = strtok(NULL, "\n");
    }

    GfuiButtonCreate(analysisScr,
                     "Back",
                     GFUI_FONT_LARGE_C,
                     320,
                     40,
                     200,
                     GFUI_ALIGN_HC_VB,
                     0,
                     prevHdle,
                     GfuiScreenReplace,
                     NULL, NULL, NULL);

    GfuiScreenActivate(analysisScr);
}


static void rmRaceResults(void *prevHdle, tRmInfo *info, int start)
{
	void *results = info->results;
	const char *race = info->_reRaceName;
	int i;
	int x1, x2, x3, x4, x5, x6, x7, x8, x9;
	int dlap;
	int y;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char path[BUFSIZE];
	const int TIMEFMTSIZE = 256;
	char timefmt[TIMEFMTSIZE];
	float fgcolor[4] = {1.0, 0.0, 1.0, 1.0};
	int laps, totLaps;
	tdble refTime;
	int nbCars;

	rmScrHdle = GfuiScreenCreate();
	snprintf(buf, BUFSIZE, "Race Results");
	GfuiTitleCreate(rmScrHdle, buf, strlen(buf));
	snprintf(buf, BUFSIZE, "%s", info->track->name);
	GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_LARGE_C,
			320, 420, GFUI_ALIGN_HC_VB, 0);
	GfuiScreenAddBgImg(rmScrHdle, "data/img/splash-result.png");
	
	x1 = 30;
	x2 = 60;
	x3 = 260;
	x4 = 330;
	x5 = 360;
	x6 = 420;
	x7 = 490;
	x8 = 545;
	x9 = 630;
	
	y = 400;
	GfuiLabelCreateEx(rmScrHdle, "Rank",      fgcolor, GFUI_FONT_MEDIUM_C, x1, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Driver",    fgcolor, GFUI_FONT_MEDIUM_C, x2+10, y, GFUI_ALIGN_HL_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Total",     fgcolor, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HR_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Best",      fgcolor, GFUI_FONT_MEDIUM_C, x4, y, GFUI_ALIGN_HR_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Laps",      fgcolor, GFUI_FONT_MEDIUM_C, x5, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Top Spd",   fgcolor, GFUI_FONT_MEDIUM_C, x6, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Damage",    fgcolor, GFUI_FONT_MEDIUM_C, x7, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Pit",       fgcolor, GFUI_FONT_MEDIUM_C, x8, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Penalty",   fgcolor, GFUI_FONT_MEDIUM_C, x9, y, GFUI_ALIGN_HR_VB, 0);	
	y -= 20;
	
	snprintf(path, BUFSIZE, "%s/%s/%s", info->track->name, RE_SECT_RESULTS, race);
	totLaps = (int)GfParmGetNum(results, path, RE_ATTR_LAPS, NULL, 0);
	snprintf(path, BUFSIZE, "%s/%s/%s/%s/%d", info->track->name, RE_SECT_RESULTS, race, RE_SECT_RANK, 1);
	refTime = GfParmGetNum(results, path, RE_ATTR_TIME, NULL, 0);
	snprintf(path, BUFSIZE, "%s/%s/%s/%s", info->track->name, RE_SECT_RESULTS, race, RE_SECT_RANK);
	nbCars = (int)GfParmGetEltNb(results, path);
	for (i = start; i < MIN(start + MAX_LINES, nbCars); i++) {
		snprintf(path, BUFSIZE, "%s/%s/%s/%s/%d", info->track->name, RE_SECT_RESULTS, race, RE_SECT_RANK, i + 1);
		laps = (int)GfParmGetNum(results, path, RE_ATTR_LAPS, NULL, 0);

		snprintf(buf, BUFSIZE, "%d", i+1);
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x1, y, GFUI_ALIGN_HC_VB, 0);

		GfuiLabelCreate(rmScrHdle, GfParmGetStr(results, path, RE_ATTR_NAME, ""), GFUI_FONT_MEDIUM_C,
				x2, y, GFUI_ALIGN_HL_VB, 0);

		if (laps == totLaps) {
			if (i == 0) {
				GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_TIME, NULL, 0), 0);
			} else {
				GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_TIME, NULL, 0) - refTime, 1);
			}
			GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HR_VB, 0);
		} else {
			dlap = totLaps - laps;
			if (dlap == 1) {
				snprintf(buf, BUFSIZE, "+1 Lap");
			} else {
				snprintf(buf, BUFSIZE, "+%d Laps", dlap);
			}
			GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HR_VB, 0);

		}

		GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_BEST_LAP_TIME, NULL, 0), 0);
		GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C,
				x4, y, GFUI_ALIGN_HR_VB, 0);

		snprintf(buf, BUFSIZE, "%d", laps);
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x5, y, GFUI_ALIGN_HC_VB, 0);

		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_TOP_SPEED, NULL, 0) * 3.6));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x6, y, GFUI_ALIGN_HC_VB, 0);

		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_DAMMAGES, NULL, 0)));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x7, y, GFUI_ALIGN_HC_VB, 0);

		snprintf(buf, BUFSIZE, "%d", (int)(GfParmGetNum(results, path, RE_ATTR_NB_PIT_STOPS, NULL, 0)));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x8, y, GFUI_ALIGN_HC_VB, 0);

		GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_PENALTYTIME, NULL, 0), 0);
		GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C, x9, y, GFUI_ALIGN_HR_VB, 0);

		y -= 15;
	}

// 	char analysis[2048] = {0};
// const char* filepath = "/home/lewis/.torcs/DrivingData/granite_analysis.txt";
// int attempts = 0;
// while (attempts < 60) {   // wait up to ~10 seconds
//     struct stat st;
// 	printf("Waiting for analysis... attempt %d\n", attempts);

//     if (stat(filepath, &st) == 0 && st.st_size > 0) {
//         break;  // file exists and has content
//     }
//     sleep(1);
//     attempts++;
// }
// printf("Finished waiting. Attempts: %d\n", attempts);

// FILE* f = fopen(filepath, "r");
// if (f) {
//     size_t bytesRead = fread(analysis, 1, sizeof(analysis) - 1, f);
//     analysis[bytesRead] = '\0';   // ensure null termination
//     fclose(f);
// } else {
//     strcpy(analysis, "Granite AI Coach analysis unavailable.");
// }


// 	std::vector<std::string> lines = wrapText(analysis, 100);

// int yPos = 350;   // starting height (move up here)

// for (size_t i = 0; i < lines.size(); i++) {
//     GfuiLabelCreate(rmScrHdle,
//                     lines[i].c_str(),
//                     GFUI_FONT_MEDIUM_C,
//                     320, yPos,
//                     GFUI_ALIGN_HC_VB,
//                     0);

//     yPos -= 18;  // spacing between lines
// }


	if (start > 0) {
		RmPrevRace.prevHdle = prevHdle;
		RmPrevRace.info     = info;
		RmPrevRace.start    = start - MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-up.png", "data/img/arrow-up.png",
				"data/img/arrow-up.png", "data/img/arrow-up-pushed.png",
				80, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmPrevRace, rmChgRaceScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_UP,   "Previous Results", (void*)&RmPrevRace, rmChgRaceScreen, NULL);
	}
GfuiButtonCreate(rmScrHdle,
                 "View Granite Analysis",
                 GFUI_FONT_LARGE_C,
                 320,
                 60,
                 250,
                 GFUI_ALIGN_HC_VB,
                 0,                     // shortcut
                 rmScrHdle,             // pass current screen
                 rmGraniteAnalysis,     // callback
                 NULL,
                 NULL,
                 NULL);


	GfuiButtonCreate(rmScrHdle,
			"Continue",
			GFUI_FONT_LARGE,
			/* 210, */
			320,
			40,
			150,
			GFUI_ALIGN_HC_VB,
			0,
			prevHdle,
			GfuiScreenReplace,
			NULL,
			(tfuiCallback)NULL,
			(tfuiCallback)NULL);

	if (i < nbCars) {
		RmNextRace.prevHdle = prevHdle;
		RmNextRace.info     = info;
		RmNextRace.start    = start + MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-down.png", "data/img/arrow-down.png",
				"data/img/arrow-down.png", "data/img/arrow-down-pushed.png",
				540, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmNextRace, rmChgRaceScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_DOWN, "Next Results", (void*)&RmNextRace, rmChgRaceScreen, NULL);
	}

	GfuiAddKey(rmScrHdle, (unsigned char)27, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddKey(rmScrHdle, (unsigned char)13, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddSKey(rmScrHdle, GLUT_KEY_F12, "Take a Screen Shot", NULL, GfuiScreenShot, NULL);

	GfuiScreenActivate(rmScrHdle);
}


static void rmChgQualifScreen(void *vprc)
{
	void *prevScr = rmScrHdle;
	tRaceCall *prc = (tRaceCall*)vprc;

	rmQualifResults(prc->prevHdle, prc->info, prc->start);
	GfuiScreenRelease(prevScr);
}


static void rmQualifResults(void *prevHdle, tRmInfo *info, int start)
{
	void *results = info->results;
	const char *race = info->_reRaceName;
	int i;
	int x1, x2, x3;
	int y;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char path[BUFSIZE];
	const int TIMEFMTSIZE = 256;
	char timefmt[TIMEFMTSIZE];
	float fgcolor[4] = {1.0, 0.0, 1.0, 1.0};
	int nbCars;
	int offset;

	rmScrHdle = GfuiScreenCreate();
	snprintf(buf, BUFSIZE, "Qualification Results");
	GfuiTitleCreate(rmScrHdle, buf, strlen(buf));
	snprintf(buf, BUFSIZE, "%s", info->track->name);
	GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_LARGE_C,
			320, 420, GFUI_ALIGN_HC_VB, 0);
	GfuiScreenAddBgImg(rmScrHdle, "data/img/splash-result.png");

	offset = 200;
	x1 = offset + 30;
	x2 = offset + 60;
	x3 = offset + 240;
	
	y = 400;
	GfuiLabelCreateEx(rmScrHdle, "Rank",      fgcolor, GFUI_FONT_MEDIUM_C, x1, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Driver",    fgcolor, GFUI_FONT_MEDIUM_C, x2+10, y, GFUI_ALIGN_HL_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Time",      fgcolor, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HR_VB, 0);
	y -= 20;
	
	snprintf(path, BUFSIZE, "%s/%s/%s/%s", info->track->name, RE_SECT_RESULTS, race, RE_SECT_RANK);
	nbCars = (int)GfParmGetEltNb(results, path);
	
	for (i = start; i < MIN(start + MAX_LINES, nbCars); i++) {
		snprintf(path, BUFSIZE, "%s/%s/%s/%s/%d", info->track->name, RE_SECT_RESULTS, race, RE_SECT_RANK, i + 1);

		snprintf(buf, BUFSIZE, "%d", i+1);
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x1, y, GFUI_ALIGN_HC_VB, 0);

		GfuiLabelCreate(rmScrHdle, GfParmGetStr(results, path, RE_ATTR_NAME, ""), GFUI_FONT_MEDIUM_C,
				x2, y, GFUI_ALIGN_HL_VB, 0);

		GfTime2Str(timefmt, TIMEFMTSIZE, GfParmGetNum(results, path, RE_ATTR_BEST_LAP_TIME, NULL, 0), 0);
		GfuiLabelCreate(rmScrHdle, timefmt, GFUI_FONT_MEDIUM_C,
				x3, y, GFUI_ALIGN_HR_VB, 0);
		y -= 15;
	}


	if (start > 0) {
		RmPrevRace.prevHdle = prevHdle;
		RmPrevRace.info     = info;
		RmPrevRace.start    = start - MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-up.png", "data/img/arrow-up.png",
				"data/img/arrow-up.png", "data/img/arrow-up-pushed.png",
				80, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmPrevRace, rmChgQualifScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_UP,   "Previous Results", (void*)&RmPrevRace, rmChgQualifScreen, NULL);
	}

	GfuiButtonCreate(rmScrHdle,
			"Continue",
			GFUI_FONT_LARGE,
			320,
			40,
			150,
			GFUI_ALIGN_HC_VB,
			0,
			prevHdle,
			GfuiScreenReplace,
			NULL,
			(tfuiCallback)NULL,
			(tfuiCallback)NULL);

	if (i < nbCars) {
		RmNextRace.prevHdle = prevHdle;
		RmNextRace.info     = info;
		RmNextRace.start    = start + MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-down.png", "data/img/arrow-down.png",
				"data/img/arrow-down.png", "data/img/arrow-down-pushed.png",
				540, 40, GFUI_ALIGN_HL_VB, 1,
				(void*)&RmNextRace, rmChgQualifScreen,
				NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_DOWN, "Next Results", (void*)&RmNextRace, rmChgQualifScreen, NULL);
	}

	GfuiAddKey(rmScrHdle, (unsigned char)27, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddKey(rmScrHdle, (unsigned char)13, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddSKey(rmScrHdle, GLUT_KEY_F12, "Take a Screen Shot", NULL, GfuiScreenShot, NULL);

	GfuiScreenActivate(rmScrHdle);
}


static void rmChgStandingScreen(void *vprc)
{
	void *prevScr = rmScrHdle;
	tRaceCall *prc = (tRaceCall*)vprc;

	rmShowStandings(prc->prevHdle, prc->info, prc->start);
	GfuiScreenRelease(prevScr);
}


static void rmShowStandings(void *prevHdle, tRmInfo *info, int start)
{
	int i;
	int x1, x2, x3;
	int y;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char path[BUFSIZE];
	float fgcolor[4] = {1.0, 0.0, 1.0, 1.0};
	int nbCars;
	int offset;
	void *results = info->results;
	const char *race = info->_reRaceName;
	
	rmScrHdle = GfuiScreenCreate();
	snprintf(buf, BUFSIZE, "%s Results", race);
	GfuiTitleCreate(rmScrHdle, buf, strlen(buf));
	
	GfuiScreenAddBgImg(rmScrHdle, "data/img/splash-result.png");
	
	offset = 200;
	x1 = offset + 30;
	x2 = offset + 60;
	x3 = offset + 240;
	
	y = 400;
	GfuiLabelCreateEx(rmScrHdle, "Rank",      fgcolor, GFUI_FONT_MEDIUM_C, x1, y, GFUI_ALIGN_HC_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Driver",    fgcolor, GFUI_FONT_MEDIUM_C, x2+10, y, GFUI_ALIGN_HL_VB, 0);
	GfuiLabelCreateEx(rmScrHdle, "Points",      fgcolor, GFUI_FONT_MEDIUM_C, x3, y, GFUI_ALIGN_HR_VB, 0);
	y -= 20;
	
	nbCars = (int)GfParmGetEltNb(results, RE_SECT_STANDINGS);
	for (i = start; i < MIN(start + MAX_LINES, nbCars); i++) {
		snprintf(path, BUFSIZE, "%s/%d", RE_SECT_STANDINGS, i + 1);
		
		snprintf(buf, BUFSIZE, "%d", i+1);
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x1, y, GFUI_ALIGN_HC_VB, 0);
		
		GfuiLabelCreate(rmScrHdle, GfParmGetStr(results, path, RE_ATTR_NAME, ""), GFUI_FONT_MEDIUM_C,
				x2, y, GFUI_ALIGN_HL_VB, 0);
		
		snprintf(buf, BUFSIZE, "%d", (int)GfParmGetNum(results, path, RE_ATTR_POINTS, NULL, 0));
		GfuiLabelCreate(rmScrHdle, buf, GFUI_FONT_MEDIUM_C,
				x3, y, GFUI_ALIGN_HR_VB, 0);
		y -= 15;
	}
	
	
	if (start > 0) {
		RmPrevRace.prevHdle = prevHdle;
		RmPrevRace.info     = info;
		RmPrevRace.start    = start - MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-up.png", "data/img/arrow-up.png",
					"data/img/arrow-up.png", "data/img/arrow-up-pushed.png",
					80, 40, GFUI_ALIGN_HL_VB, 1,
					(void*)&RmPrevRace, rmChgStandingScreen,
					NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_UP,   "Previous Results", (void*)&RmPrevRace, rmChgStandingScreen, NULL);
	}
	
	GfuiButtonCreate(rmScrHdle,
				"Continue",
				GFUI_FONT_LARGE,
				210,
				40,
				150,
				GFUI_ALIGN_HC_VB,
				0,
				prevHdle,
				GfuiScreenReplace,
				NULL,
				(tfuiCallback)NULL,
				(tfuiCallback)NULL);
	
	rmSaveId = GfuiButtonCreate(rmScrHdle,
				"Save",
				GFUI_FONT_LARGE,
				430,
				40,
				150,
				GFUI_ALIGN_HC_VB,
				0,
				info,
				rmSaveRes,
				NULL,
				(tfuiCallback)NULL,
				(tfuiCallback)NULL);
	
	if (i < nbCars) {
		RmNextRace.prevHdle = prevHdle;
		RmNextRace.info     = info;
		RmNextRace.start    = start + MAX_LINES;
		GfuiGrButtonCreate(rmScrHdle, "data/img/arrow-down.png", "data/img/arrow-down.png",
					"data/img/arrow-down.png", "data/img/arrow-down-pushed.png",
					540, 40, GFUI_ALIGN_HL_VB, 1,
					(void*)&RmNextRace, rmChgStandingScreen,
					NULL, (tfuiCallback)NULL, (tfuiCallback)NULL);
		GfuiAddSKey(rmScrHdle, GLUT_KEY_PAGE_DOWN, "Next Results", (void*)&RmNextRace, rmChgStandingScreen, NULL);
	}
	
	GfuiAddKey(rmScrHdle, (unsigned char)27, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddKey(rmScrHdle, (unsigned char)13, "", prevHdle, GfuiScreenReplace, NULL);
	GfuiAddSKey(rmScrHdle, GLUT_KEY_F12, "Take a Screen Shot", NULL, GfuiScreenShot, NULL);
	
	GfuiScreenActivate(rmScrHdle);
}


/** @brief Display results
 *  @ingroup racemantools
 *  @param[in] prevHdle Handle to previous result screen (used if the results require more than one screen)
 *  @param[in] info tRmInfo.results carries the result parameter set handle
 */
void RmShowResults(void *prevHdle, tRmInfo *info)
{
	switch (info->s->_raceType) {
		case RM_TYPE_PRACTICE:
			rmPracticeResults(prevHdle, info, 0);
			return;

		case RM_TYPE_RACE:
			rmRaceResults(prevHdle, info, 0);
			return;

		case RM_TYPE_QUALIF:
			rmQualifResults(prevHdle, info, 0);
			return;
	}
}


/** @brief Display standings
 *  @ingroup racemantools
 *  @param[in] prevHdle Handle to previous standings screen (used if the standings require more than one screen)
 *  @param[in] info tRmInfo.results carries the result parameter set handle containing the standings
 */
void RmShowStandings(void *prevHdle, tRmInfo *info)
{
    rmShowStandings(prevHdle, info, 0);
}
