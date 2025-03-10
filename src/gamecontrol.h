/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gamecontrol.h High level game control functions. */

#ifndef GAMECONTROL_H
#define GAMECONTROL_H

void StartNewGame();
void ShutdownGame();

void OnNewDay();
void OnNewMonth();
void OnNewYear();
void OnNewFrame(uint32 frame_delay);

#endif

