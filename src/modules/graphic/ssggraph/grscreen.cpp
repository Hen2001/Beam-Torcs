/***************************************************************************

    file        : grscreen.cpp
    created     : Thu May 15 22:11:03 CEST 2003
    copyright   : (C) 2003 by Eric Espi�                       
    email       : eric.espie@torcs.org   
    version     : $Id: grscreen.cpp,v 1.22.2.5 2012/06/09 11:44:46 berniw Exp $

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
    		
    @author	<a href=mailto:eric.espie@torcs.org>Eric Espie</a>
    @version	$Id: grscreen.cpp,v 1.22.2.5 2012/06/09 11:44:46 berniw Exp $
*/

#include <plib/ssg.h>
#include <ctime>
#include <tgfclient.h>
#include <portability.h>
#include "grutil.h"
#include "grmain.h"
#include "grscene.h"
#include "grshadow.h"
#include "grskidmarks.h"
#include "grcar.h"
#include "grboard.h"
#include "grcarlight.h"
#include <tgfclient.h>
#include "grscreen.h"
#include <GL/glut.h>
#include <string>

std::string chatbotMessage = "Waiting for AI...";
std::string aiCommentary = "AI Commentary Loading...";
std::string aiCoaching = "AI Coaching Loading...";
int telemetryHudEnabled = 1;   // default ON
// --- Segment timing state ---
static const char* SEGMENT_NAMES[10] = {
    "First Straight",        // 0: seg < 40
    "Hairpin",  // 1: seg < 100
    "Corner 2",        // 2: seg < 175
    "Corner 3",          // 3: seg < 235
    "Long Left",     // 4: seg < 310
    "Back Straight",         // 5: seg < 390
    "The Corkscrew", // 6: seg < 500
    "Kink",           // 7: seg < 540
    "Final Straight",   // 8: seg < 605
};

static int    seg_lastMacro     = -1;
static int    seg_lastLap       = -1;
static double seg_startTime     = 0.0;
static int    seg_lastFinished  = -1;
static double seg_lastTime      = 0.0;

// Per-segment time for the previous lap (index = segment number)
static double seg_prevLapTimes[10] = {0.0};
// Per-segment time being accumulated for the current lap
static double seg_currentLapTimes[10] = {0.0};

bool coach = 1;
bool commentary = 0;

cGrScreen::cGrScreen(int myid)
{
	id = myid;
	curCar = NULL;
	curCam = NULL;
	mirrorCam = NULL;
	dispCam = NULL;
	boardCam = NULL;
	bgCam = NULL;
	board = NULL;
	curCamHead = 0;
	drawCurrent = 0;
	active = 0;
	selectNextFlag = 0;
	selectPrevFlag = 0;
	mirrorFlag = 1;
	memset(cams, 0, sizeof(cams));
	viewRatio = 1.33;
	cars = 0;
}

cGrScreen::~cGrScreen()
{
	unsigned int i;
	class cGrCamera *cam;
	
	for (i = 0; i < sizeof(cams)/sizeof(cams[0]); i++) {
		while ((cam =  GF_TAILQ_FIRST(&cams[i])) != 0) {
			cam->remove(&cams[i]);
			delete cam;
		}
	}
	
	delete boardCam;
	delete mirrorCam;
	delete bgCam;
	
	if (board != NULL) {
		board->shutdown ();
	}
	
	FREEZ(cars);
	
	if (board != NULL) {
		delete board;
		board = NULL;
	}
}

int cGrScreen::isInScreen(int x, int y)
{
	if (!active) {
		return 0;
	}
	
	if ((x >= scrx) &&
		(y >= scry) &&
		(x < (scrx + scrw)) &&
		(y < (scry + scrh)))
	{
		return 1;
	}
	
	return 0;
}

void cGrScreen::setCurrentCar(tCarElt *newCurCar)
{
	curCar = newCurCar;
}

