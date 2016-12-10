/*
 * This is the standard shared library interface file for external hardware
 * Copyright (C) 2016 Sylvain AZARIAN <sylvain.azarian@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef EXTERNAL_HARDWARE_DEF_H
#define EXTERNAL_HARDWARE_DEF_H

#include <stdint.h>
#include <sys/types.h>
#ifdef _WINDOWS
#ifndef CALLPREFIX
    #define CALLPREFIX _stdcall
#endif
#else
    #define CALLPREFIX
#endif

// call this function to log something into the SDRNode central log file
// call is log( UUID, severity, msg)
typedef int   (CALLPREFIX _tlogFun)(char *, int, char *);

// call this function to push samples to the SDRNode
// call is pushSamples( UUID, ptr to float array of samples, sample count, channel count )
typedef int   (CALLPREFIX  _pushSamplesFun)( char *, float *, int, int);

// global for all devices
typedef int   (CALLPREFIX _initLibrary)(char *json_init_params, _tlogFun*, _pushSamplesFun*);
typedef int   (CALLPREFIX _setBoardUUID)(int, char *); // device, uuid
typedef int   (CALLPREFIX _getBoardCount)();
typedef char* (CALLPREFIX _getHardwareName)(int); // device

typedef int   (CALLPREFIX _getPossibleSampleRateCount)(int); // device
typedef unsigned int   (CALLPREFIX _getPossibleSampleRateValue)(int,int); // device, rank
typedef unsigned int   (CALLPREFIX _getPrefferedSampleRateValue)(int); // device

// Gain management
typedef int    (CALLPREFIX  _getRxGainStageCount)(int); // device
typedef const char* (CALLPREFIX _getRxGainStageName)( int, int ); //device, gain stage
typedef const char* (CALLPREFIX _getRxGainStageUnitName)( int , int); //device, gain stage
typedef int     (CALLPREFIX _getRxGainStageType)(int, int); //device, gain stage
typedef float  (CALLPREFIX _getMinGainValue)(int, int); //device, gain stage
typedef float  (CALLPREFIX _getMaxGainValue)(int, int); //device, gain stage
typedef int    (CALLPREFIX _getGainDiscreteValuesCount)(int, int); //device, gain stage
typedef float  (CALLPREFIX _getGainDiscreteValue)(int, int, int); //device, gain stage, rank
typedef uint64_t (CALLPREFIX _getMin_HWRx_CenterFreq)(int); // device
typedef uint64_t (CALLPREFIX _getMax_HWRx_CenterFreq)(int); // device


typedef char* (CALLPREFIX _getSerialNumber)( int ); // device
typedef int   (CALLPREFIX _prepareRXEngine)(int); // device
typedef int   (CALLPREFIX _finalizeRXEngine)(int); // device
typedef int   (CALLPREFIX _setRxSampleRate)(int,unsigned int); // device, sample rate
typedef int   (CALLPREFIX _getActualRxSampleRate)(int); // device


typedef int   (CALLPREFIX _setRxCenterFreq)(int,int64_t); // device, center frequency
typedef uint64_t (CALLPREFIX _getRxCenterFreq)(int); // device


typedef int    (CALLPREFIX _setRxGain)(int,int,float); // device, stage, value
typedef float  (CALLPREFIX _getRxGainValue)(int,int); // device, stage
typedef bool   (CALLPREFIX _setAutoGainMode)(int); // device


#endif // EXTERNAL_HARDWARE_DEF_H
