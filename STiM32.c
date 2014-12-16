/******************************************************************************
*
* File Name          :  STiM32.c
* Description        :  STIMULATOR Control firmware 
*
* Last revision      :  IH 2014-12-16
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
#define STIM32_VERSION          "141216"

#define  STIMULATOR_HANDLER_ID  UNUSED5_SCHHDL_ID
#define  GUIUPDATE_DIVIDER      1       // GUI is called every 100 SysTicks
#define  STATECHANGE_CNT_LIMIT  10

#define  FIFO_SIZE              128

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
    POSITIVE_VOLTAGE_MAX,
    POSITIVE_VOLTAGE_HALF,
    ZERO_VOLTAGE,
    NEGATIVE_VOLTAGE_HALF,
    NEGATIVE_VOLTAGE_MAX,
    } OutputVoltage_code;

typedef enum {
    FREQUENCY_1KHZ=1,
    FREQUENCY_2KHZ=2,
    FREQUENCY_3KHZ=3,
    } Frequency_code;

typedef struct 
    {
        Frequency_code frequency;
        u16 frequency_divider;
    
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
        u32 CAE1;   // current after edge1
        u32 ADCOUT_FOR_DEBUG;
    }
    Readout_struct;

/* Forward declarations ------------------------------------------------------*/
enum MENU_code Application_Handler(void);

enum MENU_code  Cancel( void );
enum MENU_code  Quit( void );
enum MENU_code  RestoreApp( void );

enum MENU_code  MenuSetup_Freq();
enum MENU_code  MenuSetup_PDur();
enum MENU_code  MenuSetup_C();
enum MENU_code  MenuSetup_D();

enum MENU_code  SetFrequency_1();
enum MENU_code  SetFrequency_2();
enum MENU_code  SetFrequency_3();

enum MENU_code  SetPulseDuration_1();
enum MENU_code  SetPulseDuration_2();
enum MENU_code  SetPulseDuration_3();
enum MENU_code  SetPulseDuration_4();

void TimerHandler1(void);

static void GUI(GUIaction_code, u16 );
static void LongDelay(u8 delayInSeconds);
static enum MENU_code MsgVersion(void);
static void UpdatePulseSequence(void);

static void SetOutputVoltage(OutputVoltage_code);
    

/* Constants -----------------------------------------------------------------*/
const char Application_Name[8+1] = {"STiM32"};      // Max 8 characters