/* Set Screen size & position */
void cGrScreen::activate(int x, int y, int w, int h)
{
	viewRatio = (float)w / (float)h;
	
	scrx = x;
	scry = y;
	scrw = w;
	scrh = h;
	
	if (mirrorCam) {
		mirrorCam->setViewport (scrx, scry, scrw, scrh);
		mirrorCam->setPos (scrx + scrw / 4, scry +  5 * scrh / 6 - scrh / 10, scrw / 2, scrh / 6);
	}
	if (curCam) {
		curCam->limitFov ();
		curCam->setZoom (GR_ZOOM_DFLT);
	}
	active = 1;
}

void cGrScreen::desactivate(void)
{
	active = 0;
}

/* Set camera zoom value */
void cGrScreen::setZoom(long zoom)
{
	curCam->setZoom(zoom);
}

void cGrScreen::selectNextCar(void)
{
	selectNextFlag = 1;
}

void cGrScreen::selectPrevCar(void)
{
	selectPrevFlag = 1;
}

void cGrScreen::selectBoard(long brd)
{
	board->selectBoard(brd);
}

void cGrScreen::selectTrackMap()
{
	const int BUFSIZE=1024;
	char path[BUFSIZE];
	char path2[BUFSIZE];
	int viewmode;
	
	board->getTrackMap()->selectTrackMap();
	viewmode = board->getTrackMap()->getViewMode();
	
	snprintf(path, BUFSIZE, "%s/%d", GR_SCT_DISPMODE, id);
	GfParmSetNum(grHandle, path, GR_ATT_MAP, NULL, (tdble)viewmode);
	/* save also as user's preference if human */
	if (curCar->_driverType == RM_DRV_HUMAN) {
		snprintf(path2, BUFSIZE, "%s/%s", GR_SCT_DISPMODE, curCar->_name);
		GfParmSetNum(grHandle, path2, GR_ATT_MAP, NULL, (tdble)viewmode);
	}
	GfParmWriteFile(NULL, grHandle, "Graph");
}

void cGrScreen::switchMirror(void)
{
	const int BUFSIZE=1024;
	char path[BUFSIZE];
	char path2[BUFSIZE];

	mirrorFlag = 1 - mirrorFlag;
	snprintf(path, BUFSIZE, "%s/%d", GR_SCT_DISPMODE, id);
	GfParmSetNum(grHandle, path, GR_ATT_MIRROR, NULL, (tdble)mirrorFlag);
	/* save also as user's preference if human */
	if (curCar->_driverType == RM_DRV_HUMAN) {
		snprintf(path2, BUFSIZE, "%s/%s", GR_SCT_DISPMODE, curCar->_name);
		GfParmSetNum(grHandle, path, GR_ATT_MIRROR, NULL, (tdble)mirrorFlag);
	}
	GfParmWriteFile(NULL, grHandle, "Graph");
}


/* Select the camera by number */
void cGrScreen::selectCamera(long cam)
{
	const int BUFSIZE=1024;
	char buf[BUFSIZE];
	char path[BUFSIZE];
	char path2[BUFSIZE];
	
	
	if (cam == curCamHead) {
		/* Same camera list, choose the next one */
		curCam = curCam->next();
		if (curCam == (cGrPerspCamera*)GF_TAILQ_END(&cams[cam])) {
			curCam = (cGrPerspCamera*)GF_TAILQ_FIRST(&cams[cam]);
		}
	} else {
		/* Change of camera list, take the first one */
		curCamHead = cam;
		curCam = (cGrPerspCamera*)GF_TAILQ_FIRST(&cams[cam]);
	}

	if (curCam == NULL) {
		/* back to default camera */
		curCamHead = 0;
		curCam = (cGrPerspCamera*)GF_TAILQ_FIRST(&cams[curCamHead]);
	}

	snprintf(path, BUFSIZE, "%s/%d", GR_SCT_DISPMODE, id);
	GfParmSetStr(grHandle, path, GR_ATT_CUR_DRV, curCar->_name);
	GfParmSetNum(grHandle, path, GR_ATT_CAM, (char*)NULL, (tdble)curCam->getId());
	GfParmSetNum(grHandle, path, GR_ATT_CAM_HEAD, (char*)NULL, (tdble)curCamHead);
	
	/* save also as user's preference if human */
	if (curCar->_driverType == RM_DRV_HUMAN) {
		snprintf(path2, BUFSIZE, "%s/%s", GR_SCT_DISPMODE, curCar->_name);
		GfParmSetNum(grHandle, path2, GR_ATT_CAM, (char*)NULL, (tdble)curCam->getId());
		GfParmSetNum(grHandle, path2, GR_ATT_CAM_HEAD, (char*)NULL, (tdble)curCamHead);
	}
	
	snprintf(buf, BUFSIZE, "%s-%d-%d", GR_ATT_FOVY, curCamHead, curCam->getId());
	curCam->loadDefaults(buf);
	drawCurrent = curCam->getDrawCurrent();
	curCam->limitFov ();
	GfParmWriteFile(NULL, grHandle, "Graph");
}

