/*
#==========================================================================================
# + + +   This Software is released under the "Simplified BSD License"  + + +
# Copyright 2016 SDR Technologies SAS and F4GKR Sylvain AZARIAN . All rights reserved.
#
#Redistribution and use in source and binary forms, with or without modification, are
#permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice, this list of
#	  conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright notice, this list
#	  of conditions and the following disclaimer in the documentation and/or other materials
#	  provided with the distribution.
#
#THIS SOFTWARE IS PROVIDED BY Sylvain AZARIAN F4GKR ``AS IS'' AND ANY EXPRESS OR IMPLIED
#WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
#FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Sylvain AZARIAN OR
#CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
#ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#The views and conclusions contained in the software and documentation are those of the
#authors and should not be interpreted as representing official policies, either expressed
#or implied, of Sylvain AZARIAN F4GKR.
#
# Adds AirSpy capability to SDRNode
#==========================================================================================
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <QString>
#include <QSettings>
#include <QLibrary>
#include <QDebug>

#include "entrypoint.h"
#include "mir_sdr.h"

#define RSP_UNINIT (0)
#define RSP_RSPI (1)
#define RSP_RSPII (2)

#define DEBUG_DRIVER (0)
#define MAX_RSP_BOARDS (8)

#define SDRPLAY_AM_MIN     150e3
#define SDRPLAY_AM_MAX      30e6
#define SDRPLAY_FM_MIN      64e6
#define SDRPLAY_FM_MAX     108e6
#define SDRPLAY_B3_MIN     162e6
#define SDRPLAY_B3_MAX     240e6
#define SDRPLAY_B45_MIN    470e6
#define SDRPLAY_B45_MAX    960e6
#define SDRPLAY_L_MIN     1450e6
#define SDRPLAY_L_MAX     1675e6

#define GainTypeContinuous (0)
#define GainTypeOnOff (2)

char *driver_name ;

struct RSP_Params {
    mir_sdr_Bw_MHzT bwType;
    int sampling_rate ;
    int decim_factor ;
    int queue_size ;
};

struct t_sample_rates {
    unsigned int *sample_rates ;
    struct RSP_Params *rsp_params ;

    int enum_length ;
    int preffered_sr_index ;

};


typedef struct __attribute__ ((__packed__)) _sCplx
{
    float re;
    float im;
} TYPECPX;

#define QUEUE_SIZE (65536*4)

// this structure stores the device state
struct t_rx_device {
    int device_type ;
    char *device_name ;
    unsigned int idx ;
    char *device_serial_number ;

    struct t_sample_rates* rates;


    qint64 hw_frequency ;
    int gRdB;
    double gain_dB;
    double fsHz;
    double rfHz;
    int decimation ;
    mir_sdr_Bw_MHzT bwType;
    mir_sdr_If_kHzT ifType;
    mir_sdr_AgcControlT agcControl;
    mir_sdr_ReasonForReinitT reinitReson;

    int maxGain;
    int samplesPerPacket;
    int minGain;
    int dcMode;
    int agcSetPoint;
    int gRdBsystem;
    int lnaEnable;
    qint64 min_frq_hz ;
    qint64 max_frq_hz ;

    char *uuid ;
    bool running ;

    //
    TYPECPX *samples_block ;
    unsigned int wr_pos ;
    int queue_size  ;

    // for DC removal
    TYPECPX xn_1 ;
    TYPECPX yn_1 ;

    struct ext_Context context ;
    int stage_count ;
    char **stage_name ;
    char **stage_unit ;
    int *stage_type ;
};

unsigned int device_count ;
mir_sdr_DeviceT devices[MAX_RSP_BOARDS] ;
struct t_rx_device *rx;


mir_sdr_ApiVersion_t call_mir_sdr_ApiVersion ;
mir_sdr_DebugEnable_t call_mir_sdr_DebugEnable ;
mir_sdr_GetDevices_t call_mir_sdr_GetDevices ;
mir_sdr_DCoffsetIQimbalanceControl_t call_mir_sdr_DCoffsetIQimbalanceControl;
mir_sdr_DecimateControl_t call_mir_sdr_DecimateControl ;
mir_sdr_AgcControl_t call_mir_sdr_AgcControl ;
mir_sdr_StreamInit_t call_mir_sdr_StreamInit ;

mir_sdr_Reinit_t call_mir_sdr_Reinit ;

mir_sdr_SetDcMode_t call_mir_sdr_SetDcMode ;
mir_sdr_SetDcTrackTime_t call_mir_sdr_SetDcTrackTime ;
mir_sdr_SetSyncUpdatePeriod_t call_mir_sdr_SetSyncUpdatePeriod ;
mir_sdr_SetSyncUpdateSampleNum_t call_mir_sdr_SetSyncUpdateSampleNum ;
mir_sdr_StreamUninit_t call_mir_sdr_StreamUninit ;

mir_sdr_SetDeviceIdx_t call_mir_sdr_SetDeviceIdx ;
mir_sdr_ReleaseDeviceIdx_t call_mir_sdr_ReleaseDeviceIdx ;
mir_sdr_GetHwVersion_t call_mir_sdr_GetHwVersion ;

mir_sdr_RSPII_AntennaControl_t call_mir_sdr_RSPII_AntennaControl ;
mir_sdr_RSPII_ExternalReferenceControl_t call_mir_sdr_RSPII_ExternalReferenceControl ;
mir_sdr_RSPII_BiasTControl_t call_mir_sdr_RSPII_BiasTControl ;
mir_sdr_RSPII_RfNotchEnable_t call_mir_sdr_RSPII_RfNotchEnable ;
mir_sdr_AmPortSelect_t call_mir_sdr_AmPortSelect ;
mir_sdr_SetTransferMode_t call_mir_sdr_SetTransferMode ;

_tlogFun* sdrNode_LogFunction ;
_pushSamplesFun *acqCbFunction ;

int last_device_id = -1 ;

#ifdef _WIN64
#include <windows.h>
// Win  DLL Main entry
BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID *lpvReserved ) {
    Q_UNUSED(hInstance);
    Q_UNUSED(dwReason);
    Q_UNUSED(lpvReserved);
    return( TRUE ) ;
}

#endif

void reinit_device(struct t_rx_device* dev) ;
void set_gain_limits( struct t_rx_device* dev, double freq );

void log( int device_id, int level, char *msg ) {
    if( sdrNode_LogFunction != NULL ) {
        (*sdrNode_LogFunction)(rx[device_id].uuid,level,msg);
        return ;
    }
    printf("Trace:%s\n", msg );
}





/*
 * First function called by SDRNode - must return 0 if hardware is not present or problem
 */
