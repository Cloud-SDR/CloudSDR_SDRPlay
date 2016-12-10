/*
#==========================================================================================
# + + +   This Software is released under the "Simplified BSD License"  + + +
# Copyright 2014 F4GKR Sylvain AZARIAN . All rights reserved.
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
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "sdrplay_stub.h"
#include "entrypoint.h"
#define DEBUG_DRIVER (0)

char *driver_name ;
void* acquisition_thread( void *params ) ;
SDRPlayStub *stub ;

struct t_sample_rates {
    unsigned int *sample_rates ;
    int enum_length ;
    int preffered_sr_index ;
};

// this structure stores the device state
struct t_rx_device {

    char *device_name ;
    char *device_serial_number ;

    struct t_sample_rates* rates;
    int current_sample_rate ;

    int64_t min_frq_hz ; // minimal frequency for this device
    int64_t max_frq_hz ; // maximal frequency for this device
    int64_t center_frq_hz ; // currently set frequency


    float gain ;
    float gain_min ;
    float gain_max ;

    char *uuid ;
    bool running ;
    bool acq_stop ;
    sem_t mutex;

    pthread_t receive_thread ;
    // for DC removal
    TYPECPX xn_1 ;
    TYPECPX yn_1 ;

    struct ext_Context context ;
};

int device_count ;
char *stage_name ;
char *stage_unit ;

struct t_rx_device *rx;

_tlogFun* sdrNode_LogFunction ;
_pushSamplesFun *acqCbFunction ;

#ifdef _WIN64
#include <windows.h>
// Win  DLL Main entry
BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID *lpvReserved ) {
    return( TRUE ) ;
}
#endif

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

#ifdef _WIN64
    const size_t WCHARBUF = 100;
    const char dllname[] = "./mir_sdr_api.dll" ;
    wchar_t  wszDest[WCHARBUF];
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, dllname, -1, wszDest, WCHARBUF);
#endif

    sdrNode_LogFunction = ptr ;
    acqCbFunction = acqCb ;
    stub = new SDRPlayStub();


    driver_name = (char *)malloc( 100*sizeof(char));
    snprintf(driver_name,100,"SDRPlay");
    if( stub->loadDLL( wszDest ) == false ) {
        return(0);
    }

    // only one RSP managed...
    device_count = 1 ;
    rx = (struct t_rx_device *)malloc( device_count * sizeof(struct t_rx_device));
    if( rx == NULL ) {
        return(0);
    }
    tmp = rx ;
    // iterate through devices to populate structure
    for( int d=0 ; d < device_count ; d++ , tmp++ ) {
        tmp->uuid = NULL ;
        tmp->running = false ;
        tmp->acq_stop = false ;
        sem_init(&tmp->mutex, 0, 0);

        tmp->device_name = (char *)malloc( 64 *sizeof(char));
        tmp->device_serial_number = (char *)malloc( 16 *sizeof(char));
        sprintf( tmp->device_name, "SDRPlay");
        sprintf( tmp->device_serial_number, "0001");

        tmp->min_frq_hz =  100e3 ;
        tmp->max_frq_hz = 2000e6 ;


        tmp->center_frq_hz = tmp->min_frq_hz + 1e6 ; // arbitrary startup freq

        // allocate rates
        tmp->rates = (struct t_sample_rates*)malloc( sizeof(struct t_sample_rates));
        tmp->rates->enum_length = 6 ; // we manage 5 different sampling rates
        tmp->rates->sample_rates = (unsigned int *)malloc( tmp->rates->enum_length * sizeof( unsigned int )) ;
        tmp->rates->sample_rates[0] =  700*1000 ;
        tmp->rates->sample_rates[1] = 1536*1000 ;
        tmp->rates->sample_rates[2] = 5000*1000 ;
        tmp->rates->sample_rates[3] = 6000*1000 ;
        tmp->rates->sample_rates[4] = 7000*1000 ;
        tmp->rates->sample_rates[5] = 7000*1000 ;
        tmp->rates->preffered_sr_index = 1 ; // our default sampling rate will be 1024 KHz


        // set default SR
        tmp->current_sample_rate = tmp->rates->sample_rates[tmp->rates->preffered_sr_index] ;

        tmp->gain_min = -100 ;
        tmp->gain_max = -30 ;
        tmp->gain = tmp->gain_min + (tmp->gain_max - tmp->gain_min)/2 ;
        stub->setGainReduction( tmp->gain );
        tmp->context.ctx_version = 0 ;

        // create acquisition threads
        pthread_create(&tmp->receive_thread, NULL, acquisition_thread, tmp );
    }

    // all RTLSDR have one single gain stage
    stage_name = (char *)malloc( 10*sizeof(char));
    snprintf( stage_name,10,"RFGain");
    stage_unit = (char *)malloc( 10*sizeof(char));
    snprintf( stage_unit,10,"dB");
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
    if( device_id >= device_count )
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
    if( device_id >= device_count )
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
    if( device_id >= device_count )
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
    if( device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;

    struct t_sample_rates* rates = dev->rates ;
    if( index > rates->enum_length )
        return(0);

    return( rates->sample_rates[index] );
}

LIBRARY_API unsigned int getPrefferedSampleRateValue(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    struct t_sample_rates* rates = dev->rates ;
    int index = rates->preffered_sr_index ;
    return( rates->sample_rates[index] );
}
//-------------------------------------------------------------------
LIBRARY_API int64_t getMin_HWRx_CenterFreq(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->min_frq_hz ) ;
}

LIBRARY_API int64_t getMax_HWRx_CenterFreq(int device_id) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s\n", __func__);
    if( device_id >= device_count )
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
    // RTLSDR have only one stage
    return(1);
}

LIBRARY_API char* getRxGainStageName( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    // RTLSDR have only one stage so the name is same for all
    return( stage_name );
}

LIBRARY_API char* getRxGainStageUnitName( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    // RTLSDR have only one stage so the unit is same for all
    return( stage_unit );
}

LIBRARY_API int getRxGainStageType( int device_id, int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    // continuous value
    return(0);
}

LIBRARY_API float getMinGainValue(int device_id,int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    if( device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->gain_min ) ;
}

LIBRARY_API float getMaxGainValue(int device_id,int stage) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d,%d)\n", __func__, device_id, stage );
    if( device_id >= device_count )
        return(0);
    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->gain_max ) ;
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
    if( device_id >= device_count )
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
    if( device_id >= device_count )
        return(RC_NOK);

    // here we keep it simple, just fire the relevant mutex
    struct t_rx_device *dev = &rx[device_id] ;
    dev->acq_stop = false ;
    sem_post(&dev->mutex);

    return(RC_OK);
}

/**
 * @brief finalizeRXEngine stops the acquisition process
 * @param device_id
 * @return
 */