static class cGrPerspCamera *ThedispCam;	/* the display camera */

static int
comparCars(const void *car1, const void *car2)
{
	float d1 = ThedispCam->getDist2(*(tCarElt**)car1);
	float d2 = ThedispCam->getDist2(*(tCarElt**)car2);
	
	if (d1 > d2) {
		return -1;
	} else {
		return 1;
	}
}

void cGrScreen::camDraw(tSituation *s)
{
	int i;
	
	glDisable(GL_COLOR_MATERIAL);
	
	START_PROFILE("dispCam->update*");
	dispCam->update(curCar, s);
	STOP_PROFILE("dispCam->update*");
	
	if (dispCam->getDrawBackground()) {
		glDisable(GL_LIGHTING);
		glDisable(GL_DEPTH_TEST);
		grDrawBackground(dispCam, bgCam);
		glClear(GL_DEPTH_BUFFER_BIT);
	}
	glEnable(GL_DEPTH_TEST);
	
	START_PROFILE("dispCam->action*");
	dispCam->action();
	STOP_PROFILE("dispCam->action*");
	
	START_PROFILE("grDrawCar*");
	glFogf(GL_FOG_START, dispCam->getFogStart());
	glFogf(GL_FOG_END, dispCam->getFogEnd());
	glEnable(GL_FOG);
	
	/*sort the cars by distance for transparent windows */
	ThedispCam = dispCam;
	qsort(cars, s->_ncars, sizeof(tCarElt*), comparCars);
	
	for (i = 0; i < s->_ncars; i++) {
		grDrawCar(cars[i], curCar, dispCam->getDrawCurrent(), dispCam->getDrawDriver(), s->currentTime, dispCam);
	} 
	STOP_PROFILE("grDrawCar*");
	
	START_PROFILE("grDrawScene*");
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	grDrawScene();
	STOP_PROFILE("grDrawScene*");
}

void setTelemetryHud(int enabled)
{
    telemetryHudEnabled = enabled;

    GfParmSetNum(grHandle,
                 GR_SCT_GRAPHIC,
                 "TelemetryHUD",
                 NULL,
                 (tdble)enabled);

    GfParmWriteFile(NULL, grHandle, "Graph");
}

static int getMacroSegment(int segId) {
    if (segId < 40)  return 0;
    if (segId < 100) return 1;
    if (segId < 175) return 2;
    if (segId < 235) return 3;
    if (segId < 310) return 4;
    if (segId < 390) return 5;
    if (segId < 500) return 6;
    if (segId < 540) return 7;
    if (segId < 605) return 8;
    return 9;
}