/**
 * @brief initLibrary is called when the DLL is loaded, only for the first instance of the devices (when the getBoardCount() function returns
 *        more than 1)
 * @param json_init_params a JSOn structure to pass parameters from scripting to drivers
 * @param ptr pointer to function for logging
 * @param acqCb pointer to RF IQ processing function
 * @return
 */
LIBRARY_API int initLibrary(char *json_init_params,
                            _tlogFun* ptr,
                            _pushSamplesFun *acqCb ) {


    struct t_rx_device *tmp ;
    sdrNode_LogFunction = ptr ;
    acqCbFunction = acqCb ;
    QString fileName ;
    mir_sdr_ErrT err;
    float ver;

    Q_UNUSED(json_init_params);

#ifdef _WIN64
    QSettings m("HKEY_LOCAL_MACHINE\\SOFTWARE\\SDRplay",
                QSettings::NativeFormat);
    m.beginGroup("API");
    fileName = m.value("Install_Dir").toString() + "\\x64\\mir_sdr_api.dll";
    m.endGroup();
#else
    fileName = "libmirsdrapi-rsp.so" ;
#endif

    QLibrary *extDLL = new QLibrary( fileName );
    if( extDLL == NULL ) {
        qDebug() << "SDRPlayStub::loadDLL" << fileName << " fails." ;
        return(-1);
    }
    call_mir_sdr_ApiVersion = (mir_sdr_ApiVersion_t)extDLL->resolve("mir_sdr_ApiVersion");
    call_mir_sdr_DebugEnable = (mir_sdr_DebugEnable_t)extDLL->resolve("mir_sdr_DebugEnable");
    call_mir_sdr_GetDevices = (mir_sdr_GetDevices_t)extDLL->resolve("mir_sdr_GetDevices");
    call_mir_sdr_DCoffsetIQimbalanceControl = (mir_sdr_DCoffsetIQimbalanceControl_t)extDLL->resolve("mir_sdr_DCoffsetIQimbalanceControl");
    call_mir_sdr_DecimateControl = (mir_sdr_DecimateControl_t)extDLL->resolve("mir_sdr_DecimateControl");
    call_mir_sdr_AgcControl = (mir_sdr_AgcControl_t)extDLL->resolve("mir_sdr_AgcControl");
    call_mir_sdr_StreamInit = (mir_sdr_StreamInit_t)extDLL->resolve("mir_sdr_StreamInit");

    call_mir_sdr_Reinit = (mir_sdr_Reinit_t)extDLL->resolve("mir_sdr_Reinit");

    call_mir_sdr_SetDcMode = (mir_sdr_SetDcMode_t)extDLL->resolve("mir_sdr_SetDcMode");
    call_mir_sdr_SetDcTrackTime = (mir_sdr_SetDcTrackTime_t)extDLL->resolve("mir_sdr_SetDcTrackTime");
    call_mir_sdr_SetSyncUpdatePeriod = (mir_sdr_SetSyncUpdatePeriod_t)extDLL->resolve("mir_sdr_SetSyncUpdatePeriod");
    call_mir_sdr_SetSyncUpdateSampleNum = (mir_sdr_SetSyncUpdateSampleNum_t)extDLL->resolve("mir_sdr_SetSyncUpdateSampleNum");
    call_mir_sdr_StreamUninit = (mir_sdr_StreamUninit_t)extDLL->resolve("mir_sdr_StreamUninit");


    call_mir_sdr_SetDeviceIdx = (mir_sdr_SetDeviceIdx_t)extDLL->resolve("mir_sdr_SetDeviceIdx");
    call_mir_sdr_ReleaseDeviceIdx = (mir_sdr_ReleaseDeviceIdx_t)extDLL->resolve("mir_sdr_ReleaseDeviceIdx");
    call_mir_sdr_GetHwVersion = (mir_sdr_GetHwVersion_t)extDLL->resolve("mir_sdr_GetHwVersion");

    call_mir_sdr_RSPII_AntennaControl = (mir_sdr_RSPII_AntennaControl_t)extDLL->resolve("mir_sdr_RSPII_AntennaControl");
    call_mir_sdr_AmPortSelect = (mir_sdr_AmPortSelect_t)extDLL->resolve("mir_sdr_AmPortSelect");
    call_mir_sdr_SetTransferMode = (mir_sdr_SetTransferMode_t)extDLL->resolve("mir_sdr_SetTransferMode") ;

    if( call_mir_sdr_ApiVersion == NULL ) {
        if(DEBUG_DRIVER ) qDebug() << "SDRPlayStub::loadDLL" << fileName << "resolve 'sdr_ApiVersion' fails" ;
        return(-1);
    }

    err = (*call_mir_sdr_ApiVersion)(&ver);
    if( DEBUG_DRIVER ) qDebug() << "sdr_ApiVersion() -->" << (ver) ;
    if ((ver < MIR_SDR_API_VERSION) || (ver > MIR_SDR_API_VERSION_MAX )){
        if(DEBUG_DRIVER ) qDebug() << "SDRPlayStub::loadDLL" << fileName << "sdr_ApiVersion() not valid " ;
        return(-1);
    }


    if( call_mir_sdr_DebugEnable == NULL ) {
        if(DEBUG_DRIVER ) qDebug() << "SDRPlayStub::loadDLL" << fileName << "resolve 'mir_sdr_DebugEnable' fails" ;
        return(-1);
    }
    if( DEBUG_DRIVER ) {
        (*call_mir_sdr_DebugEnable)(1);
    } else {
        (*call_mir_sdr_DebugEnable)(0);
    }
    (*call_mir_sdr_DebugEnable)(1);

    err = (*call_mir_sdr_GetDevices)((mir_sdr_DeviceT*)&devices,
                                     &device_count, MAX_RSP_BOARDS);
    if( err != mir_sdr_Success) {
        device_count = 0 ;
        if( DEBUG_DRIVER ) qDebug() << "call_mir_sdr_GetDevices -->" << err ;
        return(-1);
    }

#ifndef _WIN64
        if( call_mir_sdr_SetTransferMode != NULL ) {
          //  (*call_mir_sdr_SetTransferMode)(mir_sdr_BULK) ;
        }
#endif



   if( DEBUG_DRIVER ) qDebug() << "PlayTools::PlayTools() ok with board_count=" << device_count ;

    driver_name = (char *)malloc( 100*sizeof(char));
    snprintf(driver_name,100,"SDRPlay");

    rx = (struct t_rx_device *)malloc( device_count * sizeof(struct t_rx_device));
    if( rx == NULL ) {
        return(0);
    }

    tmp = rx ;
    // iterate through devices to populate structure
    // TO DO : test device availabilty !
    for( unsigned int d=0 ; d < device_count ; d++ , tmp++ ) {

        tmp->idx = d ;
        tmp->uuid = NULL ;
        tmp->running = false ;
        tmp->device_name = (char *)malloc( 64 *sizeof(char));
        tmp->device_serial_number = (char *)malloc( 256 *sizeof(char));

        switch( devices[d].hwVer ) {
        case RSP_RSPII:
            sprintf( tmp->device_name, "SdrPlay_RSP2");
            tmp->stage_count = 2 ;
            tmp->stage_name = (char **)malloc( tmp->stage_count * sizeof( char *));
            tmp->stage_name[0] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_name[0], 64, "RF Atten.");
            tmp->stage_name[1] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_name[1], 64, "RF Input");

            tmp->stage_type = (int *)malloc( tmp->stage_count * sizeof( int ));
            tmp->stage_type[0] = GainTypeContinuous ;
            tmp->stage_type[1] = GainTypeOnOff ;

            tmp->stage_unit = (char **)malloc( tmp->stage_count * sizeof( char *));
            tmp->stage_unit[0] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_unit[0], 64, "dB");

            tmp->stage_unit[1] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_unit[1], 64, "Input A:Input B");
            break ;

        case RSP_RSPI:
        default:
            sprintf( tmp->device_name, "SdrPlay_RSP1");
            tmp->stage_count = 1 ;
            tmp->stage_name = (char **)malloc( tmp->stage_count * sizeof( char *));
            tmp->stage_name[0] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_name[0], 64, "RF Atten.");

            tmp->stage_type = (int *)malloc( tmp->stage_count * sizeof( int ));
            tmp->stage_type[0] = GainTypeContinuous ;

            tmp->stage_unit = (char **)malloc( tmp->stage_count * sizeof( char *));
            tmp->stage_unit[0] = (char *)malloc( 64 * sizeof(char));
            snprintf( tmp->stage_unit[0], 64, "dB");

            break ;
        }
        tmp->device_type = devices[d].hwVer ;

        sprintf( tmp->device_serial_number, "%s", devices[d].SerNo );

        tmp->min_frq_hz = 150e3 ;
        tmp->max_frq_hz = 2000e6 ;

        tmp->wr_pos = 0 ;
        tmp->samples_block = NULL ;
        tmp->queue_size = QUEUE_SIZE ;

        // allocate rates
        tmp->rates = (struct t_sample_rates*)malloc( sizeof(struct t_sample_rates));
        tmp->rates->enum_length = 6 ;

        tmp->rates->sample_rates = (unsigned int *)malloc( tmp->rates->enum_length * sizeof( unsigned int )) ;
        tmp->rates->rsp_params  = ( struct RSP_Params *)malloc( sizeof( struct RSP_Params ) * tmp->rates->enum_length ) ;

        struct RSP_Params *ptr = tmp->rates->rsp_params ;



        tmp->rates->sample_rates[0] =  600*1000 ;
        ptr[0].bwType = mir_sdr_BW_0_600 ;
        ptr[0].sampling_rate = 2400000 ;
        ptr[0].decim_factor = 4 ;
        ptr[0].queue_size = 128000 ;

        tmp->rates->sample_rates[1] =  1536*1000 ;
        ptr[1].bwType = mir_sdr_BW_1_536 ;
        ptr[1].sampling_rate = 6144000 ;
        ptr[1].decim_factor = 4 ;
        ptr[1].queue_size = 256e3 ;

        tmp->rates->sample_rates[2] = 5000*1000 ;
        ptr[2].bwType = mir_sdr_BW_5_000 ;
        ptr[2].decim_factor = 1 ;
        ptr[2].sampling_rate = 5000*1000 ;
        ptr[2].queue_size = 1024e3 ;

        tmp->rates->sample_rates[3] = 6000*1000 ;
        ptr[3].bwType = mir_sdr_BW_6_000 ;
        ptr[3].decim_factor = 1 ;
        ptr[3].sampling_rate = 6000*1000 ;
        ptr[3].queue_size = 1024e3 ;

        tmp->rates->sample_rates[4] = 7000*1000 ;
        ptr[4].bwType = mir_sdr_BW_7_000 ;
        ptr[4].decim_factor = 1 ;
        ptr[4].sampling_rate = 7000*1000 ;
        ptr[4].queue_size = 1024e3 ;

        tmp->rates->sample_rates[5] = 8000*1000 ;
        ptr[5].bwType = mir_sdr_BW_8_000 ;
        ptr[5].decim_factor = 1 ;
        ptr[5].sampling_rate = 8000*1000 ;
        ptr[5].queue_size = 1024e3 ;

        tmp->rates->preffered_sr_index = 1 ; // our default sampling rate will be 1536 KHz
        tmp->running = false ;
        tmp->fsHz = ptr[tmp->rates->preffered_sr_index].sampling_rate ;
        tmp->decimation = ptr[tmp->rates->preffered_sr_index].decim_factor ;
        tmp->bwType = ptr[tmp->rates->preffered_sr_index].bwType ;
        tmp->ifType = mir_sdr_IF_Zero ;
        tmp->dcMode = 0 ;
        tmp->gRdB = 50 ;
        tmp->agcSetPoint = -30 ;
        tmp->lnaEnable = 0 ;
        tmp->agcControl = mir_sdr_AGC_100HZ ;
        tmp->hw_frequency = 96e6 ;

        set_gain_limits( tmp, 96e6 );

        tmp->xn_1.re = tmp->xn_1.im = 0 ;
        tmp->yn_1.re = tmp->yn_1.im = 0 ;
        tmp->context.ctx_version = 0 ;
    }


    return(RC_OK);
}


