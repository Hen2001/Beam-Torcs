/***************************************************************************

    file                 : human.cpp
    created              : Sat Mar 18 23:16:38 CET 2000
    copyright            : (C) 2000-2013 by Eric Espie, Bernhard Wymann
    email                : torcs@free.fr
    version              : $Id: human.cpp,v 1.45.2.18 2014/05/22 11:51:24 berniw Exp $

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
	Human driver
	@author	Bernhard Wymann, Eric Espie
	@version	$Id: human.cpp,v 1.45.2.18 2014/05/22 11:51:24 berniw Exp $
*/


#ifdef _WIN32
#include <windows.h>
#define isnan _isnan
#endif

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <plib/js.h>

#include <tgfclient.h>
#include <portability.h>

#include <track.h>
#include <car.h>
#include <raceman.h>
#include <robottools.h>
#include <robot.h>

#include <playerpref.h>
#include "pref.h"
#include "human.h"
#include <map>



#include "AiFeatures.h"

#define DRWD 0
#define DFWD 1
#define D4WD 2

static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation *s);
static void drive_mt(int index, tCarElt* car, tSituation *s);
static void drive_at(int index, tCarElt* car, tSituation *s);
static void newrace(int index, tCarElt* car, tSituation *s);
static void endrace(int index, tCarElt* car, tSituation *s);  // Added end race function to log STATS
static int  pitcmd(int index, tCarElt* car, tSituation *s);

int joyPresent = 0;
//bool responseBot = 0;
 tTrack	*curTrack;

static float color[] = {0.0, 0.0, 1.0, 1.0};

static int prevRemainingLaps = -1;

static tCtrlJoyInfo	*joyInfo = NULL;
static tCtrlMouseInfo	*mouseInfo = NULL;
static int		masterPlayer = -1;

tHumanContext *HCtx[10] = {0};

static int speedLimiter	= 0;
static tdble Vtarget;

static double lapTimes[100] = {0};  // stores each lap time
static int    lapCount = 0;

static double totalSpeed   = 0.0; // Added for logging function
static int    speedSamples = 0;

typedef struct
{
	int state;
	int edgeDn;
	int edgeUp;
} tKeyInfo;

static tKeyInfo keyInfo[256];
static tKeyInfo skeyInfo[256];

static int currentKey[256];
static int currentSKey[256];

static double lastKeyUpdate = -10.0;

static int	firstTime = 0;

static bool statsWritten = false;

static bool hasLapStarted = false;

#ifdef _WIN32
/* should be present in mswindows */
BOOL WINAPI DllEntryPoint (HINSTANCE hDLL, DWORD dwReason, LPVOID Reserved)
{
    return TRUE;
}
#endif



#include <fstream> // Required for file writing
#include <iomanip>
#include <sstream>
#include <string>   
#include <iostream>

#include <sys/stat.h> // For mkdir
#include <libgen.h> // For dirname
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <curl/curl.h>

static std::ofstream speedOut;
static std::ofstream trackOut;

static void printPerformanceReport()
{
    // ANSI colours
    const char* GREEN  = "\033[92m";
    const char* RED    = "\033[91m";
    const char* YELLOW = "\033[93m";
    const char* CYAN   = "\033[96m";
    const char* BOLD   = "\033[1m";
    const char* RESET  = "\033[0m";

    // helper to determine color based on comparison to BEST
    auto rating = [&](double actual, double best) -> const char* {
        if (best <= 0) return YELLOW;
        if (actual <= best) return GREEN;
        if (actual <= best * 1.05) return YELLOW; // Within 5% of best is "OK"
        return RED;
    };

    // ── Read segment_times.json ───────────────────────────────────────────────
    const char* homeDir = getenv("HOME");
    if (!homeDir) return;
    std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";

    std::map<int, std::map<int, double>> segTimes;
    std::map<int, std::map<int, int>>    segCounts;
    std::map<int, double> segmentBests; // Global best per segment for this session
    double overallBestLapTime = 1e9;
    int maxSegmentIdx = 0;

    std::ifstream segFile((dataDir + "/segment_times.json").c_str());
    if (segFile.is_open()) {
        std::string line;
        while (std::getline(segFile, line)) {
            if (!line.empty() && line.back() == ',') line.pop_back();
            if (line.empty()) continue;

            int lap = -1, seg = -1;
            double t = 0.0;

            size_t pos = line.find("\"lap\":");
            if (pos != std::string::npos) lap = std::stoi(line.substr(pos + 6));
            pos = line.find("\"segment\":");
            if (pos != std::string::npos) seg = std::stoi(line.substr(pos + 10));
            pos = line.find("\"time\":");
            if (pos != std::string::npos) t = std::stod(line.substr(pos + 7));

            if (lap >= 0 && seg >= 0) {
                segTimes[lap][seg] += t;
                segCounts[lap][seg] += 1;
                if (seg > maxSegmentIdx) maxSegmentIdx = seg;
            }
        }
        segFile.close();
    }

    if (segTimes.empty()) {
        printf("No performance data found.\n");
        return;
    }

    // First pass: Calculate Best Times for each segment across all laps
    for (auto const& [lap, segments] : segTimes) {
        double lapTotal = 0.0;
        for (auto const& [seg, time] : segments) {
            double avg = time / segCounts[lap][seg];
            lapTotal += avg;
            if (segmentBests.find(seg) == segmentBests.end() || avg < segmentBests[seg]) {
                segmentBests[seg] = avg;
            }
        }
        if (lapTotal < overallBestLapTime) overallBestLapTime = lapTotal;
    }

    // ── Print report ──────────────────────────────────────────────────────────
    const char* trackName = curTrack ? curTrack->internalname : "Unknown";
    printf("\n%s%s╔══════════════════════════════════════════════════╗%s\n", BOLD, CYAN, RESET);
    printf("%s%s║ %-48s ║%s\n", BOLD, CYAN, trackName, RESET);
    printf("%s%s╚══════════════════════════════════════════════════╝%s\n", BOLD, CYAN, RESET);

    for (auto& lapEntry : segTimes) {
        int lap = lapEntry.first;
        printf("\n%s%s── Lap %d Segment Analysis ──%s\n", BOLD, CYAN, lap, RESET);
        printf("  %-6s %8s %8s %8s %8s\n", "Seg", "Time(s)", "SessBest", "Diff", "Rating");
        printf("  %s\n", "-------------------------------------------------------");

        double lapTotal = 0.0;

        // Iterate up to the highest segment found for this track
        for (int seg = 0; seg <= maxSegmentIdx; seg++) {
            if (segCounts[lap].count(seg) == 0) continue;

            double avg = segTimes[lap][seg] / segCounts[lap][seg];
            double best = segmentBests[seg];
            lapTotal += avg;
            double diff = avg - best;
            const char* col = rating(avg, best);

            printf("  %-6d %8.2f %8.2f %s%+8.2f%s  [%s%s%s]\n",
                   seg + 1, avg, best, 
                   (diff > 0 ? RED : GREEN), diff, RESET,
                   col, (avg <= best ? "BEST" : (avg <= best * 1.05 ? " OK " : "SLOW")), RESET);
        }

        printf("  %s\n", "-------------------------------------------------------");
        printf("  %-15s %8.2f (Best Session Lap: %.2f)\n", "Lap Total:", lapTotal, overallBestLapTime);
    }

    printf("\n%s%s══════════════════════════════════════════════════%s\n", BOLD, CYAN, RESET);
    printf("  Legend: %sBEST%s (New/Matches Session Best)  %sOK%s (+5%%)  %sSLOW%s (>5%%)\n",
           GREEN, RESET, YELLOW, RESET, RED, RESET);
    printf("%s%s══════════════════════════════════════════════════%s\n\n", BOLD, CYAN, RESET);
}

static void shutdown(int index)
{
	// 1. Close the speed log
if (speedOut.is_open()) {
    if (speedOut.tellp() > 3) { 
        speedOut.seekp(-2, std::ios_base::end); 
        speedOut << "\n]" << std::endl;
    } else {
        speedOut << "]" << std::endl;
    }
    speedOut.close();
}

    // Close track log 
    if (trackOut.is_open()) {
        trackOut.seekp(-2, std::ios_base::cur); 
        trackOut << "\n]" << std::endl;           
        trackOut.close();
    }
    if (coach) {
        system("pkill -9 -f liveCoach.py");
    }
    if (commentary) {
        system("pkill -9 -f liveComs.py");
    }
	if (engineer){
		system("pkill -9 -f race_engineer.py");
	}
	printPerformanceReport();
  
	
	int	idx = index - 1;

	free (HCtx[idx]);

	if (firstTime) {
		GfParmReleaseHandle(PrefHdle);
		GfctrlJoyRelease(joyInfo);
		GfctrlMouseRelease(mouseInfo);
		GfuiKeyEventRegisterCurrent(NULL);
		GfuiSKeyEventRegisterCurrent(NULL);
		firstTime = 0;
	}
}

void logTrackPosition(tCarElt* car, tSituation *s) {
    static double lastPosWriteTime = 0;

    if (s->currentTime - lastPosWriteTime < 0.2) {
        return;
    }
    lastPosWriteTime = s->currentTime;
    
    if (!trackOut.is_open()) {
        const char* homeDir = getenv("HOME");
        if (!homeDir) return;
        std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
        mkdir(dataDir.c_str(), 0755);
        std::string fullPath = dataDir + "/track_pos.json";
        trackOut.open(fullPath.c_str(), std::ios_base::app); 
        trackOut << "[" << std::endl; // Start the array if new file
    }
    
    if (trackOut.is_open()) {
        trackOut << "{"
                  << "\"time\":" << s->currentTime << ","
                  << "\"pos_x\":" << car->_pos_X << ","
                  << "\"pos_y\":" << car->_pos_Y << ","
                  << "\"track_pos\":" << car->_trkPos.toMiddle << "," 
                  << "\"segment_id\":" << car->_trkPos.seg->id << "," 
                  << "\"to_start\":" << car->_trkPos.toStart << ","   
                  << "\"lap\":" << car->_laps                        
                  << "}," << std::endl;
    }
}