tMenu MenuMainSTiM32 =
{
    1,
    "STiM32 Main Menu",
    4, 0, 0, 0, 0, 0,
    0,
    {
        { "Set Frequency",           MenuSetup_Freq,    Application_Handler,    0 },
        { "Set Pulse Duration",      MenuSetup_PDur,    Application_Handler ,   0 },
        { "Cancel",                  Cancel,            RestoreApp ,            0 },
        { "Quit",                    Quit,              0,                      1 },            
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

tMenu MenuSetPulseDuration =
{
    1,
    "Set Pulse Duration",
    5, 0, 0, 0, 0, 0,
    0,
    {
        { "PDur 1",     SetPulseDuration_1,    Application_Handler,    0 },
        { "PDur 2",     SetPulseDuration_2,    Application_Handler ,   0 },
        { "PDur 3",     SetPulseDuration_3,    Application_Handler ,   0 },
        { "PDur 4",     SetPulseDuration_4,    Application_Handler ,   0 },
        { "Cancel",     Cancel,                Application_Handler,    0 },
    }
};


/* Global variables ----------------------------------------------------------*/
static PendingRequest_code ActualPendingRequest;
static Pulse_Sequence_struct PulseSeq;
static Readout_struct Readout;
static StimState_code StimState;
static u16 ReadoutLimit_CAE1_for_Run;
static u16 ReadoutLimit_CAE1_for_Idle;

static u8 MyFifoRxBuffer[FIFO_SIZE];       
static u8 MyFifoTxBuffer[FIFO_SIZE];

static bool time1Elapsed;

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

volatile u32 nb_bytes = 0;
volatile u32 nb_byteSent = 0;

volatile u32 ad_value_0_to_4095;

char* pbuff; 

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


    // Real code using connected hardware
                    
                        

    {u32 i;
    
    
    SetOutputVoltage(ZERO_VOLTAGE);
    WHILE_DELAY_LOOP(PulseSeq.delay0_loop_counts)

    SetOutputVoltage(POSITIVE_VOLTAGE_MAX);
    WHILE_DELAY_LOOP(PulseSeq.delay1_loop_counts)    
        

    SetOutputVoltage(ZERO_VOLTAGE);
    WHILE_DELAY_LOOP(PulseSeq.delay2_loop_counts)    

    SetOutputVoltage(POSITIVE_VOLTAGE_HALF);
    WHILE_DELAY_LOOP(PulseSeq.delay3_loop_counts)    
    
    SetOutputVoltage(ZERO_VOLTAGE);
    
    CX_Read(CX_ADC1, &ad_value_0_to_4095, 0);
    Readout.ADCOUT_FOR_DEBUG = ad_value_0_to_4095;    
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
    
    
    //-------------------------------------
    // Initialize ...
        
    
    // ... Pulse Sequence
    PulseSeq.frequency = FREQUENCY_3KHZ; 
    
    PulseSeq.delay0_microseconds = 0;    
    PulseSeq.edge1  = 10;    
    PulseSeq.delay1_microseconds = 50;    
    PulseSeq.edge2  = 10;    
    PulseSeq.delay2_microseconds = 10;    
    PulseSeq.edge3  = 10;    
    PulseSeq.delay3_microseconds = 50;    
    PulseSeq.edge4  = 10;    
        
    UpdatePulseSequence();
        
    
    // ... GUI    
    GUI(GUI_INITIALIZE,0);
    
    // ... request mechanism
    ActualPendingRequest = PENDING_REQUEST_NONE;
    
    // ... state machine
    StimState = STIMSTATE_IDLE; 
    
    // ... readout limits
    ReadoutLimit_CAE1_for_Run = 10;
    ReadoutLimit_CAE1_for_Idle = 10;

    // ... miscellaneous    



    // ... CX Extension

    
#ifdef DEBUG_NOHW       

    // test settings
    
    CX_Configure( CX_GPIO_PIN4, CX_GPIO_Mode_OUT_PP, 0 );  //Push-pull mode
    CX_Write( CX_GPIO_PIN4, CX_GPIO_LOW, 0 );
    
#else        

    // real settings
    
    // SPI Setup
    
    tCX_SPI_Config s_SpiInit;
    
    s_SpiInit.Speed = CX_SPI_Mode_High;                 // The speed range of the serial bit rate.
    s_SpiInit.WordLength = CX_SPI_8_Bits;               // The number of transferred data bit. Standard is 8, but could be 16 for some specific devices.
    s_SpiInit.Mode = CX_SPI_MODE_MASTER;                // 1: master, 0: slave
    s_SpiInit.Polarity = CX_SPI_POL_LOW;                // Indicates the steady state (idle state of the clock when no transmission).
    s_SpiInit.Phase = CX_SPI_PHA_FIRST;                 // Phase:  0 indicates that the first edge of the clock when leaving the idle state is active
                                                        //         1 indicates that the second edge of the clock when leaving the idle state is active
    s_SpiInit.MSB1LSB0 = CX_SPI_MSBFIRST;               // First bit to be sent.  1: MSB first, 0: LSB first
    s_SpiInit.Nss = CX_SPI_Soft;                        // NSS signal management : 1 = by hardware (NSS pin), 0 = by software using the SSI bit
    s_SpiInit.RxBuffer = MyFifoRxBuffer;                // Rolling buffer to be used for reception
    s_SpiInit.RxBufferLen = sizeof( MyFifoRxBuffer );   // Size of the receive buffer
    s_SpiInit.TxBuffer = MyFifoTxBuffer;                // Buffer to be used for transmission
    s_SpiInit.TxBufferLen = sizeof( MyFifoRxBuffer );   // Size

    CX_Configure( CX_SPI,  &s_SpiInit, 0 );
        
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

enum MENU_code  MenuSetup_PDur(void)
    {    
    MENU_Set( ( tMenu* ) &MenuSetPulseDuration );             
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

enum MENU_code  SetPulseDuration_1(void)
    {       
    PulseSeq.delay1_microseconds = 50;        
    PulseSeq.delay2_microseconds = 10;        
    PulseSeq.delay3_microseconds = 50;                
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseDuration_2(void)
    {    
    PulseSeq.delay1_microseconds = 10;        
    PulseSeq.delay2_microseconds = 90;        
    PulseSeq.delay3_microseconds = 10;                
    UpdatePulseSequence();
    
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseDuration_3(void)
    {
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code  SetPulseDuration_4(void)
    {
    ActualPendingRequest = PENDING_REQUEST_REDRAW;    
    return MENU_CONTINUE_COMMAND;
    }

enum MENU_code Quit( void )
{
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
#define MICROSECONDS_TO_LOOP_COUNTS(us)   ((float)(us)*7.78)

static void UpdatePulseSequence()
    {
        PulseSeq.delay0_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay0_microseconds);
        PulseSeq.delay1_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay1_microseconds);
        PulseSeq.delay2_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay2_microseconds);
        PulseSeq.delay3_loop_counts = MICROSECONDS_TO_LOOP_COUNTS(PulseSeq.delay3_microseconds);     
    
        switch(PulseSeq.frequency)
        {
            case FREQUENCY_1KHZ:    PulseSeq.frequency_divider = 3;     break;
            case FREQUENCY_2KHZ:    PulseSeq.frequency_divider = 1;     break;  //IH140321 TODO   THIS DOES NOT WORK LIKE THIS: 
            case FREQUENCY_3KHZ:    PulseSeq.frequency_divider = 1;     break;
        }
    }

/*******************************************************************************
* Function Name  : SetOutputVoltage
* Description    : controls the MAX5439 digital potentiometer connected like this
                    L ... negative voltage input (typ -10V)
                    H ... positive voltage input (typ +10V)
                    W ... output voltage (the wiper between L and H)
                   MAX5439 has 128 taps so the control word has 7 bits.

* Input          : OutputVoltage_code oVcode
* Return         : None
*******************************************************************************/
static void SetOutputVoltage(OutputVoltage_code oVcode)
    {
    
        static u8 controlByteForMAX5439=0;        
        volatile u32 nb_byteSent = 1;
    
        switch(oVcode)
        {
            case POSITIVE_VOLTAGE_MAX:      controlByteForMAX5439=127;  break;
            case POSITIVE_VOLTAGE_HALF:     controlByteForMAX5439=95;   break;
            case ZERO_VOLTAGE:              controlByteForMAX5439=63;   break;
            case NEGATIVE_VOLTAGE_HALF:     controlByteForMAX5439=31;   break;
            case NEGATIVE_VOLTAGE_MAX:      controlByteForMAX5439=0;    break;
        }
    
        CX_Write(CX_SPI,&controlByteForMAX5439,&nb_byteSent);
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
    
        
    float readoutYScalingFactor = 0.1;  //was 1.1 for debugging
    
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
            
            // display readout figure
            {
            u8 str[30];        
            UTIL_int2str( str, Readout.CAE1, 4, FALSE);    
            
            //IH141010 FOR DEBUGGING ONLY --------
                UTIL_int2str( str, Readout.ADCOUT_FOR_DEBUG, 4, FALSE);    
            //------------------                
            
            DRAW_SetCharMagniCoeff(4);            
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
                
            // display time
            DRAW_DisplayTime( 10, 10);            
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
        
            //IH140321 TODO show battery status
            DRAW_SetCharMagniCoeff(1);
            DRAW_DisplayStringWithMode( 0,100,"Battery: OK", ALL_SCREEN, NORMAL_TEXT, CENTER);            
            break;                                                     
        }
    }

