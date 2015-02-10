/******************************************************************************
*
* File Name          :  STiM32.c
* Description        :  STIMULATOR Control firmware 
*
* Last revision      :  IH 2015-02-10
*
*   TODO:
**
*   150128      Bug: space between : and ss in time string
*   150127      Implement variable pulse voltage - do this by defining additional sequences
*
*******************************************************************************/

/* 
*   IH1412116
*   IMPORTANT: The FPU type (ARM toolsets) should be set to Hard EABI
*/

/* Includes ------------------------------------------------------------------*/
#include "circle_api.h"
#include <string.h>

/* DEBUG Setting defines -----------------------------------------------------------*/
//#define DEBUG_NOHW

/* Private defines -----------------------------------------------------------*/
#define STIM32_VERSION          "150210a"

#define  STIMULATOR_HANDLER_ID  UNUSED5_SCHHDL_ID
#define  GUIUPDATE_DIVIDER      1       // GUI is called every 100 SysTicks
#define  STATECHANGE_CNT_LIMIT  10

#define  FIFO_SIZE              128

#define  VBAT_MV_LOW                    4000
#define  BATTERY_STATUS_STRING_LENGHT   8
#define  SETTINGS_STRING_LENGHT         32

#define  BKP_FREQUENCY          BKP_USER1
#define  BKP_PULSESEQ           BKP_USER2
#define  BKP_PULSEPEAKVOLTAGE   BKP_USER3

#define NOMINAL_BATTERY_VOLTAGE_MV     4020
/* lower voltage limit; under this voltage, the 8V pulse voltage option is disabled */ 
#define LIMIT_FOR8V_BATTERY_VOLTAGE_MV 3900


/* Typedefs ------------------------------------------------------------------*/
typedef enum {
    GUI_INITIALIZE,
    GUI_NORMAL_UPDATE,    
    GUI_CLEAR,
    GUI_INTRO_SCREEN,
    } GUIaction_code;

typedef enum {
    PENDING_REQUEST_NONE,

    PENDING_REQUEST_REDRAW,    
    PENDING_REQUEST_SHOWING_INTRO_SCREEN,
    } PendingRequest_code;

typedef enum {
    STIMSTATE_IDLE,

    STIMSTATE_RUN,
    STIMSTATE_WAITING_FOR_RUN,
    STIMSTATE_WAITING_FOR_IDLE,
    } StimState_code;

typedef enum {
    UPPERPANELSTATE_OVERLOAD,
    UPPERPANELSTATE_WAITING,
    UPPERPANELSTATE_DISPLAY_READOUT,
    } UpperPanelState_code;

typedef enum {
    POSITIVE_VOLTAGE_MAX,
    POSITIVE_VOLTAGE_HALF,
    ZERO_VOLTAGE,
    NEGATIVE_VOLTAGE_HALF,
    NEGATIVE_VOLTAGE_MAX,
    } OutputVoltage_code;

typedef enum {
    PULSEPEAKVOLTAGE_8V=1,
    PULSEPEAKVOLTAGE_6V=2,
    PULSEPEAKVOLTAGE_4V=3,    
    } PulsePeakVoltage_code;

typedef enum {
    FREQUENCY_1KHZ=1,
    FREQUENCY_2KHZ=2,
    FREQUENCY_3KHZ=3,
    } Frequency_code;

typedef enum {
    PULSESEQUENCE_1=1,
    PULSESEQUENCE_2=2,
    PULSESEQUENCE_3=3,
    PULSESEQUENCE_4=4,
    } PulseSequence_code;

typedef enum {
    SEQUENCEMULTIPLICITY_SINGLE,
    SEQUENCEMULTIPLICITY_DOUBLE,
    } SequenceMultiplicity_code;

typedef struct 
    {
        Frequency_code frequency;
        PulseSequence_code pulseSeq;
        PulsePeakVoltage_code peakVoltage;
        u16 frequency_divider;
        SequenceMultiplicity_code sequence_multiplicity;
        float voltage_multiplication_factor;
    
        u16 delay_between_sequences_microseconds;
        u16 delay_between_sequences_loop_counts;
    
        u16 delay0_microseconds;     
        u16 delay0_loop_counts;     
        s16 edge1;
    
        u16 delay1_microseconds;     
        u16 delay1_loop_counts;             
        s16 edge2;
        
        u16 delay2_microseconds;     
        u16 delay2_loop_counts;         
        s16 edge3;
            
        u16 delay3_microseconds;     
        u16 delay3_loop_counts;         
        s16 edge4;

/*
                  d3
        d1     --------   
      ------   |      |
      |    |   |      |
d0    |    | d2|      |
------|    -----      ------
     e1   e2   e3     e4

*/
    
    } 
    Pulse_Sequence_struct;

typedef struct 
    {
        u32     CAE1;           // current after edge1
        bool    isOverloaded;
    }
    Readout_struct;

/* Forward declarations ------------------------------------------------------*/
enum MENU_code Application_Handler(void);

enum MENU_code  Cancel( void );
enum MENU_code  ShutDown( void );
enum MENU_code  Quit( void );
enum MENU_code  RestoreApp( void );

enum MENU_code  MenuSetup_Freq();
enum MENU_code  MenuSetup_PSeq();
enum MENU_code  MenuSetup_PVolt();

enum MENU_code  SetFrequency_1();
enum MENU_code  SetFrequency_2();
enum MENU_code  SetFrequency_3();

enum MENU_code  SetPulseSequence_1();
enum MENU_code  SetPulseSequence_2();
enum MENU_code  SetPulseSequence_3();
enum MENU_code  SetPulseSequence_4();

enum MENU_code  SetPulsePeakVoltage_1(void);
enum MENU_code  SetPulsePeakVoltage_2(void);
enum MENU_code  SetPulsePeakVoltage_3(void);