void updateTelemetryMessage(tCarElt* car, tSituation* s)
{
    char line1[128];
    char line2[128];

    int segId  = car->_trkPos.seg->id;
    int macro  = getMacroSegment(segId);
    int lap    = car->_laps;
    double now = s->currentTime;

    // On lap change: promote current lap times to previous, then reset
    if (lap != seg_lastLap) {
        if (seg_lastLap != -1) {
            for (int i = 0; i < 10; i++) {
                if (seg_currentLapTimes[i] > 0.0)
                    seg_prevLapTimes[i] = seg_currentLapTimes[i];
            }
        }
        memset(seg_currentLapTimes, 0, sizeof(seg_currentLapTimes));
        seg_lastLap      = lap;
        seg_lastMacro    = -1;
        seg_lastFinished = -1;
    }

    // Crossed into a new macro segment
    if (macro != seg_lastMacro) {
        if (seg_lastMacro != -1) {
            seg_lastTime                       = now - seg_startTime;
            seg_currentLapTimes[seg_lastMacro] = seg_lastTime;
            seg_lastFinished                   = seg_lastMacro;
        }
        seg_startTime = now;
        seg_lastMacro = macro;
    }

    double elapsed = now - seg_startTime;
    const char* segName = SEGMENT_NAMES[macro];

    // Line 1: current segment and live elapsed time
    snprintf(line1, sizeof(line1), "%s | %.2fs", segName, elapsed);

    // Line 2: delta vs previous lap for the last completed segment
    if (seg_lastFinished != -1) {
        double prev = seg_prevLapTimes[seg_lastFinished];
        if (prev > 0.0) {
            double delta = seg_lastTime - prev;
            const char* sign = (delta <= 0.0) ? "" : "+";
            snprintf(line2, sizeof(line2), "Prev: %s %.2fs (%s%.2fs)",
                     SEGMENT_NAMES[seg_lastFinished], seg_lastTime, sign, delta);
        } else {
            // No previous lap data yet for this segment
            snprintf(line2, sizeof(line2), "Prev: %s %.2fs (no ref)",
                     SEGMENT_NAMES[seg_lastFinished], seg_lastTime);
        }
    } else {
        snprintf(line2, sizeof(line2), "Waiting for first split...");
    }

    chatbotMessage = line1 + std::string("\n") + line2;
}

void drawBitmapText(const char *text, float x, float y)
{
    glRasterPos2f(x, y);
    while (*text) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *text);
        text++;
    }
}

void drawChatPanel()
{
    float left   = 0.0f;
    float bottom = 20.0f;
    float width  = 380.0f;
    float height = 55.0f;          // taller to fit two lines

    float right  = left + width;
    float top    = bottom + height;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.2f, 0.2f, 0.2f, 0.5f);
    glBegin(GL_QUADS);
        glVertex2f(left - 2,  bottom - 2);
        glVertex2f(right + 2, bottom - 2);
        glVertex2f(right + 2, top + 2);
        glVertex2f(left - 2,  top + 2);
    glEnd();

    glColor4f(0.0f, 0.0f, 0.0f, 0.35f);
    glBegin(GL_QUADS);
        glVertex2f(left,  bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left,  top);
    glEnd();

    glDisable(GL_BLEND);

    glColor3f(1.0f, 1.0f, 1.0f);

    // Split chatbotMessage on \n and draw each line separately
    std::string msg = chatbotMessage;
    size_t nl = msg.find('\n');
    if (nl != std::string::npos) {
        drawBitmapText(msg.substr(0, nl).c_str(),  left + 8, bottom + height - 18);
        drawBitmapText(msg.substr(nl + 1).c_str(), left + 8, bottom + height - 34);
    } else {
        drawBitmapText(msg.c_str(), left + 8, bottom + height - 18);
    }
}