/**
 * @brief setBoardUUID this function is called by SDRNode to assign a unique ID to each device managed by the driver
 * @param device_id [0..getBoardCount()[
 * @param uuid the unique ID
 * @return
 */
LIBRARY_API int setBoardUUID( int device_id, char *uuid ) {
    int len = 0 ;

    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%s)\n", __func__, device_id, uuid );

    if( uuid == NULL ) {
        return(RC_NOK);
    }
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    len = strlen(uuid);
    if( rx[device_id].uuid != NULL ) {
        free( rx[device_id].uuid );
    }
    rx[device_id].uuid = (char *)malloc( len * sizeof(char));
    strcpy( rx[device_id].uuid, uuid);
    return(RC_OK);
}

/**
 * @brief getHardwareName called by SDRNode to retrieve the name for the nth device
 * @param device_id [0..getBoardCount()[
 * @return a string with the hardware name, this name is listed in the 'devices' admin page and appears 'as is' in the scripts
 */
LIBRARY_API char *getHardwareName(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( (unsigned int)device_id >= device_count )
        return(NULL);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->device_name );
}

/**
 * @brief getBoardCount called by SDRNode to retrieve the number of different boards managed by the driver
 * @return the number of devices managed by the driver
 */
LIBRARY_API int getBoardCount() {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    return(device_count);
}

