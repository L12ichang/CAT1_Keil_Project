/*************************************************************
程序功能：贝岭电能计量
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_bl0942.h"
#include "sys_tick.h"
#include "u32_q.h"
//#include "sys_data.h"
#include "hw_uart2.h"
#if BL0942_REPRO_TEST_ENABLE
#include "Portable.h"
#endif
//#include "hw_bl0942.h"
//#include "sys_rn8209c.h"
#include "sys_data.h"
#include <math.h>
#include "factory_user_data.h"
#include "sys_calibration_snapshot.h"
#include "sys_bl0942_frame.h"
#define READ_HEADER                 0x58
#define WRITE_HEADER                0xa8
#define READ_ALL_HEAD               0xaa

#define WRITE_PACKET_LENGTH         6
#define READ_PACKET_LENGTH          4
#define READ_PACKET_MAX_LENGTH      23
#define FS                          500000

//电参数寄存器
#define Addr_I_WAVE									0x01					 //电流通道波形*
#define Addr_V_WAVE									0x02					//电压通道波形*
#define Addr_I_RMS									0x03					 //电流有效值
#define Addr_V_RMS									0x04					//电压有效值
#define Addr_I_FAST_RMS						    	0x05					 //电流快速有效值*
#define Addr_WATT									0x06			 		 //有功功率
#define Addr_CF_CNT									0x07					 //有功电能脉冲计数
#define	Addr_FREQ									0x08					 //工频频率
#define Addr_STATUS									0x09					 //状态
#define Addr_VERSION								0x0F					//版本
//用户操作寄存器
#define Addr_I_CHOS									0x11						//电流通道直流偏置校正
#define Addr_I_RMSOS								0x12						//电流通道有效值小信号校正
#define Addr_WA_CREEP								0x14						//有功功率防潜阈值
#define Addr_FAST_RMS_TH						    0x15						//
#define Addr_FAST_RMS_CYC						    0x16
#define Addr_FREQ_CYC								0x17
#define Addr_MASK								    0x18
#define Addr_MODE								    0x19
#define Addr_GAIN_CR								0x1A
#define Addr_SOFT_RESET							    0x1C						//软复位
#define Addr_WRPROT									0x1D						//用户写保护设置





#define STOP                    0   //dali2_device_variables_ram.FailureStatus.short_circuit
/*   移植关闭
#define UPDATE_AC_POWERFACTOR(pf)           part253_set_PowerFactorRange(pf);
#define UPDATE_AC_ENERGYP(EnergyP)          part252_set_ActiveEnergy(EnergyP)
#define UPDATE_AC_POWER(power)              part252_set_ActivePower(power)
#define LOAD_AC_ENERGYP()               sys_data.ac_EnergyP //重新上电时加载           
#define STORE_AC_ENERGYP(EnergyP)       sys_data.ac_EnergyP = EnergyP;   //刷新数据，用于本模块之外的数据来储存          
#define UPDATE_AC_FREQ(freq)            part253_set_ExternalSupplyVoltageFrequenc(freq)
#define UPDATE_AC_VOLTAGE(vol)          part253_set_ExternalSupplyVoltag(vol)
#define UPDATE_AC_APPARENT_POWER(power)          part252_set_ApparentPower(power)
*/
typedef enum
{
    SYS_BL0942_STATE_IDLE,
    SYS_BL0942_STATE_INIT,    
    SYS_BL0942_STATE_READ,    
    SYS_BL0942_STATE_WAIT_READ_READY,    
}sys_bl0942_state_en;

typedef enum
{
    SYS_BL0942_INIT_IDLE,
    SYS_BL0942_INIT_WRITE_ENABLE,
    SYS_BL0942_INIT_WRITE_DISABLE,
    SYS_BL0942_INIT_COMPLETE
}sys_bl0942_init_en;