void logSpeed(tCarElt* car, tSituation *s) {
    static double lastPosWriteTime = 0;

    if (s->currentTime - lastPosWriteTime < 0.5) {
        return;
    }
    lastPosWriteTime = s->currentTime;

    totalSpeed += fabs(car->_speed_x);
    speedSamples++;

    if (!speedOut.is_open()) {
        const char* homeDir = getenv("HOME");
        if (!homeDir) return;
        std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
        mkdir(dataDir.c_str(), 0755);
        std::string fullPath = dataDir + "/speed.json";
        speedOut.open(fullPath.c_str(), std::ios_base::app); 
        speedOut << "[" << std::endl; // Start the array if new file
    }

    if (speedOut.is_open()) {
        speedOut << "{"
                << "\"time\":" << s->currentTime << ","
                << "\"segment_id\":" << car->_trkPos.seg->id << ","  
                << "\"speedX\":" << car->_speed_X << ","
                << "\"speedy\":" << car->_speed_Y << ","
                << "\"speedx\":" << car->_speed_x << ","
                << "\"Kmph\":" << (car->_speed_x * 3.6) 
                << "}," << std::endl;
    }
}

// ── Per-track macro segment functions ────────────────────────────────────────

int getMacroSegment_Corkscrew(int segId) {
    if (segId < 40)  return 0;
    if (segId < 100) return 1;
    if (segId < 175) return 2;
    if (segId < 235) return 3;
    if (segId < 310) return 4;
    if (segId < 390) return 5;
    if (segId < 500) return 6;
    if (segId < 540) return 7;
    if (segId < 604) return 8;
    return 9;
}

int getMacroSegment_CGSpeedway(int segId) {
    if (segId < 81)  return 0;
    if (segId < 191) return 1;
    return 2;
}

int getMacroSegment_CGTrack2(int segId) {
    if (segId < 171) return 0;
    if (segId < 306) return 1;
    return 2;
}

int getMacroSegment_CGTrack3(int segId) {
    if (segId < 66)  return 0;
    if (segId < 131) return 1;
    if (segId < 251) return 2;
    return 3;
}

int getMacroSegment_OlethrosRoad(int segId) {
    if (segId < 131) return 0;
    if (segId < 301) return 1;
    if (segId < 486) return 2;
    if (segId < 606) return 3;
    return 4;
}

int getMacroSegment_Ruudskogen(int segId) {
    if (segId < 201) return 0;
    if (segId < 421) return 1;
    if (segId < 571) return 2;
    return 3;
}

int getMacroSegment_Spring(int segId) {
    if (segId < 141)  return 0;
    if (segId < 251)  return 1;
    if (segId < 346)  return 2;
    if (segId < 451)  return 3;
    if (segId < 541)  return 4;
    if (segId < 676)  return 5;
    if (segId < 751)  return 6;
    if (segId < 831)  return 7;
    if (segId < 916)  return 8;
    if (segId < 1016) return 9;
    if (segId < 1151) return 10;
    if (segId < 1271) return 11;
    if (segId < 1431) return 12;
    if (segId < 1581) return 13;
    if (segId < 1921) return 14;
    if (segId < 2141) return 15;
    if (segId < 2471) return 16;
    if (segId < 2741) return 17;
    return 18;
}

int getMacroSegment_ETrack1(int segId) {
    if (segId < 2)   return 0;
    if (segId < 51)  return 1;
    if (segId < 101) return 2;
    if (segId < 141) return 3;
    if (segId < 231) return 4;
    if (segId < 261) return 5;
    if (segId < 371) return 6;
    if (segId < 401) return 7;
    return 8;
}

int getMacroSegment_ETrack2(int segId) {
    if (segId < 101)  return 0;
    if (segId < 251)  return 1;
    if (segId < 501)  return 2;
    if (segId < 681)  return 3;
    if (segId < 801)  return 4;
    if (segId < 1001) return 5;
    if (segId < 1081) return 6;
    if (segId < 1201) return 7;
    if (segId < 1301) return 8;
    return 9;
}

int getMacroSegment_ETrack3(int segId) {
    if (segId < 61)  return 0;
    if (segId < 101) return 1;
    if (segId < 201) return 2;
    if (segId < 281) return 3;
    if (segId < 401) return 4;
    if (segId < 601) return 5;
    if (segId < 701) return 6;
    if (segId < 731) return 7;
    return 8;
}

int getMacroSegment_ETrack4(int segId) {
    if (segId < 41)  return 0;
    if (segId < 151) return 1;
    if (segId < 451) return 2;
    if (segId < 501) return 3;
    if (segId < 601) return 4;
    if (segId < 701) return 5;
    return 6;
}

int getMacroSegment_ETrack6(int segId) {
    if (segId < 26)  return 0;
    if (segId < 76)  return 1;
    if (segId < 101) return 2;
    if (segId < 151) return 3;
    if (segId < 231) return 4;
    if (segId < 291) return 5;
    if (segId < 340) return 6;
    if (segId < 381) return 7;
    if (segId < 440) return 8;
    return 9;
}

int getMacroSegment_ERoad(int segId) {
    if (segId < 41)  return 0;
    if (segId < 71)  return 1;
    if (segId < 131) return 2;
    if (segId < 181) return 3;
    if (segId < 311) return 4;
    if (segId < 341) return 5;
    if (segId < 391) return 6;
    return 7;
}

int getMacroSegment_Forza(int segId) {
    if (segId < 291)  return 0;
    if (segId < 401)  return 1;
    if (segId < 601)  return 2;
    if (segId < 651)  return 3;
    if (segId < 701)  return 4;
    if (segId < 741)  return 5;
    if (segId < 951)  return 6;
    if (segId < 1051) return 7;
    if (segId < 1251) return 8;
    if (segId < 1401) return 9;
    return 10;
}

int getMacroSegment_Street1(int segId) {
    if (segId < 91)  return 0;
    if (segId < 178) return 1;
    if (segId < 250) return 2;
    return 3;
}

int getMacroSegment_Wheel1(int segId) {
    if (segId < 127) return 0;
    if (segId < 388) return 1;
    return 2;
}

int getMacroSegment_Wheel2(int segId) {
    if (segId < 116) return 0;
    if (segId < 443) return 1;
    if (segId < 522) return 2;
    if (segId < 589) return 3;
    if (segId < 644) return 4;
    return 5;
}

int getMacroSegment_Aalborg(int segId) {
    if (segId < 125) return 0;
    if (segId < 175) return 1;
    return 2;
}

int getMacroSegment_Alpine1(int segId) {
    if (segId < 140) return 0;
    if (segId < 501) return 1;
    if (segId < 691) return 2;
    if (segId < 927) return 3;
    return 4;
}

int getMacroSegment_Alpine2(int segId) {
    if (segId < 206) return 0;
    if (segId < 374) return 1;
    if (segId < 601) return 2;
    return 3;
}

int getMacroSegment_Brondelehach(int segId) {
    if (segId < 141) return 0;
    if (segId < 301) return 1;
    if (segId < 521) return 2;
    if (segId < 701) return 3;
    return 4;
}