/**
 * @brief getPossibleSampleRateCount called to know how many sample rates are available. Used to fill the select zone in admin
 * @param device_id
 * @return sample rate in Hz
 */
LIBRARY_API int getPossibleSampleRateCount(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( (unsigned int)device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->rates->enum_length );
}

/**
 * @brief getPossibleSampleRateValue
 * @param device_id
 * @param index
 * @return
 */
LIBRARY_API unsigned int getPossibleSampleRateValue(int device_id, int index) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, index );
    if( (unsigned int)device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;

    struct t_sample_rates* rates = dev->rates ;
    if( index > rates->enum_length )
        return(0);

    return( rates->sample_rates[index] );
}

LIBRARY_API unsigned int getPrefferedSampleRateValue(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( (unsigned int)device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    struct t_sample_rates* rates = dev->rates ;
    int index = rates->preffered_sr_index ;
    return( rates->sample_rates[index] );
}
//-------------------------------------------------------------------
LIBRARY_API int64_t getMin_HWRx_CenterFreq(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( (unsigned int)device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->min_frq_hz ) ;
}

LIBRARY_API int64_t getMax_HWRx_CenterFreq(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( (unsigned int)device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->max_frq_hz ) ;
}

//-------------------------------------------------------------------
// Gain management
// devices have stages (LNA, VGA, IF...) . Each stage has its own gain
// range, its own name and its own unit.
// each stage can be 'continuous gain' or 'discrete' (on/off for example)
//-------------------------------------------------------------------
LIBRARY_API int getRxGainStageCount(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->stage_count );
}