void TimerHandler1(void);

static void GUI(GUIaction_code, u16 );
static void LongDelay(u8 delayInSeconds);
static enum MENU_code MsgVersion(void);
static void UpdatePulseSequence(void);
static void SetAutorun(void);
static void BackUpParameters(void);
static void RestoreParameters(void);
static char* GetBatteryStatusString(void);
static char* GetSettingsString(void);
static void PlayBeep(void);

static void SetOutputVoltage(OutputVoltage_code, float multiplication_factor);
static void GeneratePulseSequenceAndReadCAE(void);        
    

/* Constants -----------------------------------------------------------------*/
const char Application_Name[8+1] = {"STiM32"};      // Max 8 characters

tMenu MenuMainSTiM32 =
{
    1,
    "STiM32 Main Menu",
    6, 0, 0, 0, 0, 0,
    0,
    {
        { "Set Frequency",           MenuSetup_Freq,    Application_Handler,    0 },
        { "Set Pulse Sequence",      MenuSetup_PSeq,    Application_Handler ,   0 },
        { "Set Pulse Voltage",       MenuSetup_PVolt,   Application_Handler ,   0 },
        { "Cancel",                  Cancel,            RestoreApp ,            0 },
        { "Shutdown",                ShutDown,          0,                      1 },
        { "Quit to OS",              Quit,              0,                      1 },            
    }
};

tMenu MenuSetFrequency =
{
    1,
    "Set Frequency",
    4, 0, 0, 0, 0, 0,
    0,
    {
        { " 1 kHz ",     SetFrequency_1,    Application_Handler,    0 },
        { " 2 kHz ",     SetFrequency_2,    Application_Handler ,   0 },
        { " 3 kHz ",     SetFrequency_3,    Application_Handler ,   0 },        
        { "Cancel",      Cancel,            Application_Handler,    0 },
    }
};

tMenu MenuSetPulseSequence =
{
    1,
    "Set Pulse Sequence",
    5, 0, 0, 0, 0, 0,
    0,
    {
        { "PSeq 1",     SetPulseSequence_1,    Application_Handler,    0 },
        { "PSeq 2",     SetPulseSequence_2,    Application_Handler ,   0 },
        { "PSeq 3",     SetPulseSequence_3,    Application_Handler ,   0 },
        { "PSeq 4",     SetPulseSequence_4,    Application_Handler ,   0 },
        { "Cancel",     Cancel,                Application_Handler,    0 },
    }
};

tMenu MenuSetPulsePeakVoltage =
{
    1,
    "Set Peak Voltage",
    4, 0, 0, 0, 0, 0,
    0,
    {
        { " 8 V ",     SetPulsePeakVoltage_1,    Application_Handler,    0 },
        { " 6 V ",     SetPulsePeakVoltage_2,    Application_Handler ,   0 },
        { " 4 V ",     SetPulsePeakVoltage_3,    Application_Handler ,   0 },        
        { "Cancel",    Cancel,                   Application_Handler,    0 },
    }
};

tMenu MenuSetPulsePeakVoltage_No8VOption =
{
    1,
    "Set Peak Voltage",
    3, 0, 0, 0, 0, 0,
    0,
    {
        { " 6 V ",     SetPulsePeakVoltage_2,    Application_Handler ,   0 },
        { " 4 V ",     SetPulsePeakVoltage_3,    Application_Handler ,   0 },        
        { "Cancel",    Cancel,                   Application_Handler,    0 },
    }
};


/* Global variables ----------------------------------------------------------*/
static PendingRequest_code ActualPendingRequest;
static Pulse_Sequence_struct PulseSeq;
static Readout_struct Readout;
static StimState_code StimState;
static u16 ReadoutLimit_CAE1_for_Run;
static u16 ReadoutLimit_CAE1_for_Idle;
static u16 ActualBatteryVoltagemV;

static u8 MyFifoRxBuffer[FIFO_SIZE];       
static u8 MyFifoTxBuffer[FIFO_SIZE];

static char BatteryStatusString[BATTERY_STATUS_STRING_LENGHT];
static char SettingsStatusString[SETTINGS_STRING_LENGHT];