LIBRARY_API int finalizeRXEngine( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    dev->acq_stop = true ;
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
    if( device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    if( sample_rate == dev->current_sample_rate ) {
        return(RC_OK);
    }

    if( stub->isRunning() ) {
        return(RC_NOK);
    }

    dev->current_sample_rate = sample_rate ;
    dev->context.ctx_version++ ;
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
    if( device_id >= device_count )
        return(RC_NOK);
    struct t_rx_device *dev = &rx[device_id] ;
    return(dev->current_sample_rate);
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
    if( device_id >= device_count )
        return(RC_NOK);
    struct t_rx_device *dev = &rx[device_id] ;
    if( stub->tune( frq_hz ) ) {
        dev->center_frq_hz = frq_hz ;
        dev->context.ctx_version++ ;
        dev->context.center_freq = frq_hz ;
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
    if( device_id >= device_count )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->center_frq_hz ) ;
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
    if( device_id >= device_count )
        return(RC_NOK);
    if( stage_id >= 1 )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;

    // check value against device range
    if( gain_value > dev->gain_max ) {
        gain_value = dev->gain_max ;
    }
    if( gain_value < dev->gain_min ) {
        gain_value = dev->gain_min ;
    }

    stub->setGainReduction(fabsf(gain_value));
    dev->gain = gain_value ;

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

    if( device_id >= device_count )
        return(RC_NOK);
    if( stage_id >= 1 )
        return(RC_NOK);

    struct t_rx_device *dev = &rx[device_id] ;
    return( dev->gain) ;
}

LIBRARY_API bool setAutoGainMode( int device_id ) {
    if( DEBUG_DRIVER ) fprintf(stderr,"%s(%d)\n", __func__, device_id);
    if( device_id >= device_count )
        return(false);
    return(false);
}

//-----------------------------------------------------------------------------------------

/**
 * @brief acquisition_thread This function is locked by the mutex and waits before starting the acquisition in asynch mode
 * @param params
 * @return
 */
void* acquisition_thread( void *params ) {
    TYPECPX *samples ;
    int samplesPerPacket ;
    struct t_rx_device* my_device = (struct t_rx_device*)params ;

    if( DEBUG_DRIVER ) fprintf(stderr,"%s() start thread\n", __func__ );
    for( ; ; ) {
        my_device->running = false ;
        if( DEBUG_DRIVER ) fprintf(stderr,"%s() thread waiting\n", __func__ );
        if( DEBUG_DRIVER ) fflush(stderr);
        sem_wait( &my_device->mutex );
        if( DEBUG_DRIVER ) fprintf(stderr,"%s() rtlsdr_read_async\n", __func__ );

        stub->setSamplingRate( my_device->current_sample_rate ) ;
        if( stub->start() ) {
            my_device->running = true ;
            while( !my_device->acq_stop ) {
                samples =  stub->getSamples( &samplesPerPacket );
                // push samples to SDRNode callback function
                // we only manage one channel per device
                if( (*acqCbFunction)( my_device->uuid,
                                      (float *)samples, samplesPerPacket, 1,
                                      &my_device->context) <= 0 ) {
                    free(samples);
                }
            }
        }
    }
    return(NULL);
}