LIBRARY_API char* getRxGainStageName( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    stage = stage % dev->stage_count ; // stays in [0..stage_count]..
    return( dev->stage_name[stage]);
}

LIBRARY_API char* getRxGainStageUnitName( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    // RTLSDR have only one stage so the unit is same for all
    struct t_rx_device *dev = &rx[device_id] ;
    stage = stage % dev->stage_count ; // stays in [0..stage_count]..
    return( dev->stage_unit[stage]);
}

LIBRARY_API int getRxGainStageType( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    struct t_rx_device *dev = &rx[device_id] ;
    stage = stage % dev->stage_count ; // stays in [0..stage_count]..
    return( dev->stage_type[stage]);
}

LIBRARY_API float getMinGainValue(int device_id,int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    if( (unsigned int)device_id >= device_count )
        return(0);

    return( -90 ) ;
}

LIBRARY_API float getMaxGainValue(int device_id,int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    if( (unsigned int)device_id >= device_count )
        return(0);

    return( -30 ) ;
}

LIBRARY_API int getGainDiscreteValuesCount( int device_id, int stage ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage);
    return(0);
}

LIBRARY_API float getGainDiscreteValue( int device_id, int stage, int index ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d, %d,%d)\n", __func__, device_id, stage, index);
    return(0);
}