/*******************************************************************************
* Function Name  : STIMULATOR_Handler
* Description    : Generates single pulse sequence and reads the feedback signal
* Input          : None
* Return         : Readout 
*******************************************************************************/
void STIMULATOR_Handler( void ) 
{
static u32 state_change_cnt = 0;
static u32 frequency_cnt = 0;

if((frequency_cnt++) % PulseSeq.frequency_divider)
            {
            return;
            }
        
#define WHILE_DELAY_LOOP(loopCounts)      {i=(loopCounts);while(i--);}
        
#ifdef DEBUG_NOHW

    // Code for debugging (no hardware connected)

    static u16 TickCnt=0;    
    
    if(TickCnt<1000)
        {        
            Readout.CAE1 = ReadoutLimit_CAE1_for_Run-1;                           
        }
    else if(TickCnt<3000)
        {
            Readout.CAE1 = ReadoutLimit_CAE1_for_Run + ((float)TickCnt-1000.0)/2000.0*100;        
        }
    else if(TickCnt<4000)
        {
            Readout.CAE1 = ReadoutLimit_CAE1_for_Run + 100;                    
        }        
    if(TickCnt++==4000)
        {
        TickCnt=0;                
        }    

    {u32 i;
    CX_Write( CX_GPIO_PIN4, CX_GPIO_LOW, 0 );
    WHILE_DELAY_LOOP(PulseSeq.delay0_loop_counts)
    
    CX_Write( CX_GPIO_PIN4, CX_GPIO_HIGH, 0 );
    WHILE_DELAY_LOOP(PulseSeq.delay1_loop_counts)
    
    CX_Write( CX_GPIO_PIN4, CX_GPIO_LOW, 0 );
    WHILE_DELAY_LOOP(PulseSeq.delay2_loop_counts)
    
    CX_Write( CX_GPIO_PIN4, CX_GPIO_HIGH, 0 );
    WHILE_DELAY_LOOP(PulseSeq.delay3_loop_counts)
    
    CX_Write( CX_GPIO_PIN4, CX_GPIO_LOW, 0 );
    }
    
#else        

    // Real code using connected hardware
                    
    switch(PulseSeq.sequence_multiplicity)
    {        
        case SEQUENCEMULTIPLICITY_SINGLE:
            GeneratePulseSequenceAndReadCAE();        
            break;
        
        case SEQUENCEMULTIPLICITY_DOUBLE:
            GeneratePulseSequenceAndReadCAE();        
            {u32 i; WHILE_DELAY_LOOP(PulseSeq.delay_between_sequences_loop_counts)}
            GeneratePulseSequenceAndReadCAE();        
            break;
    }   
                    
#endif
        
    switch(StimState)
    {
        case STIMSTATE_IDLE:  
        
                LED_Set( LED_RED, LED_ON);                
                LED_Set( LED_GREEN, LED_OFF);           
                            
        
                // check if still idle    
                if(Readout.CAE1 >= ReadoutLimit_CAE1_for_Run)
                    {
                    StimState = STIMSTATE_WAITING_FOR_RUN;
                    state_change_cnt = 0;
                    }
                break;
        
        case STIMSTATE_RUN:  
            
                LED_Set( LED_RED, LED_OFF);                
                LED_Set( LED_GREEN, LED_ON);                
                                                              
        
                // check if still running
                if(Readout.CAE1 <= ReadoutLimit_CAE1_for_Idle)
                    {
                    StimState = STIMSTATE_WAITING_FOR_IDLE;
                    state_change_cnt = 0;
                    }                
                break;
                
        case STIMSTATE_WAITING_FOR_IDLE:  
                if(Readout.CAE1 > ReadoutLimit_CAE1_for_Idle)
                    {
                    StimState = STIMSTATE_RUN;
                    }
                else                    
                    if(++state_change_cnt == STATECHANGE_CNT_LIMIT)
                        {
                        StimState = STIMSTATE_IDLE;
                        }                                          
                break;
                    
        case STIMSTATE_WAITING_FOR_RUN:  
                if(Readout.CAE1 < ReadoutLimit_CAE1_for_Run)
                    {
                    StimState = STIMSTATE_IDLE;
                    }
                else                    
                    if(++state_change_cnt == STATECHANGE_CNT_LIMIT)
                        {
                        StimState = STIMSTATE_RUN;
                        }                                          
                break;
    }

}

/*******************************************************************************
* Function Name  : Application_Ini
* Description    : Initialization function of Circle_App. This function will
*                  be called only once by CircleOS.
* Input          : None
* Return         : MENU_CONTINUE_COMMAND
*******************************************************************************/
enum MENU_code Application_Ini(void)
    {        

    LCD_SetOffset(OFFSET_OFF);
    
    UTIL_SetDividerHandler(MENU_SCHHDL_ID, 10);             //  10 is default
    MENU_SetAppliDivider( 10 );                             // This application will be called every 10*10 =100 SysTicks
    UTIL_SetSchHandler(STIMULATOR_HANDLER_ID, STIMULATOR_Handler );
    UTIL_SetDividerHandler(STIMULATOR_HANDLER_ID, 1);       // This handler will be called every single SysTick
    
    UTIL_SetPll(SPEED_VERY_HIGH);                           // CPU frequency is 120MHz; Systick frequency is 3kHZ
                                                            // see EvoPrimer Manual for STM32F429ZI
    
    LCD_SetRotateScreen( 1 );
    SetAutorun();
    
    //-------------------------------------
    // Initialize ...
              
    // ... set frequency and pulse sequence   
    RestoreParameters();  
    UpdatePulseSequence();    
    
    // ... GUI    
    GUI(GUI_INITIALIZE,0);
    
    // ... request mechanism
    ActualPendingRequest = PENDING_REQUEST_NONE;
    
    // ... state machine
    StimState = STIMSTATE_IDLE; 
    
    // ... readout limits
    ReadoutLimit_CAE1_for_Run = 150;
    ReadoutLimit_CAE1_for_Idle = 170;

    // ... miscellaneous    

    ActualBatteryVoltagemV = UTIL_GetBat();
    
    RTC_SetTime(0,0,0);  //IH150126 this clears any preset RTC ... but we do not care in our app

    // ... CX Extension
    
#ifdef DEBUG_NOHW       

    // test settings
    
    CX_Configure( CX_GPIO_PIN4, CX_GPIO_Mode_OUT_PP, 0 );  //Push-pull mode
    CX_Write( CX_GPIO_PIN4, CX_GPIO_LOW, 0 );
    
#else        

    // real settings
    
    // SPI Setup
    
    tCX_SPI_Config s_SpiInit;
    
    s_SpiInit.Speed = CX_SPI_Mode_VeryHigh;             // The speed range of the serial bit rate.
    s_SpiInit.WordLength = CX_SPI_8_Bits;               // The number of transferred data bit. Standard is 8, but could be 16 for some specific devices.
    s_SpiInit.Mode = CX_SPI_MODE_MASTER;                // 1: master, 0: slave
    s_SpiInit.Polarity = CX_SPI_POL_LOW;                // Indicates the steady state (idle state of the clock when no transmission).
    s_SpiInit.Phase = CX_SPI_PHA_FIRST;                 // Phase:  0 indicates that the first edge of the clock when leaving the idle state is active
                                                        //         1 indicates that the second edge of the clock when leaving the idle state is active
    s_SpiInit.MSB1LSB0 = CX_SPI_MSBFIRST;               // First bit to be sent.  1: MSB first, 0: LSB first
    s_SpiInit.Nss = CX_SPI_Soft;                        // NSS signal management : 1 = by hardware (NSS pin), 0 = by software using the SSI bit
                                                        // IH141230 this must be set to CX_SPI_Soft, but the actual didgital potentiometer
                                                        // update is triggered by rising edge of NSS bit (PIN8)
    s_SpiInit.RxBuffer = MyFifoRxBuffer;                // Rolling buffer to be used for reception
    s_SpiInit.RxBufferLen = sizeof( MyFifoRxBuffer );   // Size of the receive buffer
    s_SpiInit.TxBuffer = MyFifoTxBuffer;                // Buffer to be used for transmission
    s_SpiInit.TxBufferLen = sizeof( MyFifoRxBuffer );   // Size

    CX_Configure( CX_SPI,  &s_SpiInit, 0 );
                            
    // NSS (aka CS(neg)) pin setup                        
    CX_Configure( CX_GPIO_PIN8, CX_GPIO_Mode_OUT_PP, 0 );  //Push-pull mode    
    CX_Write( CX_GPIO_PIN8, CX_GPIO_HIGH, 0 );             // initial NSS state is HIGH
    
    // ADC Setup
   
    CX_Configure( CX_ADC1,  0 , 0 );
    
 #endif   
 
    //-------------------------------------
    
    //--- at start, show intro screen for 2 seconds
        
    ActualPendingRequest = PENDING_REQUEST_SHOWING_INTRO_SCREEN;
    GUI(GUI_INTRO_SCREEN,0);                                                     
    UTIL_SetTimer(2000,TimerHandler1);
        
    return MENU_CONTINUE_COMMAND;
    }