void LiveCommentary()
{
    static double lastCheck = 0.0;
    double now = time(nullptr);
    if (now - lastCheck < 0.5) return;
    lastCheck = now;

    FILE* f = fopen("/tmp/live_commentary.txt", "r");
    if (!f) return;

    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        aiCommentary = std::string(buf);  
    }
    fclose(f);
}
void drawCommentaryBox()
{
    float left   = 250.0f;
    float width  = 600.0f;
    float height = 30.0f;
    float top    = 598.0f;   // near top of screen
    float bottom = top - height;
    float right  = left + width;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Dark red background for commentary feel
    glColor4f(0.4f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(left,  bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left,  top);
    glEnd();

    glDisable(GL_BLEND);

    glColor3f(1.0f, 1.0f, 0.2f);  // yellow text
    drawBitmapText(aiCommentary.c_str(), left + 10, bottom + 10);
}

void LiveCoaching()
{
    static double lastCheck = 0.0;
    double now = time(nullptr);
    if (now - lastCheck < 0.5) return;
    lastCheck = now;

    std::string path = std::string(getenv("HOME")) + "/.torcs/live_coaching.txt";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;

    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        aiCoaching = std::string(buf);
    }
    fclose(f);
}
void drawCoachingBox()
{
    float left   = 250.0f;
    float width  = 600.0f;
    float height = 30.0f;
    float top    = 598.0f;   // near top of screen
    float bottom = top - height;
    float right  = left + width;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Dark red background for commentary feel
    glColor4f(0.4f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(left,  bottom);
        glVertex2f(right, bottom);
        glVertex2f(right, top);
        glVertex2f(left,  top);
    glEnd();

    glDisable(GL_BLEND);

    glColor3f(1.0f, 1.0f, 0.2f);  // yellow text
    drawBitmapText(aiCoaching.c_str(), left + 10, bottom + 10);
}
/* Update screen display */
void cGrScreen::update(tSituation *s, float Fps)
{
	int i;
	//ssgLight *light;
	int carChanged;
	const int BUFSIZE=1024;
	char buf[BUFSIZE];	

	if (!active) {
		return;
	}
	
	carChanged = 0;
	if (selectNextFlag) {
		for (i = 0; i < (s->_ncars - 1); i++) {
			if (curCar == s->cars[i]) {
				curCar = s->cars[i + 1];
				curCar->priv.collision = 0;
				carChanged = 1;
				break;
			}
		}
		selectNextFlag = 0;
	}

	if (selectPrevFlag) {
		for (i = 1; i < s->_ncars; i++) {
			if (curCar == s->cars[i]) {
				curCar = s->cars[i - 1];
				curCar->priv.collision = 0;
				carChanged = 1;
				break;
			}
		}
		selectPrevFlag = 0;
	}
	if (carChanged) {
		snprintf(buf, BUFSIZE, "%s/%d", GR_SCT_DISPMODE, id);
		GfParmSetStr(grHandle, buf, GR_ATT_CUR_DRV, curCar->_name);
		loadParams (s);
		GfParmWriteFile(NULL, grHandle, "Graph");
		curCam->onSelect(curCar, s);
	}
	
	//light = ssgGetLight (0);
	
	/* MIRROR */
	if (mirrorFlag && curCam->isMirrorAllowed ()) {
		mirrorCam->activateViewport ();
		dispCam = mirrorCam;
		glClear (GL_DEPTH_BUFFER_BIT);
		camDraw (s);
		mirrorCam->store ();
	}
	
	glViewport(scrx, scry, scrw, scrh);
	dispCam = curCam;
	camDraw(s);
	
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);    
	glDisable(GL_COLOR_MATERIAL);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glEnable(GL_TEXTURE_2D);
	
	/* MIRROR */
	if (mirrorFlag && curCam->isMirrorAllowed ()) {
		mirrorCam->display ();
		glViewport (scrx, scry, scrw, scrh);
	}
	
	START_PROFILE("boardCam*");
	boardCam->action();
	STOP_PROFILE("boardCam*");
	
	START_PROFILE("grDisp**");
	glDisable(GL_TEXTURE_2D);
	
	TRACE_GL("cGrScreen::update glDisable(GL_DEPTH_TEST)");
	board->refreshBoard(s, Fps, 0, curCar);
	if (telemetryHudEnabled) {
		if (commentary){
			LiveCommentary();
		}
		if (coach){
			LiveCoaching();
		}
    	updateTelemetryMessage(curCar, s);
    	drawChatPanel();
		if (commentary){
			drawCommentaryBox(); 
		}
		if (coach){
			drawCoachingBox();
		}
	}

	TRACE_GL("cGrScreen::update display boards");
	
	STOP_PROFILE("grDisp**");
}