/**
 * @brief getSerialNumber returns the (unique for this hardware name) serial number. Serial numbers are useful to manage more than one unit
 * @param device_id
 * @return
 */
LIBRARY_API char* getSerialNumber( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->device_serial_number );
}

//----------------------------------------------------------------------------------
// Manage acquisition
// SDRNode calls 'prepareRxEngine(device)' to ask for the start of acquisition
// Then, the driver shall call the '_pushSamplesFun' function passed at initLibrary( ., ., _pushSamplesFun* fun , ...)
// when the driver shall stop, SDRNode calls finalizeRXEngine()

/**
 * @brief prepareRXEngine trig on the acquisition process for the device
 * @param device_id
 * @return RC_OK if streaming has started, RC_NOK otherwise
 */
LIBRARY_API int prepareRXEngine( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    // here we keep it simple, just fire the relevant mutex
    struct t_rx_device *dev = &rx[device_id] ;
    if( dev->running ) {
        return(RC_OK);
    }

    reinit_device(dev);

    return( (dev->running ? 1:0));
}

/**
 * @brief finalizeRXEngine stops the acquisition process
 * @param device_id
 * @return
 */
LIBRARY_API int finalizeRXEngine( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    if( dev->running ) {
        (*call_mir_sdr_StreamUninit)() ;
        dev->running = false ;
    }
    return(RC_OK);
}

/**
 * @brief setRxSampleRate configures the sample rate for the device (in Hz). Can be different from the enum given by getXXXSampleRate
 * @param device_id
 * @param sample_rate
 * @return
 */
LIBRARY_API int setRxSampleRate( int device_id , int sample_rate) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id,sample_rate);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    struct RSP_Params *ptr = (struct RSP_Params*)dev->rates->rsp_params ;

    for(  int i=0 ; i < dev->rates->enum_length ; i++ ) {
         if( dev->rates->sample_rates[i] == (unsigned int)sample_rate ) {
             dev->fsHz = (double)ptr[i].sampling_rate ;
             dev->bwType = ptr[i].bwType ;
             dev->decimation = ptr[i].decim_factor ;
             if( !dev->running ) {
                 dev->queue_size = ptr[i].queue_size ;
                 if( DEBUG_DRIVER ) qDebug() << "setting queue size to " << dev->queue_size ;
             }

             if( ptr[i].decim_factor > 1 )  {
                 (*call_mir_sdr_DecimateControl)(1, dev->decimation, 0);
             } else {
                 (*call_mir_sdr_DecimateControl)(0, 2, 0);
             }
             dev->reinitReson = (mir_sdr_ReasonForReinitT)(dev->reinitReson | mir_sdr_CHANGE_BW_TYPE);
             break ;
         }
    }

    set_gain_limits( dev, dev->rfHz ) ;
    dev->context.sample_rate = sample_rate ;
    return(RC_OK);
}

/**
 * @brief getActualRxSampleRate called to know what is the actual sampling rate (hz) for the given device
 * @param device_id
 * @return
 */