u16  ac_voltage_8209;   //交流电的电压，单位 0.1V
u16  ac_current;   // 交流电的电流，单位 mA
u16  Z_ac_current; // 休正后的交流电的电流，单位 mA  修正背景：电路中有CX电容中出现容性无功电流，需要算法处理输出交准确的电流
u16  ac_freq;      // 交流电的频率单位0.01HZ
u16  ac_power_S;   //
u16  ac_powerpa;   //有功功率  单位：0.01W
u16  ac_powerq;    //无功功率
u32  energy_this_time = 0;

u32 total_power_this_time=0;
u32 bl0942_checksum_error_count = 0;
u32 bl0942_timeout_count = 0;
u32 bl0942_uart_error_count = 0;
u32 bl0942_compat_frame_count = 0;

#if BL0942_REPRO_TEST_ENABLE
volatile u32 g_bl0942_repro_read_start_count = 0;
volatile u32 g_bl0942_repro_frame_ok_count = 0;
volatile u32 g_bl0942_repro_frame_bad_count = 0;
volatile u32 g_bl0942_repro_last_ok_tick = 0;
#endif

sys_bl0942_state_en  sys_bl0942_state = SYS_BL0942_STATE_IDLE;
sys_bl0942_init_en  sys_bl0942_init1 =  SYS_BL0942_INIT_IDLE;

static u8  _tx_buffer[READ_PACKET_MAX_LENGTH];
static u16  _timer_for_read = 1000;
static u32  _ac_EnergyP;   //耗电量，单位 wh  ,千分之一度
//static u32 xdata _cf_cnt_first=0xffffffff;
//static u32 xdata _ac_EnergyP_first=0; //上电后，cf_cnt可能不是0。这样当cf_cnt溢出时使用的电量。单位 wh  ,千分之一度

static u32  _cf_cnt_bak=0;

static u8  _cf_over_cnt=0;
static u32  energy_tmp = 0; //有功电能， 断电归0     单位 0.01WH

static u32 bl0942_energy_counter_raw = 0;
static u32 bl0942_energy_clear_base = 0;
static boolean_en bl0942_energy_clear_pending = BOOL_FALSE;

//u32 xdata i_rms;
//u32 xdata v_rms;
//u32 xdata i_fast_rms;
//u32 xdata watt;
//u32 xdata cf_cnt;
//u32 xdata freq;
u8  ac_pf;
static u16 _timer=0;
static u16 minute=0;
static u32 bl0942_energy_0_01wh_remainder = 0;
static u32 bl0942_last_uart_error_count = 0;

#define SYS_BL0942_ENERGY_ACCUM_DIVISOR 60U

   u8 bl0942data_ready;
void sys_bl0942_timer(void)
{
    u32 energy_accum;

    if(_timer_for_read > 0)
    {
        --_timer_for_read;
    }
     ++_timer;
    if(_timer > 6000)//满一分钟
    {
          _timer=0;

        /* ac_powerpa is 0.01W. One minute of energy in 0.01Wh is ac_powerpa / 60. */
        energy_accum = (u32)ac_powerpa + bl0942_energy_0_01wh_remainder;
        total_power_this_time += energy_accum / SYS_BL0942_ENERGY_ACCUM_DIVISOR;
        bl0942_energy_0_01wh_remainder = energy_accum % SYS_BL0942_ENERGY_ACCUM_DIVISOR;

       // printf("total_power_this_time=%d(0.01Wh)\n",(u16)total_power_this_time);
        
        if(  ++ minute>=720)//每12小时(720分钟)存一次累积能耗到Flash
        {
          minute  =0;
            sys_data.ac_EnergyP+=total_power_this_time;  //将本周期累积能耗加入累计值
            total_power_this_time=0;
          sys_data_store() ;
        }
        
    }
}

static void sys_bl0942_check_uart_error(void)
{
    u32 uart_error_count;

    uart_error_count = hw_uart2_get_error_count();
    if (uart_error_count != bl0942_last_uart_error_count)
    {
        bl0942_uart_error_count += uart_error_count - bl0942_last_uart_error_count;
        bl0942_last_uart_error_count = uart_error_count;
        sys_bl0942_state = SYS_BL0942_STATE_READ;
        _timer_for_read = 10;
    }
}


