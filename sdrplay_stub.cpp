#include "sdrplay_stub.h"
#include <pthread.h>
#include <semaphore.h>
#include <math.h>

#define DEBUG_STUB (0)
#define LNAGRTHRES 59

typedef struct SR
{
    double DisplayValue;		//Values to display in DialogBox
    double SampleRate;			//Sample Rate to be programmed to SDRplay
    int DownSample;				//Required Downsample ratio to achieve some Sample Rates
} SR_;

struct SR SampleRate[] = {
                         { 0.2,  2.0,   9 },
                         { 0.3,  2.0,   6 },
                         { 0.4,  3.0,   7 },
                         { 0.5,  2.0,   4 },
                         { 0.6,  4.0,   7 },
                         { 0.75, 3.0,   4 },
                         { 0.8,  7.0,   8 },
                         { 1.0,  2.0,   2 },
                         { 2.0,  2.0,   0 },
                         { 3.0,  3.0,   0 },
                         { 4.0,  4.0,   0 },
                         { 5.0,  5.0,   0 },
                         { 6.0,  6.0,   0 },
                         { 7.0,  7.0,   0 },
                         { 8.0,  8.0,   0 },
                         { 8.2,  8.192, 0 }
                         };

struct bw_t {
    mir_sdr_Bw_MHzT bwType;
    double			BW;
};


static bw_t bandwidths[] = {
    { mir_sdr_BW_0_200, 0.2 },		{ mir_sdr_BW_0_300, 0.3 },		{ mir_sdr_BW_0_600, 0.6 },
    { mir_sdr_BW_1_536, 1.536 },	{ mir_sdr_BW_5_000, 5.000 },	{ mir_sdr_BW_6_000, 6.000 },
    { mir_sdr_BW_7_000, 7.000 },	{ mir_sdr_BW_8_000, 8.000 }
};

#define NUM_BANDS	8            //  0				1			2			3			4			5	        6			7
const double band_fmin[NUM_BANDS] = { 0.1,			12.0, 		30.0,		60.0,		120.0,		250.0,		420.0,		1000.0	};
const double band_fmax[NUM_BANDS] = { 11.999999,	29.999999,  59.999999,	119.999999,	249.999999,	419.999999,	999.999999, 2000.0	};
const int band_LNAgain[NUM_BANDS] = { 24,			24,			24,			24,			24,			24,			7,			5		};
const int band_MIXgain[NUM_BANDS] = { 19,			19,			19,			19,			19,			19,			19,			19		};
const int band_fullTune[NUM_BANDS]= { 1,			1,			1,			1,			1,			1,			1,			1		};
const int band_MaxGR[NUM_BANDS] =	{ 102,			102,		102,		102,		102,		102,		85,			85		};

#define LO120MHz 24576000
#define LO144MHz 22000000
#define LO168MHz 19200000

SDRPlayStub::SDRPlayStub() {

    extDLL = NULL ;
    call_mir_sdr_Init = NULL ;
    call_mir_sdr_Uninit = NULL ;
    call_mir_sdr_ReadPacket = NULL ;
    call_mir_sdr_SetRf = NULL ;
    call_mir_sdr_SetFs = NULL ;
    call_mir_sdr_SetGr = NULL ;
    call_mir_sdr_SetGrParams = NULL ;
    call_mir_sdr_SetDcMode = NULL ;
    call_mir_sdr_SetDcTrackTime = NULL ;
    call_mir_sdr_SetSyncUpdateSampleNum = NULL ;
    call_mir_sdr_SetSyncUpdatePeriod = NULL ;
    call_mir_sdr_ApiVersion = NULL ;
    call_mir_sdr_ResetUpdateFlags = NULL ;

    FqOffsetPPM = 0;
    DcCompensationMode = 3;
    TrackingPeriod = 20;
    lastGainReduction = -1;
    lastLNAGRTHRES = -1;
    DecimationEN = false;
    Frequency = -1.0;
    LOChangeNeeded = false ;
    GainReduction = 40 ;
    fsMhz = 1.536 ;
    bwType = mir_sdr_BW_1_536 ;
    IFMode = mir_sdr_IF_Zero ;
    Frequency = 98.0 ;
    xn_1.im = xn_1.re = 0 ;
    yn_1.im = yn_1.re = 0 ;

    sem_init(&sem, 0, 1);
    running = false ;
}

void SDRPlayStub::setGainReduction(int gr) {
    for( int i=0 ; i < 8 ; i++ ) {
        bw_t b = bandwidths[i];
        if( b.bwType == bwType ) {
            if( gr > band_MaxGR[i]) {
                gr =band_MaxGR[i] ;
            }
            break ;
        }
    }
    GainReduction = gr ;
    if( !running ) {
        return ;
    }

    SDRplayInitalise();
}