// ── Dispatcher — routes to the correct track function via curTrack->internalname
// NOTE: If any track returns 0 unexpectedly, add a debug print in initTrack to
//       confirm the exact internalname string: printf("[DEBUG] Track: %s\n", track->internalname);
int getMacroSegment(int segId) {
    if (!curTrack) return 0;
    const char* name = curTrack->internalname;

    if      (strcmp(name, "corkscrew")        == 0) return getMacroSegment_Corkscrew(segId);
    else if (strcmp(name, "g-speedway")        == 0) return getMacroSegment_CGSpeedway(segId);
    else if (strcmp(name, "g-track-2")         == 0) return getMacroSegment_CGTrack2(segId);
    else if (strcmp(name, "g-track-3")         == 0) return getMacroSegment_CGTrack3(segId);
    else if (strcmp(name, "ole-road-1")   == 0) return getMacroSegment_OlethrosRoad(segId);
    else if (strcmp(name, "ruudskogen")        == 0) return getMacroSegment_Ruudskogen(segId);
    else if (strcmp(name, "spring")            == 0) return getMacroSegment_Spring(segId);
    else if (strcmp(name, "e-track-1")          == 0) return getMacroSegment_ETrack1(segId);
    else if (strcmp(name, "e-track-2")          == 0) return getMacroSegment_ETrack2(segId);
    else if (strcmp(name, "e-track-3")          == 0) return getMacroSegment_ETrack3(segId);
    else if (strcmp(name, "e-track-4")          == 0) return getMacroSegment_ETrack4(segId);
    else if (strcmp(name, "e-track-6")          == 0) return getMacroSegment_ETrack6(segId);
    else if (strcmp(name, "e-road")            == 0) return getMacroSegment_ERoad(segId);
    else if (strcmp(name, "forza")             == 0) return getMacroSegment_Forza(segId);
    else if (strcmp(name, "street-1")          == 0) return getMacroSegment_Street1(segId);
    else if (strcmp(name, "wheel-1")           == 0) return getMacroSegment_Wheel1(segId);
    else if (strcmp(name, "wheel-2")           == 0) return getMacroSegment_Wheel2(segId);
    else if (strcmp(name, "aalborg")           == 0) return getMacroSegment_Aalborg(segId);
    else if (strcmp(name, "alpine-1")          == 0) return getMacroSegment_Alpine1(segId);
    else if (strcmp(name, "alpine-2")          == 0) return getMacroSegment_Alpine2(segId);
    else if (strcmp(name, "brondehach")        == 0) return getMacroSegment_Brondelehach(segId);

    // Unknown track — fallback: every segment ID maps to macro segment 0
    printf("[getMacroSegment] WARNING: unknown track '%s', returning 0\n", name);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────

static int lastMacroSegment = -1;
static double segmentStartTime = 0;
static int lastLap = -1;


void writeSegmentTimeToJson(int segment, int lap, double time) {
    const char* homeDir = getenv("HOME");
    if (!homeDir) return;

    std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
    mkdir(dataDir.c_str(), 0755);

    std::string fullPath = dataDir + "/segment_times.json";

    std::ofstream outFile(fullPath.c_str(), std::ios_base::app);

    if (outFile.is_open()) {
        outFile << "{"
                << "\"lap\":" << lap << ","
                << "\"segment\":" << segment << ","
                << "\"time\":" << time
                << "}," << std::endl;
        outFile.close();
    }
}

// At file scope, near the other static segment tracking variables
static double inMemSegTimes[100][9] = {0}; // [lap][macroSeg]

void logSegmentPosition(tCarElt *car, tSituation *s)
{
    int segId    = car->_trkPos.seg->id;
    int macroSeg = getMacroSegment(segId);
    int lap      = car->_laps;

    // ── Lap change detected ──────────────────────────────────────────────
    if (lap != lastLap && lastLap != -1)
    {
        // Close off the final segment of the completed lap
        if (lastMacroSegment != -1)
        {
            double timeSpent = s->currentTime - segmentStartTime;
            inMemSegTimes[lastLap][lastMacroSegment] += timeSpent;
            writeSegmentTimeToJson(lastMacroSegment, lastLap, timeSpent);
        }

        // Sum all 9 macro segments → computed lap time
        double computedLapTime = 0.0;
        for (int i = 0; i < 9; i++)
            computedLapTime += inMemSegTimes[lastLap][i];

        if (computedLapTime > 0.0 && lapCount < 100)
        {
            lapTimes[lapCount++] = computedLapTime;
            printf("[LapTime] Lap %d computed from segments: %.3f s\n",
                   lastLap, computedLapTime);
        }

        // Reset for new lap
        lastLap          = lap;
        lastMacroSegment = -1;
        segmentStartTime = s->currentTime;
        return;
    }

    if (lap != lastLap)
        lastLap = lap;

    // ── Macro segment transition ─────────────────────────────────────────
    if (macroSeg != lastMacroSegment)
    {
        if (lastMacroSegment != -1)
        {
            double timeSpent = s->currentTime - segmentStartTime;
            inMemSegTimes[lap][lastMacroSegment] += timeSpent;
            writeSegmentTimeToJson(lastMacroSegment, lap, timeSpent);
        }

        segmentStartTime = s->currentTime;
        lastMacroSegment = macroSeg;
    }
}

  
static void endStatistics(tCarElt* car, tSituation *s)
{
    // Throttle: Only overwrite every 0.1 seconds to save CPU/Disk I/O
    static double lastWriteTime = 0;
    if (s->currentTime - lastWriteTime < 0.1) return; 
    lastWriteTime = s->currentTime;

    const char* homeDir = getenv("HOME");
    if (!homeDir) return;

    std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
    mkdir(dataDir.c_str(), 0755);
    std::string fullPath = dataDir + "/end_statistics.json";

    // Compute avg and best from segment-derived lapTimes[]
    double avgSpeed = (speedSamples > 0) ? (totalSpeed / speedSamples) : 0.0;
    double avgLapTime = 0.0;
    double bestLapTime = 0.0;

    if (lapCount > 0) {
        bestLapTime = lapTimes[0];
        for (int i = 0; i < lapCount; i++) {
            avgLapTime += lapTimes[i];
            if (lapTimes[i] < bestLapTime)
                bestLapTime = lapTimes[i];
        }
        avgLapTime /= lapCount;
    }

    // std::ios::trunc ensures the file is wiped and rewritten from scratch
    std::ofstream outFile(fullPath.c_str(), std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        return;
    }

    outFile << "{"
        << "\"avg_speed_kmh\":"  << (avgSpeed * 3.6)  << ","
        << "\"avg_lap_time\":"   << avgLapTime         << ","
        << "\"best_lap_time\":"  << bestLapTime        << ","
        << "\"laps_completed\":" << (lapCount > 0 ? lapCount : car->_laps) << ","
        << "\"current_lap\":"    << car->_laps         << ","
        << "\"finish_pos\":"     << car->_pos          << ","
        << "\"damage\":"         << car->_dammage      << ","
        << "\"time_penalty\":"   << car->_penaltyTime  << ","
        << "\"lap_times\":[";

    for (int i = 0; i < lapCount; i++) {
        outFile << "{\"lap\":" << (i + 1) << ",\"time\":" << lapTimes[i] << "}";
        if (i < lapCount - 1) outFile << ",";
    }

    outFile << "]}" << std::endl;
    outFile.close();
}

void logEngineerData(tCarElt* car, tSituation *s)
{

	// ============== Telemetry Calculations ==============

	// Tyre temperatures - average inner/mid/outer per wheel
    float tyreTempFL = (car->priv.wheel[FRNT_LFT].temp_in + 
                        car->priv.wheel[FRNT_LFT].temp_mid + 
                        car->priv.wheel[FRNT_LFT].temp_out) / 3.0f;
    float tyreTempFR = (car->priv.wheel[FRNT_RGT].temp_in + 
                        car->priv.wheel[FRNT_RGT].temp_mid + 
                        car->priv.wheel[FRNT_RGT].temp_out) / 3.0f;
    float tyreTempRL = (car->priv.wheel[REAR_LFT].temp_in + 
                        car->priv.wheel[REAR_LFT].temp_mid + 
                        car->priv.wheel[REAR_LFT].temp_out) / 3.0f;
    float tyreTempRR = (car->priv.wheel[REAR_RGT].temp_in + 
                        car->priv.wheel[REAR_RGT].temp_mid + 
                        car->priv.wheel[REAR_RGT].temp_out) / 3.0f;

	// Tyre condition (1.0 = new, 0.0 = destroyed)
    float condFL = car->priv.wheel[FRNT_LFT].condition;
    float condFR = car->priv.wheel[FRNT_RGT].condition;
    float condRL = car->priv.wheel[REAR_LFT].condition;
    float condRR = car->priv.wheel[REAR_RGT].condition;

    // Brake temps (0.0 = cool, 1.0 = hot)
    float brakeTempFL = car->priv.wheel[FRNT_LFT].brakeTemp;
    float brakeTempFR = car->priv.wheel[FRNT_RGT].brakeTemp;
    float brakeTempRL = car->priv.wheel[REAR_LFT].brakeTemp;
    float brakeTempRR = car->priv.wheel[REAR_RGT].brakeTemp;

	// Averages for easier analysis
	float avgTypeCondition = (condFL + condFR + condRL + condRR) / 4.0f;
	float avgTyreTemp = (tyreTempFL + tyreTempFR + tyreTempRL + tyreTempRR) / 4.0f;
	float avgBrakeTemp = (brakeTempFL + brakeTempFR + brakeTempRL + brakeTempRR) / 4.0f;

	// speed to kph & avg speed calculation
	float speedKph = car->pub.DynGCg.vel.x * 3.6f;
    if (speedKph < 0) speedKph = -speedKph;
	double avgSpeed = (speedSamples > 0) ? (totalSpeed / speedSamples) : 0.0;


	// ================ Writing to json ================

	static double lastWriteTime = 0;
	if (s->currentTime - lastWriteTime < 2.0) return; // only write every 2 secs
	lastWriteTime = s->currentTime;

	std::string path = std::string(getenv("HOME")) + 
                    "/.torcs/DrivingData/Race_Engineer_Data.json";

	std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
	if (!f.is_open()) {
		printf("ERROR: Could not open Race_Engineer_Data.json for writing\n");
		return;
	}

	f << std::fixed << std::setprecision(3);
    f << "{\n";
    f << "  \"speed_kmh\": "       << speedKph                    << ",\n";
	f << "  \"avg_speed_kmh\": "   << (avgSpeed * 3.6)            << ",\n";
	f << "  \"dist_raced\": "      << car->race.distRaced          << ",\n";
    f << "  \"lap\": "             << car->race.laps               << ",\n";
    f << "  \"lap_time\": "        << car->race.curLapTime         << ",\n";
    f << "  \"best_lap_time\": "   << car->race.bestLapTime        << ",\n";
	f << "  \"fuel\": "            << car->priv.fuel               << ",\n";
    f << "  \"avg_tyre_temp\": " << avgTyreTemp 				   << ",\n";
	f << "  \"avg_tyre_condition\": " << avgTypeCondition 		   << ",\n";
    f << "  \"avg_brake_temp\": " << avgBrakeTemp 				   << ",\n";
	f << "  \"damage\": "          << car->_dammage            << "\n";
    f << "}\n";
    f.close();
}

void logLiveCommentary(tCarElt* car, tSituation *s) {
    static double lastLiveWrite = 0;
    // Log every 2 seconds to give the AI time to think
    if (s->currentTime - lastLiveWrite < 2.0) return;
    lastLiveWrite = s->currentTime;

    std::string path = std::string(getenv("HOME")) + "/.torcs/DrivingData/live_data.json";
    std::ofstream liveFile(path.c_str(), std::ios::out | std::ios::trunc);

    if (liveFile.is_open()) {
        liveFile << "{"
                 << "\"speed\":" << (car->_speed_x * 3.6) << ","
                 << "\"gear\":" << car->_gear << ","
                 << "\"distToStart\":" << car->_trkPos.toStart << ","
                 << "\"damage\":" << car->_dammage << ","
                 << "\"trackPos\":" << car->_trkPos.toMiddle << ","
				 << "\"place\":" << car->_pos << ","
				 << "\"Segment\":" << car->_trkPos.seg->id  << ""
                 << "}";
        liveFile.close();
    }
}
void logLiveCoaching(tCarElt* car, tSituation *s) {
    static double lastLiveWrite = 0;
    if (s->currentTime - lastLiveWrite < 2.0) return;
    lastLiveWrite = s->currentTime;

    int macro = getMacroSegment(car->_trkPos.seg->id);
    int lap   = car->_laps;

    // Read segment_times.json that's already being written by logSegmentPosition
    std::string segFile = std::string(getenv("HOME")) + "/.torcs/DrivingData/segment_times.json";
    std::map<int, std::map<int, double>> segTimes; // [lap][seg] = time

    std::ifstream f(segFile.c_str());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line.back() == ',') line.pop_back();
        int l = -1, seg = -1; double t = 0.0;
        size_t pos;
        if ((pos = line.find("\"lap\":"))     != std::string::npos) l   = std::stoi(line.substr(pos + 6));
        if ((pos = line.find("\"segment\":")) != std::string::npos) seg = std::stoi(line.substr(pos + 10));
        if ((pos = line.find("\"time\":"))    != std::string::npos) t   = std::stod(line.substr(pos + 7));
        if (l >= 0 && seg >= 0) segTimes[l][seg] = t;
    }

    // Get prev lap times (lap - 1)
    double prevTime = 0.0, curSegTime = 0.0;
    if (segTimes.count(lap - 1) && segTimes[lap - 1].count(macro))
        prevTime = segTimes[lap - 1][macro];
    if (segTimes.count(lap) && segTimes[lap].count(macro))
        curSegTime = segTimes[lap][macro];

    double delta = (prevTime > 0.0 && curSegTime > 0.0) ? (curSegTime - prevTime) : 0.0;

    std::string path = std::string(getenv("HOME")) + "/.torcs/DrivingData/live_coaching_data.json";
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (out.is_open()) {
        out << "{"
            << "\"speed\":"       << (car->_speed_x * 3.6) << ","
            << "\"gear\":"        << car->_gear             << ","
            << "\"damage\":"      << car->_dammage          << ","
            << "\"trackPos\":"    << car->_trkPos.toMiddle  << ","
            << "\"segment\":"     << macro                  << ","
            << "\"lap\":"         << lap                    << ","
            << "\"prevSegTime\":" << prevTime               << ","
            << "\"curSegTime\":"  << curSegTime             << ","
            << "\"delta\":"       << delta                  << ","
            << "\"hasPrevLap\":"  << (prevTime > 0.0 ? "true" : "false")
            << "}";
        out.close();
    }
}
/*
 * Function
 *	InitFuncPt
 *
 * Description
 *	Robot functions initialisation
 *
 * Parameters
 *	pt	pointer on functions structure
 *
 * Return
 *	0
 *
 * Remarks
 *
 */