#if 0  //10mR
/*
I = (I_RMS*Vref)/(305978*RL), Vref=1.218, RL是10mR
放大1000倍，mA为单位
I = (I_RMS * 1218) / (305978*10)
I = I_RMS / 2512
*/
u16 get_ac_current(u32 i_rms)
{
    return(i_rms / (u16)(2512)); //
}



/*
U = (V_RMS*Vref*(R1+R2))/(73989*R1*1000),  R2=1960，R1=0.51，RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
U = V_RMS * 1.218*1960.51 / (73989*0.51*1000)
U = V_RMS *2387.90118 / 37734390

放大10倍，0.1V为单位
U = V_RMS / 1580
*/
u16 get_ac_voltage(u32 v_rms)
{
    return(v_rms / 1580);
}


/*
w = (WATT*Vref*(R1+R2))/(3537*RL*R1*1000),  R2=1960，R1=0.51，RL=10mR,RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
w = WATT * 1.218*1.218*1960.51 / (3537*10*0.51*1000)
w = WATT *2908.46363724 / 18038700

放大100倍，0.01W为单位
w = WATT / 62
*/
u32 get_ac_power(u32 watt)
{
    return(watt / 62);
}


/*
cf = (1638.4*256*Vref*(R2+R1))/(3600000*3537*RL*R1*1000),  R2=1960，R1=0.51，RL=10mR,RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
cf = (1638.4*256*1.218*1.218*1960.51)/(3600000*3537*10*0.51*1000)
cf = 1219898066.753028096/64939320000000
cf = 1/53233 度
*/
u32 get_ac_energy(u32 cf_cnt)  //
{
    if(cf_cnt > 42949)
    {
        return(cf_cnt*100 / 53);        
    }
    else
    {
        return(cf_cnt*100000 / 53233);        
    }
}


#endif

#define RL              10//mR
#define Vref            1.218
#define R1              0.75
#define R2              1720

#if 1  //20mR
/*
I = (I_RMS*Vref)/(305978*RL), Vref=1.218, RL是20mR
放大1000倍，mA为单位
I = (I_RMS * 1218) / (305978*20)
I = I_RMS / 5024
*/
u16 get_ac_current(u32 i_rms)
{
    //return(i_rms / (u16)(5024)); //
    return(i_rms / (u16)(((double)305978*RL)/((double)Vref*1000)));
}



/*
U = (V_RMS*Vref*(R1+R2))/(73989*R1*1000),  R2=1960，R1=0.51，RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
U = V_RMS * 1.218*1960.51 / (73989*0.51*1000)
U = V_RMS *2387.90118 / 37734390

放大10倍，0.1V为单位
U = V_RMS / 1580
*/


/************************************
功能描述：开根号处理
输入参数：被开方数，32位无符号整数
输出返回：开方结果，16位无符号整数
*************************************/
u16 sqrt_16(u32 M) 
{ 
    u16 N, i; 
    u32 tmp, ttp;   // 结果、循环计数 
    if (M ==0)              // 被开方数，开方结果也为0 
        return 0;
   N = 0;
   tmp = (M >> 30);          //获取最高位：B[m-1] 
    M <<= 2; 
    if (tmp >1)             // 最高位为1 
    { 
        N++;                // 结果当前位为1，否则为默认的0 
        tmp -= N; 
    }
   for (i=15; i>0; i--)      // 求剩余的15位 
    { 
        N <<=1;             // 左移一位
       tmp <<= 2; 
        tmp += (M >>30);     // 假设
       ttp = N; 
        ttp = (ttp<<1)+1;
       M <<= 2; 
        if (tmp >=ttp)       // 假设成立 
        { 
            tmp -=ttp; 
            N ++; 
        }
   }
 
   return N; 
}



#define PI        3.1415926f
u64 isqrt64(u64 n);
#if BL0942_USE_FLOAT_XCAP_COMPENSATION
float sqrt_float(float num) ;
float square(float num) ;
#endif