/*******************************************************************************
* Function Name  : Application_Handler
* Description    : Management of the Circle_App. This function will be called
*                  every multiple of SysTisk by CircleOS while it returns MENU_CONTINUE.
* Input          : None
* Return         : MENU_CONTINUE
*******************************************************************************/
enum MENU_code Application_Handler(void)
    {
    // This routine will get called repeatedly by CircleOS, until we
    // return MENU_LEAVE
    
    static int GUIUpdate_cnt = 0;    
        
  
    // process special requests first    
    switch(ActualPendingRequest)
    {
        case PENDING_REQUEST_NONE:  // this is default; continue
            break;
                              
        case PENDING_REQUEST_REDRAW:  
            
            BUTTON_SetMode( BUTTON_ONOFF ) ;            
            ActualPendingRequest = PENDING_REQUEST_NONE;           
            GUI(GUI_CLEAR,0);                                                     
            break;       
        
        case PENDING_REQUEST_SHOWING_INTRO_SCREEN:            
            return MENU_CONTINUE;
    }
  
    // normal processing    
    if (!(GUIUpdate_cnt % GUIUPDATE_DIVIDER))
        {
        GUI(GUI_NORMAL_UPDATE,0);     
        ActualBatteryVoltagemV = UTIL_GetBat();        //IH150202 check actual battery status every 100 ticks
        }   
    GUIUpdate_cnt++;
  
    // check button state to invoke main menu
    if ( BUTTON_GetState() == BUTTON_PUSHED )
    {
        BUTTON_WaitForRelease();
        MENU_Set( ( tMenu* ) &MenuMainSTiM32 );
        return MENU_CHANGE;
    }

    return MENU_CONTINUE;  
    }

/*******************************************************************************
* Function Group: Timer Handlers
*******************************************************************************/
void TimerHandler1(void)
    {    
    ActualPendingRequest = PENDING_REQUEST_NONE;
    GUI(GUI_INITIALIZE,0);
    }

/*******************************************************************************
* Function Group: Setup Menu Handlers
*******************************************************************************/
enum MENU_code  MenuSetup_Freq(void)
    {
    MENU_Set( ( tMenu* ) &MenuSetFrequency );                 
    return MENU_CHANGE;
    }

enum MENU_code  MenuSetup_PSeq(void)
    {    
    MENU_Set( ( tMenu* ) &MenuSetPulseSequence );             
    return MENU_CHANGE;
    }

enum MENU_code  MenuSetup_PVolt(void)
    {    
    if(ActualBatteryVoltagemV >= LIMIT_FOR8V_BATTERY_VOLTAGE_MV)
    {
        MENU_Set( ( tMenu* ) &MenuSetPulsePeakVoltage );
    }
    else
    {
        MENU_Set( ( tMenu* ) &MenuSetPulsePeakVoltage_No8VOption );
    }
    return MENU_CHANGE;
    }


