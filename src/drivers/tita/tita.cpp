/***************************************************************************

    file                 : berniw.cpp
    created              : Mon Apr 17 13:51:00 CET 2000
    copyright            : (C) 2000-2006 by Bernhard Wymann
    email                : berniw@bluewin.ch
    version              : $Id: tita.cpp,v 1.11.2.2 2013/08/05 17:22:44 berniw Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include "berniw.h"

#include <fstream>
#include <string>
#include <iostream>
#include <sys/stat.h>
#include <cmath>


// Function prototypes.
static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation * situation);
static void drive(int index, tCarElt* car, tSituation *situation);
static void newRace(int index, tCarElt* car, tSituation *situation);
static void endrace(int index, tCarElt* car, tSituation *s);   // Added end race function to log STATS
static int  InitFuncPt(int index, void *pt);
static int  pitcmd(int index, tCarElt* car, tSituation *s);
static void shutdown(int index);
float getClutch(MyCar* myc, tCarElt* car);

static const char* botname[BOTS] = {
	"tita 1", "tita 2", "tita 3", "tita 4", "tita 5",
	"tita 6", "tita 7", "tita 8", "tita 9", "tita 10"
};

static const char* botdesc[BOTS] = {
	"tita 1", "tita 2", "tita 3", "tita 4", "tita 5",
	"tita 6", "tita 7", "tita 8", "tita 9", "tita 10"
};

// ── Logging state ─────────────────────────────────────────────────────────────

static double lapTimes[100]  = {0};
static int    lapCount       = 0;

static double totalSpeed     = 0.0;
static int    speedSamples   = 0;

static int    prevRemainingLaps = -1;
static bool   statsWritten      = false;

static int    lastMacroSegment  = -1;
static double segmentStartTime  = 0.0;
static int    lastLap           = -1;

static int    titaPrevLap[BOTS] = {0};   // per-bot previous lap tracker

// ── Logging helpers ───────────────────────────────────────────────────────────

static std::string getTitaDataDir()
{
    const char* homeDir = getenv("HOME");
    if (!homeDir) return "";
    std::string dataDir = std::string(homeDir) + "/.torcs/DrivingData";
    mkdir(dataDir.c_str(), 0755);
    return dataDir;
}

void titaLogTrackPosition(tCarElt* car, tSituation *s)
{
    static double lastPosWriteTime = 0.0;
    if (s->currentTime - lastPosWriteTime < 1.0) return;
    lastPosWriteTime = s->currentTime;

    std::string dataDir = getTitaDataDir();
    if (dataDir.empty()) return;

    std::ofstream outFile;
    outFile.open((dataDir + "/tita_track_pos.json").c_str(), std::ios_base::app);
    if (outFile.is_open()) {
        outFile << "{"
                << "\"time\":"       << s->currentTime            << ","
                << "\"pos_x\":"      << car->_pos_X               << ","
                << "\"pos_y\":"      << car->_pos_Y               << ","
                << "\"track_pos\":"  << car->_trkPos.toMiddle     << ","
                << "\"segment_id\":" << car->_trkPos.seg->id      << ","
                << "\"to_start\":"   << car->_trkPos.toStart      << ","
                << "\"lap\":"        << car->_laps
                << "}," << std::endl;
        outFile.close();
    }
}

void titaLogSpeed(tCarElt* car, tSituation *s)
{
    static double lastSpeedWriteTime = 0.0;
    if (s->currentTime - lastSpeedWriteTime < 0.5) return;
    lastSpeedWriteTime = s->currentTime;

    totalSpeed += fabs(car->_speed_x);
    speedSamples++;

    std::string dataDir = getTitaDataDir();
    if (dataDir.empty()) return;

    std::ofstream outFile;
    outFile.open((dataDir + "/tita_speed.json").c_str(), std::ios_base::app);
    if (outFile.is_open()) {
        outFile << "{"
                << "\"time\":"       << s->currentTime       << ","
                << "\"segment_id\":" << car->_trkPos.seg->id << ","
                << "\"speedX\":"     << car->_speed_X        << ","
                << "\"speedy\":"     << car->_speed_Y        << ","
                << "\"speedx\":"     << car->_speed_x
                << "}," << std::endl;
        outFile.close();
    }
}

static int getMacroSegment(int segId)
{
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

static void writeSegmentTimeToJson(int segment, int lap, double time)
{
    std::string dataDir = getTitaDataDir();
    if (dataDir.empty()) return;

    std::ofstream outFile((dataDir + "/tita_segment_times.json").c_str(), std::ios_base::app);
    if (outFile.is_open()) {
        outFile << "{"
                << "\"lap\":"     << lap     << ","
                << "\"segment\":" << segment << ","
                << "\"time\":"    << time
                << "}," << std::endl;
        outFile.close();
    }
}

static void titaLogSegmentPosition(tCarElt *car, tSituation *s)
{
    int segId    = car->_trkPos.seg->id;
    int macroSeg = getMacroSegment(segId);
    int lap      = car->_laps;

    if (lap != lastLap) {
        lastLap          = lap;
        lastMacroSegment = -1;
    }

    if (macroSeg != lastMacroSegment) {
        if (lastMacroSegment != -1) {
            double timeSpent = s->currentTime - segmentStartTime;
            writeSegmentTimeToJson(lastMacroSegment, lap, timeSpent);
        }
        segmentStartTime = s->currentTime;
        lastMacroSegment = macroSeg;
    }
}

static void endStatistics(tCarElt* car, tSituation *s)
{
    std::string dataDir = getTitaDataDir();
    if (dataDir.empty()) return;

    double avgSpeed   = (speedSamples > 0) ? (totalSpeed / speedSamples) : 0.0;
    double avgLapTime = (car->_laps   > 0) ? (s->currentTime / car->_laps) : 0.0;

    std::ofstream outFile((dataDir + "/tita_end_statistics.json").c_str(),
                          std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        printf("TITA ERROR: Could not open tita_end_statistics.json for writing\n");
        return;
    }

    outFile << "{"
            << "\"avg_speed_ms\":"   << avgSpeed                  << ","
            << "\"avg_speed_kmh\":"  << avgSpeed * 3.6            << ","
            << "\"avg_lap_time\":"   << avgLapTime                << ","
            << "\"best_lap_time\":"  << car->_bestLapTime         << ","
            << "\"last_lap_time\":"  << car->_lastLapTime         << ","
            << "\"laps_completed\":" << car->_laps                << ","
            << "\"total_distance\":" << car->_distRaced           << ","
            << "\"finish_pos\":"     << car->_pos                 << ","
            << "\"damage\":"         << car->_dammage             << ","
            << "\"fuel_used\":"      << (car->_tank - car->_fuel) << ","
            << "\"lap_times\":[";

    for (int i = 0; i < lapCount; i++) {
        outFile << "{\"lap\":" << (i + 1) << ",\"time\":" << lapTimes[i] << "}";
        if (i < lapCount - 1) outFile << ",";
    }

    outFile << "]}" << std::endl;
    outFile.close();
    printf("TITA Stats Recorded (lap %d).\n", car->_laps);
}

static void clearDrivingData()
{
    std::string dataDir = getTitaDataDir();
    if (dataDir.empty()) return;

    const char* files[] = {
        "tita_track_pos.json",
        "tita_speed.json",
        "tita_end_statistics.json",
        "tita_segment_times.json",
        NULL
    };

    for (int i = 0; files[i] != NULL; i++) {
        std::string fullPath = dataDir + "/" + files[i];
        std::ofstream f(fullPath.c_str(), std::ios::out | std::ios::trunc);
        f.close();
    }
    printf("TITA Files Cleared.\n");
}

static void handleLapLogging(int index, tCarElt* car, tSituation *s)
{
    int idx = index - 1;

    // New lap completed
    if (car->_laps != titaPrevLap[idx] && car->_laps > 1) {
        if (lapCount < 100) {
            lapTimes[lapCount++] = car->_lastLapTime;
        }
        endStatistics(car, s);
        titaLogSegmentPosition(car, s);
    }

    // Final lap: remaining laps just dropped to 0
    if (prevRemainingLaps > 0 && car->_remainingLaps == 0 && !statsWritten) {
        printf("TITA DEBUG: final lap detected, curLapTime=%.3f\n", car->_curLapTime);
        if (car->_curLapTime > 0.0 && lapCount < 100) {
            lapTimes[lapCount++] = car->_curLapTime;
        }
        endStatistics(car, s);
        statsWritten = true;
    }

    prevRemainingLaps = car->_remainingLaps;
    titaPrevLap[idx]  = car->_laps;
}

// ── Module entry point ────────────────────────────────────────────────────────

extern "C" int tita(tModInfo *modInfo)
{
	for (int i = 0; i < BOTS; i++) {
		modInfo[i].name = strdup(botname[i]);
		modInfo[i].desc = strdup(botdesc[i]);
		modInfo[i].fctInit = InitFuncPt;
		modInfo[i].gfId    = ROB_IDENT;
		modInfo[i].index   = i+1;
	}
	return 0;
}


static int InitFuncPt(int index, void *pt)
{
	tRobotItf *itf = (tRobotItf *)pt;

	itf->rbNewTrack = initTrack;
	itf->rbNewRace  = newRace;
	itf->rbDrive    = drive;
	itf->rbEndRace  = endrace;      // Added end race function to log STATS
	itf->rbShutdown	= shutdown;
	itf->rbPitCmd   = pitcmd;
	itf->index      = index;
	return 0;
}


static MyCar* mycar[BOTS] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static OtherCar* ocar = NULL;
static TrackDesc* myTrackDesc = NULL;
static double currenttime;
static const tdble waitToTurn = 1.0;


static void shutdown(int index) {
	int i = index - 1;
	if (mycar[i] != NULL) {
		delete mycar[i];
		mycar[i] = NULL;
	}
	if (myTrackDesc != NULL) {
		delete myTrackDesc;
		myTrackDesc = NULL;
	}
	if (ocar != NULL) {
		delete [] ocar;
		ocar = NULL;
	}
}


static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation * situation)
{
	if ((myTrackDesc != NULL) && (myTrackDesc->getTorcsTrack() != track)) {
		delete myTrackDesc;
		myTrackDesc = NULL;
	}
	if (myTrackDesc == NULL) {
		myTrackDesc = new TrackDesc(track);
	}

	char buffer[BUFSIZE];
	char* trackname = strrchr(track->filename, '/') + 1;

	switch (situation->_raceType) {
		case RM_TYPE_PRACTICE:
			snprintf(buffer, BUFSIZE, "drivers/tita/%d/practice/%s", index, trackname);
			break;
		case RM_TYPE_QUALIF:
			snprintf(buffer, BUFSIZE, "drivers/tita/%d/qualifying/%s", index, trackname);
			break;
		case RM_TYPE_RACE:
			snprintf(buffer, BUFSIZE, "drivers/tita/%d/race/%s", index, trackname);
			break;
		default:
			break;
	}

	*carParmHandle = GfParmReadFile(buffer, GFPARM_RMODE_STD);
	if (*carParmHandle == NULL) {
		snprintf(buffer, BUFSIZE, "drivers/tita/%d/default.xml", index);
		*carParmHandle = GfParmReadFile(buffer, GFPARM_RMODE_STD);
    }

	float fuel = GfParmGetNum(*carParmHandle, BERNIW_SECT_PRIV, BERNIW_ATT_FUELPERLAP,
		(char*)NULL, track->length*MyCar::MAX_FUEL_PER_METER);

	float fuelmargin = (situation->_raceType == RM_TYPE_RACE) ? 1.0 : 0.0;

	fuel *= (situation->_totLaps + fuelmargin);
	GfParmSetNum(*carParmHandle, SECT_CAR, PRM_FUEL, (char*)NULL, MIN(fuel, 100.0));
}


static void newRace(int index, tCarElt* car, tSituation *situation)
{
	// Reset all logging state at the start of every race
	prevRemainingLaps = -1;
	memset(lapTimes, 0, sizeof(lapTimes));
	lapCount         = 0;
	totalSpeed       = 0.0;
	speedSamples     = 0;
	statsWritten     = false;
	lastMacroSegment = -1;
	segmentStartTime = 0.0;
	lastLap          = -1;
	memset(titaPrevLap, 0, sizeof(titaPrevLap));
	clearDrivingData();

	if (ocar != NULL) {
		delete [] ocar;
	}
	ocar = new OtherCar[situation->_ncars];
	for (int i = 0; i < situation->_ncars; i++) {
		ocar[i].init(myTrackDesc, situation->cars[i], situation);
	}

	if (mycar[index-1] != NULL) {
		delete mycar[index-1];
	}
	mycar[index-1] = new MyCar(myTrackDesc, car, situation);

	currenttime = situation->currentTime;
}


static void endrace(int index, tCarElt* car, tSituation *s)
{
	endStatistics(car, s);
}


static void drive(int index, tCarElt* car, tSituation *situation)
{
	tdble angle;
	tdble brake;
	tdble b1;
	tdble b2;
	tdble b3;
	tdble b4;
	tdble b5;
	tdble steer, targetAngle, shiftaccel;

	MyCar* myc = mycar[index-1];
	Pathfinder* mpf = myc->getPathfinderPtr();

	b1 = b2 = b3 = b4 = b5 = 0.0;
	shiftaccel = 0.0;

	myc->update(myTrackDesc, car, situation);

	if ( car->_dammage < myc->undamaged/3 && myc->bmode != myc->NORMAL) {
		myc->loadBehaviour(myc->NORMAL);
	} else if (car->_dammage > myc->undamaged/3 && car->_dammage < (myc->undamaged*2)/3 && myc->bmode != myc->CAREFUL) {
		myc->loadBehaviour(myc->CAREFUL);
	} else if (car->_dammage > (myc->undamaged*2)/3 && myc->bmode != myc->SLOW) {
		myc->loadBehaviour(myc->SLOW);
	}

	if (currenttime != situation->currentTime) {
		currenttime = situation->currentTime;
		for (int i = 0; i < situation->_ncars; i++) ocar[i].update();
	}

	if (myc->trtime < 5.0 && myc->bmode != myc->START) {
		myc->loadBehaviour(myc->START);
		myc->startmode = true;
	}
	if (myc->startmode && myc->trtime > 5.0) {
		myc->startmode = false;
		myc->loadBehaviour(myc->NORMAL);
	}

	mpf->plan(myc->getCurrentSegId(), car, situation, myc, ocar);

	memset(&car->ctrl, 0, sizeof(tCarCtrl));
	car->_gearCmd = car->_gear;

	if (myc->getCurrentSegId() >= 0 && myc->getCurrentSegId() < 5 && !myc->fuelchecked) {
		if (car->race.laps > 0) {
			myc->fuelperlap = MAX(myc->fuelperlap, (myc->lastfuel+myc->lastpitfuel-car->priv.fuel));
		}
		myc->lastfuel = car->priv.fuel;
		myc->lastpitfuel = 0.0;
		myc->fuelchecked = true;
	} else if (myc->getCurrentSegId() > 5) {
		myc->fuelchecked = false;
	}

	if (!mpf->getPitStop() && (car->_remainingLaps-car->_lapsBehindLeader) > 0 && (car->_dammage > myc->MAXDAMMAGE ||
		(car->priv.fuel < (myc->fuelperlap*(1.0+myc->FUEL_SAFETY_MARGIN)) &&
		 car->priv.fuel < (car->_remainingLaps-car->_lapsBehindLeader)*myc->fuelperlap)))
	{
		mpf->setPitStop(true, myc->getCurrentSegId());
	}

	if (mpf->getPitStop()) {
		car->_raceCmd = RM_CMD_PIT_ASKED;
	}

	targetAngle = atan2(myc->dynpath->getLoc(myc->destpathsegid)->y - car->_pos_Y, myc->dynpath->getLoc(myc->destpathsegid)->x - car->_pos_X);
	targetAngle -= car->_yaw;
	NORM_PI_PI(targetAngle);
    steer = targetAngle / car->_steerLock;

	if (!mpf->getPitStop()) {
		steer = steer + MIN(myc->STEER_P_CONTROLLER_MAX, myc->derror*myc->STEER_P_CONTROLLER_GAIN)*myc->getErrorSgn();
		if (fabs(steer) > 1.0) {
			steer/=fabs(steer);
		}
	} else {
		tdble dl, dw;
		RtDistToPit(car, myTrackDesc->getTorcsTrack(), &dl, &dw);
		if (dl < 1.0f) {
			b5 = 1.0f;
		}
	}

	double omega = myc->getSpeed()/myc->dynpath->getRadius(myc->currentpathsegid);
	steer += myc->STEER_D_CONTROLLER_GAIN*(omega - myc->getCarPtr()->_yaw_rate);

    tdble brakecoeff = 1.0/(2.0*g*myc->currentseg->getKfriction()*myc->CFRICTION);
    tdble brakespeed, brakedist;
	tdble lookahead = 0.0;
	int i = myc->getCurrentSegId();
	brake = 0.0;

	while (lookahead < brakecoeff * myc->getSpeedSqr()) {
		lookahead += myc->dynpath->getLength(i);
		brakespeed = myc->getSpeedSqr() - myc->dynpath->getSpeedsqr(i);
		if (brakespeed > 0.0) {
			tdble gm, qb, qs;
			gm = myTrackDesc->getSegmentPtr(myc->getCurrentSegId())->getKfriction()*myc->CFRICTION*myTrackDesc->getSegmentPtr(myc->getCurrentSegId())->getKalpha();
			qs = myc->dynpath->getSpeedsqr(i);

			brakedist = brakespeed*(myc->mass/(2.0*gm*g*myc->mass + qs*(gm*myc->ca + myc->cw)));

			if (brakedist > lookahead - myc->getWheelTrack()) {
				qb = brakespeed*brakecoeff/brakedist;
				if (qb > b2) {
					b2 = qb;
				}
			}
		}
		i = (i + 1 + mpf->getnPathSeg()) % mpf->getnPathSeg();
	}

	if (myc->getSpeedSqr() > myc->dynpath->getSpeedsqr(myc->currentpathsegid)) {
		b1 = (myc->getSpeedSqr() - myc->dynpath->getSpeedsqr(myc->currentpathsegid)) / (myc->getSpeedSqr());
	}

	if (myc->getDeltaPitch() > myc->MAXALLOWEDPITCH && myc->getSpeed() > myc->FLYSPEED) {
		b4 = 1.0;
	}

	if (myc->getSpeed() > myc->TURNSPEED && myc->tr_mode == 0) {
		if (myc->derror > myc->PATHERR) {
			vec2d *cd = myc->getDir();
			vec2d *pd = myc->dynpath->getDir(myc->currentpathsegid);
			float z = cd->x*pd->y - cd->y*pd->x;
			if (z*myc->getErrorSgn() >= 0.0) {
				targetAngle = atan2(myc->dynpath->getDir(myc->currentpathsegid)->y, myc->dynpath->getDir(myc->currentpathsegid)->x);
				targetAngle -= car->_yaw;
				NORM_PI_PI(targetAngle);
				double toborder = MAX(1.0, myc->currentseg->getWidth()/2.0 - fabs(myTrackDesc->distToMiddle(myc->getCurrentSegId(), myc->getCurrentPos())));
				b3 = (myc->getSpeed()/myc->STABLESPEED)*(myc->derror-myc->PATHERR)/toborder;
			}
		}
	}

	if (b1 > b2) brake = b1; else brake = b2;
	if (brake < b3) brake = b3;
	if (brake < b4) {
		brake = MIN(1.0, b4);
		tdble abs_mean;
		abs_mean = (car->_wheelSpinVel(REAR_LFT) + car->_wheelSpinVel(REAR_RGT))*car->_wheelRadius(REAR_LFT)/myc->getSpeed();
		abs_mean /= 2.0;
    	brake = brake * abs_mean;
	} else {
		brake = MIN(1.0, brake);
		tdble abs_min = 1.0;
		for (int i = 0; i < 4; i++) {
			tdble slip = car->_wheelSpinVel(i) * car->_wheelRadius(i) / myc->getSpeed();
			if (slip < abs_min) abs_min = slip;
		}
    	brake = brake * abs_min;
	}

	float weight = myc->mass*G;
	float maxForce = weight + myc->ca*myc->MAX_SPEED*myc->MAX_SPEED;
	float force = weight + myc->ca*myc->getSpeedSqr();
	brake = brake*MIN(1.0, force/maxForce);
	if (b5 > 0.0f) {
		brake = b5;
	}

	if (myc->tr_mode == 0) {
		if (car->_gear <= 0) {
			car->_gearCmd =  1;
		} else {
			float gr_up = car->_gearRatio[car->_gear + car->_gearOffset];
			float omega = car->_enginerpmRedLine/gr_up;
			float wr = car->_wheelRadius(2);

			if (omega*wr*myc->SHIFT < car->_speed_x) {
				car->_gearCmd++;
			} else {
				float gr_down = car->_gearRatio[car->_gear + car->_gearOffset - 1];
				omega = car->_enginerpmRedLine/gr_down;
				if (car->_gear > 1 && omega*wr*myc->SHIFT > car->_speed_x + myc->SHIFT_MARGIN) {
					car->_gearCmd--;
				}
			}
		}
	}

	tdble cerror, cerrorh;
	cerrorh = sqrt(car->_speed_x*car->_speed_x + car->_speed_y*car->_speed_y);
	if (cerrorh > myc->TURNSPEED) {
		cerror = fabs(car->_speed_x)/cerrorh;
	} else {
		cerror = 1.0;
	}

	if (myc->tr_mode == 0) {
		if (brake > 0.0) {
			myc->accel = 0.0;
			car->_accelCmd = myc->accel;
			car->_brakeCmd = brake*cerror;
		} else {
			if (myc->getSpeedSqr() < myc->dynpath->getSpeedsqr(myc->getCurrentSegId())) {
				if (myc->accel < myc->ACCELLIMIT) {
					myc->accel += myc->ACCELINC;
				}
				car->_accelCmd = myc->accel/cerror;
			} else {
				if (myc->accel > 0.0) {
					myc->accel -= myc->ACCELINC;
				}
				car->_accelCmd = myc->accel = MIN(myc->accel/cerror, shiftaccel/cerror);
			}
			tdble slipspeed = myc->querySlipSpeed(car);
			if (slipspeed > myc->TCL_SLIP) {
				car->_accelCmd = car->_accelCmd - MIN(car->_accelCmd, (slipspeed - myc->TCL_SLIP)/myc->TCL_RANGE);
			}
		}
	}

	tdble bx = myc->getDir()->x, by = myc->getDir()->y;
	tdble cx = myc->currentseg->getMiddle()->x - car->_pos_X, cy = myc->currentseg->getMiddle()->y - car->_pos_Y;
	tdble parallel = (cx*bx + cy*by) / (sqrt(cx*cx + cy*cy)*sqrt(bx*bx + by*by));

	if ((myc->getSpeed() < myc->TURNSPEED) && (parallel < cos(90.0*PI/180.0)) && (mpf->dist2D(myc->getCurrentPos(), myc->dynpath->getLoc(myc->getCurrentSegId())) > myc->TURNTOL)) {
		myc->turnaround += situation->deltaTime;
	} else myc->turnaround = 0.0;
	if ((myc->turnaround >= waitToTurn) || (myc->tr_mode >= 1)) {
		if (myc->tr_mode == 0) {
			myc->tr_mode = 1;
		}
        if ((car->_gearCmd > -1) && (myc->tr_mode < 2)) {
			car->_accelCmd = 0.0;
			if (myc->tr_mode == 1) {
				car->_gearCmd--;
			}
			car->_brakeCmd = 1.0;
		} else {
			myc->tr_mode = 2;
			if (parallel < cos(90.0*PI/180.0) && (mpf->dist2D(myc->getCurrentPos(), myc->dynpath->getLoc(myc->getCurrentSegId())) > myc->TURNTOL)) {
				angle = queryAngleToTrack(car);
				car->_steerCmd = ( -angle > 0.0) ? 1.0 : -1.0;
				car->_brakeCmd = 0.0;

				if (myc->accel < 1.0) {
					myc->accel += myc->ACCELINC;
				}
				car->_accelCmd = myc->accel;
				tdble slipspeed = myc->querySlipSpeed(car);
				if (slipspeed < -myc->TCL_SLIP) {
					car->_accelCmd = car->_accelCmd - MIN(car->_accelCmd, (myc->TCL_SLIP - slipspeed)/myc->TCL_RANGE);
				}
			} else {
				if (myc->getSpeed() < 1.0) {
					myc->turnaround = 0;
					myc->tr_mode = 0;
					myc->loadBehaviour(myc->START);
					myc->startmode = true;
					myc->trtime = 0.0;
				}
				car->_brakeCmd = 1.0;
				car->_steerCmd = 0.0;
				car->_accelCmd = 0.0;
			}
		}
	}

	if (myc->tr_mode == 0) car->_steerCmd = steer;
	car->_clutchCmd = getClutch(myc, car);

	// ── Per-frame logging (mirrors human.cpp common_drive) ────────────────────
	titaLogTrackPosition(car, situation);
	titaLogSegmentPosition(car, situation);
	titaLogSpeed(car, situation);
	handleLapLogging(index, car, situation);
}


static int pitcmd(int index, tCarElt* car, tSituation *s)
{
	MyCar* myc = mycar[index-1];
	Pathfinder* mpf = myc->getPathfinderPtr();

	float fullracedist = (myTrackDesc->getTorcsTrack()->length*s->_totLaps);
	float remaininglaps = (fullracedist - car->_distRaced)/myTrackDesc->getTorcsTrack()->length;

	car->_pitFuel = MAX(MIN(myc->fuelperlap*(remaininglaps+myc->FUEL_SAFETY_MARGIN) - car->_fuel, car->_tank - car->_fuel), 0.0);
	myc->lastpitfuel = MAX(car->_pitFuel, 0.0);
	car->_pitRepair = car->_dammage;
	mpf->setPitStop(false, myc->getCurrentSegId());
	myc->loadBehaviour(myc->START);
	myc->startmode = true;
	myc->trtime = 0.0;

	return ROB_PIT_IM;
}


float getClutch(MyCar* myc, tCarElt* car)
{
	if (car->_gear > 1) {
		myc->clutchtime = 0.0;
		return 0.0;
	} else {
		float drpm = car->_enginerpm - car->_enginerpmRedLine/2.0;
		myc->clutchtime = MIN(myc->CLUTCH_FULL_MAX_TIME, myc->clutchtime);
		float clutcht = (myc->CLUTCH_FULL_MAX_TIME - myc->clutchtime)/myc->CLUTCH_FULL_MAX_TIME;
		if (car->_gear == 1 && car->_accelCmd > 0.0) {
			myc->clutchtime += (float) RCM_MAX_DT_ROBOTS;
		}

		if (drpm > 0) {
			float speedr;
			if (car->_gearCmd == 1) {
				float omega = car->_enginerpmRedLine/car->_gearRatio[car->_gear + car->_gearOffset];
				float wr = car->_wheelRadius(2);
				speedr = (myc->CLUTCH_SPEED + MAX(0.0, car->_speed_x))/fabs(wr*omega);
				float clutchr = MAX(0.0, (1.0 - speedr*2.0*drpm/car->_enginerpmRedLine));
				return MIN(clutcht, clutchr);
			} else {
				myc->clutchtime = 0.0;
				return 0.0;
			}
		} else {
			return clutcht;
		}
	}
}