/* 
i0 未校正前的电流， 单位A
pf X电容之后负载的PF值，范围是 0到1.0
f是交流频率 ,  单位HZ，通常是 50或者60
cap电容值,单位 f 。 
返回校正后的电流，单位A。
*/
#if BL0942_USE_FLOAT_XCAP_COMPENSATION
float get_xcap_current_creent(float i0, float pf, float Uac, float f, float cap)
{
    return sqrt_float(square(i0*pf) + square((2.0f*PI*f*cap*Uac) + i0*sqrt_float(1.0f-pf*pf)));
}

float square(float num) 
{
    return powf(num, 2.0f);  // 计算 num 的平方
}

//执行过程消耗时间7us
float sqrt_float(float num) 
{
    if (num < 0) return -1.0f;  // 负数返回-1表示错误
    return sqrtf(num);          // 使用标准库的sqrtf函数
}
#endif



u16 get_ac_voltage(u32 v_rms)
{
   // return(v_rms / 1580);
   return(v_rms / (u16)(((double)73989*R1*100)/((double)Vref*((double)R1+(double)R2))));
}


/*
w = (WATT*Vref*(R1+R2))/(3537*RL*R1*1000),  R2=1960，R1=0.51，RL=20mR,RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
w = WATT * 1.218*1.218*1960.51 / (3537*20*0.51*1000)
w = WATT *2908.46363724 / 36077400

放大100倍，0.01W为单位
w = WATT / 124
*/
u32 get_ac_power(u32 watt)
{
   // return(watt / 124);
    return(watt / (u16)(((double)3537*RL*R1*10)/((double)Vref*Vref*((double)R1+(double)R2))));  //公式中的10本来是1000，由于功率需要放大100倍所以为10
 
}




#define ONE_CYCLE_ENERGY        (u32)((0xffffff*100)/(u32)(((double)3600000*3537*RL*R1)/((double)1638.4*256*Vref*Vref*((double)R2+(double)R1)))) //cf_cnt循环一周后的电量，单位0.01wh


/*
cf = (1638.4*256*Vref*(R2+R1))/(3600000*3537*RL*R1*1000),  R2=1960，R1=0.51，RL=20mR,RL 单位为毫欧， R2,R1 单位为 K 欧； Vref=1.218 伏
cf = (1638.4*256*1.218*1.218*1960.51)/(3600000*3537*20*0.51*1000)
cf = 1219898066.753028096/129878640000000
cf = 1/106467 度
*/
u32 get_ac_energy(u32 cf_cnt)  //
{
    if(cf_cnt > 42949)
    {
        //return(cf_cnt*100 / 106);        
        return((cf_cnt*100) / (u32)(((double)3600000*3537*RL*R1)/((double)1638.4*256*Vref*Vref*((double)R2+(double)R1))));
    }
    else
    {
        //return(cf_cnt*100000 / 106467);     
        return((cf_cnt*100000) / (u32)(((double)3600000*3537*RL*R1*1000)/((double)1638.4*256*Vref*Vref*((double)R2+(double)R1))));   
    }
}


#endif

void sys_bl0942_write(u8 addr, u32 dat)
{
    _tx_buffer[0] = WRITE_HEADER;
    _tx_buffer[1] = addr;
    _tx_buffer[2] = u32ll(dat);
    _tx_buffer[3] = u32lh(dat);
    _tx_buffer[4] = u32hl(dat);
    _tx_buffer[5] = ~(_tx_buffer[0]+_tx_buffer[1]+_tx_buffer[2]+_tx_buffer[3]+_tx_buffer[4]);
    hw_bl0942_uart_write(_tx_buffer, 6);
}


void sys_bl0942_write_enable(void)
{
    sys_bl0942_write(Addr_WRPROT, 0x55);
}

void sys_bl0942_write_disable(void)
{
    sys_bl0942_write(Addr_WRPROT, 0x00);
}