static int
InitFuncPt(int index, void *pt)
{
	tRobotItf *itf = (tRobotItf *)pt;
	int idx = index - 1;

	if (masterPlayer == -1) {
		masterPlayer = index;
	}

	if (firstTime < 1) {
		firstTime = 1;
		joyInfo = GfctrlJoyInit();
		if (joyInfo) {
			joyPresent = 1;
		}

		mouseInfo = GfctrlMouseInit();
	}


	/* Allocate a new context for that player */
	HCtx[idx] = (tHumanContext *) calloc (1, sizeof (tHumanContext));

	HCtx[idx]->ABS = 1.0;
	HCtx[idx]->AntiSlip = 1.0;

	itf->rbNewTrack = initTrack;	/* give the robot the track view called */
	/* for every track change or new race */
	itf->rbNewRace  = newrace;
	itf->rbEndRace  = endrace; 

	HmReadPrefs(index);

	if (HCtx[idx]->Transmission == 0) {
		itf->rbDrive    = drive_at;
	} else {
		itf->rbDrive    = drive_mt;		/* drive during race */
	}
	itf->rbShutdown = shutdown;
	itf->rbPitCmd   = pitcmd;
	itf->index      = index;

	return 0;
}

/*
 * Function
 *	human
 *
 * Description
 *	DLL entry point (general to all types of modules)
 *
 * Parameters
 *	modInfo	administrative info on module
 *
 * Return
 *	0
 *
 * Remarks
 *
 */


extern "C" int
human(tModInfo *modInfo)
{
	int i;
	const char *driver;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char sstring[BUFSIZE];

	memset(modInfo, 0, 10*sizeof(tModInfo));

	snprintf(buf, BUFSIZE, "%sdrivers/human/human.xml", GetLocalDir());
	void *DrvInfo = GfParmReadFile(buf, GFPARM_RMODE_REREAD | GFPARM_RMODE_CREAT);

	if (DrvInfo != NULL) {
		for (i = 0; i < 10; i++) {
			snprintf(sstring, BUFSIZE, "Robots/index/%d", i+1);
			driver = GfParmGetStr(DrvInfo, sstring, "name", "");
			if (strlen(driver) == 0) {
				break;
			}

			modInfo->name    = strdup(driver);	/* name of the module (short) */
			modInfo->desc    = strdup("Joystick controlable driver");	/* description of the module (can be long) */
			modInfo->fctInit = InitFuncPt;	/* init function */
			modInfo->gfId    = ROB_IDENT;	/* supported framework version */
			modInfo->index   = i+1;
			modInfo++;
		}
		// Just release in case we got it.
		GfParmReleaseHandle(DrvInfo);
	}

	return 0;
}


/*
 * Function
 *
 *
 * Description
 *	search under drivers/human/tracks/<trackname>/car-<model>-<index>.xml
 *		     drivers/human/car-<model>-<index>.xml
 *		     drivers/human/tracks/<trackname>/car-<model>.xml
 *		     drivers/human/car-<model>.xml
 *
 * Parameters
 *
 *
 * Return
 *
 *
 * Remarks
 *
 */
static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation *s)
{
	const char *carname;
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	char sstring[BUFSIZE];
	tdble fuel;
	int idx = index - 1;

	curTrack = track;

	printf("[DEBUG] Track internal name: %s\n", track->internalname); // Remove once all names confirmed

	snprintf(sstring, BUFSIZE, "Robots/index/%d", index);
	snprintf(buf, BUFSIZE, "%sdrivers/human/human.xml", GetLocalDir());
	void *DrvInfo = GfParmReadFile(buf, GFPARM_RMODE_REREAD | GFPARM_RMODE_CREAT);
	carname = "";
	if (DrvInfo != NULL) {
		carname = GfParmGetStr(DrvInfo, sstring, "car name", "");
	}

	*carParmHandle = NULL;
	// If session type is "race" and we have a race setup use it
	if (s->_raceType == RM_TYPE_RACE) {
		*carParmHandle = RtParmReadSetup(RACE, "human", index, track->internalname, carname);
	}

	// If session type is "qualifying" and we have a qualifying setup use it, use qualifying setup as 
	// fallback if not race setup is available
	if (s->_raceType == RM_TYPE_QUALIF || (*carParmHandle == NULL && s->_raceType == RM_TYPE_RACE)) {
		*carParmHandle = RtParmReadSetup(QUALIFYING, "human", index, track->internalname, carname);
	}

	// If we have not yet loaded a setup we have not found a fitting one or want to use the practice setup,
	// so try to load this
	if (*carParmHandle == NULL) {
		*carParmHandle = RtParmReadSetup(PRACTICE, "human", index, track->internalname, carname);
	}

	// Absolute fallback, nothing found
	if (*carParmHandle == NULL) {
		snprintf(sstring, BUFSIZE, "%sdrivers/human/car.xml", GetLocalDir ());
		*carParmHandle = GfParmReadFile(sstring, GFPARM_RMODE_REREAD);
	}


	if (curTrack->pits.type != TR_PIT_NONE) {
		snprintf(sstring, BUFSIZE, "%s/%s/%d", HM_SECT_PREF, HM_LIST_DRV, index);
		HCtx[idx]->NbPitStopProg = (int)GfParmGetNum(PrefHdle, sstring, HM_ATT_NBPITS, (char*)NULL, 0);
		GfOut("Player: index %d , Pits stops %d\n", index, HCtx[idx]->NbPitStopProg);
	} else {
		HCtx[idx]->NbPitStopProg = 0;
	}
	fuel = 0.0008 * curTrack->length * (s->_totLaps + 1) / (1.0 + ((tdble)HCtx[idx]->NbPitStopProg)) + 20.0;
	if (*carParmHandle) {
		GfParmSetNum(*carParmHandle, SECT_CAR, PRM_FUEL, (char*)NULL, fuel);
	}
	Vtarget = curTrack->pits.speedLimit;
	if (DrvInfo != NULL) {
		GfParmReleaseHandle(DrvInfo);
	}
}


/*
 * Function
 *
 *
 * Description
 *
 *
 * Parameters
 *
 *
 * Return
 *
 */

static void clearDrivingData()
{
    const char* homeDir = getenv("HOME");
    if (!homeDir) return;

    std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
	

    const char* files[] = {
        "track_pos.json",
        "speed.json",
		"end_statistics.json",
		"segment_times.json",
		"live_data.json",
		"live_coaching_data.json",
		"granite_analysis.txt",
		"granite_error.log",
        // "inputs.json",
        NULL  
    };

    for (int i = 0; files[i] != NULL; i++) {   // File Clear
        std::string fullPath = dataDir + "/" + files[i];
        std::ofstream f(fullPath.c_str(), std::ios::out | std::ios::trunc);
        f.close();
    }
	printf("File Cleared.\n");
}

static void endrace(int index, tCarElt* car, tSituation *s)
{
	endStatistics(car, s);

    if (!statsWritten) {
        // Flush the last open segment into lapTimes if not already done
        if (lastMacroSegment != -1 && lastLap != -1) {
            double timeSpent = 0.0;
            for (int i = 0; i < 9; i++)
                timeSpent += inMemSegTimes[lastLap][i];
            if (timeSpent > 5.0 && lapCount < 100)
                lapTimes[lapCount++] = timeSpent;
        }
        endStatistics(car, s);
        statsWritten = true;
    }
}