LIBRARY_API int getActualRxSampleRate( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);
    struct t_rx_device *dev = &rx[device_id] ;
    return( (int)(dev->fsHz/dev->decimation) );
}

/**
 * @brief setRxCenterFreq tunes device to frq_hz (center frequency)
 * @param device_id
 * @param frq_hz
 * @return
 */
LIBRARY_API int setRxCenterFreq( int device_id, int64_t frq_hz ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%ld)\n", __func__, device_id, (long)frq_hz);
    if( DEBUG_DRIVER ) fflush(stderr);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    dev->rfHz = (double)frq_hz ;

    if( dev->running ) {
        dev->context.ctx_version++ ;
        dev->context.center_freq = frq_hz ;
        dev->reinitReson = (mir_sdr_ReasonForReinitT)(dev->reinitReson | mir_sdr_CHANGE_RF_FREQ);
        reinit_device(dev);
        return(RC_OK);
    }


    return(RC_NOK);
}

/**
 * @brief getRxCenterFreq retrieve the current center frequency for the device
 * @param device_id
 * @return
 */
LIBRARY_API int64_t getRxCenterFreq( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    return( (qint64)dev->rfHz ) ;
}

/**
 * @brief setRxGain sets the current gain
 * @param device_id
 * @param stage_id
 * @param gain_value
 * @return
 */
LIBRARY_API int setRxGain( int device_id, int stage_id, float gain_value ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d,%f)\n", __func__, device_id,stage_id,gain_value);
    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);


    struct t_rx_device *dev = &rx[device_id] ;
    if( dev->device_type == RSP_RSPII ) {
        if( stage_id == 1 ) {
            if( gain_value > 0 ) {
                if( call_mir_sdr_RSPII_AntennaControl != NULL ) {
                    (*call_mir_sdr_RSPII_AntennaControl)(mir_sdr_RSPII_ANTENNA_B) ;
                }
            } else {
                if( call_mir_sdr_RSPII_AntennaControl != NULL ) {
                    (*call_mir_sdr_RSPII_AntennaControl)(mir_sdr_RSPII_ANTENNA_A) ;
                }
            }
            return(RC_OK);
        }
    }

    // check value against device range
    dev->gRdB = (int)fabs(gain_value);
    if( dev->gRdB > dev->maxGain )
        dev->gRdB = dev->maxGain ;
    if( dev->gRdB < dev->minGain )
        dev->gRdB = dev->minGain ;

    if( dev->running ) {
        dev->reinitReson = (mir_sdr_ReasonForReinitT)(dev->reinitReson | mir_sdr_CHANGE_GR);
        reinit_device(dev);
    }

    return(RC_OK);
}

/**
 * @brief getRxGainValue reads the current gain value
 * @param device_id
 * @param stage_id
 * @return
 */
LIBRARY_API float getRxGainValue( int device_id , int stage_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id,stage_id);

    if( (unsigned int)device_id >= device_count )
        return(RC_NOK);
    if( stage_id >= 1 )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    return( -1*dev->gRdB ) ;
}

LIBRARY_API bool setAutoGainMode( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( (unsigned int)device_id >= device_count )
        return(false);
    return(false);
}

//-----------------------------------------------------------------------------------------
void set_gain_limits( struct t_rx_device* dev, double freq ) {
    if (freq <= SDRPLAY_AM_MAX) {
        dev->minGain = -4;
        dev->maxGain = 98;
    }
    else if (freq <= SDRPLAY_FM_MAX) {
        dev->minGain = 1;
        dev->maxGain = 103;
    }
    else if (freq <= SDRPLAY_B3_MAX) {
        dev->minGain = 5;
        dev->maxGain = 107;
    }
    else if (freq <= SDRPLAY_B45_MAX) {
        dev->minGain = 9;
        dev->maxGain = 94;
    }
    else if (freq <= SDRPLAY_L_MAX) {
        dev->minGain = 24;
        dev->maxGain = 105;
    }
}



void gcCallback(unsigned int gRdB, unsigned int lnaGRdB, void *cbContext) {
    Q_UNUSED(gRdB);
    Q_UNUSED(lnaGRdB);
    Q_UNUSED(cbContext);
    return;
}