void sys_bl0942_process(void)
{
    u32 tmp;
    sys_bl0942_frame_st meter_frame;
    u32 meter_i_rms_raw;
    u32 meter_v_rms_raw;
    u32 meter_i_fast_rms_raw;
    s32 meter_watt_raw;
    u32 meter_cf_cnt_raw;
    u16 meter_freq_raw;
    u8 meter_status_raw;
    u32 meter_tick_ms;
    u16 meter_valid_flags;

#if BL0942_REPRO_TEST_ENABLE
    if(g_bl0942_repro_force_recover != 0U)
    {
        g_bl0942_repro_force_recover = 0U;
        hw_bl0942_repro_force_recover();
        sys_bl0942_state = SYS_BL0942_STATE_READ;
        _timer_for_read = 10U;
    }
#endif

    sys_bl0942_check_uart_error();
    switch(sys_bl0942_state)
    {
        case SYS_BL0942_STATE_IDLE:
        {
           // sys_bl0942_state = SYS_BL0942_STATE_INIT;   
//            sys_bl0942_state = SYS_BL0942_STATE_READ;   
//            sys_bl0942_init1 =  SYS_BL0942_INIT_IDLE;
        }
        break;
        case SYS_BL0942_STATE_INIT:
        {
            switch(sys_bl0942_init1)
            {
                case SYS_BL0942_INIT_IDLE:
                {
                    sys_bl0942_write_enable();
                    sys_bl0942_init1 =  SYS_BL0942_INIT_WRITE_ENABLE;
                }
                break;
                case SYS_BL0942_INIT_WRITE_ENABLE:
                {
                    if(hw_bl0942_get_state() == BL0942_STATE_IDLE)
                    {
                        sys_bl0942_write(Addr_MODE, 0xc7);
                        sys_bl0942_init1 =  SYS_BL0942_INIT_WRITE_DISABLE;
                    }
                }
                break;
                case SYS_BL0942_INIT_WRITE_DISABLE:
                {
                    if(hw_bl0942_get_state() == BL0942_STATE_IDLE)
                    {
                        sys_bl0942_write_disable();
                        sys_bl0942_init1 =  SYS_BL0942_INIT_COMPLETE;
                        _timer_for_read = 100;
                    }
                }
                break;
                case SYS_BL0942_INIT_COMPLETE:
                {
                    if(hw_bl0942_get_state() == BL0942_STATE_IDLE)
                    {
                        _tx_buffer[0] = READ_HEADER;
                        _tx_buffer[1] = Addr_MODE;
                        hw_bl0942_uart_read(_tx_buffer, READ_PACKET_LENGTH);
                    }
                    else if(hw_bl0942_get_state() == BL0942_STATE_READ_READY)
                    {
                        sys_bl0942_state = SYS_BL0942_STATE_READ;  
                        printf("mode=0x%x\n", (u16)_tx_buffer[0]);
                        _timer_for_read = 0;
                    }
                    else if(_timer_for_read == 0)
                    {
                        bl0942_timeout_count++;
                        sys_bl0942_init1 =  SYS_BL0942_INIT_IDLE;
                    }
                }
                break;
            }
        }
        break;
        case SYS_BL0942_STATE_READ:
        {
            if(_timer_for_read == 0 && !(STOP))
            {
                _timer_for_read = 100;
                _tx_buffer[0] = READ_HEADER;
                _tx_buffer[1] = READ_ALL_HEAD;

#if BL0942_REPRO_TEST_ENABLE
                g_bl0942_repro_read_start_count++;
#endif

                hw_bl0942_uart_read(_tx_buffer, READ_PACKET_MAX_LENGTH);
                
                sys_bl0942_state = SYS_BL0942_STATE_WAIT_READ_READY;
            }
        }
        break;
        case SYS_BL0942_STATE_WAIT_READ_READY:
        {
            if(hw_bl0942_get_state() == BL0942_STATE_READ_READY)
            {
                meter_tick_ms = HAL_GetTick();
                if (sys_bl0942_frame_decode(_tx_buffer,
                                             READ_PACKET_MAX_LENGTH,
                                             &meter_frame) == BOOL_TRUE)
                {
                    meter_i_rms_raw = meter_frame.i_rms_raw;
                    meter_v_rms_raw = meter_frame.v_rms_raw;
                    meter_i_fast_rms_raw = meter_frame.i_fast_rms_raw;
                    meter_watt_raw = meter_frame.watt_raw;
                    meter_cf_cnt_raw = meter_frame.cf_cnt_raw;
                    meter_freq_raw = meter_frame.freq_raw;
                    meter_status_raw = meter_frame.status_raw;
                    meter_valid_flags = SYS_CALIBRATION_METER_FRAME_VALID |
                                        SYS_CALIBRATION_METER_HEAD_VALID |
                                        SYS_CALIBRATION_METER_CHECKSUM_VALID;
                    if (sys_bl0942_frame_reserved_valid(_tx_buffer) == BOOL_TRUE)
                    {
                        meter_valid_flags |= SYS_CALIBRATION_METER_RESERVED_VALID;
                    }
                    if (sys_bl0942_frame_reserved_valid(_tx_buffer) != BOOL_TRUE ||
                        sys_bl0942_frame_uses_legacy_checksum(_tx_buffer) == BOOL_TRUE)
                    {
                        if (bl0942_compat_frame_count < 0xFFFFFFFFUL)
                        {
                            bl0942_compat_frame_count++;
                        }
                    }
#if BL0942_REPRO_TEST_ENABLE
                    g_bl0942_repro_frame_ok_count++;
                    g_bl0942_repro_last_ok_tick = Timer_GetTickCount();
#endif

                    u32ll(tmp) = _tx_buffer[1];
                    u32lh(tmp) = _tx_buffer[2];
                    u32hl(tmp) = _tx_buffer[3];
                    u32hh(tmp) = 0;
                  
            
                   ac_current =get_ac_current(tmp);  //50mR 100W  
                // printf("i=%d\n", ac_current);

                    u32ll(tmp) = _tx_buffer[4];
                    u32lh(tmp) = _tx_buffer[5];
                    u32hl(tmp) = _tx_buffer[6];
                    u32hh(tmp) = 0;
                    ac_voltage_8209 = get_ac_voltage(tmp);//下偏加补偿3.7V
                    /*
                    //++++++++20mR补偿电压高时50-100ma电流区间电路偏差大++++++++++++++++
                    if(ac_voltage_8209>2500)
                    {
                         if(ac_current>50&&ac_current<100)
                         ac_current=ac_current+2;
                    } 
                  //  printf("i2=%d\n", ac_current);
                   //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                   */
                  //  printf("v=%d\n", ac_voltage_8209);
                    //更换贝岭计量芯片
                  //  UPDATE_AC_VOLTAGE(ac_voltage);

                    ac_power_S = ((u32)ac_current * ac_voltage_8209)/100;
                   // printf("s=%d\n", ac_power_S);
                    //更换贝岭计量芯片
                   // UPDATE_AC_APPARENT_POWER(ac_power_S);

                    u32ll(tmp) = _tx_buffer[10];
                    u32lh(tmp) = _tx_buffer[11];
                    u32hl(tmp) = _tx_buffer[12];
                    u32hh(tmp) = 0;
                    ac_powerpa =(u16) get_ac_power(tmp);//0.01W为单位
                   // printf("w=%d\n", ac_powerpa);
                    //更换贝岭计量芯片
                   // UPDATE_AC_POWER(ac_powerpa);

                    if(ac_power_S == 0)
                    {
                        ac_pf = 0;
                        Z_ac_current = 0;
                    }
                    else
                    {
                        ac_pf = (ac_powerpa*100)/ac_power_S;
                        if(ac_pf>99)
                        {
                             ac_pf=99;
                        }
                    }
                    //更换贝岭计量芯片
                   // UPDATE_AC_POWERFACTOR(ac_pf);
                   // printf("pf=%d\n", (u16)ac_pf);


                    u32ll(tmp) = _tx_buffer[13];
                    u32lh(tmp) = _tx_buffer[14];
                    u32hl(tmp) = _tx_buffer[15];
                    u32hh(tmp) = 0;
                    if(tmp < _cf_cnt_bak)
                    {
                        if(_cf_over_cnt < 0xff)
                        {
                            ++_cf_over_cnt;
                           // _cf_cnt_first = 0;
                        }
                    }
                    _cf_cnt_bak = tmp;
//                    if(_cf_cnt_first == 0xffffffff)
//                    {
//                        _cf_cnt_first = tmp;
//                    }
//                    else
//                    {
//                        tmp = tmp-_cf_cnt_first;
//                    }
                   // printf("tmp=%ld\n", tmp);
                    bl0942_energy_counter_raw = get_ac_energy(tmp);
                    if(_cf_over_cnt > 0)
                    {
                        bl0942_energy_counter_raw = ONE_CYCLE_ENERGY*_cf_over_cnt + bl0942_energy_counter_raw;
                    }
                    if (bl0942_energy_clear_pending == BOOL_TRUE)
                    {
                        bl0942_energy_clear_base = bl0942_energy_counter_raw;
                        bl0942_energy_clear_pending = BOOL_FALSE;
                    }
                    energy_this_time = (bl0942_energy_counter_raw >= bl0942_energy_clear_base) ?
                                       (bl0942_energy_counter_raw - bl0942_energy_clear_base) : 0U;
                    //更换贝岭计量芯片
                    // UPDATE_AC_ENERGYP(energy_this_time);

                    //printf("energy=%ld\n",energy_this_time);
                    tmp = bl0942_energy_counter_raw - energy_tmp;
                    energy_tmp = bl0942_energy_counter_raw;
                    _ac_EnergyP = _ac_EnergyP + tmp;
                  //更换贝岭计量芯片  
                  //  STORE_AC_ENERGYP(_ac_EnergyP);

                    
                    u32ll(tmp) = _tx_buffer[16];
                    u32lh(tmp) = _tx_buffer[17];
                    u32hl(tmp) = 0;
                    u32hh(tmp) = 0;
                  //  ac_freq = ((u32)200*FS)/tmp;      
                   
                    //UPDATE_AC_FREQ((ac_freq+50)/100);
                    //printf("f=%d\n", ac_freq);
                   
                  if(ac_power_S == 0)
                  {
                      Z_ac_current = 0;
                  }
                  else
                  {
#if !BL0942_USE_FLOAT_XCAP_COMPENSATION
           //extern  u32 sys_tick_get_tick(void);
                  // u32 t1=    sys_tick_get_tick();
                  u32   operation_tmp0;
                  u32   operation_tmp1 ;
                  u32   operation_tmp2 ;
                  u32   operation_tmp3 ;
                  u32   operation_tmp33;
                  u16   operation_tmp4 ;
                  u32   operation_tmp5 ;
                        operation_tmp0 = (u32)ac_pf * (u32)ac_pf;  // PF^2, scale 10000
                        operation_tmp1 = (u32)(((u64)ac_current * (u64)ac_current * (u64)operation_tmp0) / 10000U);
                        operation_tmp2 = (u32)(((u64)31416U * (u64)CX * (u64)ac_voltage_8209) / 100000000U);
                        operation_tmp3 = (operation_tmp0 < 10000U) ? (10000U - operation_tmp0) : 0U;
                        operation_tmp33=(u32)(((u64)operation_tmp3 * 65536U) / 10000U);
                        operation_tmp4  = sqrt_16( (u32)(operation_tmp33) ) ;//开1-PF平方
                        operation_tmp5=ac_current*operation_tmp4/256+operation_tmp2;//
                        Z_ac_current=(sqrt_16((operation_tmp1+operation_tmp5*operation_tmp5)*256))/16;//总耗时53us
                         printf("Z_ac_current=%d\n",(u16)Z_ac_current);
                      /*
                         printf("ac_current=%d\n",ac_current);
                         
                         printf("ac_voltage_8209=%d\n",ac_voltage_8209);                
                         printf("PF*PF=%f\n",operation_tmp0);
                         printf("i*i*PF*PF=%d\n",operation_tmp1);
                         printf("(2*PI*Fin**CX*Vin)=%d\n",operation_tmp2);
                         printf("1-PF*PF=%f\n", operation_tmp3);
                         printf("operation_tmp33=%d\n", operation_tmp33);
                         printf("kaifang=%d\n",operation_tmp4); 
                         printf("pingfang=%d\n",operation_tmp5); 
                         */
                    
#else    //公式调运算库  占用2.5KROM 耗时222us   
                     //   u32 t1=    sys_tick_get_tick();  
                          Z_ac_current=(u16)(get_xcap_current_creent( (float)ac_current/1000,(float)ac_pf/100, (float)ac_voltage_8209/10,(float)50,(float)CX/100/1000000)*1000);
                    
                     //   u32  t2 =  sys_tick_get_tick();
                     //   printf("t=%d\n",t2 -t1);                    
                     //  printf("Z_ac_current=%d\n",(u16)Z_ac_current);
                     
                     
                     
                     
                    /* No MID-specific magic correction: all product images
                       use the same BL0942 algorithm.  Any approved scaling
                       difference must be an explicit profile/calibration field. */

#endif
                  }

                //计量数据更新完一次
                 bl0942data_ready=1;
                 sys_calibration_snapshot_publish_meter(meter_tick_ms,
                                                        meter_i_rms_raw,
                                                        meter_v_rms_raw,
                                                        meter_i_fast_rms_raw,
                                                        meter_watt_raw,
                                                        meter_cf_cnt_raw,
                                                        meter_freq_raw,
                                                        meter_status_raw,
                                                        _tx_buffer,
                                                        meter_valid_flags,
                                                        bl0942_checksum_error_count);
                }
                else
                {
                    bl0942_checksum_error_count++;
#if BL0942_REPRO_TEST_ENABLE
                    g_bl0942_repro_frame_bad_count++;
#endif
                    printf("checksum error1\n");
                }                
                sys_bl0942_state = SYS_BL0942_STATE_READ;
            }
            else if(_timer_for_read == 0)
            {
                bl0942_timeout_count++;
                sys_bl0942_state = SYS_BL0942_STATE_READ;
            }
        }
        break;
    }
}