int SDRPlayStub::getGainReductionValue() {
    return( GainReduction );
}

SDRPlayStub::~SDRPlayStub() {
}

bool SDRPlayStub::loadDLL(LPCWSTR name ) {
    mir_sdr_ErrT err;
    float ver;


    extDLL = LoadLibrary( name);

    if( extDLL == NULL ) {         
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_ApiVersion = (mir_sdr_ApiVersion_t)GetProcAddress( extDLL, _T("mir_sdr_ApiVersion"));
    if( call_mir_sdr_ApiVersion == NULL ) {
        return( false );
    }
    err = (*call_mir_sdr_ApiVersion)(&ver);

    //--------------------------------------------------------------------
    call_mir_sdr_Init = (mir_sdr_Init_t)GetProcAddress( extDLL,"mir_sdr_Init");
    if( call_mir_sdr_Init == NULL ) {
        return( false );
    }

    call_mir_sdr_Uninit = (mir_sdr_Uninit_t)GetProcAddress( extDLL,"mir_sdr_Uninit");
    if( call_mir_sdr_Uninit == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------


    err = (*call_mir_sdr_Init)( GainReduction, fsMhz, Frequency, bwType, IFMode, &samplesPerPacket);
    if( err != mir_sdr_Success ) {
        return( false );
    }
    (*call_mir_sdr_Uninit)();
    current_band = b_unset ;

    //--------------------------------------------------------------------
    call_mir_sdr_ReadPacket = (mir_sdr_ReadPacket_t)GetProcAddress( extDLL,"mir_sdr_ReadPacket");
    if( call_mir_sdr_ReadPacket == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetRf = (mir_sdr_SetRf_t)GetProcAddress( extDLL,"mir_sdr_SetRf");
    if( call_mir_sdr_SetRf == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetFs = (mir_sdr_SetFs_t)GetProcAddress( extDLL,"mir_sdr_SetFs");
    if( call_mir_sdr_SetFs == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetGr = (mir_sdr_SetGr_t)GetProcAddress( extDLL,"mir_sdr_SetGr");
    if( call_mir_sdr_SetFs == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetGrParams = (mir_sdr_SetGrParams_t)GetProcAddress( extDLL,"mir_sdr_SetGrParams");
    if( call_mir_sdr_SetGrParams == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetDcMode = (mir_sdr_SetDcMode_t)GetProcAddress( extDLL,"mir_sdr_SetDcMode");
    if( call_mir_sdr_SetDcMode == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetDcTrackTime = (mir_sdr_SetDcTrackTime_t)GetProcAddress( extDLL,"mir_sdr_SetDcTrackTime");
    if( call_mir_sdr_SetDcTrackTime == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetSyncUpdateSampleNum = (mir_sdr_SetSyncUpdateSampleNum_t)GetProcAddress( extDLL,"mir_sdr_SetSyncUpdateSampleNum");
    if( call_mir_sdr_SetSyncUpdateSampleNum == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetSyncUpdatePeriod = (mir_sdr_SetSyncUpdatePeriod_t)GetProcAddress( extDLL,"mir_sdr_SetSyncUpdatePeriod");
    if( call_mir_sdr_SetSyncUpdatePeriod == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_ResetUpdateFlags = (mir_sdr_ResetUpdateFlags_t)GetProcAddress( extDLL,"mir_sdr_ResetUpdateFlags");
    if( call_mir_sdr_ResetUpdateFlags == NULL ) {
        return( false );
    }
    //--------------------------------------------------------------------
    call_mir_sdr_SetParam = (mir_sdr_SetParam_t)GetProcAddress( extDLL,"mir_sdr_SetParam");
    if( call_mir_sdr_SetParam == NULL ) {
        return( false );
    }

    Frequency = 100e6 ;
    current_band = b_unset ;


    LOplan = LO120MHz;
    LOplanAuto = true;
    running = false ;


    return( true );
}

bool SDRPlayStub::tune( int64_t freq ) {
    Frequency = ((double)freq)*1.0/1e6 ;
    if( !running ) {
        return(true) ;
    }
    SDRplayInitalise();
    return( true );
}

bool SDRPlayStub::setSamplingRate( int sr_hz ) {

    fsMhz = (double)sr_hz/1e6 ;
    for( int i=0 ; i < 8 ; i++ ) {
        bw_t b = bandwidths[i];


        if( fabs(b.BW-fsMhz) == 0 ) {
            bwType = b.bwType ;
            break ;
        }
    }
    if( !running ) {
        return(true) ;
    }
    return( true );
}

bool SDRPlayStub::start() {

     if( running )
         return(true) ;

     running = true ;

     SDRplayInitalise();

     return( true );
}

bool SDRPlayStub::stop() {
    (*call_mir_sdr_Uninit)();
    InitRequired(PostTunerDcCompensation, LOplanAuto, &LOplan, &LOChangeNeeded, Frequency, fsMhz, bwType, IFMode, 1);	//Reset Last Known conditions.
    running = false ;
    return(true);
}

#define REPEAT_GETPACKET (16)
#define ALPHA_DC (0.996)
#define ONE_OVER_32768 (0.000030517578125f)

TYPECPX *SDRPlayStub::getSamples(int *count) {
    unsigned int fs ;
    int grc ;
    int rfc ;
    int fsc ;
    TYPECPX *block ;
    TYPECPX *pt ;
    float I,Q ;
    //double cI, cQ ;
    TYPECPX tmp ;
    short *xi ;
    short *xq ;

    if( running == false )
        return( NULL );

    (*count) = REPEAT_GETPACKET * samplesPerPacket ;
    xi = (short *)malloc( samplesPerPacket * sizeof( short ));
    xq = (short *)malloc( samplesPerPacket * sizeof( short ));
    block = (TYPECPX *)malloc( (*count) * sizeof(TYPECPX) );
    pt = block ;
    sem_wait( &sem );
    for( int k=0 ; k < REPEAT_GETPACKET ; k++ ) {
        (*call_mir_sdr_ReadPacket)( xi, xq, &fs, &grc, &rfc, &fsc);
        for( int i=0 ; i < samplesPerPacket ; i++ ) {
            I = (float)xi[i] * ONE_OVER_32768 ;
            Q = (float)xq[i] * ONE_OVER_32768 ;

            tmp.re = I - xn_1.re + ALPHA_DC * yn_1.re ;
            tmp.im = Q - xn_1.im + ALPHA_DC * yn_1.im ;

            xn_1.re = I ;
            xn_1.im = Q ;

            yn_1.re = tmp.re ;
            yn_1.im = tmp.im ;

            //tmp.re = I ;
            //tmp.im = Q ;
            (*pt) = tmp ;
            pt++ ;
        }
    }
    free( xi );
    free( xq );
    sem_post(&sem);
    return( block );
}

bool SDRPlayStub::InitRequired(bool PostDcComp, bool LOAuto, int *LOFrequency, bool *pLOChangeNeeded, double Frequency, double SampleRateID, mir_sdr_Bw_MHzT BW, mir_sdr_If_kHzT IF, int RST)
{
    bool InitRequired = false;
    int FreqBand = 0;
    static bool lastLoAuto = false;
    static bool lastPostDcComp = false;
    static int LastFreqBand = -1;
    static int LastLOFrequency = -1;
    static double LastFrequency = -1;
    static int LOBand = -1;
    static int LastLOBand = -1;
    static double LastSampleRateID = -1;
    static mir_sdr_Bw_MHzT LastBW;
    static mir_sdr_If_kHzT LastIF;

    //LO Profiles			    0				1				2
    const double LO_fmin[3] = { 250, 375, 395.0 };
    const double LO_fmax[3] = { 374.999999, 394.999999, 419.999999 };
    const int	 fLO[3]		= { LO120MHz, LO144MHz, LO168MHz };

    //Frequency Bands
    #define NUM_BANDS	8     //	 0			1			2		  3			  4			  5	          6			  7
    const double fmin[NUM_BANDS] = { 0.1,		12.0,	   30.0,	  60.0,		  120.0,	  250.0,	  420.0,	  1000.0 };
    const double fmax[NUM_BANDS] = { 11.999999, 29.999999, 59.999999, 119.999999, 249.999999, 419.999999, 999.999999, 2000.0 };

    //Redefine Defaults
    InitRequired = false;
    *pLOChangeNeeded = false;

    //Reset lastknown conditions
    if (RST == 1)
    {
        LastFreqBand = -1;
        LastLOFrequency = -1;
        LastFrequency = -1;
        LastLOBand = -1;
        LastSampleRateID = -1;
        return InitRequired;
    }

    //Change in Post DC COmpensation requires an Init to start thread again
    if (lastPostDcComp != PostDcComp)
        InitRequired = true;

    //Chanage in Freqeuncy Band require an initalisation
    FreqBand = 0;
    while (Frequency > fmax[FreqBand] && FreqBand < NUM_BANDS)
        ++FreqBand;
    if (FreqBand != LastFreqBand)
    {
        InitRequired = true;
    }

    //Changed as winradcallback has been implemented insterad.  Exectuing stop and start should not require a new Init.
    //Changes in Sample rate or IF Bandwidth require an initalisation
    if ((SampleRateID != LastSampleRateID) || (IF != LastIF) || (BW != LastBW))
    {
        InitRequired = true;
    }

    //If we have moved from fixed LO to auto LO in any mode apply correct Auto LO mode
    if ((lastLoAuto == false) && (LOAuto == true))
    {
        if (Frequency < fmin[3])
        {
            *LOFrequency = LO120MHz;
            InitRequired = true;
            *pLOChangeNeeded = true;
        }
        if ((Frequency > fmax[4]) && (Frequency < fmin[6]))
        {
            while (Frequency > LO_fmax[LOBand] && LOBand < 3)
                ++LOBand;
            *LOFrequency = fLO[LOBand];
        }
    }

    // IF below 60MHz and in a different frequency band Init and apply LO again.  If in AUTO reset LO to 120MHz else leave LO setting alone
    if ((FreqBand != LastFreqBand) && (Frequency < fmin[3]))
    {
        InitRequired = true;
        *pLOChangeNeeded = true;
        if (LOAuto == true)
        {
            *LOFrequency = LO120MHz;
        }

    }

    //If not in auto mode and LO plan changes then init.
    //if ((LOAuto == FALSE) && (*LOFrequency != LastLOFrequency))
    if (*LOFrequency != LastLOFrequency)
    {
        InitRequired = true;
        *pLOChangeNeeded = true;
    }

    //When LO is set to Auto selects correct LOplan for 250 - 419 Frequency Range.
    if ((Frequency > fmax[4]) && (Frequency < fmin[6]))
    {
        LOBand = 0;
        while (Frequency > LO_fmax[LOBand] && LOBand < 3)
            ++LOBand;
        if ((LOBand != LastLOBand) || (FreqBand != LastFreqBand))
        {
            InitRequired = true;
            *pLOChangeNeeded = true;
            if (LOAuto == true)
            {
                *LOFrequency = fLO[LOBand];
            }
        }
    }

    LastFrequency = Frequency;
    LastLOFrequency = *LOFrequency;
    LastFreqBand = FreqBand;
    LastLOBand = LOBand;
    LastSampleRateID = SampleRateID;
    LastIF = IF;
    LastBW = BW;
    lastLoAuto = LOAuto;
    lastPostDcComp = PostDcComp;
    return InitRequired;
}


void SDRPlayStub::SDRplayInitalise() {
    mir_sdr_ErrT err;
    double NominalFreqency;
    static int LastDcCompenastionMode = -1;
    int SampleCount = 0;

    if( !running )
        return ;

    sem_wait( &sem );
    NominalFreqency = Frequency * (1.0E6 + FqOffsetPPM) * 1.0E-6;	// including ppm correction


    if ((InitRequired(PostTunerDcCompensation, LOplanAuto, &LOplan,
                      &LOChangeNeeded, Frequency, fsMhz, bwType, IFMode, 0) ) || (running == false))
    {

        err = (*call_mir_sdr_Uninit)();						//Stop hardware for new init
        if (LOChangeNeeded == true)
        {
            err = (*call_mir_sdr_SetParam)(101, LOplan);
        }
        err = (*call_mir_sdr_Init)(GainReduction, fsMhz, Frequency, bwType, IFMode, &SampleCount);
        (*call_mir_sdr_SetDcMode)(DcCompensationMode, 0);
        (*call_mir_sdr_SetGrParams)(0, LNAGRTHRES);			//Apply Old LNA Threshold
        (*call_mir_sdr_SetGr)(GainReduction, 1, 0);			//Reapply GR so new LNA Thres takes effect.

    }
    else
    {
        // Program Frequency Change
        if (Frequency != LastFrequency)
        {
            err = (*call_mir_sdr_SetRf)(NominalFreqency*1.0E6, 1, 0);
        }

        //Program Gain Reduction.
        if (lastLNAGRTHRES != LNAGRTHRES)
        {
            err = (*call_mir_sdr_SetGrParams)(0, LNAGRTHRES);
        }

        //Program DC Compensation.
        if (LastDcCompenastionMode != DcCompensationMode)
        {
            (*call_mir_sdr_SetDcMode)(DcCompensationMode, 0);
            (*call_mir_sdr_SetDcTrackTime)(TrackingPeriod);
        }

        //Program Gain Reduction.
        if (lastGainReduction != GainReduction || lastLNAGRTHRES != LNAGRTHRES || LastDcCompenastionMode != DcCompensationMode)
        {
            err = (*call_mir_sdr_SetGr)(GainReduction, 1, 0);
        }
    }
    sem_post(&sem);
    LastFrequency = Frequency;
    lastLNAGRTHRES = LNAGRTHRES;
    lastGainReduction = GainReduction;
    LastDcCompenastionMode = DcCompensationMode;
}