enum MENU_code  SetFrequency_1(void)
    {
    PulseSeq.frequency = FREQUENCY_1KHZ;     
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetFrequency_2(void)
    {
    PulseSeq.frequency = FREQUENCY_2KHZ;        
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetFrequency_3(void)
    {
    PulseSeq.frequency = FREQUENCY_3KHZ;    
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseSequence_1(void)
    {       
    PulseSeq.pulseSeq = PULSESEQUENCE_1;    
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseSequence_2(void)
    {
    PulseSeq.pulseSeq = PULSESEQUENCE_2;    
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseSequence_3(void)
    {
    PulseSeq.pulseSeq = PULSESEQUENCE_3;    
    UpdatePulseSequence();    
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseSequence_4(void)
    {
    PulseSeq.pulseSeq = PULSESEQUENCE_4;    
    UpdatePulseSequence();    
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulsePeakVoltage_1(void)
    {
    PulseSeq.peakVoltage = PULSEPEAKVOLTAGE_8V;     
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulsePeakVoltage_2(void)
    {
    PulseSeq.peakVoltage = PULSEPEAKVOLTAGE_6V;     
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulsePeakVoltage_3(void)
    {
    PulseSeq.peakVoltage = PULSEPEAKVOLTAGE_4V;     
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }


enum MENU_code ShutDown( void )
{
        //IH150126 immediate shutdown
        BackUpParameters();
        SHUTDOWN_Action();
}

enum MENU_code Quit( void )
{
        //IH Quit to OS
        ActualPendingRequest = PENDING_REQUEST_REDRAW;   

        BUTTON_WaitForRelease();                                     
        BUTTON_SetMode( BUTTON_ONOFF_FORMAIN );
        
        LCD_SetBackLightOn();
        LCD_SetRotateScreen( 1 );
        MENU_ClearCurrentCommand();
        DRAW_SetDefaultColor();
        DRAW_SetCharMagniCoeff( 1 );                                                      
        DRAW_Clear();
        POINTER_SetMode( POINTER_ON );      
        
        UTIL_SetPll(SPEED_MEDIUM);
                
        UTIL_SetSchHandler(STIMULATOR_HANDLER_ID, 0 );
        LED_Set( LED_GREEN, LED_OFF );
        LED_Set( LED_RED, LED_OFF );
        
        BackUpParameters();
        return MENU_Quit();
}

enum MENU_code RestoreApp( void )
{    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_RESTORE_COMMAND;    
}

enum MENU_code Cancel( void )
{
    ActualPendingRequest = PENDING_REQUEST_REDRAW;        
    return MENU_CONTINUE_COMMAND;
}


/*******************************************************************************
* Function Name  : LongDelay
* Description    : delays in the range of seconds
*                  exit to main menu after 4 seconds
* Input          : u8 delayInSeconds
* Return         : None
*******************************************************************************/
static void LongDelay(u8 delayInSeconds)
    {
    u8 hh, mm, ss, ss2;
    
    RTC_GetTime( &hh, &mm, &ss );
    ss = ss + delayInSeconds;
    ss = ss % 60;

    do
        {
        RTC_GetTime( &hh, &mm, &ss2 );
        }
    while ( ss2 != ss );           // do while < delayInSeconds seconds
    }

/*******************************************************************************
* MACRO Name     : MICROSECONDS_TO_LOOP_COUNTS
* Description    : computes the number of delay loops counts for this simple loop
*                                   
*                   {i=(loopCounts);while(i--);}
*
*                   assuming CPU speed 120MHz
*
* Input          : u32 microseconds
* Return         : u32 loopCounts
*******************************************************************************/
#define MICROSECONDS_TO_LOOP_COUNTS(us)   ((float)(us)*15.0)  //IH150107 corrected (was 7.78 before)

static void UpdatePulseSequence()
    {
        switch(PulseSeq.pulseSeq)
        {
            case PULSESEQUENCE_1:    
                PulseSeq.delay1_microseconds = 200;        
                PulseSeq.delay2_microseconds = 50;        
                PulseSeq.delay3_microseconds = 0;             
                PulseSeq.delay_between_sequences_microseconds = 200;    
                break;
            
            case PULSESEQUENCE_2:    
                PulseSeq.delay1_microseconds = 0;        
                PulseSeq.delay2_microseconds = 50;        
                PulseSeq.delay3_microseconds = 50;          
                PulseSeq.delay_between_sequences_microseconds = 400;        
                break;
            
            case PULSESEQUENCE_3:    
                PulseSeq.delay1_microseconds = 50;        
                PulseSeq.delay2_microseconds = 50;        
                PulseSeq.delay3_microseconds = 50;      
                PulseSeq.delay_between_sequences_microseconds = 400;        
                break;
            
            case PULSESEQUENCE_4:    
                PulseSeq.delay1_microseconds = 400;        
                PulseSeq.delay2_microseconds = 0;        
                PulseSeq.delay3_microseconds = 0;
                PulseSeq.delay_between_sequences_microseconds = 100;        
                break;
        }
    
        PulseSeq.delay0_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay0_microseconds);
        PulseSeq.delay1_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay1_microseconds);
        PulseSeq.delay2_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay2_microseconds);
        PulseSeq.delay3_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay3_microseconds);     
        PulseSeq.delay_between_sequences_loop_counts
                                    = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay_between_sequences_microseconds);     
    
        switch(PulseSeq.frequency)
        {
            case FREQUENCY_1KHZ:    
                    PulseSeq.frequency_divider = 3;     
                    PulseSeq.sequence_multiplicity = SEQUENCEMULTIPLICITY_SINGLE;
                    break; 
            case FREQUENCY_2KHZ:    
                    PulseSeq.frequency_divider = 3;     
                    PulseSeq.sequence_multiplicity = SEQUENCEMULTIPLICITY_DOUBLE;
                    break;
            case FREQUENCY_3KHZ:    
                    PulseSeq.frequency_divider = 1;     
                    PulseSeq.sequence_multiplicity = SEQUENCEMULTIPLICITY_SINGLE;
                    break;
        }
    
        switch(PulseSeq.peakVoltage)
        {
            case PULSEPEAKVOLTAGE_8V:    
                    PulseSeq.voltage_multiplication_factor =  8.0/8.0;
                    break;
            case PULSEPEAKVOLTAGE_6V:    
                    PulseSeq.voltage_multiplication_factor =  6.0/8.0;
                    break;
            case PULSEPEAKVOLTAGE_4V:    
                    PulseSeq.voltage_multiplication_factor =  4.0/8.0;
                    break;
        }    
    
       PulseSeq.voltage_multiplication_factor *= ((float)NOMINAL_BATTERY_VOLTAGE_MV)/((float)ActualBatteryVoltagemV);  
       if(PulseSeq.voltage_multiplication_factor >1.0)
       {
            PulseSeq.voltage_multiplication_factor = 1.0;
       }       
                    
    }


/*******************************************************************************
* Function Name  : GeneratePulseSequenceAndReadCAE
* Description    : Generates a single output pulse sequence according to PulseSeq data
                   and reads CAE (only if  PulseSeq.delay1_loop_counts>0)
                    
                    IH141230
                    In the current implementation, the values of edgeN are ignored
                    and the pulse pattern is like follows:
                    If d1>0, the first pulse is POSITIVE_VOLTAGE_MAX, otherwise the first pulse is omitted
                    If d3>0, the second pulse is NEGATIVE_VOLTAGE_MAX, otherwise the second pulse is omitted    

* Input          : None
* Return         : None
*******************************************************************************/
static void GeneratePulseSequenceAndReadCAE()
    {u32 i;    
     u32 ad_value_0_to_4095;
    
     u32 ad_value_offset  =  1500;          // ADC values under this are presented as 0
     u32 ad_value_reciproq_scale   =  3;    // values are DIVIDED by this factor
    
    SetOutputVoltage(ZERO_VOLTAGE,PulseSeq.voltage_multiplication_factor);
    WHILE_DELAY_LOOP(PulseSeq.delay0_loop_counts)

    if(PulseSeq.delay1_loop_counts>0)
    {    
        SetOutputVoltage(POSITIVE_VOLTAGE_MAX,PulseSeq.voltage_multiplication_factor);
    
        CX_Read(CX_ADC1, &ad_value_0_to_4095, 0);    
        if(ad_value_0_to_4095 < ad_value_offset)
        {
            Readout.CAE1 = 0;
            Readout.isOverloaded = 0;
        }
        else if (ad_value_0_to_4095 == 4095)
        {
            Readout.CAE1 = 0;
            Readout.isOverloaded = 1;
        }
        else    
        {        
            Readout.CAE1 = (ad_value_0_to_4095 - ad_value_offset)/ad_value_reciproq_scale;    
            Readout.isOverloaded = 0;
        }
    
        WHILE_DELAY_LOOP(PulseSeq.delay1_loop_counts)        
    }
        
    SetOutputVoltage(ZERO_VOLTAGE,PulseSeq.voltage_multiplication_factor);
    
    WHILE_DELAY_LOOP(PulseSeq.delay2_loop_counts)    
   
    if(PulseSeq.delay3_loop_counts>0)
    {    
        SetOutputVoltage(NEGATIVE_VOLTAGE_MAX,PulseSeq.voltage_multiplication_factor);
        WHILE_DELAY_LOOP(PulseSeq.delay3_loop_counts)    
    }
    
    SetOutputVoltage(ZERO_VOLTAGE,PulseSeq.voltage_multiplication_factor);                  
    }   

/*******************************************************************************
* Function Name  : SetOutputVoltage
* Description    : controls the MAX5439 digital potentiometer connected like this
                    L ... negative voltage input (typ -10V)
                    H ... positive voltage input (typ +10V)
                    W ... output voltage (the wiper between L and H)
                   MAX5439 has 128 taps so the control word has 7 bits.

* Input          : OutputVoltage_code oVcode
*                  multiplication_factor : float from 0 to 1
* Return         : None
*******************************************************************************/
static void SetOutputVoltage(OutputVoltage_code oVcode,float multiplication_factor)
    {
  
        static u8 controlByteForMAX5439=0;

        volatile u32 nb_byteSent = 1;
        
        switch(oVcode)
        {
            case POSITIVE_VOLTAGE_MAX:      controlByteForMAX5439= 63 + 64*multiplication_factor;  break;
            case POSITIVE_VOLTAGE_HALF:     controlByteForMAX5439= 63 + 32*multiplication_factor;  break; 
            case ZERO_VOLTAGE:              controlByteForMAX5439= 63 ; break;
            case NEGATIVE_VOLTAGE_HALF:     controlByteForMAX5439= 63 - 32*multiplication_factor;  break;  
            case NEGATIVE_VOLTAGE_MAX:      controlByteForMAX5439= 63 - 63*multiplication_factor;  break;  
                                                                        //IH150203 not absolutely exact, but OK
        }
    
        CX_Write(CX_GPIO_PIN8,CX_GPIO_LOW,0);     

        CX_Write(CX_SPI,&controlByteForMAX5439,&nb_byteSent);
        CX_Write(CX_GPIO_PIN8,CX_GPIO_HIGH,0);  //IH141230 this rising edge of the NSS signal actually sets the wiper 
                                                // (see MAX5439 datasheet)
    
        //IH140912 we do not wait for end of the transmission here, neither do we check the success
    
    }

/*******************************************************************************
* Function Name  : GUI
* Description    : GUI management
* Input          :  GUIaction
                    readout1
* Return         : None
*******************************************************************************/
static void GUI(GUIaction_code GUIaction, u16 readout1)
    {
    
#define STIM_UPPERPANEL_HEIGHT    60
#define STIM_LOWERPANEL_HEIGHT    30
#define STIM_MIDDLEPANEL_HEIGHT   (SCREEN_HEIGHT-STIM_LOWERPANEL_HEIGHT-STIM_UPPERPANEL_HEIGHT)
    
#define STIM_UPPERPANEL_COLOR     RGB_MAKE(0x00, 0x00, 0x88)
#define STIM_LOWERPANEL_COLOR     RGB_MAKE(0x00, 0x33, 0x00)
#define STIM_MIDDLEPANEL_COLOR    RGB_MAKE(0x0F, 0x0F, 0x0F)
#define STIM_BARBG_COLOR          RGB_WHITE
#define STIM_BARFG_COLOR          RGB_RED
       
#define STIM_SINGLE_BAR_WIDTH     8
    
    static StimState_code lastStimState = STIMSTATE_RUN;
    static u16 barPosX = 0;
    u16 barWidth = STIM_SINGLE_BAR_WIDTH;
    
    static UpperPanelState_code thisUpperPanelState;
    static UpperPanelState_code lastUpperPanelState = UPPERPANELSTATE_WAITING;  //IH150210 check if this is correct initialization
        
    float readoutYScalingFactor = 0.15;  
    
        switch(GUIaction)
        {
        case GUI_CLEAR:         //IH140319 currently identical with GUI_INITIALIZE
        case GUI_INITIALIZE:
            
            lastStimState = STIMSTATE_RUN;
            barPosX = 0;
                            
            // graphics
            // These are default values
            DRAW_SetCharMagniCoeff(1);
            DRAW_SetTextColor(RGB_WHITE);     
            DRAW_SetBGndColor(STIM_LOWERPANEL_COLOR);        
                        
            //Lower panel
            LCD_FillRect( 
                0, 0, 
                SCREEN_WIDTH, STIM_LOWERPANEL_HEIGHT, 
                STIM_LOWERPANEL_COLOR );
        
            //Middle panel
            LCD_FillRect(
                0, STIM_LOWERPANEL_HEIGHT, 
                SCREEN_WIDTH, STIM_MIDDLEPANEL_HEIGHT,                 
                STIM_MIDDLEPANEL_COLOR );
              
            //Upper panel
            LCD_FillRect(
                0, SCREEN_HEIGHT-STIM_UPPERPANEL_HEIGHT, 
                SCREEN_WIDTH, 
                STIM_UPPERPANEL_HEIGHT, 
                STIM_UPPERPANEL_COLOR );
                    
            break;
            
        case GUI_NORMAL_UPDATE:
                    
            
            if(Readout.isOverloaded)
            {
                thisUpperPanelState = UPPERPANELSTATE_OVERLOAD;                
            }
            else if(StimState==STIMSTATE_IDLE || StimState==STIMSTATE_WAITING_FOR_RUN)
            {
                thisUpperPanelState = UPPERPANELSTATE_WAITING;                                
            }
            else                
            {   
                thisUpperPanelState = UPPERPANELSTATE_DISPLAY_READOUT;                
            }
            if((thisUpperPanelState == lastUpperPanelState) && (thisUpperPanelState != UPPERPANELSTATE_DISPLAY_READOUT)) break;
            lastUpperPanelState = thisUpperPanelState;    
        
            // clear upper panel            
            LCD_FillRect(
                0, SCREEN_HEIGHT-STIM_UPPERPANEL_HEIGHT, 
                SCREEN_WIDTH, 
                STIM_UPPERPANEL_HEIGHT, 
                STIM_UPPERPANEL_COLOR );
            
            {
            u8 str[30];        
            if(thisUpperPanelState == UPPERPANELSTATE_OVERLOAD)
            {
                strcpy(str,"    OVERLOAD");
                PlayBeep();
                DRAW_SetCharMagniCoeff(2);            
            }
            else if(thisUpperPanelState == UPPERPANELSTATE_WAITING)
            {
                strcpy(str,"    Waiting...");
                DRAW_SetCharMagniCoeff(2);            
            }
            else
            {
                // display readout figure
                UTIL_int2str( str, Readout.CAE1, 4, FALSE);    
                DRAW_SetCharMagniCoeff(4);            
            }
            
            DRAW_SetTextColor(RGB_YELLOW);     
            DRAW_SetBGndColor(STIM_UPPERPANEL_COLOR);        
            
            DRAW_DisplayStringWithMode( 0,180,str, 0, NORMAL_TEXT, LEFT);            
            
            DRAW_SetCharMagniCoeff(1);            
            DRAW_SetTextColor(RGB_WHITE);     
            DRAW_SetBGndColor(STIM_LOWERPANEL_COLOR);        
            }
        
            // display graphics
            switch(StimState)
            {
            case STIMSTATE_IDLE:  
            case STIMSTATE_WAITING_FOR_RUN:  
                if(lastStimState!=STIMSTATE_IDLE)
                    {
                    //Clean middle panel
                    LCD_FillRect(
                        0, STIM_LOWERPANEL_HEIGHT, 
                        SCREEN_WIDTH, STIM_MIDDLEPANEL_HEIGHT,
                        STIM_MIDDLEPANEL_COLOR );                                                                           
                    }
                lastStimState = STIMSTATE_IDLE;
                break;
        
            case STIMSTATE_RUN:                 
            case STIMSTATE_WAITING_FOR_IDLE:  
                if(lastStimState!=STIMSTATE_RUN)
                    {
                    //Clean middle panel
                    LCD_FillRect(
                        0, STIM_LOWERPANEL_HEIGHT, 
                        SCREEN_WIDTH, STIM_MIDDLEPANEL_HEIGHT,                        
                        STIM_MIDDLEPANEL_COLOR );
                    
                    barPosX=0;
                    }
                else
                    {
                    u16 barHeight = Readout.CAE1 * readoutYScalingFactor;
                    if(barHeight>STIM_MIDDLEPANEL_HEIGHT)
                        {
                        barHeight>STIM_MIDDLEPANEL_HEIGHT;
                        }
                    LCD_FillRect(
                        barPosX, STIM_LOWERPANEL_HEIGHT, 
                        barWidth, STIM_MIDDLEPANEL_HEIGHT,                        
                        STIM_BARBG_COLOR );                    
                    LCD_FillRect(
                        barPosX, STIM_LOWERPANEL_HEIGHT, 
                        barWidth, barHeight,                        
                        STIM_BARFG_COLOR );                    
                    barPosX += barWidth;
                    
                    if(barPosX>SCREEN_WIDTH)
                        {
                        barPosX;
                        }
                    }
                lastStimState = STIMSTATE_RUN;                
                if(barPosX>SCREEN_WIDTH)
                        {
                        lastStimState = STIMSTATE_IDLE;  //begin new graphics screen                
                        }
                break;
                                    
            }        
                                        
            //display current settings and time
            DRAW_DisplayStringWithMode( 8,10,GetSettingsString(), 0, NORMAL_TEXT, RIGHT);            
                   
            break;            
        
        case GUI_INTRO_SCREEN:            
            
            DRAW_SetCharMagniCoeff(2);
            DRAW_SetTextColor(RGB_GREEN);                 
            
            LCD_FillRect(
            0, 0, 
            SCREEN_WIDTH, SCREEN_HEIGHT,                 
            RGB_ORANGE );
            
            DRAW_DisplayStringWithMode( 0,180,"STiM32", ALL_SCREEN, INVERTED_TEXT, CENTER);            
            DRAW_SetCharMagniCoeff(1);
            DRAW_DisplayStringWithMode( 0,160,STIM32_VERSION, ALL_SCREEN, INVERTED_TEXT, CENTER);            
        
            DRAW_SetCharMagniCoeff(1);
            DRAW_DisplayStringWithMode( 0,100,GetBatteryStatusString(), ALL_SCREEN, NORMAL_TEXT, CENTER);            
            break;                                                     
        }
    }

/*******************************************************************************
* Function Name  : SetAutorun
* Description    : Sets the bit 7 in SYS2 backup register to autorun this application 
* Input          : None                     
* Return         : None
*******************************************************************************/
static void SetAutorun(void)
    {
        //IH150125 the autorun is currently set in the CircleOS menu
    }

static void BackUpParameters(void)
{   
    UTIL_WriteBackupRegister (BKP_FREQUENCY, PulseSeq.frequency);
    UTIL_WriteBackupRegister (BKP_PULSESEQ, PulseSeq.pulseSeq);
    UTIL_WriteBackupRegister (BKP_PULSEPEAKVOLTAGE, PulseSeq.peakVoltage);

return;
}

static void RestoreParameters(void)
{
    u32 p_Frequency             = UTIL_ReadBackupRegister (BKP_FREQUENCY);
    u32 p_PulseSeq              = UTIL_ReadBackupRegister (BKP_PULSESEQ);
    u32 p_PulsePeakVoltage      = UTIL_ReadBackupRegister (BKP_PULSEPEAKVOLTAGE);

    // set defaults if backup not valid
    if(p_Frequency>0)           { PulseSeq.frequency = p_Frequency;         }  else  { PulseSeq.frequency = FREQUENCY_1KHZ; }
    if(p_PulseSeq>0)            { PulseSeq.pulseSeq = p_PulseSeq;           }  else  { PulseSeq.pulseSeq  = PULSESEQUENCE_1;  }
    if(p_PulsePeakVoltage>0)    { PulseSeq.peakVoltage = p_PulsePeakVoltage;}  else  { PulseSeq.peakVoltage  = PULSEPEAKVOLTAGE_8V
    ;  }

                
    return;
}

static char* GetSettingsString(void)
    {
        char *frequency_string;
        char *pulseSeq_string;
        char *peakVoltage_string;
        char time_string[6];
    
        switch(PulseSeq.frequency)
        {
            case FREQUENCY_1KHZ:
                    frequency_string = "1kHz";                    
                    break;
            case FREQUENCY_2KHZ:
                    frequency_string = "2kHz";                    
                    break;
            case FREQUENCY_3KHZ:
                    frequency_string = "3kHz";                    
                    break;
        }
    
        switch(PulseSeq.pulseSeq)
        {
            case PULSESEQUENCE_1:
                    pulseSeq_string = "Seq1";                    
                    break;
            case PULSESEQUENCE_2:
                    pulseSeq_string = "Seq2";                    
                    break;
            case PULSESEQUENCE_3:
                    pulseSeq_string = "Seq3";                    
                    break;
            case PULSESEQUENCE_4:
                    pulseSeq_string = "Seq4";                    
                    break;
        }
    
        switch(PulseSeq.peakVoltage)
        {
            case PULSEPEAKVOLTAGE_8V:
                    peakVoltage_string = "8V";                    
                    break;
            case PULSEPEAKVOLTAGE_6V:
                    peakVoltage_string = "6V";                    
                    break;
            case PULSEPEAKVOLTAGE_4V:
                    peakVoltage_string = "4V";                    
                    break;
        }       

        {
        u32 THH, TMM, TSS;
        char mm_string[3];
        char ss_string[3];
        RTC_GetTime (&THH, &TMM, &TSS);
        UTIL_int2str( mm_string, TMM, 2, TRUE);    
        UTIL_int2str( ss_string, TSS, 2, TRUE);    
        strcpy(time_string, mm_string);   
        strcat(time_string,":");    
        strcat(time_string, ss_string);    //IH150128 BUG here: space between : and ss_string (??)
        //IH150128 Hours are ignored
        }
        
    
        // max string lenght is SETTINGS_STRING_LENGHT
        strcpy(SettingsStatusString, frequency_string); // lenght = 4
        strcat(SettingsStatusString, "   ");            // lenght = 3
        strcat(SettingsStatusString, pulseSeq_string);  // length = 4
        strcat(SettingsStatusString, "   ");            // lenght = 3
        strcat(SettingsStatusString, peakVoltage_string);  // length = 2
        strcat(SettingsStatusString, "   ");            // lenght = 3
        strcat(SettingsStatusString, time_string);      // length = 5
    
        return SettingsStatusString;
    }

static char* GetBatteryStatusString(void)
    {
        u16 vbat_mV = UTIL_GetBat();
        
        // max string lenght is BATTERY_STATUS_STRING_LENGHT
        strcpy(BatteryStatusString, "Battery OK");
        if(vbat_mV<VBAT_MV_LOW)
        {
            strcpy(BatteryStatusString, "Battery LOW");
        }
    
        return BatteryStatusString;
    }

static void PlayBeep(void)
{
    //IH150128 TODO this takes too long, we need a short buzzer beep here

    u8 *beepMusic ="beep1:d=32,o=6,b=900:e";
    BUZZER_PlayMusic(beepMusic);
}