/************************************
功能描述：上电
输入参数：无
输出返回：无
*************************************/

void sys_bl0942_power_on(void)
{
    sys_bl0942_state = SYS_BL0942_STATE_READ; 
    _timer_for_read = 10;
}

/************************************
功能描述：停止供电
输入参数：无
输出返回：无
*************************************/

void sys_bl0942_power_off(void)
{

    sys_bl0942_state = SYS_BL0942_STATE_IDLE;

}

/************************************
功能描述：掉电前将本周期累积能耗同步到累计值
输入参数：无
输出返回：无
*************************************/
void sys_bl0942_power_down_save(void)
{
    sys_data.ac_EnergyP += total_power_this_time;
    total_power_this_time = 0;
}

void sys_bl0942_energy_stats_clear(void)
{
    /* The next valid CF sample becomes the new zero point for rEc. */
    bl0942_energy_clear_base = bl0942_energy_counter_raw;
    bl0942_energy_clear_pending = BOOL_TRUE;
    energy_this_time = 0U;
    total_power_this_time = 0U;
    bl0942_energy_0_01wh_remainder = 0U;
    minute = 0U;
    _ac_EnergyP = 0U;
    sys_data.ac_EnergyP = 0U;
    sys_data.today_Energy = 0U;
}

void sys_bl0942_init(void)
{
   // _ac_EnergyP = sys_data.total_power;//每次上电导出上次累计能耗   临时取代  LOAD_AC_ENERGYP();
    sys_bl0942_state = SYS_BL0942_STATE_READ; 
    //printf("ONE_CYCLE_ENERGY=0x%lx\n", ONE_CYCLE_ENERGY);
}