void newrace(int index, tCarElt* car, tSituation *s)
{
	clearDrivingData();
	lastLap          = -1;
    lastMacroSegment = -1;
    segmentStartTime = 0.0;
    memset(inMemSegTimes, 0, sizeof(inMemSegTimes));	
	prevRemainingLaps = -1;
	memset(lapTimes, 0, sizeof(lapTimes));
	lapCount = 0;
	totalSpeed = 0.0;    
    speedSamples = 0; 
	if(speedOut.is_open()) speedOut.close();
    if(trackOut.is_open()) trackOut.close();
	statsWritten = false;
	int idx = index - 1;

	if (HCtx[idx]->MouseControlUsed) {
		GfctrlMouseCenter();
	}

	memset(keyInfo, 0, sizeof(keyInfo));
	memset(skeyInfo, 0, sizeof(skeyInfo));

	memset(currentKey, 0, sizeof(currentKey));
	memset(currentSKey, 0, sizeof(currentSKey));


#ifndef WIN32
#ifdef TELEMETRY
	if (s->_raceType == RM_TYPE_PRACTICE) {
		RtTelemInit(-10, 10);
		RtTelemNewChannel("Dist", &HCtx[idx]->distToStart, 0, 0);
		RtTelemNewChannel("Ax", &car->_accel_x, 0, 0);
		RtTelemNewChannel("Ay", &car->_accel_y, 0, 0);
		RtTelemNewChannel("Steer", &car->ctrl->steer, 0, 0);
		RtTelemNewChannel("Throttle", &car->ctrl->accelCmd, 0, 0);
		RtTelemNewChannel("Brake", &car->ctrl->brakeCmd, 0, 0);
		RtTelemNewChannel("Gear", &HCtx[idx]->Gear, 0, 0);
		RtTelemNewChannel("Speed", &car->_speed_x, 0, 0);
	}
#endif
#endif

	const char *traintype =GfParmGetStr(car->_carHandle, SECT_DRIVETRAIN, PRM_TYPE, VAL_TRANS_RWD);
	if (strcmp(traintype, VAL_TRANS_RWD) == 0) {
		HCtx[idx]->drivetrain = DRWD;
	} else if (strcmp(traintype, VAL_TRANS_FWD) == 0) {
		HCtx[idx]->drivetrain = DFWD;
	} else if (strcmp(traintype, VAL_TRANS_4WD) == 0) {
		HCtx[idx]->drivetrain = D4WD;
	} 

	tControlCmd	*cmd = HCtx[idx]->CmdControl;
	if (cmd[CMD_CLUTCH].type != GFCTRL_TYPE_JOY_AXIS && 
			cmd[CMD_CLUTCH].type != GFCTRL_TYPE_MOUSE_AXIS)
		HCtx[idx]->autoClutch = 1;
	else
		HCtx[idx]->autoClutch = 0;

}

static void
updateKeys(void)
{
	int i;
	int key;
	int idx;
	tControlCmd *cmd;

	for (idx = 0; idx < 10; idx++) {
		if (HCtx[idx]) {
			cmd = HCtx[idx]->CmdControl;
			for (i = 0; i < nbCmdControl; i++) {
				if (cmd[i].type == GFCTRL_TYPE_KEYBOARD) {
					key = cmd[i].val;
					if (currentKey[key] == GFUI_KEY_DOWN) {
						if (keyInfo[key].state == GFUI_KEY_UP) {
							keyInfo[key].edgeDn = 1;
						} else {
							keyInfo[key].edgeDn = 0;
						}
					} else {
						if (keyInfo[key].state == GFUI_KEY_DOWN) {
							keyInfo[key].edgeUp = 1;
						} else {
							keyInfo[key].edgeUp = 0;
						}
					}
					keyInfo[key].state = currentKey[key];
				}

				if (cmd[i].type == GFCTRL_TYPE_SKEYBOARD) {
					key = cmd[i].val;
					if (currentSKey[key] == GFUI_KEY_DOWN) {
						if (skeyInfo[key].state == GFUI_KEY_UP) {
							skeyInfo[key].edgeDn = 1;
						} else {
							skeyInfo[key].edgeDn = 0;
						}
					} else {
						if (skeyInfo[key].state == GFUI_KEY_DOWN) {
							skeyInfo[key].edgeUp = 1;
						} else {
							skeyInfo[key].edgeUp = 0;
						}
					}
					skeyInfo[key].state = currentSKey[key];
				}
			}
		}
    }
}


static int
onKeyAction(unsigned char key, int modifier, int state)
{
	currentKey[key] = state;

	return 0;
}

static int
onSKeyAction(int key, int modifier, int state)
{
	currentSKey[key] = state;

	return 0;
}