#define ALPHA_DC (0.998)
void streamCallback(short *xi, short *xq, unsigned int firstSampleNum, int grChanged,
                                            int rfChanged, int fsChanged, unsigned int numSamples, unsigned int reset,
                                            void *cbContext) {

    struct t_rx_device *dev = (struct t_rx_device *) cbContext;
    Q_UNUSED(firstSampleNum);
    Q_UNUSED(grChanged);
    Q_UNUSED(rfChanged);
    Q_UNUSED(reset);
    Q_UNUSED(fsChanged);

    TYPECPX tmp ;
    unsigned int i ;
    float I,Q ;
    int j ;

    for ( i=0 ; i < numSamples; i++) {
            I = (float) xi[i] * 1.0 / SHRT_MAX;
            Q = (float) xq[i] * 1.0 / SHRT_MAX;
            tmp.re = I - dev->xn_1.re + ALPHA_DC * dev->yn_1.re ;
            tmp.im = Q - dev->xn_1.im + ALPHA_DC * dev->yn_1.im ;

            dev->xn_1.re = I ;
            dev->xn_1.im = Q ;

            dev->yn_1.re = tmp.re ;
            dev->yn_1.im = tmp.im ;

            j = dev->wr_pos ;
            if( j < dev->queue_size ) {
                dev->samples_block[j] = tmp ;
            } else {

                (*acqCbFunction)( dev->uuid, (float *)dev->samples_block, j, 1, &dev->context ) ;
                dev->samples_block = (TYPECPX *)malloc( dev->queue_size * sizeof(TYPECPX)) ;
                dev->samples_block[0] = tmp ;
                if( DEBUG_DRIVER ) qDebug() << "streamCallback() pushed:" << j << " samples" ;
                j = 0 ;
            }
            dev->wr_pos = j+1 ;
    }


}

void reinit_device(struct t_rx_device* dev) {
    int grMode ;
    int err ;
    int retries ;

//    if( dev->idx != last_device_id ) {
//        (*call_mir_sdr_SetDeviceIdx)( dev->idx );
//        last_device_id = dev->idx ;
//    }

    if (dev->running) {
        grMode = mir_sdr_USE_SET_GR_ALT_MODE;
        err = mir_sdr_HwError ;
        retries = 0 ;
        while( (err != mir_sdr_Success ) && (retries++<5)){
            err = (*call_mir_sdr_Reinit)(&dev->gRdB, dev->fsHz / 1e6, dev->rfHz / 1e6,
                                   dev->bwType,
                                   dev->ifType,
                                   (mir_sdr_LoModeT) 1,
                                   dev->lnaEnable,
                                   &grMode,
                                   (mir_sdr_SetGrModeT)1,
                                   &dev->samplesPerPacket,
                                   dev->reinitReson);
            if( err != mir_sdr_Success )
                usleep(2000);
            if( DEBUG_DRIVER ) qDebug() << "reinit_device() retry:" << retries ;
        }
        dev->reinitReson = mir_sdr_CHANGE_NONE ;
        if( DEBUG_DRIVER ) qDebug() << "\nreinit_device() call_mir_sdr_Reinit rc = " << err ;
    }
    else {


        if (dev->dcMode) {
            (*call_mir_sdr_SetDcMode)(4, 1);
        }
        dev->wr_pos = 0 ;
        dev->samples_block = (TYPECPX *)malloc( dev->queue_size * sizeof(TYPECPX)) ;
        grMode = mir_sdr_USE_SET_GR_ALT_MODE ; //1

        err = (*call_mir_sdr_StreamInit)(&dev->gRdB, dev->fsHz / 1e6, dev->rfHz / 1e6, dev->bwType, dev->ifType,
                                             dev->lnaEnable,
                                             &dev->gRdBsystem,
                                             (mir_sdr_SetGrModeT)grMode /* use internal gr tables acording to band */, &dev->samplesPerPacket,
                                             streamCallback,
                                             gcCallback, (void *)dev );

        if (err != mir_sdr_Success) {
            if( DEBUG_DRIVER ) qDebug() << "error PlayBoard::reinit_device()" << err ;
            return ;
        }

        // revalidate decimation
        if( dev->decimation > 1 )  {
            (*call_mir_sdr_DecimateControl)(1, dev->decimation, 0);
        } else {
            (*call_mir_sdr_DecimateControl)(0, 2, 0);
        }

        dev->running = true ;
        dev->reinitReson = mir_sdr_CHANGE_NONE ;
    }

    set_gain_limits(dev, dev->rfHz);
    dev->gain_dB = dev->maxGain - dev->gRdB;

}