void cGrScreen::loadParams(tSituation *s)
{
	int camNum;
	int i;
	class cGrCamera *cam;
	const char *carName;
	const int BUFSIZE=1024;
	char buf[BUFSIZE];	
	char path[BUFSIZE];	
	char path2[BUFSIZE];	
	
	snprintf(path, BUFSIZE, "%s/%d", GR_SCT_DISPMODE, id);
	
	if (!curCar) {
		carName = GfParmGetStr(grHandle, path, GR_ATT_CUR_DRV, "");
		for (i = 0; i < s->_ncars; i++) {
			if (!strcmp(s->cars[i]->_name, carName)) {
				break;
			}
		}
		if (i < s->_ncars) {
			curCar = s->cars[i];
		} else if (id < s->_ncars) {
			curCar = s->cars[id];
		} else {
			curCar = s->cars[0];
		}
	}
	snprintf(path2, BUFSIZE, "%s/%s", GR_SCT_DISPMODE, curCar->_name);
	
	curCamHead	= (int)GfParmGetNum(grHandle, path, GR_ATT_CAM_HEAD, NULL, 9);
	camNum	= (int)GfParmGetNum(grHandle, path, GR_ATT_CAM, NULL, 0);
	mirrorFlag	= (int)GfParmGetNum(grHandle, path, GR_ATT_MIRROR, NULL, (tdble)mirrorFlag);
	curCamHead	= (int)GfParmGetNum(grHandle, path2, GR_ATT_CAM_HEAD, NULL, (tdble)curCamHead);
	camNum	= (int)GfParmGetNum(grHandle, path2, GR_ATT_CAM, NULL, (tdble)camNum);
	mirrorFlag	= (int)GfParmGetNum(grHandle, path2, GR_ATT_MIRROR, NULL, (tdble)mirrorFlag);
	
	cam = GF_TAILQ_FIRST(&cams[curCamHead]);
	curCam = NULL;
	while (cam) {
		if (cam->getId() == camNum) {
			curCam = (cGrPerspCamera*)cam;
			break;
		}
		cam = cam->next();
	}

	if (curCam == NULL) {
		// back to default camera
		curCamHead = 0;
		curCam = (cGrPerspCamera*)GF_TAILQ_FIRST(&cams[curCamHead]);
		GfParmSetNum(grHandle, path, GR_ATT_CAM, NULL, (tdble)curCam->getId());
		GfParmSetNum(grHandle, path, GR_ATT_CAM_HEAD, NULL, (tdble)curCamHead);
	}
	
	snprintf(buf, BUFSIZE, "%s-%d-%d", GR_ATT_FOVY, curCamHead, curCam->getId());
	curCam->loadDefaults(buf);
	drawCurrent = curCam->getDrawCurrent();
	board->loadDefaults(curCar);
	telemetryHudEnabled =
    (int)GfParmGetNum(grHandle,
                      GR_SCT_GRAPHIC,
                      "TelemetryHUD",
                      NULL,
                      1);

}


/* Create cameras */
void cGrScreen::initCams(tSituation *s)
{
	tdble fovFactor;
	int i;
	
	fovFactor = GfParmGetNum(grHandle, GR_SCT_GRAPHIC, GR_ATT_FOVFACT, (char*)NULL, 1.0);
	fovFactor *= GfParmGetNum(grTrackHandle, TRK_SECT_GRAPH, TRK_ATT_FOVFACT, (char*)NULL, 1.0);
	
	if (boardCam == NULL) {
		boardCam = new cGrOrthoCamera(this,0, grWinw*600/grWinh, 0, 600);
	}
	
	if (bgCam == NULL) {
		bgCam = new cGrBackgroundCam(this);
	}
	
	if (mirrorCam == NULL) {
		mirrorCam = new cGrCarCamMirror(
			this,
			-1,
			0,					// drawCurr
			1,					// drawBG
			90.0,				// fovy
			0.0,				// fovymin
			360.0,				// fovymax
			0.3,				// near
			300.0 * fovFactor,	// far
			200.0 * fovFactor,	// fog1
			300.0 * fovFactor	// fog
		);
	}
	
	// Scene Cameras
	unsigned int j;
	class cGrCamera *cam;
	for (j = 0; j < sizeof(cams)/sizeof(cams[0]); j++) {
		while ((cam = GF_TAILQ_FIRST(&cams[j])) != 0) {
			cam->remove(&cams[j]);
			delete cam;
		}
	}
	memset(cams, 0, sizeof(cams));
	
	grCamCreateSceneCameraList(this, cams, fovFactor);
	
	cars = (tCarElt**)calloc(s->_ncars, sizeof (tCarElt*));
	for (i = 0; i < s->_ncars; i++) {
		cars[i] = s->cars[i];
	}
	
	loadParams(s);
}

void cGrScreen::initBoard(void)
{
	if (board == NULL) {
		board = new cGrBoard (id);
	}
	board->initBoard ();
}