static void common_drive(int index, tCarElt* car, tSituation *s)
{
	tdble slip;
	tdble ax0;
	tdble brake;
	tdble clutch;
	tdble throttle;
	tdble leftSteer;
	tdble rightSteer;
	int scrw, scrh, dummy;
	int idx = index - 1;
	tControlCmd	*cmd = HCtx[idx]->CmdControl;
	const int BUFSIZE = 1024;
	char sstring[BUFSIZE];

	if ((car->_trkPos.seg->id) > 0 && (car->_trkPos.seg->id) < 100)
	{
		hasLapStarted = true;
	}


	static int firstTime = 1;

	if (firstTime) {
		if (HCtx[idx]->MouseControlUsed) {
	    	GfuiMouseShow();
	    	GfctrlMouseInitCenter();
		}
		GfuiKeyEventRegisterCurrent(onKeyAction);
		GfuiSKeyEventRegisterCurrent(onSKeyAction);
		firstTime = 0;
    }


	HCtx[idx]->distToStart = RtGetDistFromStart(car);

	HCtx[idx]->Gear = (tdble)car->_gear;	/* telemetry */

	GfScrGetSize(&scrw, &scrh, &dummy, &dummy);

	memset(&(car->ctrl), 0, sizeof(tCarCtrl));

	car->_lightCmd = HCtx[idx]->lightCmd;

	if (car->_laps != HCtx[idx]->LastPitStopLap) {
		car->_raceCmd = RM_CMD_PIT_ASKED;
	}

	if (lastKeyUpdate != s->currentTime) {
		/* Update the controls only once for all the players */
		updateKeys();

		if (joyPresent) {
			GfctrlJoyGetCurrent(joyInfo);
		}

		GfctrlMouseGetCurrent(mouseInfo);
		lastKeyUpdate = s->currentTime;
	}

	if (((cmd[CMD_ABS].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_ABS].val]) ||
		((cmd[CMD_ABS].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_ABS].val].edgeUp) ||
		((cmd[CMD_ABS].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_ABS].val].edgeUp))
	{
		HCtx[idx]->ParamAbs = 1 - HCtx[idx]->ParamAbs;
		snprintf(sstring, BUFSIZE, "%s/%s/%d", HM_SECT_PREF, HM_LIST_DRV, index);
		GfParmSetStr(PrefHdle, sstring, HM_ATT_ABS, Yn[1 - HCtx[idx]->ParamAbs]);
		GfParmWriteFile(NULL, PrefHdle, "Human");
	}

	if (((cmd[CMD_ASR].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_ASR].val]) ||
		((cmd[CMD_ASR].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_ASR].val].edgeUp) ||
		((cmd[CMD_ASR].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_ASR].val].edgeUp))
	{
		HCtx[idx]->ParamAsr = 1 - HCtx[idx]->ParamAsr;
		snprintf(sstring, BUFSIZE, "%s/%s/%d", HM_SECT_PREF, HM_LIST_DRV, index);
		GfParmSetStr(PrefHdle, sstring, HM_ATT_ASR, Yn[1 - HCtx[idx]->ParamAsr]);
		GfParmWriteFile(NULL, PrefHdle, "Human");
	}

	const int bufsize = sizeof(car->_msgCmd[0]);
	snprintf(car->_msgCmd[0], bufsize, "%s %s", (HCtx[idx]->ParamAbs ? "ABS" : ""), (HCtx[idx]->ParamAsr ? "ASR" : ""));
	memcpy(car->_msgColorCmd, color, sizeof(car->_msgColorCmd));

	if (((cmd[CMD_SPDLIM].type == GFCTRL_TYPE_JOY_BUT) && (joyInfo->levelup[cmd[CMD_SPDLIM].val] == 1)) ||
		((cmd[CMD_SPDLIM].type == GFCTRL_TYPE_KEYBOARD) && (keyInfo[cmd[CMD_SPDLIM].val].state == GFUI_KEY_DOWN)) ||
		((cmd[CMD_SPDLIM].type == GFCTRL_TYPE_SKEYBOARD) && (skeyInfo[cmd[CMD_SPDLIM].val].state == GFUI_KEY_DOWN)))
	{
		speedLimiter = 1;
		snprintf(car->_msgCmd[1], bufsize, "Speed Limiter On");
	} else {
		speedLimiter = 0;
		snprintf(car->_msgCmd[1], bufsize, "Speed Limiter Off");
	}


	if (((cmd[CMD_LIGHT1].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_LIGHT1].val]) ||
		((cmd[CMD_LIGHT1].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_LIGHT1].val].edgeUp) ||
		((cmd[CMD_LIGHT1].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_LIGHT1].val].edgeUp))
	{
		if (HCtx[idx]->lightCmd & RM_LIGHT_HEAD1) {
			HCtx[idx]->lightCmd &= ~(RM_LIGHT_HEAD1 | RM_LIGHT_HEAD2);
		} else {
			HCtx[idx]->lightCmd |= RM_LIGHT_HEAD1 | RM_LIGHT_HEAD2;
		}
	}

	switch (cmd[CMD_LEFTSTEER].type) {
		case GFCTRL_TYPE_JOY_AXIS:
			ax0 = joyInfo->ax[cmd[CMD_LEFTSTEER].val] + cmd[CMD_LEFTSTEER].deadZone;
			if (ax0 > cmd[CMD_LEFTSTEER].max) {
				ax0 = cmd[CMD_LEFTSTEER].max;
			} else if (ax0 < cmd[CMD_LEFTSTEER].min) {
				ax0 = cmd[CMD_LEFTSTEER].min;
			}
			
			// normalize ax0 to -1..0
			ax0 = (ax0 - cmd[CMD_LEFTSTEER].max) / (cmd[CMD_LEFTSTEER].max - cmd[CMD_LEFTSTEER].min);
			leftSteer = -SIGN(ax0) * cmd[CMD_LEFTSTEER].pow * pow(fabs(ax0), cmd[CMD_LEFTSTEER].sens) / (1.0 + cmd[CMD_LEFTSTEER].spdSens * car->pub.speed);
			break;
		case GFCTRL_TYPE_MOUSE_AXIS:
			ax0 = mouseInfo->ax[cmd[CMD_LEFTSTEER].val] - cmd[CMD_LEFTSTEER].deadZone; //FIXME: correct?
			if (ax0 > cmd[CMD_LEFTSTEER].max) {
				ax0 = cmd[CMD_LEFTSTEER].max;
			} else if (ax0 < cmd[CMD_LEFTSTEER].min) {
				ax0 = cmd[CMD_LEFTSTEER].min;
			}
			ax0 = ax0 * cmd[CMD_LEFTSTEER].pow;
			leftSteer = pow(fabs(ax0), cmd[CMD_LEFTSTEER].sens) / (1.0 + cmd[CMD_LEFTSTEER].spdSens * car->pub.speed / 10.0);
			break;
		case GFCTRL_TYPE_KEYBOARD:
		case GFCTRL_TYPE_SKEYBOARD:
		case GFCTRL_TYPE_JOY_BUT:
			if (cmd[CMD_LEFTSTEER].type == GFCTRL_TYPE_KEYBOARD) {
				ax0 = keyInfo[cmd[CMD_LEFTSTEER].val].state;
			} else if (cmd[CMD_LEFTSTEER].type == GFCTRL_TYPE_SKEYBOARD) {
				ax0 = skeyInfo[cmd[CMD_LEFTSTEER].val].state;
			} else {
				ax0 = joyInfo->levelup[cmd[CMD_LEFTSTEER].val];
			}
			if (ax0 == 0) {
				HCtx[idx]->prevLeftSteer = leftSteer = 0;
			} else {
				ax0 = 2 * ax0 - 1;
				leftSteer = HCtx[idx]->prevLeftSteer + ax0 * cmd[CMD_LEFTSTEER].sens * s->deltaTime / (1.0 + cmd[CMD_LEFTSTEER].spdSens * car->pub.speed / 10.0);
				if (leftSteer > 1.0) leftSteer = 1.0;
				if (leftSteer < 0.0) leftSteer = 0.0;
				HCtx[idx]->prevLeftSteer = leftSteer;
			}
			break;
		default:
			leftSteer = 0;
			break;
	}

	switch (cmd[CMD_RIGHTSTEER].type) {
		case GFCTRL_TYPE_JOY_AXIS:
			ax0 = joyInfo->ax[cmd[CMD_RIGHTSTEER].val] - cmd[CMD_RIGHTSTEER].deadZone;
			if (ax0 > cmd[CMD_RIGHTSTEER].max) {
				ax0 = cmd[CMD_RIGHTSTEER].max;
			} else if (ax0 < cmd[CMD_RIGHTSTEER].min) {
				ax0 = cmd[CMD_RIGHTSTEER].min;
			}
			
			// normalize ax to 0..1
			ax0 = (ax0 - cmd[CMD_RIGHTSTEER].min) / (cmd[CMD_RIGHTSTEER].max - cmd[CMD_RIGHTSTEER].min);
			rightSteer = -SIGN(ax0) * cmd[CMD_RIGHTSTEER].pow * pow(fabs(ax0), cmd[CMD_RIGHTSTEER].sens) / (1.0 + cmd[CMD_RIGHTSTEER].spdSens * car->pub.speed);
			break;
		case GFCTRL_TYPE_MOUSE_AXIS:
			ax0 = mouseInfo->ax[cmd[CMD_RIGHTSTEER].val] - cmd[CMD_RIGHTSTEER].deadZone;
			if (ax0 > cmd[CMD_RIGHTSTEER].max) {
				ax0 = cmd[CMD_RIGHTSTEER].max;
			} else if (ax0 < cmd[CMD_RIGHTSTEER].min) {
				ax0 = cmd[CMD_RIGHTSTEER].min;
			}
			ax0 = ax0 * cmd[CMD_RIGHTSTEER].pow;
			rightSteer = - pow(fabs(ax0), cmd[CMD_RIGHTSTEER].sens) / (1.0 + cmd[CMD_RIGHTSTEER].spdSens * car->pub.speed / 10.0);
			break;
		case GFCTRL_TYPE_KEYBOARD:
		case GFCTRL_TYPE_SKEYBOARD:
		case GFCTRL_TYPE_JOY_BUT:
			if (cmd[CMD_RIGHTSTEER].type == GFCTRL_TYPE_KEYBOARD) {
				ax0 = keyInfo[cmd[CMD_RIGHTSTEER].val].state;
			} else  if (cmd[CMD_RIGHTSTEER].type == GFCTRL_TYPE_SKEYBOARD) {
				ax0 = skeyInfo[cmd[CMD_RIGHTSTEER].val].state;
			} else {
				ax0 = joyInfo->levelup[cmd[CMD_RIGHTSTEER].val];
			}
			if (ax0 == 0) {
				HCtx[idx]->prevRightSteer = rightSteer = 0;
			} else {
				ax0 = 2 * ax0 - 1;
				rightSteer = HCtx[idx]->prevRightSteer - ax0 * cmd[CMD_RIGHTSTEER].sens * s->deltaTime/ (1.0 + cmd[CMD_RIGHTSTEER].spdSens * car->pub.speed / 10.0);
				if (rightSteer > 0.0) rightSteer = 0.0;
				if (rightSteer < -1.0) rightSteer = -1.0;
				HCtx[idx]->prevRightSteer = rightSteer;
			}
			break;
		default:
			rightSteer = 0;
			break;
	}

	car->_steerCmd = leftSteer + rightSteer;


	switch (cmd[CMD_BRAKE].type) {
		case GFCTRL_TYPE_JOY_AXIS:
			brake = joyInfo->ax[cmd[CMD_BRAKE].val];
			if (brake > cmd[CMD_BRAKE].max) {
				brake = cmd[CMD_BRAKE].max;
			} else if (brake < cmd[CMD_BRAKE].min) {
				brake = cmd[CMD_BRAKE].min;
			}
			car->_brakeCmd = fabs(cmd[CMD_BRAKE].pow *
						pow(fabs((brake - cmd[CMD_BRAKE].minVal) /
							(cmd[CMD_BRAKE].max - cmd[CMD_BRAKE].min)),
						cmd[CMD_BRAKE].sens));
			break;
		case GFCTRL_TYPE_MOUSE_AXIS:
			ax0 = mouseInfo->ax[cmd[CMD_BRAKE].val] - cmd[CMD_BRAKE].deadZone;
			if (ax0 > cmd[CMD_BRAKE].max) {
				ax0 = cmd[CMD_BRAKE].max;
			} else if (ax0 < cmd[CMD_BRAKE].min) {
				ax0 = cmd[CMD_BRAKE].min;
			}
			ax0 = ax0 * cmd[CMD_BRAKE].pow;
			car->_brakeCmd =  pow(fabs(ax0), cmd[CMD_BRAKE].sens) / (1.0 + cmd[CMD_BRAKE].spdSens * car->_speed_x / 10.0);
			break;
		case GFCTRL_TYPE_JOY_BUT:
			car->_brakeCmd = joyInfo->levelup[cmd[CMD_BRAKE].val];
			break;
		case GFCTRL_TYPE_MOUSE_BUT:
			car->_brakeCmd = mouseInfo->button[cmd[CMD_BRAKE].val];
			break;
		case GFCTRL_TYPE_KEYBOARD:
			car->_brakeCmd = keyInfo[cmd[CMD_BRAKE].val].state;
			break;
		case GFCTRL_TYPE_SKEYBOARD:
			car->_brakeCmd = skeyInfo[cmd[CMD_BRAKE].val].state;
			break;
		default:
			car->_brakeCmd = 0;
			break;
	}

	switch (cmd[CMD_CLUTCH].type) {
		case GFCTRL_TYPE_JOY_AXIS:
			clutch = joyInfo->ax[cmd[CMD_CLUTCH].val];
			if (clutch > cmd[CMD_CLUTCH].max) {
				clutch = cmd[CMD_CLUTCH].max;
			} else if (clutch < cmd[CMD_CLUTCH].min) {
				clutch = cmd[CMD_CLUTCH].min;
			}
			car->_clutchCmd = fabs(cmd[CMD_CLUTCH].pow *
						pow(fabs((clutch - cmd[CMD_CLUTCH].minVal) /
							(cmd[CMD_CLUTCH].max - cmd[CMD_CLUTCH].min)),
						cmd[CMD_CLUTCH].sens));
			break;
		case GFCTRL_TYPE_MOUSE_AXIS:
			ax0 = mouseInfo->ax[cmd[CMD_CLUTCH].val] - cmd[CMD_CLUTCH].deadZone;
			if (ax0 > cmd[CMD_CLUTCH].max) {
				ax0 = cmd[CMD_CLUTCH].max;
			} else if (ax0 < cmd[CMD_CLUTCH].min) {
				ax0 = cmd[CMD_CLUTCH].min;
			}
			ax0 = ax0 * cmd[CMD_CLUTCH].pow;
			car->_clutchCmd =  pow(fabs(ax0), cmd[CMD_CLUTCH].sens) / (1.0 + cmd[CMD_CLUTCH].spdSens * car->_speed_x / 10.0);
			break;
		case GFCTRL_TYPE_JOY_BUT:
			car->_clutchCmd = joyInfo->levelup[cmd[CMD_CLUTCH].val];
			break;
		case GFCTRL_TYPE_MOUSE_BUT:
			car->_clutchCmd = mouseInfo->button[cmd[CMD_CLUTCH].val];
			break;
		case GFCTRL_TYPE_KEYBOARD:
			car->_clutchCmd = keyInfo[cmd[CMD_CLUTCH].val].state;
			break;
		case GFCTRL_TYPE_SKEYBOARD:
			car->_clutchCmd = skeyInfo[cmd[CMD_CLUTCH].val].state;
			break;
		default:
			car->_clutchCmd = 0;
			break;
	}

	// if player's used the clutch manually then we dispense with autoClutch
	if (car->_clutchCmd != 0.0f)
		HCtx[idx]->autoClutch = 0;

	switch (cmd[CMD_THROTTLE].type) {
		case GFCTRL_TYPE_JOY_AXIS:
			throttle = joyInfo->ax[cmd[CMD_THROTTLE].val];
			if (throttle > cmd[CMD_THROTTLE].max) {
				throttle = cmd[CMD_THROTTLE].max;
			} else if (throttle < cmd[CMD_THROTTLE].min) {
				throttle = cmd[CMD_THROTTLE].min;
			}
			car->_accelCmd = fabs(cmd[CMD_THROTTLE].pow *
						pow(fabs((throttle - cmd[CMD_THROTTLE].minVal) /
								(cmd[CMD_THROTTLE].max - cmd[CMD_THROTTLE].min)),
							cmd[CMD_THROTTLE].sens));
			break;
		case GFCTRL_TYPE_MOUSE_AXIS:
			ax0 = mouseInfo->ax[cmd[CMD_THROTTLE].val] - cmd[CMD_THROTTLE].deadZone;
			if (ax0 > cmd[CMD_THROTTLE].max) {
				ax0 = cmd[CMD_THROTTLE].max;
			} else if (ax0 < cmd[CMD_THROTTLE].min) {
				ax0 = cmd[CMD_THROTTLE].min;
			}
			ax0 = ax0 * cmd[CMD_THROTTLE].pow;
			car->_accelCmd =  pow(fabs(ax0), cmd[CMD_THROTTLE].sens) / (1.0 + cmd[CMD_THROTTLE].spdSens * car->_speed_x / 10.0);
			if (isnan (car->_accelCmd)) {
				car->_accelCmd = 0;
			}
			/* printf("  axO:%f  accelCmd:%f\n", ax0, car->_accelCmd); */
			break;
		case GFCTRL_TYPE_JOY_BUT:
			car->_accelCmd = joyInfo->levelup[cmd[CMD_THROTTLE].val];
			break;
		case GFCTRL_TYPE_MOUSE_BUT:
			car->_accelCmd = mouseInfo->button[cmd[CMD_THROTTLE].val];
			break;
		case GFCTRL_TYPE_KEYBOARD:
			car->_accelCmd = keyInfo[cmd[CMD_THROTTLE].val].state;
			break;
		case GFCTRL_TYPE_SKEYBOARD:
			car->_accelCmd = skeyInfo[cmd[CMD_THROTTLE].val].state;
			break;
		default:
			car->_accelCmd = 0;
			break;
	}

	if (s->currentTime > 1.0) {
		// thanks Christos for the following: gradual accel/brake changes for on/off controls.
		const tdble inc_rate = 0.2f;
		
		if (cmd[CMD_BRAKE].type == GFCTRL_TYPE_JOY_BUT ||
		    cmd[CMD_BRAKE].type == GFCTRL_TYPE_MOUSE_BUT ||
		    cmd[CMD_BRAKE].type == GFCTRL_TYPE_KEYBOARD ||
		    cmd[CMD_BRAKE].type == GFCTRL_TYPE_SKEYBOARD)
		{
			tdble d_brake = car->_brakeCmd - HCtx[idx]->pbrake;
			if (fabs(d_brake) > inc_rate && car->_brakeCmd > HCtx[idx]->pbrake) {
				car->_brakeCmd = MIN(car->_brakeCmd, HCtx[idx]->pbrake + inc_rate*d_brake/fabs(d_brake));
			}
			HCtx[idx]->pbrake = car->_brakeCmd;
		}

		if (cmd[CMD_THROTTLE].type == GFCTRL_TYPE_JOY_BUT ||
			cmd[CMD_THROTTLE].type == GFCTRL_TYPE_MOUSE_BUT ||
			cmd[CMD_THROTTLE].type == GFCTRL_TYPE_KEYBOARD ||
			cmd[CMD_THROTTLE].type == GFCTRL_TYPE_SKEYBOARD)
		{
			tdble d_accel = car->_accelCmd - HCtx[idx]->paccel;
			if (fabs(d_accel) > inc_rate && car->_accelCmd > HCtx[idx]->paccel) {
				car->_accelCmd = MIN(car->_accelCmd, HCtx[idx]->paccel + inc_rate*d_accel/fabs(d_accel));
			}
			HCtx[idx]->paccel = car->_accelCmd;
		}
	}

	if (HCtx[idx]->AutoReverseEngaged) {
		/* swap brake and throttle */
		brake = car->_brakeCmd;
		car->_brakeCmd = car->_accelCmd;
		car->_accelCmd = brake;
	}

	if (HCtx[idx]->ParamAbs) 
	{
		if (fabs(car->_speed_x) > 10.0)
		{
			int i;

			tdble skidAng = atan2(car->_speed_Y, car->_speed_X) - car->_yaw;
			NORM_PI_PI(skidAng);

			if (car->_speed_x > 5 && fabs(skidAng) > 0.2)
				car->_brakeCmd = MIN(car->_brakeCmd, 0.10 + 0.70 * cos(skidAng));

			if (fabs(car->_steerCmd) > 0.1)
			{
				tdble decel = ((fabs(car->_steerCmd)-0.1) * (1.0 + fabs(car->_steerCmd)) * 0.6);
				car->_brakeCmd = MIN(car->_brakeCmd, MAX(0.35, 1.0 - decel));
			}

			const tdble abs_slip = 2.5;
			const tdble abs_range = 5.0;

			slip = 0;
			for (i = 0; i < 4; i++) {
				slip += car->_wheelSpinVel(i) * car->_wheelRadius(i);
			}
			slip = car->_speed_x - slip/4.0f;

			if (slip > abs_slip)
				car->_brakeCmd = car->_brakeCmd - MIN(car->_brakeCmd*0.8, (slip - abs_slip) / abs_range);
		}
	}


	if (HCtx[idx]->ParamAsr) 
	{
    	tdble trackangle = RtTrackSideTgAngleL(&(car->_trkPos));
		tdble angle = trackangle - car->_yaw;
		NORM_PI_PI(angle);

		tdble maxaccel = 0.0;
		if (car->_trkPos.seg->type == TR_STR)
			maxaccel = MIN(car->_accelCmd, 0.2);
		else if (car->_trkPos.seg->type == TR_LFT && angle < 0.0)
			maxaccel = MIN(car->_accelCmd, MIN(0.6, -angle));
		else if (car->_trkPos.seg->type == TR_RGT && angle > 0.0)
			maxaccel = MIN(car->_accelCmd, MIN(0.6, angle));

		tdble origaccel = car->_accelCmd;
		tdble skidAng = atan2(car->_speed_Y, car->_speed_X) - car->_yaw;
		NORM_PI_PI(skidAng);

		if (car->_speed_x > 5 && fabs(skidAng) > 0.2)
		{
			car->_accelCmd = MIN(car->_accelCmd, 0.15 + 0.70 * cos(skidAng));
			car->_accelCmd = MAX(car->_accelCmd, maxaccel);
		}

		if (fabs(car->_steerCmd) > 0.1)
		{
			tdble decel = ((fabs(car->_steerCmd)-0.1) * (1.0 + fabs(car->_steerCmd)) * 0.8);
			car->_accelCmd = MIN(car->_accelCmd, MAX(0.35, 1.0 - decel));
		}

		tdble drivespeed = 0.0;
		switch (HCtx[idx]->drivetrain)
		{
			case D4WD:
				drivespeed = ((car->_wheelSpinVel(FRNT_RGT) + car->_wheelSpinVel(FRNT_LFT)) *
				              car->_wheelRadius(FRNT_LFT) +
				              (car->_wheelSpinVel(REAR_RGT) + car->_wheelSpinVel(REAR_LFT)) *
				              car->_wheelRadius(REAR_LFT)) / 4.0; 
				break;
			case DFWD:
				drivespeed = (car->_wheelSpinVel(FRNT_RGT) + car->_wheelSpinVel(FRNT_LFT)) *
				              car->_wheelRadius(FRNT_LFT) / 2.0;
				break;
			default:
				drivespeed = (car->_wheelSpinVel(REAR_RGT) + car->_wheelSpinVel(REAR_LFT)) *
				              car->_wheelRadius(REAR_LFT) / 2.0;
				break;
		}

		tdble slip = drivespeed - fabs(car->_speed_x);
		if (slip > 2.0)
			car->_accelCmd = MIN(car->_accelCmd, origaccel - MIN(origaccel-0.1, ((slip - 2.0)/10.0)));
	}

	if (speedLimiter) {
		tdble Dv;
		if (Vtarget != 0) {
			Dv = Vtarget - car->_speed_x;
			if (Dv > 0.0) {
				car->_accelCmd = MIN(car->_accelCmd, fabs(Dv/6.0));
			} else {
				car->_brakeCmd = MAX(car->_brakeCmd, fabs(Dv/5.0));
				car->_accelCmd = 0;
			}
		}
	}
	if (hasLapStarted){
		logTrackPosition(car, s); 
		logSegmentPosition(car, s);
		logSpeed(car, s);
		logLiveCommentary(car, s);
		logLiveCoaching(car, s);
		logEngineerData(car, s);
		endStatistics(car, s);

	}
	

	

