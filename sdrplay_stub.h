#ifndef SDRPLAY_STUB_H
#define SDRPLAY_STUB_H

#include <stdint.h>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <QString>

#ifdef _WIN64
#include <tchar.h>
#include <windows.h>
#endif
typedef enum
{
  mir_sdr_GetFd              = 0,
  mir_sdr_FreeFd             = 1,
  mir_sdr_DevNotFound        = 2,
  mir_sdr_DevRemoved         = 3
} mir_sdr_JavaReqT;


typedef enum
{
  mir_sdr_Success            = 0,
  mir_sdr_Fail               = 1,
  mir_sdr_InvalidParam       = 2,
  mir_sdr_OutOfRange         = 3,
  mir_sdr_GainUpdateError    = 4,
  mir_sdr_RfUpdateError      = 5,
  mir_sdr_FsUpdateError      = 6,
  mir_sdr_HwError            = 7,
  mir_sdr_AliasingError      = 8,
  mir_sdr_AlreadyInitialised = 9,
  mir_sdr_NotInitialised     = 10
} mir_sdr_ErrT;

typedef enum
{
  mir_sdr_BW_0_200 = 200,
  mir_sdr_BW_0_300 = 300,
  mir_sdr_BW_0_600 = 600,
  mir_sdr_BW_1_536 = 1536,
  mir_sdr_BW_5_000 = 5000,
  mir_sdr_BW_6_000 = 6000,
  mir_sdr_BW_7_000 = 7000,
  mir_sdr_BW_8_000 = 8000
} mir_sdr_Bw_MHzT;

typedef enum
{
  mir_sdr_IF_Zero  = 0,
  mir_sdr_IF_0_450 = 450,
  mir_sdr_IF_1_620 = 1620,
  mir_sdr_IF_2_048 = 2048
} mir_sdr_If_kHzT;

typedef enum
{
  mir_sdr_ISOCH = 0,
  mir_sdr_BULK  = 1
} mir_sdr_TransferModeT;

typedef enum {
    b_0_119 = 0,
    b_12_299 = 1,
    b_30_599 = 2,
    b_60_1199= 3,
    b_120_2499 = 4,
    b_250_4199 = 5,
    b_420_999 = 6,
    b_1_2 = 7,
    b_unset = 8
} mir_sdr_band ;

struct sampling_settings {
    double sr ;
    mir_sdr_Bw_MHzT bw ;
} ;

typedef struct __attribute__ ((__packed__)) _sCplx
{
    float re;
    float im;
} TYPECPX;


typedef mir_sdr_ErrT (*mir_sdr_Init_t)(int gRdB, double fsMHz, double rfMHz, mir_sdr_Bw_MHzT bwType, mir_sdr_If_kHzT ifType, int *samplesPerPacket);
typedef mir_sdr_ErrT (*mir_sdr_Uninit_t)(void);
typedef mir_sdr_ErrT (*mir_sdr_ReadPacket_t)(short *xi, short *xq, unsigned int *firstSampleNum, int *grChanged, int *rfChanged, int *fsChanged);
typedef mir_sdr_ErrT (*mir_sdr_SetRf_t)(double drfHz, int abs, int syncUpdate);
typedef mir_sdr_ErrT (*mir_sdr_SetFs_t)(double dfsHz, int abs, int syncUpdate, int reCal);
typedef mir_sdr_ErrT (*mir_sdr_SetGr_t)(int gRdB, int abs, int syncUpdate);
typedef mir_sdr_ErrT (*mir_sdr_SetGrParams_t)(int minimumGr, int lnaGrThreshold);
typedef mir_sdr_ErrT (*mir_sdr_SetDcMode_t)(int dcCal, int speedUp);
typedef mir_sdr_ErrT (*mir_sdr_SetDcTrackTime_t)(int trackTime);
typedef mir_sdr_ErrT (*mir_sdr_SetSyncUpdateSampleNum_t)(unsigned int sampleNum);
typedef mir_sdr_ErrT (*mir_sdr_SetSyncUpdatePeriod_t)(unsigned int period);
typedef mir_sdr_ErrT (*mir_sdr_ApiVersion_t)(float *version);
typedef mir_sdr_ErrT (*mir_sdr_ResetUpdateFlags_t)(int resetGainUpdate, int resetRfUpdate, int resetFsUpdate);
typedef mir_sdr_ErrT (*mir_sdr_SetParam_t)(unsigned int id, unsigned int value);

class SDRPlayStub {
public:
    explicit SDRPlayStub();
    ~SDRPlayStub();

    bool loadDLL(QString name );
    bool tune( int64_t freq );
    bool setSamplingRate( int sr_hz );
    TYPECPX *getSamples(int *count);
    void setGainReduction( int gr );
    int getGainReductionValue();

    bool isRunning() { return( running ); }
    bool start();
    bool stop();
    int getDefaultSamplePerPacket() { return( samplesPerPacket ) ; }


private:
    mir_sdr_band current_band ;
    HMODULE  extDLL ;
    mir_sdr_Init_t call_mir_sdr_Init ;
    mir_sdr_Uninit_t call_mir_sdr_Uninit ;
    mir_sdr_ReadPacket_t call_mir_sdr_ReadPacket ;
    mir_sdr_SetRf_t call_mir_sdr_SetRf ;
    mir_sdr_SetFs_t call_mir_sdr_SetFs ;
    mir_sdr_SetGr_t call_mir_sdr_SetGr ;
    mir_sdr_SetGrParams_t call_mir_sdr_SetGrParams;
    mir_sdr_SetDcMode_t call_mir_sdr_SetDcMode ;
    mir_sdr_SetDcTrackTime_t call_mir_sdr_SetDcTrackTime ;
    mir_sdr_SetSyncUpdateSampleNum_t call_mir_sdr_SetSyncUpdateSampleNum ;
    mir_sdr_SetSyncUpdatePeriod_t call_mir_sdr_SetSyncUpdatePeriod ;
    mir_sdr_ApiVersion_t call_mir_sdr_ApiVersion ;
    mir_sdr_ResetUpdateFlags_t call_mir_sdr_ResetUpdateFlags ;
    mir_sdr_SetParam_t call_mir_sdr_SetParam ;

    bool running ;
    int GainReduction ;
    double fsMhz ;
    double Frequency ;
    mir_sdr_Bw_MHzT bwType ;
    mir_sdr_If_kHzT IFMode ;
    int samplesPerPacket ;
    int FqOffsetPPM ;
    bool PostTunerDcCompensation ;
    int DcCompensationMode ;
    int TrackingPeriod;
    bool DecimationEN ;
    int lastLNAGRTHRES ;
    int lastGainReduction ;
    double LastFrequency ;
    sem_t sem ;
    bool LOplanAuto ;
    bool LOChangeNeeded ;
    int LOplan ;
    TYPECPX xn_1, yn_1 ;

    bool InitRequired(bool PostDcComp, bool LOAuto, int *LOFrequency, bool *LOChangeNeeded, double Frequency, double SampleRateID,
                      mir_sdr_Bw_MHzT BW, mir_sdr_If_kHzT IF, int RST);
    void SDRplayInitalise();
};
#endif // SDRPLAY_STUB_H