#ifndef WIN32
#ifdef TELEMETRY
	if ((car->_laps > 1) && (car->_laps < 5)) {
		if (HCtx[idx]->lap == 1) {
			RtTelemStartMonitoring("Player");
		}
		RtTelemUpdate(car->_curLapTime);
	}
	if (car->_laps == 5) {
		if (HCtx[idx]->lap == 4) {
			RtTelemShutdown();
		}
	}
#endif
#endif

if (car->_laps != HCtx[idx]->lap && car->_laps > 0) {

        endStatistics(car, s);
        // If this lap completion also consumed the last remaining lap, mark stats as written
        if (car->_remainingLaps == 0) {
            statsWritten = true;
        }
    }

    prevRemainingLaps = car->_remainingLaps;
    HCtx[idx]->lap = car->_laps;
	
}




static tdble getAutoClutch(int idx, int gear, int newgear, tCarElt *car)
{
	if (newgear != 0 && newgear < car->_gearNb) {
		if (newgear != gear) {
			HCtx[idx]->clutchtime = 0.332f - ((tdble) newgear / 65.0f);
		}

		if (HCtx[idx]->clutchtime > 0.0f)
			HCtx[idx]->clutchtime -= RCM_MAX_DT_ROBOTS;
		return 2.0f * HCtx[idx]->clutchtime;
	}
	
	return 0.0f;
}

/*
 * Function
 *
 *
 * Description
 *
 *
 * Parameters
 *
 *
 * Return
 *
 *
 * Remarks
 *	
 */
static void drive_mt(int index, tCarElt* car, tSituation *s)
{
	int i;
	int idx = index - 1;
	tControlCmd	*cmd = HCtx[idx]->CmdControl;

	common_drive(index, car, s);
	car->_gearCmd = car->_gear;
	/* manual shift sequential */
	if (((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_UP_SHFT].val]) ||
		((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgeup[cmd[CMD_UP_SHFT].val]) ||
		((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_UP_SHFT].val].edgeUp) ||
		((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_UP_SHFT].val].edgeUp))
	{
		car->_gearCmd++;
	}

	if (((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_DN_SHFT].val]) ||
		((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgeup[cmd[CMD_DN_SHFT].val]) ||
		((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_DN_SHFT].val].edgeUp) ||
		((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_DN_SHFT].val].edgeUp))
	{
		if (HCtx[idx]->SeqShftAllowNeutral || (car->_gearCmd > 1)) {
			car->_gearCmd--;
		}
	}

	/* manual shift direct */
	if (HCtx[idx]->RelButNeutral) {
		for (i = CMD_GEAR_R; i <= CMD_GEAR_6; i++) {
			if (((cmd[i].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgedn[cmd[i].val]) ||
				((cmd[i].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgedn[cmd[i].val]) ||
				((cmd[i].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[i].val].edgeDn) ||
				((cmd[i].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[i].val].edgeDn))
			{
				car->_gearCmd = 0;
			}
		}
	}

	for (i = CMD_GEAR_R; i <= CMD_GEAR_6; i++) {
		if (((cmd[i].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[i].val]) ||
			((cmd[i].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgeup[cmd[i].val]) ||
			((cmd[i].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[i].val].edgeUp) ||
			((cmd[i].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[i].val].edgeUp))
		{
			car->_gearCmd = i - CMD_GEAR_N;
		}
	}

	if (HCtx[idx]->autoClutch && car->_clutchCmd == 0.0f)
		car->_clutchCmd = getAutoClutch(idx, car->_gear, car->_gearCmd, car);

}
/*
 * Function
 *
 *
 * Description
 *
 *
 * Parameters
 *
 *
 * Return
 *	
 *
 * Remarks
 *	
 */
static void drive_at(int index, tCarElt* car, tSituation *s)
{
	int gear, i;
	int idx = index - 1;
	tControlCmd	*cmd = HCtx[idx]->CmdControl;

	common_drive(index, car, s);

	/* shift */
	gear = car->_gear;

	if (gear > 0) {
		/* return to auto-shift */
		HCtx[idx]->manual = 0;
	}
	gear += car->_gearOffset;
	car->_gearCmd = car->_gear;

    if (!HCtx[idx]->AutoReverse) {
		/* manual shift */
		if (((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_UP_SHFT].val]) ||
			((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_UP_SHFT].val].edgeUp) ||
			((cmd[CMD_UP_SHFT].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_UP_SHFT].val].edgeUp))
		{
			car->_gearCmd++;
			HCtx[idx]->manual = 1;
		}

		if (((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[CMD_DN_SHFT].val]) ||
			((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[CMD_DN_SHFT].val].edgeUp) ||
			((cmd[CMD_DN_SHFT].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[CMD_DN_SHFT].val].edgeUp))
		{
			car->_gearCmd--;
			HCtx[idx]->manual = 1;
		}

		/* manual shift direct */
		if (HCtx[idx]->RelButNeutral) {
			for (i = CMD_GEAR_R; i < CMD_GEAR_2; i++) {
				if (((cmd[i].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgedn[cmd[i].val]) ||
					((cmd[i].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgedn[cmd[i].val]) ||
					((cmd[i].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[i].val].edgeDn) ||
					((cmd[i].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[i].val].edgeDn))
				{
					car->_gearCmd = 0;
					/* return to auto-shift */
					HCtx[idx]->manual = 0;
				}
			}
		}

		for (i = CMD_GEAR_R; i < CMD_GEAR_2; i++) {
			if (((cmd[i].type == GFCTRL_TYPE_JOY_BUT) && joyInfo->edgeup[cmd[i].val]) ||
				((cmd[i].type == GFCTRL_TYPE_MOUSE_BUT) && mouseInfo->edgeup[cmd[i].val]) ||
				((cmd[i].type == GFCTRL_TYPE_KEYBOARD) && keyInfo[cmd[i].val].edgeUp) ||
				((cmd[i].type == GFCTRL_TYPE_SKEYBOARD) && skeyInfo[cmd[i].val].edgeUp))
			{
				car->_gearCmd = i - CMD_GEAR_N;
				HCtx[idx]->manual = 1;
			}
		}
    }

	/* auto shift */
	if (!HCtx[idx]->manual && !HCtx[idx]->AutoReverseEngaged) {
		tdble omega = car->_enginerpmRedLine * car->_wheelRadius(2) * 0.95;
		tdble shiftThld = 10000.0f;
		if (car->_gearRatio[gear] != 0) {
			shiftThld = omega / car->_gearRatio[gear];			
		}

		if (car->pub.speed > shiftThld) {
			car->_gearCmd++;
		} else if (car->_gearCmd > 1) {
			if (car->pub.speed < (omega / car->_gearRatio[gear-1] - 4.0)) {
				car->_gearCmd--;
			}
		}

		if (car->_gearCmd <= 0) {
			car->_gearCmd++;
		}
	}

    if (HCtx[idx]->AutoReverse) {
		/* Automatic Reverse Gear Mode */
		if (!HCtx[idx]->AutoReverseEngaged) {
			if ((car->_brakeCmd > car->_accelCmd) && (car->_speed_x < 1.0)) {
				HCtx[idx]->AutoReverseEngaged = 1;
				car->_gearCmd = CMD_GEAR_R - CMD_GEAR_N;
			}
		} else {
			/* currently in autoreverse mode */
			if ((car->_brakeCmd > car->_accelCmd) && (car->_speed_x > -1.0) && (car->_speed_x < 1.0)) {
				HCtx[idx]->AutoReverseEngaged = 0;
				car->_gearCmd = CMD_GEAR_1 - CMD_GEAR_N;
			} else {
				car->_gearCmd = CMD_GEAR_R - CMD_GEAR_N;
			}
		}
    }

	if (HCtx[idx]->autoClutch && car->_clutchCmd == 0.0f)
	    car->_clutchCmd = getAutoClutch(idx, car->_gear, car->_gearCmd, car);
}

static int pitcmd(int index, tCarElt* car, tSituation *s)
{
	tdble f1, f2;
	tdble ns;
	int idx = index - 1;

	HCtx[idx]->NbPitStops++;
	f1 = car->_tank - car->_fuel;
	if (HCtx[idx]->NbPitStopProg < HCtx[idx]->NbPitStops) {
		ns = 1.0;
	} else {
		ns = 1.0 + (HCtx[idx]->NbPitStopProg - HCtx[idx]->NbPitStops);
	}

	f2 = 0.00065 * (curTrack->length * car->_remainingLaps + car->_trkPos.seg->lgfromstart) / ns - car->_fuel;

	car->_pitFuel = MAX(MIN(f1, f2), 0);

	HCtx[idx]->LastPitStopLap = car->_laps;

	car->_pitRepair = (int)car->_dammage;

	int i;
	int key;
	tControlCmd *cmd;

	if (HCtx[idx]) {
		cmd = HCtx[idx]->CmdControl;
		for (i = 0; i < nbCmdControl; i++) {
			if (cmd[i].type == GFCTRL_TYPE_KEYBOARD || cmd[i].type == GFCTRL_TYPE_SKEYBOARD) {
				key = cmd[i].val;
				keyInfo[key].state = GFUI_KEY_UP;
				keyInfo[key].edgeDn = 0;
				keyInfo[key].edgeUp = 0;
				skeyInfo[key].state = GFUI_KEY_UP;
				skeyInfo[key].edgeDn = 0;
				skeyInfo[key].edgeUp = 0;
				currentKey[key] = GFUI_KEY_UP;
				currentSKey[key] = GFUI_KEY_UP;
			}
		}
	}

	return ROB_PIT_MENU; /* The player is able to modify the value by menu */
}